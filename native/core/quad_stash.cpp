#include "quad_stash.hpp"
#include "managed.hpp"
#include "common.hpp"
#include "hook.hpp"
#include "lifecycle.hpp"
#include "ingame_window.hpp"
#include "ingame_ui.hpp"
#include "report.hpp"
#include "stash_rules.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <map>
#include <array>
#include <vector>
#include <algorithm>
#include <sstream>
#include <stdexcept>

// The game makes each stash tab's ItemContainer 12 x 17 (StashItemContainer..ctor, ItemContainersManager.AddNewStashTab)
// and then loads the saved items into it (StashItemContainer.LoadItems): an item outside the container is refused there.
// So a quad tab is made 24 x 34 just before LoadItems, and on demand later. Everything else (placing, sorting, tab
// priority, search) reads the container's own size. The window divides its panel by the container's size once
// (ItemContainerUI.slotSize, from CalculateSlotSize); it is worked out again before each full redraw of the stash.
namespace ep::quadstash {
namespace {
using namespace managed;
struct V2 { int x, y; };
struct V2f { float x, y; };
constexpr V2 kNormal{12, 17}, kQuad{24, 34};
std::set<std::string> quad;  // StashTab ids
bool loaded = false, ready = false;
std::map<std::string, std::array<std::string, 3>> companionsLoaded;  // from the file: quad tab id -> companion ids
uint64_t resized = 0, redraws = 0, faults = 0;
std::string lastError;
const il2cpp::Class* stashUiClass = nullptr;

std::filesystem::path FilePath() {
    const auto root = ingame::view::PreferencesFolder();
    if (root.empty()) throw std::runtime_error("no folder for the quad stash tabs");
#ifdef EPOCHPACT_TESTING
    return root / L"quad-stash-test.txt";  // test games never touch the player's own
#else
    return root / L"quad-stash.txt";
#endif
}
void Load() {
    if (loaded) return;
    loaded = true;
    std::ifstream in(FilePath(), std::ios::binary);
    std::string word, id;
    while (in >> word >> id) {
        if (word == "quad" && id.size() < 200) quad.insert(id);
        else if (word == "pair") { int slot = 0; std::string other; in >> slot >> other; if (in && slot >= 1 && slot <= 3 && other.size() < 200) companionsLoaded[id][slot - 1] = other; }
    }
}
void Save() {
    const auto path = FilePath();
    std::filesystem::create_directories(path.parent_path());
    auto temp = path; temp += L".new";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << "epochpact-quad-stash 1\n";
        for (const auto& id : quad) out << "quad " << id << "\n";
        for (const auto& [id, list] : companionsLoaded) for (int k = 0; k < 3; ++k) if (!list[k].empty()) out << "pair " << id << ' ' << k + 1 << ' ' << list[k] << "\n";
        out.flush();
        if (!out) throw std::runtime_error("the quad stash tabs could not be written");
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("the quad stash tabs could not be stored");
}
size_t F(const char* cls, const char* field, const char* ns = "") {
    return Offset(cls, field, ns);
}
std::vector<void*> Tabs(void* container) {
    void* stash = Invoke(Method(container, "get_LinkedStash", 0), container);
    void* list = stash ? Get<void*>(stash, F("Stash", "Tabs", "LE.Data")) : nullptr;
    std::vector<void*> out;
    if (!list) return out;
    const int count = Value<int>(Method(list, "get_Count", 0), list);
    for (int i = 0; i < count && i < 1000; ++i) { int at = i; void* a[]{&at}; out.push_back(Invoke(Method(list, "get_Item", 1), list, a)); }
    return out;
}
std::string TabId(void* tab) { return tab ? Text(Invoke(Method(tab, "get_Id", 0), tab)) : std::string(); }
int TabNumber(void* tab) { return tab ? Get<int>(tab, F("StashTab", "_tabId", "LE.Data")) : -1; }
void* ContainerOf(const std::vector<void*>& containers, void* tab) {
    const int n = TabNumber(tab);
    return n >= 0 && static_cast<size_t>(n) < containers.size() ? containers[static_cast<size_t>(n)] : nullptr;
}
void* TabWithId(const std::vector<void*>& tabs, const std::string& id) {
    for (void* t : tabs) if (t && TabId(t) == id) return t;
    return nullptr;
}
bool IsCompanionId(const std::string& id) {
    for (const auto& [q, list] : companionsLoaded) for (const auto& c : list) if (c == id) return true;
    return false;
}
std::string TabName(void* tab) { return tab ? Text(Get<void*>(tab, F("StashTab", "_displayName", "LE.Data"))) : std::string(); }
V2 SizeOf(void* c) { return Get<V2>(c, F("ItemContainer", "size")); }
bool FitsNormal(void* c) {
    for (void* e : Entries(Invoke(Method(c, "GetContent", 0), c))) {
        if (!e) continue;
        const V2 at = Get<V2>(e, F("ItemContainerEntry", "<Position>k__BackingField")), size = Get<V2>(e, F("ItemContainerEntry", "size"));
        if (at.x < 0 || at.y < 0 || at.x + size.x > kNormal.x || at.y + size.y > kNormal.y) return false;
    }
    return true;
}
// A container's cells: a new bool[w, h], the items already in it marked again.
bool Resize(void* c, V2 to) {
    const V2 now = SizeOf(c);
    if (now.x == to.x && now.y == to.y) return false;
    if (!((now.x == kNormal.x && now.y == kNormal.y) || (now.x == kQuad.x && now.y == kQuad.y))) throw std::runtime_error("a stash tab of an unexpected size");
    auto& api = il2cpp::api();
    Root type(api.type_get_object(api.class_get_type(game::FindClass("mscorlib.dll", "System", "Boolean"))));
    int w = to.x, h = to.y; void* ca[]{type.Get(), &w, &h};
    const il2cpp::Method* create = nullptr; void* iterator = nullptr;
    while (const auto* m = api.class_get_methods(game::FindClass("mscorlib.dll", "System", "Array"), &iterator)) {
        if (std::string(api.method_get_name(m)) != "CreateInstance" || api.method_get_param_count(m) != 3) continue;
        char* second = api.type_get_name(api.method_get_param(m, 1));
        const bool match = second && std::string(second) == "System.Int32";
        if (second) api.free(second);
        if (match) { create = m; break; }
    }
    if (!create) throw std::runtime_error("no Array.CreateInstance(Type, int, int)");
    Root cells(Invoke(create, nullptr, ca));
    if (!cells.Get()) throw std::runtime_error("the new cells were not made");
    *reinterpret_cast<V2*>(static_cast<char*>(c) + F("ItemContainer", "size")) = to;
    api.field_set_value(c, api.class_get_field_from_name(game::FindClass("LE.dll", "", "ItemContainer"), "slotOccupied"), cells.Get());
    for (void* e : Entries(Invoke(Method(c, "GetContent", 0), c))) {
        if (!e) continue;
        V2 at = Get<V2>(e, F("ItemContainerEntry", "<Position>k__BackingField")), size = Get<V2>(e, F("ItemContainerEntry", "size"));
        bool occupied = true; void* sa[]{&at, &size, &occupied};
        Invoke(Method(c, "SetSlotsOccupied", 3), c, sa);
    }
    ++resized;
    return true;
}
// Every tab of a stash container to the size the file says.
size_t Apply(void* container) {
    Load();
    const auto tabs = Tabs(container);
    const auto containers = Entries(Invoke(Method(container, "get_Containers", 0), container));
    size_t changed = 0;
    for (void* tab : tabs) {
        void* c = ContainerOf(containers, tab);
        if (!tab || !c) continue;
        const bool want = quad.count(TabId(tab)) > 0;
        if (!want && !FitsNormal(c)) continue;  // never smaller than what it holds
        changed += Resize(c, want ? kQuad : kNormal);
    }
    return changed;
}
void* Manager() {
    void* m = game::StaticObject(game::FindStaticField("LE.dll", "", "ItemContainersManager", "_instance"));
    return m && game::IsAlive(m) ? m : nullptr;
}
void* StashContainer() {
    void* manager = Manager();
    void* holder = manager ? Get<void*>(manager, F("ItemContainersManager", "stash")) : nullptr;
    return holder ? Invoke(Method(holder, "get_CurrentContainer", 0), holder) : nullptr;
}
std::vector<void*> StashWindows() {
    std::vector<void*> out;
    auto& api = il2cpp::api();
    Root type(api.type_get_object(api.class_get_type(game::FindClass("LE.dll", "", "StashItemContainerUI"))));
    bool inactive = false; void* args[]{type.Get(), &inactive};
    const il2cpp::Method* find = nullptr; void* iterator = nullptr;
    while (const auto* m = api.class_get_methods(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Object"), &iterator))
        if (std::string(api.method_get_name(m)) == "FindObjectsOfType" && api.method_get_param_count(m) == 2) { find = m; break; }
    Root found(Invoke(find, nullptr, args));
    for (uintptr_t i = 0, n = found.Get() ? Get<uintptr_t>(found.Get(), 0x18) : 0; i < n && i < 8; ++i) {
        void* w = Get<void*>(found.Get(), 0x20 + 8 * i);
        if (w && game::IsAlive(w)) out.push_back(w);
    }
    return out;
}
// The window's cell size from its panel and the tab shown.
bool FixCells(void* window) {
    const V2f before = Get<V2f>(window, F("ItemContainerUI", "slotSize"));
    const V2f now = Value<V2f>(Method(window, "CalculateSlotSize", 0), window);
    if (!(now.x > 0 && now.y > 0) || (std::abs(now.x - before.x) < 1e-3f && std::abs(now.y - before.y) < 1e-3f)) return false;
    *reinterpret_cast<V2f*>(static_cast<char*>(window) + F("ItemContainerUI", "slotSize")) = now;
    return true;
}
// The grid lines are a picture of 12 x 17 cells ("grid-img", next to the window). A quad tab keeps that picture and gets
// the lines between its lines: 12 upright and 17 across, at the odd 24ths and 34ths of the window's own rectangle, in the
// colour of the game's lines (about 22, 16, 9 on screen), so the picture's frame stays only at the outside.
struct V3 { float x, y, z; };
struct V2v { float x, y; };
struct GridLines { void* window = nullptr; std::unique_ptr<Root> holder; bool quad = false; };
std::vector<GridLines> grids;
const il2cpp::Method* UnityOverload(const char* cls, const char* name, std::initializer_list<const char*> parameters, const char* image = "UnityEngine.CoreModule.dll",
                                    const char* ns = "UnityEngine") {
    auto& api = il2cpp::api(); void* iterator = nullptr;
    while (const auto* m = api.class_get_methods(game::FindClass(image, ns, cls), &iterator)) {
        if (std::string(api.method_get_name(m)) != name || api.method_get_param_count(m) != parameters.size()) continue;
        bool match = true; unsigned i = 0;
        for (const char* expected : parameters) { char* actual = api.type_get_name(api.method_get_param(m, i++)); match &= actual && std::string(actual) == expected; if (actual) api.free(actual); }
        if (match) return m;
    }
    throw std::runtime_error(std::string("missing ") + ns + "." + cls + "." + name);
}
void* TypeObject(const char* image, const char* ns, const char* cls) {
    auto& api = il2cpp::api();
    return api.type_get_object(api.class_get_type(game::FindClass(image, ns, cls)));
}
// A new UI object under parent: a RectTransform, and an Image when color is given.
void* NewRect(void* parent, const char* name, const float* color) {
    auto& api = il2cpp::api();
    const auto* goClass = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
    Root go(api.object_new(goClass));
    Root text(api.string_new(name)); void* na[]{text.Get()};
    Invoke(UnityOverload("GameObject", ".ctor", {"System.String"}), go.Get(), na);
    Root rectType(TypeObject("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform")); void* ra[]{rectType.Get()};
    void* rect = Invoke(UnityOverload("GameObject", "AddComponent", {"System.Type"}), go.Get(), ra);
    bool keep = false; void* pa[]{parent, &keep};
    Invoke(UnityOverload("Transform", "SetParent", {"UnityEngine.Transform", "System.Boolean"}), rect, pa);
    if (color) {
        Root imageType(TypeObject("UnityEngine.UI.dll", "UnityEngine.UI", "Image")); void* ia[]{imageType.Get()};
        void* image = Invoke(UnityOverload("GameObject", "AddComponent", {"System.Type"}), go.Get(), ia);
        struct Color { float r, g, b, a; } c{color[0], color[1], color[2], color[3]}; void* ca[]{&c};
        Invoke(Method(image, "set_color", 1), image, ca);
        bool no = false; void* oa[]{&no};
        Invoke(Method(image, "set_raycastTarget", 1), image, oa);
    }
    return rect;
}
void SetRect(void* rect, V2v anchorMin, V2v anchorMax, V2v pivot, V2v position, V2v size) {
    void* a1[]{&anchorMin}; Invoke(Method(rect, "set_anchorMin", 1), rect, a1);
    void* a2[]{&anchorMax}; Invoke(Method(rect, "set_anchorMax", 1), rect, a2);
    void* a3[]{&pivot}; Invoke(Method(rect, "set_pivot", 1), rect, a3);
    void* a4[]{&size}; Invoke(Method(rect, "set_sizeDelta", 1), rect, a4);
    void* a5[]{&position}; Invoke(Method(rect, "set_anchoredPosition", 1), rect, a5);
}
void* GridPicture(void* window) {
    void* t = Invoke(Method(window, "get_transform", 0), window);
    void* parent = t ? Invoke(Method(t, "get_parent", 0), t) : nullptr;
    if (!parent) return nullptr;
    Root name(il2cpp::api().string_new("grid-img")); void* a[]{name.Get()};
    return Invoke(Method(parent, "Find", 1), parent, a);
}
bool TabShownIsQuad() {
    void* container = StashContainer();
    if (!container) return false;
    const auto containers = Entries(Invoke(Method(container, "get_Containers", 0), container));
    const int shown = Get<int>(container, F("TabbedItemContainer", "currentlyActiveTab"));
    if (shown < 0 || static_cast<size_t>(shown) >= containers.size() || !containers[static_cast<size_t>(shown)]) return false;
    const V2 size = SizeOf(containers[static_cast<size_t>(shown)]);
    return size.x == kQuad.x && size.y == kQuad.y;
}
void SetGrid(void* window, bool quad) {
    GridLines* look = nullptr;
    for (auto& g : grids) if (g.window == window && g.holder && g.holder->Get() && game::IsAlive(g.holder->Get())) look = &g;
    if (!look) {
        if (!quad) return;  // never made: nothing to hide
        void* picture = GridPicture(window);
        void* windowRect = Invoke(Method(window, "get_transform", 0), window);
        void* parent = Invoke(Method(windowRect, "get_parent", 0), windowRect);
        if (!parent) return;
        GridLines g; g.window = window;
        void* holder = NewRect(parent, "EP Quad Lines", nullptr);
        // the window's own rectangle: the cells are laid out in it
        SetRect(holder, Value<V2v>(Method(windowRect, "get_anchorMin", 0), windowRect), Value<V2v>(Method(windowRect, "get_anchorMax", 0), windowRect),
                Value<V2v>(Method(windowRect, "get_pivot", 0), windowRect), Value<V2v>(Method(windowRect, "get_anchoredPosition", 0), windowRect),
                Value<V2v>(Method(windowRect, "get_sizeDelta", 0), windowRect));
        V3 one{1, 1, 1}, scale = Value<V3>(Method(windowRect, "get_localScale", 0), windowRect); (void)one;
        void* sa[]{&scale}; Invoke(Method(holder, "set_localScale", 1), holder, sa);
        // just over the picture, under the items
        int index = picture ? Value<int>(Method(picture, "GetSiblingIndex", 0), picture) + 1 : Value<int>(Method(windowRect, "GetSiblingIndex", 0), windowRect);
        void* ia[]{&index}; Invoke(Method(holder, "SetSiblingIndex", 1), holder, ia);
        struct Rect { float x, y, w, h; } r = Value<Rect>(Method(windowRect, "get_rect", 0), windowRect);
        const float width = std::max(1.f, r.w * 0.0025f);  // the game's lines: about 2 pixels on a 1440 high screen
        const float color[]{22 / 255.f, 16 / 255.f, 9 / 255.f, 1};
        for (int k = 1; k < kQuad.x; k += 2) {
            void* line = NewRect(holder, "EP Quad Line", color);
            const float at = static_cast<float>(k) / kQuad.x;
            SetRect(line, {at, 0}, {at, 1}, {0.5f, 0.5f}, {0, 0}, {width, 0});
        }
        for (int k = 1; k < kQuad.y; k += 2) {
            void* line = NewRect(holder, "EP Quad Line", color);
            const float at = static_cast<float>(k) / kQuad.y;
            SetRect(line, {0, at}, {1, at}, {0.5f, 0.5f}, {0, 0}, {0, width});
        }
        g.holder = std::make_unique<Root>(holder);
        g.quad = true;
        grids.push_back(std::move(g));
        return;
    }
    if (look->quad == quad) return;
    void* go = Invoke(Method(look->holder->Get(), "get_gameObject", 0), look->holder->Get());
    bool on = quad; void* oa[]{&on}; Invoke(Method(go, "SetActive", 1), go, oa);
    look->quad = quad;
}
void Redraw() {
    for (void* w : StashWindows()) {
        FixCells(w);
        SetGrid(w, TabShownIsQuad());
        Invoke(Method(w, "ReDrawWholeContainer", 0), w);
        ++redraws;
    }
}

void ApplyTab(int index, bool on);
// ---- Companions. Without EpochPact the game refuses items outside 12 x 17 and loses them when that tab saves. So a quad
// tab's save holds only its own quarter (x < 12, y < 17); the three others are saved in three companion tabs of its own
// (a folder "EpochPact quad"), shifted into 12 x 17: companion 1 holds x 12-23, companion 2 y 17-33, companion 3 both.
// Before LoadItems the companions' items join the quad tab's list (the game then loads them into the larger container) and
// the companions load empty; after SaveStash rebuilt a quad tab's list it is split again. Nothing can be put into a
// companion while EpochPact runs (DoBoundsFitContainer says no). Without EpochPact the player sees the four tabs, all items.
constexpr int kCompanions = 3;
constexpr V2 kOffset[kCompanions + 1]{{0, 0}, {12, 0}, {0, 17}, {12, 17}};
std::set<void*> companionContainers;
// Held while they are in the set: a freed container's address could otherwise come back as another container (the
// inventory), which would then refuse every item.
std::vector<std::unique_ptr<Root>> companionRoots;
uint64_t splits = 0, merges = 0, relocated = 0;

void* NewList(void* like) {
    auto& api = il2cpp::api();
    void* list = api.object_new(api.object_get_class(like));
    Invoke(api.class_get_method_from_name(api.object_get_class(like), ".ctor", 0), list);
    return list;
}
void SetSaved(void* tab, void* list) { void* a[]{list}; Invoke(Method(tab, "set_SavedItems", 1), tab, a); }
void* Saved(void* tab) { return Invoke(Method(tab, "get_SavedItems", 0), tab); }
// ItemInventoryPosition is a struct of two ints, held inside the entry.
V2 PositionOf(void* pair) { return Get<V2>(pair, F("ItemLocationPair", "<InventoryPosition>k__BackingField", "LE.Data")); }
// A copy of a saved entry at another place and tab: a new ItemLocationPair, the original untouched.
void* Moved(void* pair, V2 at, int tab) {
    auto& api = il2cpp::api();
    V2 position = at;  // a value: passed by its address
    const auto* pairClass = game::FindClass("LE.dll", "LE.Data", "ItemLocationPair");
    void* copy = api.object_new(pairClass);
    uint16_t container = Get<uint16_t>(pair, F("ItemLocationPair", "<ContainerID>k__BackingField", "LE.Data"));
    void* data = Get<void*>(pair, F("ItemLocationPair", "<Data>k__BackingField", "LE.Data"));
    int quantity = Get<int>(pair, F("ItemLocationPair", "<Quantity>k__BackingField", "LE.Data")), number = tab;
    void* ca[]{&container, data, &position, &quantity, &number};
    Invoke(game::FindMethod("LE.dll", "LE.Data", "ItemLocationPair", ".ctor", 5), copy, ca);
    return copy;
}
void Add(void* list, void* item) { void* a[]{item}; Invoke(Method(list, "Add", 1), list, a); }
// The quad tabs of a stash with all three companions there, and the containers of those companions.
struct Group { void* quad; std::array<void*, kCompanions> others; };
std::vector<Group> Groups(const std::vector<void*>& tabs) {
    std::vector<Group> out;
    for (const auto& id : quad) {
        auto it = companionsLoaded.find(id);
        void* q = TabWithId(tabs, id);
        if (!q || it == companionsLoaded.end()) continue;
        Group g{q, {}};
        bool all = true;
        for (int k = 0; k < kCompanions; ++k) { g.others[k] = it->second[k].empty() ? nullptr : TabWithId(tabs, it->second[k]); all &= g.others[k] != nullptr; }
        if (all) out.push_back(g);
    }
    return out;
}
void RememberCompanions(const std::vector<void*>& tabs, const std::vector<void*>& containers) {
    companionContainers.clear();
    companionRoots.clear();
    for (const auto& [id, list] : companionsLoaded) {
        if (!quad.count(id)) continue;  // a tab made normal again: its companions are plain tabs
        for (const auto& c : list) if (void* tab = TabWithId(tabs, c)) if (void* container = ContainerOf(containers, tab)) { companionRoots.push_back(std::make_unique<Root>(container)); companionContainers.insert(container); }
    }
}
// Before LoadItems: each quad tab's list with its companions' items moved back by their offset; the companions empty.
std::vector<std::pair<void*, std::unique_ptr<Root>>> MergeForLoad(void* container) {
    std::vector<std::pair<void*, std::unique_ptr<Root>>> kept;  // tab, its own list, put back after the load
    const auto tabs = Tabs(container);
    for (const auto& g : Groups(tabs)) {
        void* own = Saved(g.quad);
        if (!own) continue;
        Root merged(NewList(own));
        for (void* e : Entries(own)) if (e) Add(merged.Get(), e);
        const int number = TabNumber(g.quad);
        for (int k = 0; k < kCompanions; ++k) {
            void* theirs = Saved(g.others[k]);
            if (!theirs) continue;
            for (void* e : Entries(theirs)) {
                if (!e) continue;
                const V2 at = PositionOf(e);
                Add(merged.Get(), Moved(e, {at.x + kOffset[k + 1].x, at.y + kOffset[k + 1].y}, number));
                ++merges;
            }
            kept.emplace_back(g.others[k], std::make_unique<Root>(theirs));
            Root empty(NewList(theirs));
            SetSaved(g.others[k], empty.Get());
        }
        kept.emplace_back(g.quad, std::make_unique<Root>(own));
        SetSaved(g.quad, merged.Get());
    }
    return kept;
}
// After SaveStash rebuilt a quad tab's list: its quarters into the four tabs, shifted. An item across a quarter line goes,
// in the save only, to a free place of any quarter; if there is none it stays as it was (and needs EpochPact).
void SplitAfterSave(void* quadTab, void* quadContainer, const std::array<void*, kCompanions>& others) {
    void* list = Saved(quadTab);
    if (!list) return;
    struct Placed { void* pair; V2 at, size; };
    std::map<std::pair<int, int>, V2> sizes;
    for (void* e : Entries(Invoke(Method(quadContainer, "GetContent", 0), quadContainer)))
        if (e) { const V2 p = Get<V2>(e, F("ItemContainerEntry", "<Position>k__BackingField")); sizes[{p.x, p.y}] = Get<V2>(e, F("ItemContainerEntry", "size")); }
    bool cells[kCompanions + 1][12][17]{};
    std::vector<void*> quarter[kCompanions + 1];
    std::vector<Placed> across;
    const auto mark = [&](int q, V2 at, V2 size) { for (int x = at.x; x < at.x + size.x; ++x) for (int y = at.y; y < at.y + size.y; ++y) if (x >= 0 && x < 12 && y >= 0 && y < 17) cells[q][x][y] = true; };
    const auto free = [&](int q, V2 at, V2 size) {
        if (at.x < 0 || at.y < 0 || at.x + size.x > 12 || at.y + size.y > 17) return false;
        for (int x = at.x; x < at.x + size.x; ++x) for (int y = at.y; y < at.y + size.y; ++y) if (cells[q][x][y]) return false;
        return true;
    };
    for (void* e : Entries(list)) {
        if (!e) continue;
        const V2 at = PositionOf(e);
        auto s = sizes.find({at.x, at.y});
        const V2 size = s == sizes.end() ? V2{1, 1} : s->second;
        const int q = (at.x >= 12 ? 1 : 0) + (at.y >= 17 ? 2 : 0);
        const V2 local{at.x - kOffset[q].x, at.y - kOffset[q].y};
        if (local.x + size.x <= 12 && local.y + size.y <= 17) { mark(q, local, size); quarter[q].push_back(q == 0 ? e : Moved(e, local, TabNumber(others[q - 1]))); }
        else across.push_back({e, at, size});
    }
    for (const auto& a : across) {
        bool placed = false;
        for (int q = 0; q <= kCompanions && !placed; ++q)
            for (int y = 0; y + a.size.y <= 17 && !placed; ++y)
                for (int x = 0; x + a.size.x <= 12 && !placed; ++x)
                    if (free(q, {x, y}, a.size)) { mark(q, {x, y}, a.size); quarter[q].push_back(Moved(a.pair, {x, y}, q == 0 ? TabNumber(quadTab) : TabNumber(others[q - 1]))); placed = true; ++relocated; }
        if (!placed) quarter[0].push_back(a.pair);  // no room anywhere: kept as it was
    }
    for (int q = 0; q <= kCompanions; ++q) {
        Root fresh(NewList(list));
        for (void* e : quarter[q]) Add(fresh.Get(), e);
        void* tab = q == 0 ? quadTab : others[q - 1];
        SetSaved(tab, fresh.Get());
        void* ma[]{tab};
        Invoke(game::FindMethod("LE.dll", "LE.Data", "StashTab", "MarkDirty", 1), nullptr, ma);
    }
    ++splits;
}
bool Dirty(void* container) { return Get<bool>(container, F("ItemContainer", "<DirtyForSaving>k__BackingField")); }
void SetDirty(void* container) { *reinterpret_cast<bool*>(static_cast<char*>(container) + F("ItemContainer", "<DirtyForSaving>k__BackingField")) = true; }
using SaveFn = void (*)(void* self, void* container, const il2cpp::Method* method);
SaveFn originalSave = nullptr;
void SaveStash(void* self, void* container, const il2cpp::Method* method) {
    struct Pending { void* quad; void* quadContainer; std::array<void*, kCompanions> others; };
    std::vector<Pending> pending;
    if (container && !lifecycle::Stopping() && game::IsOfflinePlay())
        game::Guarded([&] {
            const auto tabs = Tabs(container);
            const auto containers = Entries(Invoke(Method(container, "get_Containers", 0), container));
            for (const auto& g : Groups(tabs)) {
                void* qc = ContainerOf(containers, g.quad);
                if (!qc) continue;
                bool any = Dirty(qc);
                for (void* o : g.others) if (void* oc = ContainerOf(containers, o); oc && Dirty(oc)) any = true;
                if (!any) continue;
                SetDirty(qc);  // the quad tab's list is rebuilt, then split
                pending.push_back({g.quad, qc, g.others});
            }
        }, nullptr);
    originalSave(self, container, method);
    for (const auto& p : pending) {
        std::string why;
        if (!game::Guarded([&] { SplitAfterSave(p.quad, p.quadContainer, p.others); }, &why)) { ++faults; lastError = why; Log("quad stash: the save split failed: %s", why.c_str()); }
    }
}
using FitFn = bool (*)(void* self, int64_t at, int64_t size, const il2cpp::Method* method);
FitFn originalFit = nullptr;
bool Fits(void* self, int64_t at, int64_t size, const il2cpp::Method* method) {
    if (self && companionContainers.count(self)) return false;  // a companion only keeps a quad tab's save
    return originalFit(self, at, size, method);
}
// ---- The game's Configure Tab window gets a "Quad stash" row: a copy of its own Tab Priority row (check, title,
// explanation), under the priority section. Confirm makes the tab quad or normal; normal is refused there, as the game
// closes the window after Confirm either way, so a refusal shows as the game's notice and the tab stays quad.
struct ConfigureRow { std::unique_ptr<Root> window, row, toggle; int shownTab = -999; bool wasOpen = false; };
ConfigureRow configure;
void* Child(void* transform, const char* path) {
    Root name(il2cpp::api().string_new(path)); void* a[]{name.Get()};
    return transform ? Invoke(Method(transform, "Find", 1), transform, a) : nullptr;
}
void* ComponentOf(void* transform, const char* image, const char* ns, const char* cls) {
    auto& api = il2cpp::api();
    Root type(api.type_get_object(api.class_get_type(game::FindClass(image, ns, cls))));
    void* ta[]{type.Get()};
    return transform ? Invoke(UnityOverload("Component", "GetComponent", {"System.Type"}), transform, ta) : nullptr;
}
void SetText(void* transform, const std::string& text) {
    void* tmp = ComponentOf(transform, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text");
    if (!tmp) return;
    Root s(il2cpp::api().string_new(text.c_str())); void* a[]{s.Get()};
    Invoke(Method(tmp, "set_text", 1), tmp, a);
}
// The tab the window configures: its place in the stash, -1 for none (a category).
int ConfiguredTab(void* window) {
    void* container = StashContainer();
    if (!container) return -1;
    const int id = Get<int>(window, F("ConfigureTabUI", "selectedTabID"));
    const auto tabs = Tabs(container);
    for (size_t i = 0; i < tabs.size(); ++i) if (tabs[i] && Get<int>(tabs[i], F("StashTab", "_tabId", "LE.Data")) == id) return static_cast<int>(i);
    return -1;
}
void* MakeRow(void* window) {
    void* priority = Get<void*>(window, F("ConfigureTabUI", "stashPriorityUI"));
    void* section = priority ? Invoke(Method(priority, "get_transform", 0), priority) : nullptr;
    void* source = Child(section, "Explanation Row");
    if (!source) throw std::runtime_error("the Configure Tab window has no Tab Priority row");
    void* contents = Invoke(Method(section, "get_parent", 0), section);
    void* go = Invoke(Method(source, "get_gameObject", 0), source);
    void* ia[]{go, contents};
    void* copy = Invoke(UnityOverload("Object", "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform"}), nullptr, ia);
    if (!copy) throw std::runtime_error("the Tab Priority row could not be copied");
    void* row = Invoke(Method(copy, "get_transform", 0), copy);
    Root rowName(il2cpp::api().string_new("EP Quad Row")); void* na[]{rowName.Get()};
    Invoke(Method(copy, "set_name", 1), copy, na);
    int index = Value<int>(Method(section, "GetSiblingIndex", 0), section) + 1; void* sa[]{&index};
    Invoke(Method(row, "SetSiblingIndex", 1), row, sa);
    // the copy's words are ours: the game's localizer would write its own back
    for (const char* path : {"OptionText/Title", "OptionText/Explanation"}) {
        void* t = Child(row, path);
        while (void* localizer = ComponentOf(t, "Unity.Localization.dll", "UnityEngine.Localization.Components", "LocalizeStringEvent")) {
            void* da[]{localizer};
            Invoke(UnityOverload("Object", "DestroyImmediate", {"UnityEngine.Object"}), nullptr, da);
        }
    }
    SetText(Child(row, "OptionText/Title"), ingame::ui::Tr("Quad stash"));
    SetText(Child(row, "OptionText/Explanation"), ingame::ui::Tr("24 × 34 cells, four times the items (EpochPact). Kept safe in three storage tabs for when EpochPact is removed."));
    // the check does nothing by itself: a new, empty event
    void* toggle = ComponentOf(Child(row, "Priority Toggle"), "UnityEngine.UI.dll", "UnityEngine.UI", "Toggle");
    if (!toggle) throw std::runtime_error("the copied row has no check");
    auto& api = il2cpp::api();
    const il2cpp::Class* eventClass = nullptr;
    {   // Toggle.ToggleEvent, the type of the field itself
        const auto* field = api.class_get_field_from_name(game::FindClass("UnityEngine.UI.dll", "UnityEngine.UI", "Toggle"), "onValueChanged");
        if (field) eventClass = api.class_from_type(api.field_get_type(field));
    }
    if (eventClass) {
        Root fresh(api.object_new(eventClass));
        Invoke(api.class_get_method_from_name(eventClass, ".ctor", 0), fresh.Get());
        api.field_set_value(toggle, api.class_get_field_from_name(game::FindClass("UnityEngine.UI.dll", "UnityEngine.UI", "Toggle"), "onValueChanged"), fresh.Get());
    } else {
        void* event = Invoke(Method(toggle, "get_onValueChanged", 0), toggle);
        if (event) Invoke(Method(event, "RemoveAllListeners", 0), event);
    }
    configure.row = std::make_unique<Root>(row);
    configure.toggle = std::make_unique<Root>(toggle);
    return row;
}
// While the stash is open: the row in the window, its check set each time the window opens for a tab.
void SyncConfigure() {
    auto& api = il2cpp::api();
    Root type(api.type_get_object(api.class_get_type(game::FindClass("LE.dll", "", "ConfigureTabUI"))));
    bool inactive = false; void* args[]{type.Get(), &inactive};
    Root found(Invoke(UnityOverload("Object", "FindObjectsOfType", {"System.Type", "System.Boolean"}), nullptr, args));
    void* window = found.Get() && Get<uintptr_t>(found.Get(), 0x18) ? Get<void*>(found.Get(), 0x20) : nullptr;
    if (!window || !game::IsAlive(window)) { configure.wasOpen = false; return; }
    if (!configure.window || configure.window->Get() != window || !configure.row || !configure.row->Get() || !game::IsAlive(configure.row->Get())) {
        configure = {};
        configure.window = std::make_unique<Root>(window);
        MakeRow(window);
    }
    const int tab = ConfiguredTab(window);
    void* priority = Get<void*>(window, F("ConfigureTabUI", "stashPriorityUI"));
    void* priorityGo = priority ? Invoke(Method(priority, "get_gameObject", 0), priority) : nullptr;
    const bool forTab = tab >= 0 && priorityGo && Value<bool>(Method(priorityGo, "get_activeSelf", 0), priorityGo);
    void* rowGo = Invoke(Method(configure.row->Get(), "get_gameObject", 0), configure.row->Get());
    if (Value<bool>(Method(rowGo, "get_activeSelf", 0), rowGo) != forTab) { bool on = forTab; void* oa[]{&on}; Invoke(Method(rowGo, "SetActive", 1), rowGo, oa); }
    if (!configure.wasOpen || tab != configure.shownTab) {
        void* container = StashContainer();
        const auto tabs = container ? Tabs(container) : std::vector<void*>{};
        bool isQuad = tab >= 0 && static_cast<size_t>(tab) < tabs.size() && quad.count(TabId(tabs[static_cast<size_t>(tab)])) > 0;
        void* qa[]{&isQuad};
        Invoke(Method(configure.toggle->Get(), "SetIsOnWithoutNotify", 1), configure.toggle->Get(), qa);
        configure.shownTab = tab;
    }
    configure.wasOpen = true;
}
using ConfirmFn = void* (*)(void* self, const il2cpp::Method* method);
ConfirmFn originalConfirm = nullptr;
void* Confirm(void* self, const il2cpp::Method* method) {
    int tab = -1; bool want = false, mine = false; std::string refusal;
    if (!lifecycle::Stopping() && game::IsOfflinePlay())
        game::Guarded([&] {
            if (!configure.window || configure.window->Get() != self || !configure.toggle || !configure.toggle->Get()) return;
            void* rowGo = Invoke(Method(configure.row->Get(), "get_gameObject", 0), configure.row->Get());
            if (!Value<bool>(Method(rowGo, "get_activeSelf", 0), rowGo)) return;
            tab = ConfiguredTab(self);
            if (tab < 0) return;
            mine = true;
            want = Value<bool>(Method(configure.toggle->Get(), "get_isOn", 0), configure.toggle->Get());
            void* container = StashContainer();
            const auto containers = Entries(Invoke(Method(container, "get_Containers", 0), container));
            if (!want && static_cast<size_t>(tab) < containers.size() && !FitsNormal(containers[static_cast<size_t>(tab)])) refusal = ingame::ui::Tr("Items lie outside the normal 12 × 17 cells of this tab. Move them into the top-left part first.");
        }, nullptr);
    if (!refusal.empty()) {
        // the window closes after Confirm either way: the game's own changes go through, the tab stays quad, the reason shows on top
        report::Notice(ingame::ui::Tr("Quad stash"), refusal);
        return originalConfirm(self, method);
    }
    void* result = originalConfirm(self, method);
    if (result && mine) {
        std::string why;
        if (!game::Guarded([&] { ApplyTab(tab, want); }, &why)) { ++faults; lastError = why; Log("quad stash: %s", why.c_str()); }
    }
    return result;
}
// The folder for companions: "EpochPact quad", made once; the quad tab's own folder if the game allows no more.
int CompanionFolder(void* container, void* quadTab) {
    void* stash = Invoke(Method(container, "get_LinkedStash", 0), container);
    const auto folders = [&] { return Entries(Get<void*>(stash, F("Stash", "<Categories>k__BackingField", "LE.Data"))); };
    const auto find = [&]() -> int {
        for (void* f : folders()) if (f && Text(Get<void*>(f, F("StashCategory", "_displayName", "LE.Data"))) == "EpochPact quad") return Get<int>(f, F("StashCategory", "_categoryId", "LE.Data"));
        return -1;
    };
    int id = find();
    if (id >= 0) return id;
    game::Guarded([&] {
        void* manager = Manager();
        void* holder = Get<void*>(manager, F("ItemContainersManager", "stash"));
        int index = Value<int>(Method(holder, "get_CurrentStashIndex", 0), holder);
        Root name(il2cpp::api().string_new("EpochPact quad")); void* a[]{manager, &index, name.Get()};
        Invoke(game::FindMethod("LE.dll", "", "ServerStashCommands", "ServerAddNewCategory", 3), nullptr, a);
    }, nullptr);
    id = find();
    return id >= 0 ? id : Value<int>(Method(quadTab, "get_CategoryId", 0), quadTab);
}
// Three companions for a quad tab: an empty paired one is used again, otherwise a new tab is made. Main thread.
bool EnsureCompanions(void* container, void* quadTab) {
    auto& list = companionsLoaded[TabId(quadTab)];
    bool made = false;
    for (int k = 0; k < kCompanions; ++k) {
        const auto tabs = Tabs(container);
        const auto containers = Entries(Invoke(Method(container, "get_Containers", 0), container));
        void* existing = list[k].empty() ? nullptr : TabWithId(tabs, list[k]);
        void* ec = existing ? ContainerOf(containers, existing) : nullptr;
        if (ec && Value<int>(Method(ec, "ContentCount", 0), ec) == 0) continue;
        if (existing && !ec) continue;  // there, its container not yet: leave it
        void* manager = Manager();
        void* ca[]{container};
        if (!Value<bool>(game::FindMethod("LE.dll", "", "ItemContainersManager", "CanAddNewStashTab", 1), manager, ca)) throw std::runtime_error("The game allows no more stash tabs for this quad tab's storage.");
        int folder = CompanionFolder(container, quadTab), order = static_cast<int>(tabs.size()), icon = 0, color = 1;
        std::string base = TabName(quadTab);
        if (base.size() > 16) base.resize(16);
        Root name(il2cpp::api().string_new((base + " +" + std::to_string(k + 1)).c_str()));
        void* a[]{container, &folder, &order, name.Get(), &icon, &color, nullptr, nullptr};
        void* tab = Invoke(game::FindMethod("LE.dll", "", "ItemContainersManager", "AddNewStashTab", 8), manager, a);
        if (!tab) throw std::runtime_error("The game did not make the storage tab.");
        list[k] = TabId(tab);
        made = true;
    }
    if (made) { Save(); game::Guarded([] { stashrules::RefreshTabs(); }, nullptr); }
    return made;
}
// The quad tab's list rebuilt and split now: the save is safe without waiting for the next item move.
void SaveNow(void* container, void* quadTab) {
    const auto containers = Entries(Invoke(Method(container, "get_Containers", 0), container));
    RememberCompanions(Tabs(container), containers);
    void* qc = ContainerOf(containers, quadTab);
    if (!qc) return;
    SetDirty(qc);
    void* tracker = Get<void*>(Manager(), F("ItemContainersManager", "globalDataTracker"));
    void* a[]{container};
    Invoke(game::FindMethod("LE.dll", "", "GlobalDataTracker", "SaveStash", 1), tracker, a);
}
bool needCompanions = false;  // after a load: quad tabs from before companions get theirs
using LoadFn = void (*)(void* self, const il2cpp::Method* method);
LoadFn originalLoad = nullptr;
void LoadItems(void* self, const il2cpp::Method* method) {
    std::vector<std::pair<void*, std::unique_ptr<Root>>> kept;
    if (!lifecycle::Stopping() && game::IsOfflinePlay()) {
        std::string why;
        if (!game::Guarded([&] {
                const auto n = Apply(self);
                if (n) Log("quad stash: %zu tabs made larger before their items load", n);
                kept = MergeForLoad(self);
                const auto containers = Entries(Invoke(Method(self, "get_Containers", 0), self));
                RememberCompanions(Tabs(self), containers);
            }, &why)) { ++faults; lastError = why; Log("quad stash: %s", why.c_str()); }
    }
    originalLoad(self, method);
    needCompanions = true;
    // the data as it is on disk again; the containers keep what they loaded
    for (auto& [tab, list] : kept) game::Guarded([&] { SetSaved(tab, list->Get()); }, nullptr);
}
using RedrawFn = void (*)(void* self, const il2cpp::Method* method);
RedrawFn originalRedraw = nullptr;
void ReDraw(void* self, const il2cpp::Method* method) {
    if (self && stashUiClass && il2cpp::api().object_get_class(self) == stashUiClass && !lifecycle::Stopping())
        game::Guarded([&] { if (FixCells(self)) ++redraws; SetGrid(self, TabShownIsQuad()); }, nullptr);
    originalRedraw(self, method);
}
std::string State() {
    std::ostringstream out;
    out << "{\"ok\":true,\"available\":" << Boolean(ready) << ",\"resized\":" << resized << ",\"redraws\":" << redraws << ",\"splits\":" << splits << ",\"merges\":" << merges
        << ",\"relocated\":" << relocated << ",\"faults\":" << faults << ",\"error\":" << Json(lastError) << ",\"tabs\":[";
    void* container = game::IsOfflinePlay() ? StashContainer() : nullptr;
    if (container) {
        Load();
        const auto tabs = Tabs(container);
        const auto containers = Entries(Invoke(Method(container, "get_Containers", 0), container));
        const int shown = Get<int>(container, F("TabbedItemContainer", "currentlyActiveTab"));
        for (size_t i = 0; i < tabs.size(); ++i) {
            void* c = ContainerOf(containers, tabs[i]);
            const V2 size = c ? SizeOf(c) : V2{0, 0};
            const std::string id = TabId(tabs[i]);
            std::string storageOf;
            for (const auto& [q, list] : companionsLoaded) for (const auto& other : list) if (other == id && quad.count(q)) storageOf = q;
            void* saved = Saved(tabs[i]);
            out << (i ? "," : "") << "{\"index\":" << i << ",\"number\":" << TabNumber(tabs[i]) << ",\"name\":" << Json(TabName(tabs[i])) << ",\"quad\":" << Boolean(quad.count(id) > 0)
                << ",\"storage\":" << Boolean(!storageOf.empty()) << ",\"width\":" << size.x << ",\"height\":" << size.y << ",\"items\":" << (c ? Value<int>(Method(c, "ContentCount", 0), c) : 0)
                << ",\"saved\":" << (saved ? Entries(saved).size() : 0) << ",\"fitsNormal\":" << Boolean(c && FitsNormal(c)) << ",\"shown\":" << Boolean(TabNumber(tabs[i]) == shown) << '}';
        }
    }
    out << "]}";
    return out.str();
}
}  // namespace

bool Init() {
    try {
        stashUiClass = game::FindClass("LE.dll", "", "StashItemContainerUI");
        const auto load = game::FindMethod("LE.dll", "", "StashItemContainer", "LoadItems", 0);
        const auto redraw = game::FindMethod("LE.dll", "", "ItemContainerUI", "ReDrawWholeContainer", 0);
        std::string why;
        if (!load || !hook::Install(load.code, reinterpret_cast<void*>(&LoadItems), reinterpret_cast<void**>(&originalLoad), &why)) throw std::runtime_error("load hook: " + why);
        if (!redraw || !hook::Install(redraw.code, reinterpret_cast<void*>(&ReDraw), reinterpret_cast<void**>(&originalRedraw), &why)) throw std::runtime_error("redraw hook: " + why);
        const auto save = game::FindMethod("LE.dll", "", "GlobalDataTracker", "SaveStash", 1);
        if (!save || !hook::Install(save.code, reinterpret_cast<void*>(&SaveStash), reinterpret_cast<void**>(&originalSave), &why)) throw std::runtime_error("save hook: " + why);
        const auto fit = game::FindMethod("LE.dll", "", "ItemContainer", "DoBoundsFitContainer", 2);
        if (!fit || !hook::Install(fit.code, reinterpret_cast<void*>(&Fits), reinterpret_cast<void**>(&originalFit), &why)) throw std::runtime_error("fit hook: " + why);
        const auto confirm = game::FindMethod("LE.dll", "", "ConfigureTabUI", "ConfirmConfigure", 0);
        if (!confirm || !hook::Install(confirm.code, reinterpret_cast<void*>(&Confirm), reinterpret_cast<void**>(&originalConfirm), &why)) Log("quad stash: no Configure Tab check: %s", why.c_str());
        ready = true;
    } catch (const std::exception& e) { ready = false; lastError = e.what(); }
    Log("quad stash: %s%s%s", ready ? "ready" : "unavailable", lastError.empty() ? "" : ": ", lastError.c_str());
    return ready;
}
void Frame() {
    if (!ready || lifecycle::Stopping()) return;
    if (needCompanions && game::IsOfflinePlay()) {
        std::string why;
        if (!game::Guarded([] {
                void* container = StashContainer();
                if (!container) return;
                needCompanions = false;
                Load();
                for (void* tab : Tabs(container))
                    if (tab && quad.count(TabId(tab)) && EnsureCompanions(container, tab)) { SaveNow(container, tab); Log("quad stash: storage tabs made for %s", TabName(tab).c_str()); }
                RememberCompanions(Tabs(container), Entries(Invoke(Method(container, "get_Containers", 0), container)));
            }, &why)) { ++faults; lastError = why; Log("quad stash: %s", why.c_str()); }
    }
    static uint64_t next = 0;
    const uint64_t now = GetTickCount64();
    if (now < next) return;
    next = now + 300;
    std::string why;
    if (!game::Guarded([] {
            if (!game::IsOfflinePlay()) return;
            void* ui = game::StaticObject(game::FindStaticField("LE.dll", "", "UIBase", "instance"));
            if (!ui || !game::IsAlive(ui) || !Value<bool>(Method(ui, "IsStashOpen", 0), ui)) return;
            const bool want = TabShownIsQuad();
            for (void* w : StashWindows()) { if (FixCells(w)) { Invoke(Method(w, "ReDrawWholeContainer", 0), w); ++redraws; } SetGrid(w, want); }
            if (originalConfirm) SyncConfigure();
        }, &why)) { ++faults; lastError = why; next = now + 5000; }
}
std::string Read() { return Run([] { return State(); }); }
void ReleaseRoots() { companionContainers.clear(); companionRoots.clear(); }
std::string Set(int index, bool on) {
    if (!ready) return "{\"ok\":false,\"error\":" + Json("Quad stash tabs are unavailable in this game build: " + lastError) + "}";
    return Run([=]() -> std::string { ApplyTab(index, on); return State(); });
}
namespace {
void ApplyTab(int index, bool on) {
        if (!game::IsOfflinePlay()) throw std::runtime_error("Quad stash tabs work in offline play only.");
        void* container = StashContainer();
        if (!container) throw std::runtime_error("Open your stash first.");
        Load();
        const auto tabs = Tabs(container);
        const auto containers = Entries(Invoke(Method(container, "get_Containers", 0), container));
        const int shownNumber = Get<int>(container, F("TabbedItemContainer", "currentlyActiveTab"));
        int at = index;
        if (index < 0) for (size_t i = 0; i < tabs.size(); ++i) if (TabNumber(tabs[i]) == shownNumber) at = static_cast<int>(i);
        if (at < 0 || static_cast<size_t>(at) >= tabs.size()) throw std::runtime_error("No such stash tab.");
        void* c = ContainerOf(containers, tabs[static_cast<size_t>(at)]);
        if (!c) throw std::runtime_error("No such stash tab.");
        const std::string id = TabId(tabs[static_cast<size_t>(at)]);
        for (const auto& [q, list] : companionsLoaded) for (const auto& other : list)
            if (other == id && quad.count(q)) throw std::runtime_error("This tab keeps a quad tab's items for when EpochPact is removed; it cannot be changed.");
        if (id.empty() || id.find_first_of(" \t\r\n") != std::string::npos) throw std::runtime_error("This stash tab has no usable id.");
        if (!on && !FitsNormal(c)) throw std::runtime_error("Items lie outside the normal 12 x 17 cells of this tab. Move them into the top-left part, or to another tab, first.");
        const bool was = quad.count(id) > 0;
        if (on) quad.insert(id); else quad.erase(id);
        try { Save(); } catch (...) { if (was) quad.insert(id); else quad.erase(id); throw; }
        Resize(c, on ? kQuad : kNormal);
        if (on && EnsureCompanions(container, tabs[static_cast<size_t>(at)])) Log("quad stash: storage tabs made for %s", TabName(tabs[static_cast<size_t>(at)]).c_str());
        SaveNow(container, tabs[static_cast<size_t>(at)]);
        Redraw();
        Log("quad stash: tab %d (%s) %s", at, TabName(tabs[static_cast<size_t>(at)]).c_str(), on ? "quad" : "normal");
}
}  // namespace

#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
namespace {
void Describe(std::ostringstream& out, void* transform, int depth) {
    auto& api = il2cpp::api();
    void* go = Invoke(Method(transform, "get_gameObject", 0), transform);
    out << "{\"name\":" << Json(Text(Invoke(Method(go, "get_name", 0), go))) << ",\"active\":" << Boolean(Value<bool>(Method(go, "get_activeSelf", 0), go));
    {
        V2f size = Value<V2f>(Method(transform, "get_sizeDelta", 0), transform);
        out << ",\"size\":[" << size.x << "," << size.y << "]";
    }
    Root type(api.type_get_object(api.class_get_type(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Component"))));
    void* ta[]{type.Get()};
    const il2cpp::Method* components = nullptr; void* iterator = nullptr;
    while (const auto* m = api.class_get_methods(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject"), &iterator))
        if (std::string(api.method_get_name(m)) == "GetComponents" && api.method_get_param_count(m) == 1) {
            char* p = api.type_get_name(api.method_get_param(m, 0));
            const bool match = p && std::string(p) == "System.Type";
            if (p) api.free(p);
            if (match) { components = m; break; }
        }
    Root list(components ? Invoke(components, go, ta) : nullptr);
    out << ",\"components\":[";
    for (uintptr_t i = 0, n = list.Get() ? Get<uintptr_t>(list.Get(), 0x18) : 0; i < n && i < 16; ++i) {
        void* comp = Get<void*>(list.Get(), 0x20 + 8 * i);
        const char* name = comp ? api.class_get_name(api.object_get_class(comp)) : "?";
        out << (i ? "," : "") << "{\"type\":" << Json(name ? name : "?");
        if (comp && name && std::string(name) == "Image") {
            const int kind = Value<int>(Method(comp, "get_type", 0), comp);
            void* sprite = Invoke(Method(comp, "get_sprite", 0), comp);
            out << ",\"imageType\":" << kind << ",\"sprite\":" << Json(sprite ? Text(Invoke(Method(sprite, "get_name", 0), sprite)) : "")
                << ",\"perUnit\":" << Value<float>(Method(comp, "get_pixelsPerUnitMultiplier", 0), comp);
        }
        out << "}";
    }
    out << "]";
    const int children = Value<int>(Method(transform, "get_childCount", 0), transform);
    out << ",\"childCount\":" << children;
    if (depth > 0 && children > 0) {
        out << ",\"children\":[";
        for (int i = 0; i < children && i < 24; ++i) { int k = i; void* a[]{&k}; out << (i ? "," : ""); Describe(out, Invoke(Method(transform, "GetChild", 1), transform, a), depth - 1); }
        out << "]";
    }
    out << "}";
}
}  // namespace
std::string Look() {
    return Run([] {
        std::ostringstream out; out.setf(std::ios::fixed); out.precision(2);
        out << "{\"ok\":true,\"windows\":[";
        bool first = true;
        for (void* w : StashWindows()) {
            void* t = Invoke(Method(w, "get_transform", 0), w);
            void* parent = Invoke(Method(t, "get_parent", 0), t);
            const V2f slot = Get<V2f>(w, F("ItemContainerUI", "slotSize"));
            out << (first ? "" : ",") << "{\"slot\":[" << slot.x << "," << slot.y << "],\"self\":";
            Describe(out, t, 2);
            out << ",\"parent\":";
            if (parent) Describe(out, parent, 1); else out << "null";
            out << "}";
            first = false;
        }
        out << "]}";
        return out.str();
    });
}
// The stash window's Configure Tab button, pressed as a click would.
std::string ConfigureOpen() {
    return Run([]() -> std::string {
        for (void* w : StashWindows()) {
            void* t = Invoke(Method(w, "get_transform", 0), w);
            void* parent = Invoke(Method(t, "get_parent", 0), t);
            Root name(il2cpp::api().string_new("BottomButtons")); void* a[]{name.Get()};
            void* buttons = parent ? Invoke(Method(parent, "Find", 1), parent, a) : nullptr;
            if (!buttons) continue;
            const int n = Value<int>(Method(buttons, "get_childCount", 0), buttons);
            std::string names;
            for (int i = 0; i < n; ++i) {
                int k = i; void* ga[]{&k};
                void* child = Invoke(Method(buttons, "GetChild", 1), buttons, ga);
                void* go = Invoke(Method(child, "get_gameObject", 0), child);
                const std::string label = Text(Invoke(Method(go, "get_name", 0), go));
                names += label + ";";
                if (label.find("onfigure") == std::string::npos) continue;
                auto& api = il2cpp::api();
                Root type(api.type_get_object(api.class_get_type(game::FindClass("UnityEngine.UI.dll", "UnityEngine.UI", "Button"))));
                void* ta[]{type.Get()};
                void* button = Invoke(UnityOverload("Component", "GetComponent", {"System.Type"}), child, ta);
                if (!button) continue;
                void* click = Invoke(Method(button, "get_onClick", 0), button);
                Invoke(Method(click, "Invoke", 0), click);
                return "{\"ok\":true,\"pressed\":" + Json(label) + "}";
            }
            return "{\"ok\":false,\"error\":" + Json("no configure button among " + names) + "}";
        }
        return std::string("{\"ok\":false,\"error\":\"no stash window\"}");
    });
}
// The open Configure Tab window, its objects.
std::string ConfigureLook() {
    return Run([] {
        std::ostringstream out; out.setf(std::ios::fixed); out.precision(2);
        out << "{\"ok\":true,\"windows\":[";
        auto& api = il2cpp::api();
        bool first = true;
        for (const char* cls : {"ConfigureTabUI", "StashPriorityUI"}) {
            Root type(api.type_get_object(api.class_get_type(game::FindClass("LE.dll", "", cls))));
            bool inactive = false; void* args[]{type.Get(), &inactive};
            Root found(Invoke(UnityOverload("Object", "FindObjectsOfType", {"System.Type", "System.Boolean"}), nullptr, args));
            for (uintptr_t i = 0, n = found.Get() ? Get<uintptr_t>(found.Get(), 0x18) : 0; i < n && i < 4; ++i) {
                void* ui = Get<void*>(found.Get(), 0x20 + 8 * i);
                void* t = Invoke(Method(ui, "get_transform", 0), ui);
                out << (first ? "" : ",") << "{\"class\":" << Json(cls) << ",\"tree\":";
                Describe(out, t, std::string(cls) == "StashPriorityUI" ? 4 : 3);
                out << "}";
                first = false;
            }
        }
        out << "]}";
        return out.str();
    });
}
// Test builds: the Quad stash check set as a click would, and the window's Confirm or Cancel pressed.
std::string ConfigureCheck(int value) {
    return Run([=]() -> std::string {
        if (!configure.toggle || !configure.toggle->Get()) throw std::runtime_error("no Quad stash check yet");
        if (value == 0 || value == 1) { bool on = value == 1; void* a[]{&on}; Invoke(Method(configure.toggle->Get(), "set_isOn", 1), configure.toggle->Get(), a); return std::string("{\"ok\":true}"); }
        void* window = configure.window ? configure.window->Get() : nullptr;
        if (!window) throw std::runtime_error("no window");
        void* button = Get<void*>(window, F("ConfigureTabUI", value == 2 ? "yesButton" : "noButton"));
        void* click = Invoke(Method(button, "get_onClick", 0), button);
        Invoke(Method(click, "Invoke", 0), click);
        void* error = Get<void*>(window, F("ConfigureTabUI", "errorTMP"));
        void* go = Invoke(Method(window, "get_gameObject", 0), window);
        return "{\"ok\":true,\"open\":" + std::string(Boolean(Value<bool>(Method(go, "get_activeInHierarchy", 0), go))) + ",\"error\":" + Json(error ? Text(Invoke(Method(error, "get_text", 0), error)) : "") + "}";
    });
}
// Test builds: a screenshot written by the game itself, without the report's notice on top.
std::string Shot(const std::string& path) {
    return Run([=]() -> std::string {
        Root file(il2cpp::api().string_new(path.c_str())); int size = 1; void* a[]{file.Get(), &size};  // the build keeps only (string, int)
        Invoke(game::FindMethod("UnityEngine.ScreenCaptureModule.dll", "UnityEngine", "ScreenCapture", "CaptureScreenshot", 2), nullptr, a);
        return std::string("{\"ok\":true}");
    });
}
std::string Poison(bool on) {
    return Run([=]() -> std::string {
        void* manager = Manager();
        void* inventory = manager ? Get<void*>(manager, F("ItemContainersManager", "inventory")) : nullptr;
        if (!inventory) throw std::runtime_error("no inventory");
        if (on) companionContainers.insert(inventory); else companionContainers.erase(inventory);
        return std::string("{\"ok\":true,\"poisoned\":") + Boolean(companionContainers.count(inventory) != 0) + "}";
    });
}
std::string TestMove(int x, int y, int number) {
    return Run([=]() -> std::string {
        if (!game::IsOfflinePlay()) throw std::runtime_error("offline only");
        void* container = StashContainer();
        void* manager = Manager();
        if (!container || !manager) throw std::runtime_error("no stash");
        const auto containers = Entries(Invoke(Method(container, "get_Containers", 0), container));
        const int shown = number >= 0 ? number : Get<int>(container, F("TabbedItemContainer", "currentlyActiveTab"));
        if (shown < 0 || static_cast<size_t>(shown) >= containers.size()) throw std::runtime_error("no tab shown");
        void* tab = containers[static_cast<size_t>(shown)];
        void* inventory = Get<void*>(manager, F("ItemContainersManager", "inventory"));
        const auto entries = Entries(Invoke(Method(inventory, "GetContent", 0), inventory));
        if (entries.empty() || !entries.front()) throw std::runtime_error("the inventory is empty");
        void* entry = entries.front();
        void* data = Get<void*>(entry, F("ItemContainerEntry", "data"));
        Root pin(data);
        int quantity = Get<int>(entry, F("ItemContainerEntry", "quantity")), context = 0;
        void* ra[]{entry, &quantity, &context};
        if (!Value<bool>(Method(inventory, "TryRemoveItem", 3), inventory, ra)) throw std::runtime_error("the item did not leave the inventory");
        V2 at{x, y};
        const il2cpp::Method* add = nullptr; void* iterator = nullptr; auto& api = il2cpp::api();
        while (const auto* m = api.class_get_methods(game::FindClass("LE.dll", "", "ItemContainer"), &iterator))
            if (std::string(api.method_get_name(m)) == "TryAddItem" && api.method_get_param_count(m) == 4) { add = m; break; }
        void* aa[]{pin.Get(), &quantity, &at, &context};
        const bool added = Value<bool>(add, tab, aa);
        if (!added) { void* back[]{pin.Get(), &quantity, &context}; Invoke(Method(inventory, "TryAddItem", 3), inventory, back); }
        Redraw();
        const V2 size = Get<V2>(entry, F("ItemContainerEntry", "size"));
        return std::string("{\"ok\":true,\"added\":") + Boolean(added) + ",\"width\":" + std::to_string(size.x) + ",\"height\":" + std::to_string(size.y) + "}";
    });
}
#endif
}  // namespace ep::quadstash
