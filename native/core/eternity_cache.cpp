#include "eternity_cache.hpp"

#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "ingame_ui.hpp"
#include "managed.hpp"

#include <algorithm>
#include <atomic>
#include <initializer_list>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <vector>

// The Eternity Cache wherever the character is (offline play only):
//
//   eternitycache <save id>   opens the game's own Eternity Cache panel: the past cache (a Unique with Legendary
//                             Potential and an Exalted item, then Seal), or, with a legendary waiting in it, the
//                             future cache to take it out. The game makes the legendary (absorb4ModExaltedItemTo-
//                             BecomeLegendary, RollLegendaryProperties) as in the Temporal Sanctum.
//
// From the first open until the game closes:
//   - ActorCanUseEternityCacheInThisZone and clientSideCanUseEternityCacheCheck answer true: no Temporal Sanctum
//     needed and no "once per run" (the game's EternityCacheZoneManager exists only there);
//   - while the panel (its open, slot changes, affix label and list, buttons and Seal) and the cache's own checks run
//     with no dungeon run of the player, the game's dungeon service
//     shows them a Tier 4 Temporal Sanctum run (DungeonRun.Tier 3): Uniques of any level (Item.getMax-
//     UniqueLevelRequirementForDungeonTier: Tier 1 50, Tier 2 65, Tier 3 75, Tier 4 200) and the affix to keep chosen
//     by the player (dungeonTierAllowsSelectingAffixInEternityCache: Tier 2 and up). Only those calls see it: the
//     dungeon service has no run again right after each one, so the rest of the game is not in a dungeon.
// In a real dungeon run the game's own run and rules stay; only the once-per-run limit is lifted.
//
// One affix choice per Legendary Potential: the game's panel offers one guaranteed affix. With LP 2 or 3 the panel shows
// that many choices instead (copies of its own dropdown; an affix chosen in one is not offered in the others, and each
// also offers Let fate decide); the game's dropdown stays hidden on Let fate decide. LP 4 needs none: all four go over.
// The Seal stays off until each choice is made. The game's absorb4ModExaltedItemToBecomeLegendary takes the guaranteed
// affix first and then random ones up to the Unique's LP, and needs the Exalted's four affixes in place (it does not
// change them), so it runs once per affix with LP 1 and that affix guaranteed. A critical seal's higher LP (the game's
// RollLegendaryProperties, before the absorb) adds random ones.

namespace ep::eternity {

namespace {

using namespace managed;
using MethodInfo = const il2cpp::Method*;

std::atomic<bool> g_on{false};
std::atomic<uint64_t> g_opens{0}, g_scopes{0}, g_faults{0};
thread_local int t_depth = 0;
bool g_hooked = false;
std::unique_ptr<Root> g_fakeRun;
game::MethodRef m_service, m_setRun, m_openPanel, m_player;
size_t o_activeRun = 0;

// The dungeon service shows the fake Tier 4 run while one of the cache's checks runs (outermost call only), and
// has no run again afterwards. A real run of the player is left alone.
struct FakeRun {
    void* service = nullptr;
    bool set = false;
    FakeRun() {
        if (t_depth++ || !g_on.load() || !g_fakeRun || !game::IsOfflinePlay()) return;
        service = Invoke(m_service);
        if (!service || Get<void*>(service, o_activeRun)) return;
        void* run = g_fakeRun->Get();
        void* a[]{run};
        Invoke(m_setRun, service, a);
        set = true;
        g_scopes.fetch_add(1, std::memory_order_relaxed);
    }
    ~FakeRun() {
        --t_depth;
        if (!set) return;
        try {
            void* a[]{nullptr};
            Invoke(m_setRun, service, a);
        } catch (...) {
            g_faults.fetch_add(1, std::memory_order_relaxed);
        }
    }
    FakeRun(const FakeRun&) = delete;
    FakeRun& operator=(const FakeRun&) = delete;
};

// A guarded fake run: a failure to show it leaves the game's own call as it is.
template<class F> auto WithRun(F call) {
    std::unique_ptr<FakeRun> scope;
    try {
        scope = std::make_unique<FakeRun>();
    } catch (...) {
        g_faults.fetch_add(1, std::memory_order_relaxed);
    }
    return call();
}

// ---------------------------------------------------------------- one affix choice per Legendary Potential
constexpr int kFate = 5, kOptions = 6;  // the panel's dropdown: 0 "Select", 1-4 the affixes, 5 "Let fate decide"
struct Box { std::unique_ptr<Root> object, dropdown; std::vector<int> map; int picked = 0; };
std::vector<Box> g_boxes;
void* g_boxesPanel = nullptr;
void* g_boxesExalted = nullptr;
int g_boxesLp = -1, g_shown = 0;
bool g_multi = false, g_syncing = false, g_sealing = false;
std::vector<uint16_t> g_picks;

game::MethodRef Core(const char* cls, const char* name, int arity) { return game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", cls, name, arity); }
game::MethodRef Dropdown(const char* name, int arity) { return game::FindMethod("Unity.TextMeshPro.dll", "TMPro", "TMP_Dropdown", name, arity); }
const il2cpp::Method* Signature(const char* cls, const char* name, std::initializer_list<const char*> parameters) {
    auto& a = il2cpp::api();
    void* iterator = nullptr;
    while (auto* m = a.class_get_methods(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", cls), &iterator)) {
        if (std::string(a.method_get_name(m)) != name || a.method_get_param_count(m) != parameters.size()) continue;
        bool match = true;
        unsigned i = 0;
        for (const char* expected : parameters) {
            char* actual = a.type_get_name(a.method_get_param(m, i++));
            match &= actual && std::string(actual) == expected;
            if (actual) a.free(actual);
        }
        if (match) return m;
    }
    throw std::runtime_error(std::string("Eternity Cache: missing ") + cls + "." + name);
}
void SetActive(void* object, bool on) { void* a[]{&on}; Invoke(Core("GameObject", "SetActive", 1), object, a); }
void* ObjectOf(void* component) { return Invoke(Core("Component", "get_gameObject", 0), component); }
void SetSilently(void* dropdown, int value) { void* a[]{&value}; Invoke(Dropdown("SetValueWithoutNotify", 1), dropdown, a); }
std::vector<void*> Options(void* dropdown) { return Entries(Invoke(Dropdown("get_options", 0), dropdown)); }
void* Panel() { return game::StaticObject(game::FindStaticField("LE.dll", "", "EternityCachePanelUI", "instance")); }

// What is in a cache slot, or nullptr: the manager's getEternityCache...Item throw on an empty slot, and the slot's own
// getItem is not current yet while the slot's change event runs.
void* SlotItem(void* manager, const char* slot, const char* getter) {
    void* cache = manager ? Get<void*>(manager, Offset("ItemContainersManager", "eternityCache")) : nullptr;
    void* container = cache ? Get<void*>(cache, Offset("ItemContainersManager.EternityCacheContainers", slot)) : nullptr;
    if (!container || !Value<bool>(Method(container, "HasContent", 0), container)) return nullptr;
    return Invoke(Method(manager, getter, 0), manager);
}

void DropBoxes() {
    for (auto& b : g_boxes)
        if (b.object && game::IsAlive(b.object->Get())) { void* a[]{b.object->Get()}; Invoke(Signature("Object", "Destroy", {"UnityEngine.Object"}), nullptr, a); }
    g_boxes.clear();
    g_shown = 0;
}
// Copies of the panel's dropdown under it, one line each (its parent keeps them with the panel).
void EnsureBoxes(void* panel, void* main, size_t need) {
    bool alive = panel == g_boxesPanel;
    for (const auto& b : g_boxes) alive = alive && b.object && game::IsAlive(b.object->Get());
    if (!alive) { DropBoxes(); g_boxesPanel = panel; }
    struct Vec2 { float x, y; };
    struct Rect { float x, y, width, height; };
    Root transform(Invoke(Core("Component", "get_transform", 0), main));
    const auto at = Value<Vec2>(Core("RectTransform", "get_anchoredPosition", 0), transform.Get());
    const auto rect = Value<Rect>(Core("RectTransform", "get_rect", 0), transform.Get());
    while (g_boxes.size() < need) {
        Root source(ObjectOf(main));
        Root parent(Invoke(Core("Transform", "get_parent", 0), transform.Get()));
        bool world = false;
        void* a[]{source.Get(), parent.Get(), &world};
        Box box;
        box.object = std::make_unique<Root>(Invoke(Signature("Object", "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform", "System.Boolean"}), nullptr, a));
        Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("Unity.TextMeshPro.dll", "TMPro", "TMP_Dropdown"))));
        void* ta[]{type.Get()};
        box.dropdown = std::make_unique<Root>(Invoke(Signature("GameObject", "GetComponent", {"System.Type"}), box.object->Get(), ta));
        Root boxTransform(Invoke(Core("Component", "get_transform", 0), box.dropdown->Get()));
        Vec2 position{at.x, at.y - static_cast<float>(g_boxes.size()) * (rect.height + 6)};
        void* pa[]{&position};
        Invoke(Core("RectTransform", "set_anchoredPosition", 1), boxTransform.Get(), pa);
        bool on = true;
        void* ia[]{&on};
        Invoke(game::FindMethod("UnityEngine.UI.dll", "UnityEngine.UI", "Selectable", "set_interactable", 1), box.dropdown->Get(), ia);
        g_boxes.push_back(std::move(box));
    }
}
// A choice's options: the panel's own, without the affixes the other choices hold.
void Rebuild(Box& box, const std::vector<void*>& options) {
    void* dropdown = box.dropdown->Get();
    Invoke(Dropdown("ClearOptions", 0), dropdown);
    Root list(Invoke(Dropdown("get_options", 0), dropdown));
    const auto* add = il2cpp::api().class_get_method_from_name(il2cpp::api().object_get_class(list.Get()), "Add", 1);
    box.map.clear();
    int value = 0;
    for (int i = 0; i < kOptions; ++i) {
        bool taken = false;
        if (i > 0 && i < kFate)
            for (int shown = 0; shown < g_shown; ++shown) taken = taken || (&g_boxes[shown] != &box && g_boxes[shown].picked == i);
        if (taken) continue;
        if (i == box.picked) value = static_cast<int>(box.map.size());
        box.map.push_back(i);
        void* a[]{options[i]};
        Invoke(add, list.Get(), a);
    }
    Invoke(Dropdown("RefreshShownValue", 0), dropdown);
    SetSilently(dropdown, value);
}
// The panel's choices for what is in the cache now.
void Sync(void* panel) {
    if (g_syncing || !g_on.load() || !panel || !game::IsAlive(panel)) return;
    g_syncing = true;
    struct Done { ~Done() { g_syncing = false; } } done;
    void* main = Get<void*>(panel, Offset("EternityCachePanelUI", "affixSelectionDropdown"));
    void* wrapper = Get<void*>(panel, Offset("EternityCachePanelUI", "affixSelectionWrapper"));
    void* label = Get<void*>(panel, Offset("EternityCachePanelUI", "affixSelectionLabel"));
    if (!main || !wrapper || !label) return;
    void* manager = Invoke(game::FindMethod("LE.dll", "", "ItemContainersManager", "get_Instance", 0));
    void* unique = SlotItem(manager, "beforeMain", "getEternityCacheUniqueItem");
    void* exalted = SlotItem(manager, "beforeOther", "getEternityCacheExaltedItem");
    const int lp = unique ? Get<uint8_t>(unique, Offset("ItemData", "legendaryPotential")) : 0;
    const auto options = Options(main);
    const bool multi = unique && exalted && lp >= 2 && static_cast<int>(options.size()) == kOptions &&
                       !Get<bool>(panel, Offset("EternityCachePanelUI", "isFuture")) && Value<bool>(Core("GameObject", "get_activeSelf", 0), wrapper);
    Root mainObject(ObjectOf(main));
    Root labelObject(ObjectOf(label));
    if (!multi) {
        for (auto& b : g_boxes) if (b.object && game::IsAlive(b.object->Get())) SetActive(b.object->Get(), false);
        g_shown = 0;
        if (g_multi) {
            g_multi = false;
            SetActive(mainObject.Get(), true);
            SetActive(labelObject.Get(), true);
            SetSilently(main, 0);
            void* a[]{exalted};
            Invoke(Method(panel, "updateSelectedAffixLabelText", 1), panel, a);
        }
        return;
    }
    const bool fresh = !g_multi || exalted != g_boxesExalted || lp != g_boxesLp;
    const int need = lp >= 4 ? 0 : lp;
    EnsureBoxes(panel, main, static_cast<size_t>(need));
    g_multi = true;
    g_boxesExalted = exalted;
    g_boxesLp = lp;
    SetSilently(main, kFate);
    void* la[]{exalted};
    Invoke(Method(panel, "updateSelectedAffixLabelText", 1), panel, la);
    SetActive(mainObject.Get(), false);
    SetActive(labelObject.Get(), need == 0);
    if (need == 0) {
        Root text(il2cpp::api().string_new(ingame::ui::Tr("Legendary Potential 4: all four affixes go over.").c_str()));
        void* a[]{text.Get()};
        Invoke(Method(label, "set_text", 1), label, a);
    }
    g_shown = need;
    for (size_t i = 0; i < g_boxes.size(); ++i) {
        if (fresh) g_boxes[i].picked = 0;
        SetActive(g_boxes[i].object->Get(), static_cast<int>(i) < need);
    }
    for (int i = 0; i < need; ++i) Rebuild(g_boxes[i], options);
    Invoke(Method(panel, "updateButtonInteractivity", 0), panel);
}
void GuardedSync(void* panel) {
    try {
        Sync(panel);
    } catch (const std::exception& e) {
        if (g_faults.fetch_add(1, std::memory_order_relaxed) < 5) Log("eternity cache: affix choices: %s", e.what());
    }
}
// A choice made in one of the copies: the others lose that affix, the Seal may turn on.
void BoxChanged(void* dropdown) {
    for (auto& changed : g_boxes) {
        if (!changed.dropdown || changed.dropdown->Get() != dropdown) continue;
        void* panel = Panel();
        if (!panel) return;
        const int value = Value<int>(Dropdown("get_value", 0), dropdown);
        if (value >= 0 && value < static_cast<int>(changed.map.size())) changed.picked = changed.map[value];
        const auto options = Options(Get<void*>(panel, Offset("EternityCachePanelUI", "affixSelectionDropdown")));
        if (static_cast<int>(options.size()) != kOptions) return;
        g_syncing = true;
        struct Done { ~Done() { g_syncing = false; } } done;
        for (int i = 0; i < g_shown; ++i) if (&g_boxes[i] != &changed) Rebuild(g_boxes[i], options);
        Invoke(Method(panel, "updateButtonInteractivity", 0), panel);
        return;
    }
}
using SetValueFn = void (*)(void* self, int32_t value, bool send, MethodInfo);
SetValueFn o_setValue = nullptr;
void d_setValue(void* self, int32_t value, bool send, MethodInfo m) {
    o_setValue(self, value, send, m);
    if (!send || g_boxes.empty() || g_syncing) return;
    try {
        BoxChanged(self);
    } catch (const std::exception& e) {
        if (g_faults.fetch_add(1, std::memory_order_relaxed) < 5) Log("eternity cache: affix choice: %s", e.what());
    }
}
// Once per affix with LP 1 and that affix guaranteed: the chosen ones, then random ones up to the rolled LP. The game's
// absorb takes only a Unique with LP (rarity 7) and makes it a Legendary (9), so each call starts from a Unique again.
using AbsorbFn = void (*)(void* self, void* exalted, uint32_t selected, MethodInfo);  // Nullable<UInt16>: has at byte 0, value at bytes 2-3
AbsorbFn o_absorb = nullptr;
void d_absorb(void* self, void* exalted, uint32_t selected, MethodInfo m) {
    if (!g_sealing || g_picks.empty() || !self || !exalted) return o_absorb(self, exalted, selected, m);
    std::vector<uint16_t> order = g_picks;
    uint8_t* lp = reinterpret_cast<uint8_t*>(self) + Offset("ItemData", "legendaryPotential");
    uint8_t* rarity = reinterpret_cast<uint8_t*>(self) + Offset("ItemData", "rarity");
    const uint8_t rolled = *lp, unique = *rarity;
    std::vector<uint16_t> rest;
    for (void* a : Entries(Invoke(Method(exalted, "GetValidAffixesForEternityCacheSealing", 0), exalted))) {
        const auto id = Get<uint16_t>(a, Offset("ItemAffix", "affixId"));
        if (std::find(order.begin(), order.end(), id) == order.end()) rest.push_back(id);
    }
    std::shuffle(rest.begin(), rest.end(), std::mt19937{std::random_device{}()});
    order.insert(order.end(), rest.begin(), rest.end());
    const size_t want = (std::min)(order.size(), (std::max<size_t>)(rolled, g_picks.size()));
    try {
        for (size_t i = 0; i < want; ++i) {
            *lp = 1;
            *rarity = unique;
            o_absorb(self, exalted, (static_cast<uint32_t>(order[i]) << 16) | 1u, m);
        }
    } catch (...) {
        *lp = rolled;
        throw;
    }
    *lp = rolled;
}

using CanUseFn = bool (*)(void* actor, MethodInfo);
CanUseFn o_canUse = nullptr;
bool d_canUse(void* actor, MethodInfo m) { return (g_on.load() && game::IsOfflinePlay()) || o_canUse(actor, m); }

using ClientCheckFn = bool (*)(MethodInfo);
ClientCheckFn o_clientCheck = nullptr;
bool d_clientCheck(MethodInfo m) { return (g_on.load() && game::IsOfflinePlay()) || o_clientCheck(m); }

using CanSealFn = bool (*)(void* self, uint32_t affix, MethodInfo);  // Nullable<UInt16>: 4 bytes, by value
CanSealFn o_canSeal = nullptr;
bool d_canSeal(void* self, uint32_t affix, MethodInfo m) { return WithRun([&] { return o_canSeal(self, affix, m); }); }

using ReceiveFn = bool (*)(void* self, void* item, int32_t count, MethodInfo);
ReceiveFn o_receive = nullptr;
bool d_receive(void* self, void* item, int32_t count, MethodInfo m) { return WithRun([&] { return o_receive(self, item, count, m); }); }

using OpenFn = void (*)(void* self, bool future, MethodInfo);
OpenFn o_open = nullptr;
void d_open(void* self, bool future, MethodInfo m) { WithRun([&] { o_open(self, future, m); GuardedSync(self); return 0; }); }

using UniqueErrorFn = void (*)(void* self, bool isUnique, bool hasPotential, int32_t level, bool type, bool cls, bool faction, bool corrupted, MethodInfo);
UniqueErrorFn o_uniqueError = nullptr;
void d_uniqueError(void* self, bool isUnique, bool hasPotential, int32_t level, bool type, bool cls, bool faction, bool corrupted, MethodInfo m) {
    WithRun([&] { o_uniqueError(self, isUnique, hasPotential, level, type, cls, faction, corrupted, m); return 0; });
}

using PanelBoolFn = bool (*)(void* self, MethodInfo);
PanelBoolFn o_panelCanSeal = nullptr, o_affixValid = nullptr;
bool d_panelCanSeal(void* self, MethodInfo m) {
    const bool can = WithRun([&] { return o_panelCanSeal(self, m); });
    if (!can || !g_multi) return can;
    for (int i = 0; i < g_shown && i < static_cast<int>(g_boxes.size()); ++i) if (g_boxes[i].picked <= 0) return false;
    return true;
}
bool d_affixValid(void* self, MethodInfo m) { return WithRun([&] { return o_affixValid(self, m); }); }

using SelectedAffixFn = void* (*)(void* self, void* item, MethodInfo);
SelectedAffixFn o_selectedAffix = nullptr;
void* d_selectedAffix(void* self, void* item, MethodInfo m) { return WithRun([&] { return o_selectedAffix(self, item, m); }); }

using PanelVoidFn = void (*)(void* self, MethodInfo);
PanelVoidFn o_affixVisibility = nullptr, o_seal = nullptr, o_dropdownOptions = nullptr, o_buttons = nullptr;
void d_affixVisibility(void* self, MethodInfo m) { WithRun([&] { o_affixVisibility(self, m); GuardedSync(self); return 0; }); }
// The panel's own Seal: the seal clears the cache's slots, and their change handlers write the "Dungeon Tier N Reward"
// label from the run (updateSelectedAffixLabelText); the whole seal sees the run, so it is never cut short.
void d_seal(void* self, MethodInfo m) {
    struct Done { ~Done() { g_sealing = false; g_picks.clear(); } } done;
    g_picks.clear();
    if (g_multi) {
        // the choices that name an affix; a seal that cannot read them does not happen
        try {
            void* manager = Invoke(game::FindMethod("LE.dll", "", "ItemContainersManager", "get_Instance", 0));
            void* exalted = SlotItem(manager, "beforeOther", "getEternityCacheExaltedItem");
            if (!exalted) throw std::runtime_error("no Exalted item in the cache");
            const auto valid = Entries(Invoke(Method(exalted, "GetValidAffixesForEternityCacheSealing", 0), exalted));
            for (int i = 0; i < g_shown; ++i) {
                const int picked = g_boxes[i].picked;
                if (picked > 0 && picked < kFate) g_picks.push_back(Get<uint16_t>(valid.at(picked - 1), Offset("ItemAffix", "affixId")));
            }
        } catch (const std::exception& e) {
            Log("eternity cache: seal refused: %s", e.what());
            g_faults.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        g_sealing = true;
    }
    WithRun([&] { o_seal(self, m); return 0; });
}
void d_dropdownOptions(void* self, MethodInfo m) { WithRun([&] { o_dropdownOptions(self, m); GuardedSync(self); return 0; }); }
void d_buttons(void* self, MethodInfo m) { WithRun([&] { o_buttons(self, m); return 0; }); }

using LabelFn = void (*)(void* self, void* item, MethodInfo);
LabelFn o_label = nullptr;
void d_label(void* self, void* item, MethodInfo m) { WithRun([&] { o_label(self, item, m); return 0; }); }

using AffixChangedFn = void (*)(void* self, int32_t index, MethodInfo);
AffixChangedFn o_affixChanged = nullptr;
void d_affixChanged(void* self, int32_t index, MethodInfo m) { WithRun([&] { o_affixChanged(self, index, m); return 0; }); }

// The slots' change handlers (a Unique or Exalted put in or taken out, the seal clearing them).
using ChangedFn = void (*)(void* self, void* sender, void* entry, MethodInfo);
ChangedFn o_exaltedChanged = nullptr, o_uniqueChanged = nullptr, o_afterChanged = nullptr;
void d_exaltedChanged(void* self, void* sender, void* entry, MethodInfo m) { WithRun([&] { o_exaltedChanged(self, sender, entry, m); GuardedSync(self); return 0; }); }
void d_uniqueChanged(void* self, void* sender, void* entry, MethodInfo m) { WithRun([&] { o_uniqueChanged(self, sender, entry, m); GuardedSync(self); return 0; }); }
void d_afterChanged(void* self, void* sender, void* entry, MethodInfo m) { WithRun([&] { o_afterChanged(self, sender, entry, m); return 0; }); }

struct HookSpec { const char* cls; const char* name; int arity; void* detour; void** original; };

// Hooks, the fake run and the methods the open needs (once, on the main thread).
void Prepare() {
    if (g_hooked) return;
    auto find = [](const char* cls, const char* name, int arity, const char* ns = "") {
        auto m = game::FindMethod("LE.dll", ns, cls, name, arity);
        if (!m) throw std::runtime_error(std::string("Eternity Cache: missing ") + cls + "." + name);
        return m;
    };
    m_service = find("ServiceProvider", "get_DungeonService", 0, "LE.Services");
    m_setRun = find("ClientDungeonService", "set_ActiveRun", 1, "LE.Dungeons");
    m_openPanel = find("UIBase", "OpenEternityCachePanel", 1);
    m_player = find("PlayerFinder", "getPlayerActor", 0);
    o_activeRun = game::FieldOffset("LE.dll", "LE.Dungeons", "ClientDungeonService", "<ActiveRun>k__BackingField");
    const size_t tier = game::FieldOffset("LE.dll", "LE.Dungeons", "DungeonRun", "Tier");
    const size_t dungeon = game::FieldOffset("LE.dll", "LE.Dungeons", "DungeonRun", "dungeonID");
    const auto* runClass = game::FindClass("LE.dll", "LE.Dungeons", "DungeonRun");
    if (!o_activeRun || !tier || !dungeon || !runClass) throw std::runtime_error("Eternity Cache: dungeon run layout not found");
    // A Tier 4 Temporal Sanctum run (DungeonID Dungeon1 = 0, the Temporal Sanctum; Tier is 0-based)
    auto run = std::make_unique<Root>(il2cpp::api().object_new(runClass));
    Invoke(find("DungeonRun", ".ctor", 0, "LE.Dungeons"), run->Get());
    *reinterpret_cast<int32_t*>(static_cast<char*>(run->Get()) + tier) = 3;
    *reinterpret_cast<uint8_t*>(static_cast<char*>(run->Get()) + dungeon) = 0;
    const HookSpec hooks[]{
        {"EternityCacheZoneManager", "ActorCanUseEternityCacheInThisZone", 1, reinterpret_cast<void*>(&d_canUse), reinterpret_cast<void**>(&o_canUse)},
        {"EternityCacheZoneManager", "clientSideCanUseEternityCacheCheck", 0, reinterpret_cast<void*>(&d_clientCheck), reinterpret_cast<void**>(&o_clientCheck)},
        {"ItemContainersManager", "canSealEternityCache", 1, reinterpret_cast<void*>(&d_canSeal), reinterpret_cast<void**>(&o_canSeal)},
        {"EternityCacheBeforeMainContainer", "CanReceiveItem", 2, reinterpret_cast<void*>(&d_receive), reinterpret_cast<void**>(&o_receive)},
        {"EternityCachePanelUI", "Open", 1, reinterpret_cast<void*>(&d_open), reinterpret_cast<void**>(&o_open)},
        {"EternityCachePanelUI", "ShowUniqueError", 7, reinterpret_cast<void*>(&d_uniqueError), reinterpret_cast<void**>(&o_uniqueError)},
        {"EternityCachePanelUI", "canSeal", 0, reinterpret_cast<void*>(&d_panelCanSeal), reinterpret_cast<void**>(&o_panelCanSeal)},
        {"EternityCachePanelUI", "isSelectedAffixValid", 0, reinterpret_cast<void*>(&d_affixValid), reinterpret_cast<void**>(&o_affixValid)},
        {"EternityCachePanelUI", "getSelectedAffix", 1, reinterpret_cast<void*>(&d_selectedAffix), reinterpret_cast<void**>(&o_selectedAffix)},
        {"EternityCachePanelUI", "updateAffixSelectionVisibility", 0, reinterpret_cast<void*>(&d_affixVisibility), reinterpret_cast<void**>(&o_affixVisibility)},
        {"EternityCachePanelUI", "updateSelectedAffixLabelText", 1, reinterpret_cast<void*>(&d_label), reinterpret_cast<void**>(&o_label)},
        {"EternityCachePanelUI", "seal", 0, reinterpret_cast<void*>(&d_seal), reinterpret_cast<void**>(&o_seal)},
        {"EternityCachePanelUI", "updateAffixDropdownOptions", 0, reinterpret_cast<void*>(&d_dropdownOptions), reinterpret_cast<void**>(&o_dropdownOptions)},
        {"EternityCachePanelUI", "updateButtonInteractivity", 0, reinterpret_cast<void*>(&d_buttons), reinterpret_cast<void**>(&o_buttons)},
        {"EternityCachePanelUI", "onAffixSelectionChanged", 1, reinterpret_cast<void*>(&d_affixChanged), reinterpret_cast<void**>(&o_affixChanged)},
        {"EternityCachePanelUI", "onBeforeExaltedContainerContentChanged", 2, reinterpret_cast<void*>(&d_exaltedChanged), reinterpret_cast<void**>(&o_exaltedChanged)},
        {"EternityCachePanelUI", "onBeforeUniqueContainerContentChanged", 2, reinterpret_cast<void*>(&d_uniqueChanged), reinterpret_cast<void**>(&o_uniqueChanged)},
        {"EternityCachePanelUI", "onAfterContainersContentsChanged", 2, reinterpret_cast<void*>(&d_afterChanged), reinterpret_cast<void**>(&o_afterChanged)},
        {"ItemData", "absorb4ModExaltedItemToBecomeLegendary", 2, reinterpret_cast<void*>(&d_absorb), reinterpret_cast<void**>(&o_absorb)},
        {"TMP_Dropdown", "SetValue", 2, reinterpret_cast<void*>(&d_setValue), reinterpret_cast<void**>(&o_setValue)},
    };
    std::vector<game::MethodRef> targets;
    for (const auto& h : hooks) {
        if (std::string(h.cls) != "TMP_Dropdown") { targets.push_back(find(h.cls, h.name, h.arity)); continue; }
        const auto m = Dropdown(h.name, h.arity);
        if (!m) throw std::runtime_error("Eternity Cache: missing TMP_Dropdown.SetValue");
        targets.push_back(m);
    }
    std::vector<void*> installed;
    for (size_t i = 0; i < targets.size(); ++i) {
        std::string why;
        if (hook::IsInstalled(targets[i].code)) continue;
        if (!hook::Install(targets[i].code, hooks[i].detour, hooks[i].original, &why)) {
            for (void* code : installed) { std::string ignored; hook::Remove(code, &ignored); }
            throw std::runtime_error(std::string("Eternity Cache: ") + hooks[i].cls + "." + hooks[i].name + ": " + why);
        }
        installed.push_back(targets[i].code);
    }
    g_fakeRun = std::move(run);
    g_hooked = true;
    Log("eternity cache: ready (%zu hooks)", installed.size());
}

}  // namespace

std::string Open(const std::string& expectedId) {
    return Run([expectedId] {
        if (!game::IsOfflinePlay()) throw std::runtime_error(game::GateText());
        if (expectedId.empty() || expectedId.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("offline save id required");
        Prepare();
        void* actor = Invoke(m_player);
        if (!actor || !game::IsAlive(actor)) throw std::runtime_error("enter a zone with an offline character first");
        void* tracker = Get<void*>(actor, Offset("Actor", "characterDataTracker"));
        void* data = tracker ? Get<void*>(tracker, Offset("CharacterDataTracker", "charData")) : nullptr;
        if (!data || Text(Invoke(Method(data, "get_Id", 0), data)) != expectedId)
            throw std::runtime_error("loaded offline save id changed; refresh the panel");
        void* ui = game::StaticObject(game::FindStaticField("LE.dll", "", "UIBase", "instance"));
        if (!ui || !game::IsAlive(ui)) throw std::runtime_error("Game interface unavailable.");
        // a legendary waiting in the cache: the future cache, to take it out
        void* manager = Invoke(game::FindMethod("LE.dll", "", "ItemContainersManager", "get_Instance", 0));
        void* cache = manager ? Get<void*>(manager, Offset("ItemContainersManager", "eternityCache")) : nullptr;
        void* after = cache ? Get<void*>(cache, Offset("ItemContainersManager.EternityCacheContainers", "after")) : nullptr;
        if (!after) throw std::runtime_error("Eternity Cache containers unavailable");
        const bool waiting = Invoke(Method(after, "getItem", 0), after) != nullptr;
        g_on = true;
        bool future = waiting;
        void* a[]{&future};
        Invoke(m_openPanel, ui, a);
        g_opens.fetch_add(1, std::memory_order_relaxed);
        return std::string("{\"ok\":true,\"opened\":true,\"future\":") + Boolean(future) + "}";
    });
}

#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
namespace {
std::string ItemJson(void* item) {
    if (!item) return "null";
    std::string affixes;
    for (void* a : Entries(Get<void*>(item, Offset("ItemData", "affixes"))))
        if (a) affixes += (affixes.empty() ? "" : ",") + std::string("[") + std::to_string(Get<uint16_t>(a, Offset("ItemAffix", "affixId"))) + "," +
                          std::to_string(Get<uint8_t>(a, Offset("ItemAffix", "affixTier"))) + "]";
    return "{\"type\":" + std::to_string(Get<uint8_t>(item, Offset("ItemData", "itemType"))) +
           ",\"sub\":" + std::to_string(Get<uint16_t>(item, Offset("ItemData", "subType"))) +
           ",\"rarity\":" + std::to_string(Get<uint8_t>(item, Offset("ItemData", "rarity"))) +
           ",\"uniqueId\":" + std::to_string(Get<uint16_t>(item, Offset("ItemData", "uniqueID"))) +
           ",\"lp\":" + std::to_string(Get<uint8_t>(item, Offset("ItemData", "legendaryPotential"))) + ",\"affixes\":[" + affixes + "]}";
}
}  // namespace
std::string Test(const std::string& expectedId, const std::vector<std::string>& args) {
    return Run([expectedId, args] {
        if (!game::IsOfflinePlay() || args.empty()) throw std::runtime_error("use eternitytest <save id> state|put x y|seal id|none|uiseal index");
        Prepare();
        void* actor = Invoke(m_player);
        void* tracker = actor ? Get<void*>(actor, Offset("Actor", "characterDataTracker")) : nullptr;
        void* data = tracker ? Get<void*>(tracker, Offset("CharacterDataTracker", "charData")) : nullptr;
        if (!data || Text(Invoke(Method(data, "get_Id", 0), data)) != expectedId) throw std::runtime_error("loaded offline save id changed");
        void* manager = Invoke(game::FindMethod("LE.dll", "", "ItemContainersManager", "get_Instance", 0));
        void* cache = Get<void*>(manager, Offset("ItemContainersManager", "eternityCache"));
        const auto item = [&](const char* name) {
            void* c = Get<void*>(cache, Offset("ItemContainersManager.EternityCacheContainers", name));
            return c ? Invoke(Method(c, "getItem", 0), c) : nullptr;
        };
        std::string result = "null";
        if (args[0] == "put" && args.size() == 3) {
            struct V2 { int32_t x, y; } at{std::stoi(args[1]), std::stoi(args[2])};
            int32_t inventory = 1;  // ContainerID.INVENTORY
            void* a[]{&inventory, &at};
            result = Boolean(Value<bool>(Method(manager, "tryManualQuickSwapFromInventoryToEternity", 2), manager, a));
        } else if (args[0] == "seal" && args.size() == 2) {
            struct Nullable { bool has; uint16_t value; } affix{args[1] != "none", static_cast<uint16_t>(args[1] == "none" ? 0 : std::stoi(args[1]))};
            void* a[]{&affix};
            result = Boolean(Value<bool>(Method(manager, "tryToSealEternityCache", 1), manager, a));
        } else if (args[0] == "uiseal" && args.size() == 2) {
            void* panel = game::StaticObject(game::FindStaticField("LE.dll", "", "EternityCachePanelUI", "instance"));
            if (!panel) throw std::runtime_error("Eternity Cache panel not built");
            void* dropdown = Get<void*>(panel, Offset("EternityCachePanelUI", "affixSelectionDropdown"));
            int index = std::stoi(args[1]);  // below 0: the affix choices stay as they are
            void* a[]{&index};
            if (dropdown && index >= 0) Invoke(game::FindMethod("Unity.TextMeshPro.dll", "TMPro", "TMP_Dropdown", "set_value", 1), dropdown, a);
            Invoke(Method(panel, "seal", 0), panel);
            result = "true";
        } else if (args[0] == "clearafter") {
            void* after = Get<void*>(cache, Offset("ItemContainersManager.EternityCacheContainers", "after"));
            Invoke(Method(after, "Clear", 0), after);
            void* panel = game::StaticObject(game::FindStaticField("LE.dll", "", "EternityCachePanelUI", "instance"));
            if (panel) Invoke(Method(panel, "Close", 0), panel);
            result = "true";
        } else if ((args[0] == "show" || args[0] == "hide") && args.size() == 2) {
            Invoke(game::FindMethod("Unity.TextMeshPro.dll", "TMPro", "TMP_Dropdown", args[0] == "show" ? "Show" : "Hide", 0), g_boxes.at(std::stoul(args[1])).dropdown->Get());
            result = "true";
        } else if (args[0] == "pick" && args.size() == 3) {
            // a choice as the player makes it: <box> <the panel's option number: 0 none, 1-4 affix, 5 fate>
            Box& box = g_boxes.at(std::stoul(args[1]));
            const auto at = std::find(box.map.begin(), box.map.end(), std::stoi(args[2]));
            if (at == box.map.end()) throw std::runtime_error("that option is not offered in this choice");
            int index = static_cast<int>(at - box.map.begin());
            void* a[]{&index};
            Invoke(game::FindMethod("Unity.TextMeshPro.dll", "TMPro", "TMP_Dropdown", "set_value", 1), box.dropdown->Get(), a);
            result = "true";
        } else if (args[0] != "state") {
            throw std::runtime_error("use eternitytest <save id> state|put x y|seal id|none|uiseal index");
        }
        // the panel's affix choices, as the dropdown shows them
        std::string options;
        void* panel = game::StaticObject(game::FindStaticField("LE.dll", "", "EternityCachePanelUI", "instance"));
        void* dropdown = panel ? Get<void*>(panel, Offset("EternityCachePanelUI", "affixSelectionDropdown")) : nullptr;
        if (dropdown) {
            void* list = Invoke(game::FindMethod("Unity.TextMeshPro.dll", "TMPro", "TMP_Dropdown", "get_options", 0), dropdown);
            for (void* o : Entries(list)) options += (options.empty() ? "" : ",") + Json(o ? Text(Invoke(Method(o, "get_text", 0), o)) : "");
        }
        std::string boxes;
        for (int i = 0; i < g_shown; ++i) {
            std::string offered;
            for (int o : g_boxes[i].map) offered += (offered.empty() ? "" : ",") + std::to_string(o);
            boxes += (boxes.empty() ? "" : ",") + std::string("{\"picked\":") + std::to_string(g_boxes[i].picked) + ",\"offered\":[" + offered + "]}";
        }
        void* seal = panel ? Get<void*>(panel, Offset("EternityCachePanelUI", "sealButton")) : nullptr;
        const bool enabled = seal && Value<bool>(game::FindMethod("UnityEngine.UI.dll", "UnityEngine.UI", "Selectable", "get_interactable", 0), seal);
        return "{\"ok\":true,\"result\":" + result + ",\"boxes\":[" + boxes + "],\"multi\":" + Boolean(g_multi) + ",\"sealEnabled\":" + Boolean(enabled) +
               ",\"unique\":" + ItemJson(item("beforeMain")) + ",\"exalted\":" + ItemJson(item("beforeOther")) +
               ",\"after\":" + ItemJson(item("after")) + ",\"sealed\":" + Boolean(panel && Get<bool>(panel, Offset("EternityCachePanelUI", "cacheSealed"))) +
               ",\"options\":[" + options + "]}";
    });
}
#endif

std::string Status() {
    std::ostringstream out;
    out << "eternitycache: " << (g_on.load() ? "on" : "off") << ", opens " << g_opens.load() << ", tier 4 checks " << g_scopes.load()
        << ", faults " << g_faults.load();
    return out.str();
}

}  // namespace ep::eternity
