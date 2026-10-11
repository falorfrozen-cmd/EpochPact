#include "items.hpp"

#include "common.hpp"
#include "feature.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "smart_loot.hpp"
#include "managed.hpp"
#include "pickup_rules.hpp"
#include <memory>

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace ep::items {

namespace {

using il2cpp::Field;
using il2cpp::Method;

// ---- rarity: GenerateItems.RollRarity(int ilvl, float uniqueAndSetDropRateMultiplier) -> byte

feature::Feature g_rarity;

using RollRarityFn = uint8_t (*)(int32_t ilvl, float uniqueMultiplier, const Method* m);
RollRarityFn o_rollRarity = nullptr;

// xorshift64*, seeded per process; only ever called from game threads, and the atomic
// keeps two threads from reusing a state.
uint64_t Rand64() {
    static std::atomic<uint64_t> s{0x2545F4914F6CDD1Dull};
    uint64_t x = s.fetch_add(0x9E3779B97F4A7C15ull, std::memory_order_relaxed);
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

uint8_t d_rollRarity(int32_t ilvl, float uniqueMultiplier, const Method* m) {
    const uint8_t rolled = o_rollRarity(ilvl, uniqueMultiplier, m);
    double mult = 1.0;
    if (rolled < 4 && feature::Active(g_rarity, &mult)) {
        const double chance = (mult - 1.0) / mult;  // x2: one in two rolls, x10: nine in ten
        if ((Rand64() >> 11) * (1.0 / 9007199254740992.0) < chance) {
            const uint8_t out = static_cast<uint8_t>(rolled + 1);
            if (g_rarity.firstPending.exchange(false))
                Log("rarity: first boosted roll ilvl %d: rarity %u -> %u (x%g)", ilvl, rolled, out, mult);
            return out;
        }
    }
    return rolled;
}

feature::Hook h_rollRarity{"GenerateItems.RollRarity", {}, reinterpret_cast<void*>(&d_rollRarity),
                           reinterpret_cast<void**>(&o_rollRarity)};

// ---- autopickup: the game's own pickup entry points, called from its per-frame distant
// pickup handler (the same code that vacuums labels for a held controller button).

std::atomic<double> g_auto{0.0};
std::atomic<double> g_radius{0.0};
std::atomic<uint64_t> g_scans{0}, g_calls{0};
std::atomic<bool> g_autoFirst{false};
std::atomic<unsigned long> g_lastScan{0};

// ItemTooltipOrganizer keeps every live pickable label in a static DList; ground item
// labels add themselves to it. DList<T> wraps a List<T> in its first instance field at
// +0x10; List<T> keeps its backing array at +0x10 and its count at +0x18 (the layout of
// every reference-typed List<T> in IL2CPP). Item arrays keep the length at +0x18 and
// their first element at +0x20.
constexpr size_t kListItems = 0x10, kListSize = 0x18;
constexpr size_t kArrayLength = 0x18, kArrayFirst = 0x20;
constexpr int kMaxPerScan = 256;

const Field* g_labelListField = nullptr;         // ItemTooltipOrganizer.pickableGroundLabelList
const il2cpp::Class* g_groundItemLabel = nullptr;
game::MethodRef m_requestPickup;                 // GroundItemLabel.requestPickup()
const Field* g_managerField = nullptr;           // GroundItemManager.instance
const Field* g_hoveredItemField = nullptr;       // TooltipItem.highlightedTooltipItem
const Field* g_uiField = nullptr;                // UIBase.instance
size_t g_tooltipSystemOffset = 0;                // UIBase._tooltipSystem
size_t g_hoverRectOffset = 0;
size_t g_tooltipCardOffset = 0;
size_t g_tooltipManagerOffset = 0, g_legacyActiveOffset = 0, g_tooltipLockOffset = 0;
size_t g_labelFollowerOffset = 0;
game::MethodRef m_getActor, m_componentTransform, m_transformPosition, m_followerPosition;
game::MethodRef m_activeTooltipItem;             // TooltipSystem includes V2/container/controller tooltips
game::MethodRef m_componentObject,m_activeHierarchy;

bool InspectingItem() {
    // A failed/missing hover read must pause collection, not dismiss a tooltip.
    if (!g_hoveredItemField || !g_uiField || !g_tooltipSystemOffset || !m_activeTooltipItem || !g_tooltipCardOffset || !g_tooltipManagerOffset || !g_legacyActiveOffset || !g_tooltipLockOffset || !m_componentObject || !m_activeHierarchy) return true;
    void* hovered = nullptr;
    if (!game::TryStaticObject(g_hoveredItemField, &hovered)) return true;
    bool inspecting = false;
    if (!game::TryIsAlive(hovered, &inspecting)) return true;
    if (inspecting) return true;
    // Container-hover and the V2 tooltip system do not set highlightedTooltipItem.
    // Use the game's unified getter: it handles both V2 and the legacy manager.
    // This is a cached native getter, with no per-frame reflection or boxed result.
    void* ui = nullptr;
    if (!game::TryStaticObject(g_uiField, &ui)) return true;
    bool alive = false;
    if (!game::TryIsAlive(ui, &alive) || !alive) return true;
    void* active = nullptr;
    const bool readable = game::Guarded([&] {
        void* system = *reinterpret_cast<void* const*>(static_cast<char*>(ui) + g_tooltipSystemOffset);
        bool systemAlive = false;
        if (!game::TryIsAlive(system, &systemAlive) || !systemAlive)
            throw std::runtime_error("item tooltip system unavailable");
        if (*reinterpret_cast<const bool*>(static_cast<char*>(system)+g_tooltipLockOffset)) { active=system; return; }
        // This build can render through the legacy manager while the V2 card is
        // inactive. Its active flag, unlike ActiveItemTooltipItem, is cleared by
        // CloseAllTooltips and covers comparison/controller cards as well.
        void* manager=*reinterpret_cast<void* const*>(static_cast<char*>(system)+g_tooltipManagerOffset);
        bool managerAlive=false;
        if(!game::TryIsAlive(manager,&managerAlive))throw std::runtime_error("tooltip manager unreadable");
        if(managerAlive && *reinterpret_cast<const bool*>(static_cast<char*>(manager)+g_legacyActiveOffset)){active=manager;return;}
        // Pending tooltips have a hovered rect before their asynchronous item
        // card becomes ready. Do not pick that item up during this gap.
        if (g_hoverRectOffset) {
            void* rect = *reinterpret_cast<void* const*>(static_cast<char*>(system) + g_hoverRectOffset);
            if (!game::TryIsAlive(rect, &inspecting)) throw std::runtime_error("tooltip hover unreadable");
            if (inspecting) { active = rect; return; }
        }
        active = reinterpret_cast<void* (*)(void*, const Method*)>(m_activeTooltipItem.code)(system, m_activeTooltipItem.info);
        if(active){
            // The getter retains the last item after CloseItemTooltip. A cached
            // item is not an open card: otherwise collection stays paused forever.
            void* card=*reinterpret_cast<void* const*>(static_cast<char*>(system)+g_tooltipCardOffset);
            bool cardAlive=false;if(!game::TryIsAlive(card,&cardAlive))throw std::runtime_error("tooltip card unreadable");
            if(!cardAlive){active=nullptr;return;}
            void* object=reinterpret_cast<void* (*)(void*,const Method*)>(m_componentObject.code)(card,m_componentObject.info);
            if(!reinterpret_cast<bool (*)(void*,const Method*)>(m_activeHierarchy.code)(object,m_activeHierarchy.info))active=nullptr;
        }
    }, nullptr);
    return !readable || active != nullptr;
}

struct Position { float x,y,z; };
struct GroundOffsets {size_t list=0,location=0,id=0;};
GroundOffsets g_ground[6];
size_t g_offlineItems=0,g_labelVisuals=0,g_visualId=0,g_pickupId=0;
bool g_vacuumReady=false;
void Pickup(void* object,game::MethodRef method,int kind){
    if(!g_vacuumReady){managed::Invoke(method,object);return;}
    void* manager=game::StaticObject(g_managerField),*actor=managed::Invoke(m_getActor);
    if(!manager||!actor||!game::IsAlive(actor))return;
    const auto id=kind==0?managed::Get<uint32_t>(managed::Get<void*>(object,g_labelVisuals),g_visualId):managed::Get<uint32_t>(object,g_pickupId);
    void* list=managed::Get<void*>(managed::Get<void*>(manager,g_offlineItems),g_ground[kind].list);
    void* array=managed::Get<void*>(list,kListItems);int n=managed::Get<int>(list,kListSize);
    if(n<0||n>8192||!array||static_cast<uintptr_t>(n)>managed::Get<uintptr_t>(array,kArrayLength))return;
    for(int i=0;i<n;++i){void* record=managed::Get<void*>(array,kArrayFirst+8*i);
        if(!record||managed::Get<uint32_t>(record,g_ground[kind].id)!=id)continue;
        managed::Root pin(record),transform(managed::Invoke(m_componentTransform,actor));
        Position player=managed::Value<Position>(m_transformPosition,transform.Get());
        auto& location=*reinterpret_cast<Position*>(static_cast<char*>(record)+g_ground[kind].location);
        rules::BorrowLocation borrow(location,player);
        managed::Invoke(method,object);return;
    }
}
bool InRadius(void* object, const Position& player, bool label) {
    if(g_radius.load()==0) return true; // Explicit "all loaded drops" option.
    using namespace managed;
    Position position{};
    if(label) {
        void* follower=Get<void*>(object,g_labelFollowerOffset);
        if(!follower || !game::IsAlive(follower)) return false;
        position=Value<Position>(m_followerPosition,follower);
    } else {
        Root transform(Invoke(m_componentTransform,object));
        position=Value<Position>(m_transformPosition,transform.Get());
    }
    const double x=position.x-player.x, y=position.y-player.y, z=position.z-player.z;
    const double radius=g_radius.load();
    return x*x+y*y+z*z<=radius*radius;
}

struct ActiveList {
    const char* what;
    size_t offset;          // inside GroundItemManager
    game::MethodRef pick;   // GoldPickupInteraction.PickUp() and friends
};
ActiveList g_activeLists[5];

// A snapshot of one managed List<T>: element pointers are copied out first, because the
// pickup calls remove elements from the list while it is being walked.
template <typename F>
int SnapshotList(void* listObj, void** out, int cap, F&& keep) {
    if (!listObj) return 0;
    void* arr = *reinterpret_cast<void* const*>(static_cast<const char*>(listObj) + kListItems);
    int32_t size = *reinterpret_cast<int32_t*>(static_cast<char*>(listObj) + kListSize);
    if (!arr || size <= 0) return 0;
    const int32_t length = *reinterpret_cast<int32_t*>(static_cast<char*>(arr) + kArrayLength);
    if (size > length) size = length;  // never trust the count alone
    if (size > cap) size = cap;
    int n = 0;
    for (int32_t i = 0; i < size; ++i) {
        if (void* elem = *reinterpret_cast<void* const*>(static_cast<char*>(arr) + kArrayFirst + 8 * i))
            if (keep(elem)) out[n++] = elem;
    }
    return n;
}

void PickAll() {
    void* labels[kMaxPerScan];
    void* gold[kMaxPerScan];
    void* potions[kMaxPerScan];
    void* xp[kMaxPerScan];
    void* favor[kMaxPerScan];
    void* bones[kMaxPerScan];
    int labelCount = 0, goldCount = 0, potionCount = 0, xpCount = 0, favorCount = 0, boneCount = 0;
    int labelSeen = 0;

    game::Guarded(
        [&] {
            Position player{};
            if(g_radius.load()!=0) {
                void* actor=managed::Invoke(m_getActor);
                if(!actor || !game::IsAlive(actor)) return;
                managed::Root transform(managed::Invoke(m_componentTransform,actor));
                player=managed::Value<Position>(m_transformPosition,transform.Get());
            }
            // Ground item labels (equipment), through the static list the UI itself keeps.
            if (g_labelListField && g_groundItemLabel) {
                if (void* dlist = game::StaticObject(g_labelListField)) {
                    void* inner = *reinterpret_cast<void* const*>(static_cast<char*>(dlist) + kListItems);
                    labelCount = SnapshotList(inner, labels, kMaxPerScan, [&](void* label) {
                        ++labelSeen;
                        return *reinterpret_cast<void* const*>(label) == g_groundItemLabel && InRadius(label,player,true) && smartloot::Accept(label);
                    });
                }
            }
            // Gold, potions, tomes and bones, through the manager's own active lists.
            if (g_managerField) {
                if (void* mgr = game::StaticObject(g_managerField)) {
                    void** out[5] = {gold, potions, xp, favor, bones};
                    int* count[5] = {&goldCount, &potionCount, &xpCount, &favorCount, &boneCount};
                    for (int i = 0; i < 5; ++i) {
                        if (!smartloot::Category(i) || !g_activeLists[i].pick || !g_activeLists[i].offset) continue;
                        void* listObj = *reinterpret_cast<void* const*>(static_cast<char*>(mgr) + g_activeLists[i].offset);
                        *count[i] = SnapshotList(listObj, out[i], kMaxPerScan, [&](void* object) { return InRadius(object,player,false); });
                    }
                }
            }
        },
        nullptr);

    // Pickup mutates the manager lists. Root every snapshot before the first call
    // and use runtime_invoke to catch managed pickup/inventory exceptions.
    std::vector<std::unique_ptr<managed::Root>> roots;
    if (!game::Guarded([&] {
        const std::pair<void**,int> snapshots[]{{labels,labelCount},{gold,goldCount},{potions,potionCount},
            {xp,xpCount},{favor,favorCount},{bones,boneCount}};
        for (const auto& [list,count] : snapshots) for (int i=0;i<count;++i) roots.push_back(std::make_unique<managed::Root>(list[i]));
    }, nullptr)) return;
    int calls = 0;
    if (m_requestPickup) {
        for (int i = 0; i < labelCount; ++i) {
            if (calls >= 8 || InspectingItem()) break;
            if (game::Guarded([&] { if(game::IsAlive(labels[i])) { Pickup(labels[i],m_requestPickup,0); ++calls; } },nullptr)) {}
        }
    }
    const struct {
        void** list;
        int count;
        const game::MethodRef* pick;
    } lists[5] = {{gold, goldCount, &g_activeLists[0].pick},
                  {potions, potionCount, &g_activeLists[1].pick},
                  {xp, xpCount, &g_activeLists[2].pick},
                  {favor, favorCount, &g_activeLists[3].pick},
                  {bones, boneCount, &g_activeLists[4].pick}};
    int kind=0;
    for (const auto& l : lists) {
        ++kind;
        if (!l.pick->code) continue;
        for (int i = 0; i < l.count; ++i) {
            if (calls >= 8 || InspectingItem()) break;
            game::Guarded([&] { if(game::IsAlive(l.list[i])) { Pickup(l.list[i],*l.pick,kind); ++calls; } },nullptr);
        }
    }

    if (calls) g_calls.fetch_add(static_cast<uint64_t>(calls));
    if (g_autoFirst.exchange(false))
        Log("autopickup: first scan: item labels %d picked (%d seen), gold %d, potions %d, xp tomes %d, favor %d, bones %d; %d calls",
            labelCount, labelSeen, goldCount, potionCount, xpCount, favorCount, boneCount, calls);
}

using TickFn = void (*)(void* self, float deltaTime, const Method* m);
TickFn o_distantTick = nullptr;

void d_distantTick(void* self, float deltaTime, const Method* m) {
    const bool active = g_auto.load(std::memory_order_relaxed) != 0.0 && game::IsOfflinePlay();
    const bool inspectingBeforeTick = active && InspectingItem();
    // This native handler does only queued distant item pickups. Its own
    // requestPickup calls also close tooltips, even when OUR scan is paused.
    // Leave queued labels untouched until inspection ends. ClickedItem and the
    // normal manual pickup paths are separate and remain available.
    if(inspectingBeforeTick) return;
    o_distantTick(self, deltaTime, m);  // the game's own distant pickup stays in charge
    if (!active) return;
    if (inspectingBeforeTick || InspectingItem()) return;  // Leave both the tooltip and scan timer alone.
    if (!smartloot::CanCollect()) return;
    const unsigned long now = GetTickCount();
    unsigned long last = g_lastScan.load(std::memory_order_relaxed);
    if (now - last < 250) return;
    if (!g_lastScan.compare_exchange_strong(last, now, std::memory_order_relaxed)) return;
    g_scans.fetch_add(1, std::memory_order_relaxed);
    PickAll();
}

feature::Hook h_distantTick{"DistantItemPickupHandler.OnUpdateTick", {}, reinterpret_cast<void*>(&d_distantTick),
                            reinterpret_cast<void**>(&o_distantTick)};

std::string AutoStatus() {
    const bool in = h_distantTick.ref && hook::IsInstalled(h_distantTick.ref.code);
    char buf[200];
    std::snprintf(buf, sizeof buf, "autopickup: %s, hook %s, scans %llu, pickup calls %llu", g_auto.load() > 0.5 ? "on" : "off",
                  in ? "in" : "out", static_cast<unsigned long long>(g_scans.load()),
                  static_cast<unsigned long long>(g_calls.load()));
    return buf;
}

}  // namespace

bool Init() {
    g_rarity.cmd = "rarity";
    g_rarity.max = 10;
    g_rarity.hooks = {&h_rollRarity};
    h_rollRarity.ref = game::FindMethod("LE.dll", "", "GenerateItems", "RollRarity", 2);

    h_distantTick.ref = game::FindMethod("LE.dll", "", "DistantItemPickupHandler", "OnUpdateTick", 1);
    g_labelListField = game::FindStaticField("LE.dll", "", "ItemTooltipOrganizer", "pickableGroundLabelList");
    g_managerField = game::FindStaticField("LE.dll", "", "GroundItemManager", "instance");
    g_hoveredItemField = game::FindStaticField("LE.dll", "", "TooltipItem", "highlightedTooltipItem");
    g_uiField = game::FindStaticField("LE.dll", "", "UIBase", "instance");
    g_tooltipSystemOffset = game::FieldOffset("LE.dll", "", "UIBase", "_tooltipSystem");
    g_hoverRectOffset=game::FieldOffset("LE.dll","LE.UI.Tooltips","TooltipSystem","_activeHoveredItemRect");
    g_tooltipCardOffset=game::FieldOffset("LE.dll","LE.UI.Tooltips","TooltipSystem","_itemTooltip");
    g_tooltipManagerOffset=game::FieldOffset("LE.dll","LE.UI.Tooltips","TooltipSystem","_tooltipItemManager");
    g_tooltipLockOffset=game::FieldOffset("LE.dll","LE.UI.Tooltips","TooltipSystem","<TooltipLockActive>k__BackingField");
    g_legacyActiveOffset=game::FieldOffset("LE.dll","","TooltipItemManager","active");
    m_componentObject=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Component","get_gameObject",0);
    m_activeHierarchy=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","GameObject","get_activeInHierarchy",0);
    g_labelFollowerOffset=game::FieldOffset("LE.dll","","PickupableGroundLabel","sceneFollower");
    m_followerPosition=game::FindMethod("LE.dll","","GUISceneFollower","get_FollowTargetPosition",0);
    m_getActor=game::FindMethod("LE.dll","","PlayerFinder","getPlayerActor",0);
    m_componentTransform=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Component","get_transform",0);
    m_transformPosition=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Transform","get_position",0);
    m_activeTooltipItem = game::FindMethod("LE.dll", "LE.UI.Tooltips", "TooltipSystem", "get_ActiveItemTooltipItem", 0);
    g_groundItemLabel = game::FindClass("LE.dll", "", "GroundItemLabel");
    m_requestPickup = game::FindMethod("LE.dll", "", "GroundItemLabel", "requestPickup", 0);
    g_offlineItems=game::FieldOffset("LE.dll","","GroundItemManager","offlineItems");
    g_labelVisuals=game::FieldOffset("LE.dll","","GroundItemLabel","visuals");
    g_visualId=game::FieldOffset("LE.dll","","GroundItemVisuals","id");
    g_pickupId=game::FieldOffset("LE.dll","","PickupGroundObjectInteraction","id");
    const char* recordTypes[]{"GroundItem","GroundGold","GroundPotion","GroundXPTome","GroundFavorTome","GroundAncientBone"};
    const char* recordLists[]{"items","gold","potions","xpTomes","favorTomes","ancientBones"};
    g_vacuumReady=g_offlineItems&&g_labelVisuals&&g_visualId&&g_pickupId&&m_getActor&&m_componentTransform&&m_transformPosition;
    for(int i=0;i<6;++i){std::string type=std::string("GroundItemManager.")+recordTypes[i];
        g_ground[i]={game::FieldOffset("LE.dll","","GroundItemManager.GroundItemList",recordLists[i]),game::FieldOffset("LE.dll","",type.c_str(),"location"),game::FieldOffset("LE.dll","",type.c_str(),"id")};
        g_vacuumReady=g_vacuumReady&&g_ground[i].list&&g_ground[i].location&&g_ground[i].id;
    }

    auto list = [](const char* field, const char* image, const char* cls, const char* pick) {
        ActiveList l{field, game::FieldOffset("LE.dll", "", "GroundItemManager", field),
                     game::FindMethod(image, "", cls, pick, 0)};
        return l;
    };
    g_activeLists[0] = list("activeGoldPiles", "LE.dll", "GoldPickupInteraction", "PickUp");
    g_activeLists[1] = list("activePotions", "LE.dll", "PotionPickupInteraction", "PickUp");
    g_activeLists[2] = list("activeXPTomes", "LE.dll", "PickupExperiencePotionInteraction", "PickUp");
    g_activeLists[3] = list("activeFavorTomes", "LE.dll", "PickupFavorTomeInteraction", "PickUp");
    g_activeLists[4] = list("activeAncientBones", "LE.dll", "PickupAncientBonesInteraction", "PickUp");

    Log("items: RollRarity %s; autopickup: tick %s, label list %s, manager %s, GroundItemLabel %s, requestPickup %s, hover guard %s; active lists "
        "gold %d potion %d xp %d favor %d bone %d",
        h_rollRarity.ref ? "found" : "MISSING", h_distantTick.ref ? "found" : "MISSING", g_labelListField ? "found" : "MISSING",
        g_managerField ? "found" : "MISSING", g_groundItemLabel ? "found" : "MISSING", m_requestPickup ? "found" : "MISSING",
        g_hoveredItemField && g_uiField && g_tooltipSystemOffset && m_activeTooltipItem ? "found (legacy + V2)" : "MISSING",
        g_activeLists[0].pick ? 1 : 0, g_activeLists[1].pick ? 1 : 0, g_activeLists[2].pick ? 1 : 0, g_activeLists[3].pick ? 1 : 0,
        g_activeLists[4].pick ? 1 : 0);
    return static_cast<bool>(h_rollRarity.ref);
}

std::string SetRarity(double m) { return feature::Set(g_rarity, m); }

std::string RarityStatus() { return feature::Line(g_rarity); }

std::string AutoPickupStatus() { return AutoStatus(); }

std::string SetAuto(double on) {
    if (!h_distantTick.ref) return "autopickup: refused: DistantItemPickupHandler.OnUpdateTick was not found in this game build";
    if (on != 0.0 && on != 1.0) return "autopickup: refused: use 0 (off) or 1 (on)";
    if (on == 0.0) {
        g_auto = 0.0;
        std::string why;
        if (hook::IsInstalled(h_distantTick.ref.code) && !hook::Remove(h_distantTick.ref.code, &why))
            return "autopickup: off, but the hook stays: " + why;
        return "autopickup -> off (hook removed)";
    }
    if (!smartloot::CanCollect()) return "autopickup: refused: enter a loaded offline character first";
    if (!g_hoveredItemField || !g_uiField || !g_tooltipSystemOffset || !m_activeTooltipItem || !g_tooltipCardOffset || !g_tooltipManagerOffset || !g_legacyActiveOffset || !g_tooltipLockOffset || !m_componentObject || !m_activeHierarchy)
        return "autopickup: refused: item tooltip hover state was not found in this game build";
    if (!hook::IsInstalled(h_distantTick.ref.code)) {
        std::string why;
        if (!hook::Install(h_distantTick.ref.code, h_distantTick.detour, h_distantTick.original, &why)) {
            g_auto = 0.0;
            return "autopickup: refused: the hook did not go in: " + why;
        }
    }
    g_auto = 1.0;
    g_autoFirst = true;
    g_lastScan = 0;
    return "autopickup -> on (items, gold, potions, tomes and bones; pauses while inspecting an item)";
}

std::string Status() { return feature::Line(g_rarity) + "\n" + AutoStatus(); }

bool IsInspectingItem(){return InspectingItem();}
std::string RadiusStatus(){return "{\"ok\":true,\"radius\":"+std::to_string(g_radius.load())+",\"allLoaded\":"+(g_radius.load()==0?"true":"false")+"}";}
std::string SetRadius(double radius){
    if(!std::isfinite(radius)||radius<0||radius>60)return "{\"ok\":false,\"error\":\"Pickup radius must be between 0 and 60.\"}";
    if(radius!=0 && (!g_labelFollowerOffset||!m_followerPosition||!m_getActor||!m_componentTransform||!m_transformPosition))return "{\"ok\":false,\"error\":\"Pickup range metadata is unavailable.\"}";
    g_radius=radius;return RadiusStatus();
}

}  // namespace ep::items
