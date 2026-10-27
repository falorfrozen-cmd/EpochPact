#include "items.hpp"

#include "common.hpp"
#include "feature.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "loot.hpp"
#include "smart_loot.hpp"
#include "managed.hpp"
#include "pickup_rules.hpp"
#include "loot_crafting_rules.hpp"
#include "mainthread.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>
#include <sstream>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

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

// ---- Weaver's Will: ItemData.RollWeaversWill(UniqueList.Entry, int minimum, int ilvl, float corruption) -> int.
// Its only caller, ItemData.RollLegendaryProperties, stores the result in ItemData.weaversWill
// for a Unique whose entry has the Weaver's Will legendary type; the game clamps it to 28.

feature::Feature g_weaversWill;

using RollWeaversWillFn = int32_t (*)(void* item, void* entry, int32_t minimum, int32_t ilvl, float corruption, const Method* m);
RollWeaversWillFn o_rollWeaversWill = nullptr;

int32_t d_rollWeaversWill(void* item, void* entry, int32_t minimum, int32_t ilvl, float corruption, const Method* m) {
    const int32_t rolled = o_rollWeaversWill(item, entry, minimum, ilvl, corruption, m);
    double mult = 1.0;
    if (rolled > 0 && feature::Active(g_weaversWill, &mult)) {
        const int32_t out = lootcraft::rules::ScaleWeaversWill(rolled, mult, smartloot::MaximumWeaversWill());
        if (g_weaversWill.firstPending.exchange(false))
            Log("weaverswill: first boosted roll ilvl %d: %d -> %d (x%g)", ilvl, rolled, out, mult);
        return out;
    }
    return rolled;
}

feature::Hook h_rollWeaversWill{"ItemData.RollWeaversWill", {}, reinterpret_cast<void*>(&d_rollWeaversWill),
                                reinterpret_cast<void**>(&o_rollWeaversWill)};

// ---- Legendary Potential: ItemData.RollLegendaryPotential(UniqueList.Entry, int minimum, int ilvl, float corruption,
// float cofMultiplier, ref bool improvedByCoF, float nonCoFMultiplier) -> int. With "lprolls n" the game's own roll
// is made up to n times and the highest kept, so it never passes the game's odds or its maximum (ItemData.
// MaxLegendaryPotential); improvedByCoF is the kept roll's.

feature::Feature g_lpRolls;
int g_maximumLP = 4;  // ItemData.MaxLegendaryPotential, read at Init

using RollLegendaryPotentialFn = int32_t (*)(void* item, void* entry, int32_t minimum, int32_t ilvl, float corruption,
                                             float cof, bool* improvedByCoF, float nonCof, const Method* m);
RollLegendaryPotentialFn o_rollLegendaryPotential = nullptr;

int32_t d_rollLegendaryPotential(void* item, void* entry, int32_t minimum, int32_t ilvl, float corruption, float cof,
                                 bool* improvedByCoF, float nonCof, const Method* m) {
    int32_t best = o_rollLegendaryPotential(item, entry, minimum, ilvl, corruption, cof, improvedByCoF, nonCof, m);
    double rolls = 1.0;
    if (!feature::Active(g_lpRolls, &rolls)) return best;
    const int32_t first = best;
    bool bestImproved = improvedByCoF && *improvedByCoF;
    const int extra = lootcraft::rules::ExtraLegendaryRolls(rolls);
    for (int i = 0; i < extra && best < g_maximumLP; ++i) {
        bool improved = false;
        const int32_t again = o_rollLegendaryPotential(item, entry, minimum, ilvl, corruption, cof, &improved, nonCof, m);
        if (again > best) { best = again; bestImproved = improved; }
    }
    if (improvedByCoF) *improvedByCoF = bestImproved;
    if (g_lpRolls.firstPending.exchange(false))
        Log("lprolls: first boosted roll ilvl %d corruption %g: %d -> %d (up to %g rolls)", ilvl, corruption, first, best, rolls);
    return best;
}

feature::Hook h_rollLegendaryPotential{"ItemData.RollLegendaryPotential", {}, reinterpret_cast<void*>(&d_rollLegendaryPotential),
                                       reinterpret_cast<void**>(&o_rollLegendaryPotential)};

// ---- autopickup: the game's own pickup entry points, called from its per-frame distant
// pickup handler (the same code that vacuums labels for a held controller button).

std::atomic<double> g_auto{0.0};
std::atomic<double> g_radius{0.0};
std::atomic<uint64_t> g_scans{0}, g_calls{0};
std::atomic<bool> g_autoFirst{false};
std::atomic<unsigned long> g_lastScan{0};
// Player-chosen scan interval: 0.25 s (the original) up to 10 s.
std::atomic<unsigned> g_intervalMs{250};
// Set when the game shows its own "inventory full" notification. Equipment
// labels are skipped until the player closes the inventory panel; gold,
// potions, tomes, bones and materials keep being collected.
std::atomic<bool> g_inventoryFull{false};
bool g_inventoryWasOpen = false;
std::atomic<uint64_t> g_fullEvents{0}, g_fullSkipped{0};
// Why collection paused or did nothing: the check that stopped the last paused tick (0 none, 1 hover
// metadata missing, 2 item just highlighted, 3 UI gone, 4 tooltip lock, 5 item tooltip open, 6 pending
// hover, 7 item card open, 8 tooltip unreadable), tick counters and the last scan's numbers.
std::atomic<int> g_inspectReason{0};
std::atomic<uint64_t> g_ticks{0}, g_pausedInspecting{0}, g_pausedNoCharacter{0}, g_missedRecords{0};
std::atomic<uint64_t> g_staleHighlight{0};  // hover checks that found a highlight with no tooltip behind it
// TooltipItem.highlightedTooltipItem: OnPointerEnter opens the item tooltip and sets it in the same
// call, OnPointerExit clears it, OnDisable does not. A ground label picked up or hidden under the
// cursor leaves it set, and the label pool later hands that object out again for another drop. So
// on its own it only covers the moment before the tooltip state appears (main thread only).
constexpr uint64_t kHighlightGraceMs = 750;
void* g_highlightSeen = nullptr;
uint64_t g_highlightSince = 0;
struct ScanNumbers { int labelsSeen = 0, labels = 0, materials = 0, gold = 0, others = 0, calls = 0, skippedFull = 0; bool stoppedInspecting = false; };
ScanNumbers g_lastScanNumbers;  // main thread

// ---- loot log: every item and gold pickup, by EpochPact or by the player.
// The game's own pickup methods are observed; their results are unchanged.
struct LootEntry {
    uint64_t seq = 0, time = 0;
    std::string kind, name, rarity, rule, tiers;
    int ruleNumber = 0, gold = 0, outcome = -1;
    int type = -1, subtype = -1;  // the game's item type/subtype (101 shard: subtype = affix ID, 102 rune, 103 glyph)
    bool automatic = false;
};
std::mutex g_logLock;
std::deque<LootEntry> g_log;
uint64_t g_logSeq = 0;
std::atomic<uint64_t> g_logFaults{0};
std::atomic<int> g_ruleOrderMin{0}, g_ruleOrderMax{0};
thread_local bool t_autoPickup = false;  // set around EpochPact's own pickup calls
thread_local uint32_t t_pickupId = 0;    // the item pickupItem is taking now (it logs that one itself)
// Every game start numbers the log from 1 again; the session tells a reader which game it reads.
uint64_t g_logSession = 0;
constexpr size_t kLogCapacity = 300;

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

game::MethodRef m_inventoryFull, m_isInventoryOpen;
using InventoryFullFn = void (*)(void* self, void* actor, const Method* m);
InventoryFullFn o_inventoryFull = nullptr;
void d_inventoryFull(void* self, void* actor, const Method* m) {
    o_inventoryFull(self, actor, m);  // the player still sees the game's own message
    g_inventoryFull = true;
    g_fullEvents.fetch_add(1, std::memory_order_relaxed);
}
// UIBase.IsInventoryOpen through its code pointer: no boxing in the frame path.
bool InventoryOpen() {
    if (!g_uiField || !m_isInventoryOpen) return false;
    void* ui = game::StaticObject(g_uiField);
    if (!ui || !game::IsAlive(ui)) return false;
    return reinterpret_cast<bool (*)(void*, const Method*)>(m_isInventoryOpen.code)(ui, m_isInventoryOpen.info);
}
// Resume once the player has opened and closed the inventory after it filled.
void TrackInventoryFull() {
    if (!g_inventoryFull.load(std::memory_order_relaxed)) { g_inventoryWasOpen = false; return; }
    bool open = false;
    if (!game::Guarded([&] { open = InventoryOpen(); }, nullptr)) return;
    if (open) g_inventoryWasOpen = true;
    else if (g_inventoryWasOpen) { g_inventoryWasOpen = false; g_inventoryFull = false; }
}

bool InspectingItem() {
    // A failed/missing hover read must pause collection, not dismiss a tooltip.
    if (!g_hoveredItemField || !g_uiField || !g_tooltipSystemOffset || !m_activeTooltipItem || !g_tooltipCardOffset || !g_tooltipManagerOffset || !g_legacyActiveOffset || !g_tooltipLockOffset || !m_componentObject || !m_activeHierarchy) { g_inspectReason = 1; return true; }
    void* hovered = nullptr;
    if (!game::TryStaticObject(g_hoveredItemField, &hovered)) { g_inspectReason = 8; return true; }
    bool highlighted = false, inspecting = false;
    if (!game::TryIsAlive(hovered, &highlighted)) { g_inspectReason = 8; return true; }
    // Container-hover and the V2 tooltip system do not set highlightedTooltipItem.
    // Use the game's unified getter: it handles both V2 and the legacy manager.
    // This is a cached native getter, with no per-frame reflection or boxed result.
    void* ui = nullptr;
    if (!game::TryStaticObject(g_uiField, &ui)) { g_inspectReason = 8; return true; }
    bool alive = false;
    if (!game::TryIsAlive(ui, &alive) || !alive) { g_inspectReason = 3; return true; }
    void* active = nullptr;
    int reason = 0;
    const bool readable = game::Guarded([&] {
        void* system = *reinterpret_cast<void* const*>(static_cast<char*>(ui) + g_tooltipSystemOffset);
        bool systemAlive = false;
        if (!game::TryIsAlive(system, &systemAlive) || !systemAlive)
            throw std::runtime_error("item tooltip system unavailable");
        if (*reinterpret_cast<const bool*>(static_cast<char*>(system)+g_tooltipLockOffset)) { active=system; reason=4; return; }
        // This build can render through the legacy manager while the V2 card is
        // inactive. Its active flag, unlike ActiveItemTooltipItem, is cleared by
        // CloseAllTooltips and covers comparison/controller cards as well.
        void* manager=*reinterpret_cast<void* const*>(static_cast<char*>(system)+g_tooltipManagerOffset);
        bool managerAlive=false;
        if(!game::TryIsAlive(manager,&managerAlive))throw std::runtime_error("tooltip manager unreadable");
        if(managerAlive && *reinterpret_cast<const bool*>(static_cast<char*>(manager)+g_legacyActiveOffset)){active=manager;reason=5;return;}
        // Pending tooltips have a hovered rect before their asynchronous item
        // card becomes ready. Do not pick that item up during this gap.
        if (g_hoverRectOffset) {
            void* rect = *reinterpret_cast<void* const*>(static_cast<char*>(system) + g_hoverRectOffset);
            if (!game::TryIsAlive(rect, &inspecting)) throw std::runtime_error("tooltip hover unreadable");
            if (inspecting) { active = rect; reason = 6; return; }
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
            else reason=7;
        }
    }, nullptr);
    if (!readable || active) { g_inspectReason = !readable ? 8 : reason; return true; }
    if (!highlighted) { g_highlightSeen = nullptr; g_inspectReason = 0; return false; }
    // Highlighted, no tooltip: either the moment before it opens, or a label left behind.
    const uint64_t now = GetTickCount64();
    if (hovered != g_highlightSeen) { g_highlightSeen = hovered; g_highlightSince = now; }
    if (now - g_highlightSince < kHighlightGraceMs) { g_inspectReason = 2; return true; }
    g_staleHighlight.fetch_add(1, std::memory_order_relaxed);
    g_inspectReason = 0;
    return false;
}

struct Position { float x,y,z; };
struct GroundOffsets {size_t list=0,location=0,id=0;};
GroundOffsets g_ground[6];
size_t g_offlineItems=0,g_labelVisuals=0,g_visualId=0,g_pickupId=0;
bool g_vacuumReady=false;
struct AutoScope { bool previous = t_autoPickup; AutoScope() { t_autoPickup = true; } ~AutoScope() { t_autoPickup = previous; } };
void Pickup(void* object,game::MethodRef method,int kind){
    AutoScope automatic;
    if(!g_vacuumReady){managed::Invoke(method,object);return;}
    void* manager=game::StaticObject(g_managerField),*actor=managed::Invoke(m_getActor);
    if(!manager||!actor||!game::IsAlive(actor)){g_missedRecords.fetch_add(1,std::memory_order_relaxed);return;}
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
    g_missedRecords.fetch_add(1,std::memory_order_relaxed);  // no ground record with this id: nothing was asked
}
size_t g_groundItemData = 0, g_goldValue = 0, g_itemTypeOffset = 0, g_subTypeOffset = 0, g_uniqueIdOffset = 0;
game::MethodRef m_itemListGet, m_uniqueName, m_unpack, m_pickupItem, m_pickupGold;
void* FindRecord(void* groundList, int kind, uint32_t id) {
    void* list = managed::Get<void*>(groundList, g_ground[kind].list);
    if (!list) return nullptr;
    void* array = managed::Get<void*>(list, kListItems);
    const int n = managed::Get<int>(list, kListSize);
    if (!array || n < 0 || n > 8192 || static_cast<uintptr_t>(n) > managed::Get<uintptr_t>(array, kArrayLength)) return nullptr;
    for (int i = 0; i < n; ++i) {
        void* record = managed::Get<void*>(array, kArrayFirst + 8 * i);
        if (record && managed::Get<uint32_t>(record, g_ground[kind].id) == id) return record;
    }
    return nullptr;
}
bool DescribeItem(void* item, LootEntry* e) {
    using namespace managed;
    Root pin(item);
    int type = Get<uint8_t>(item, g_itemTypeOffset), sub = Get<uint16_t>(item, g_subTypeOffset);
    Root itemList(Invoke(m_itemListGet));
    void* nameArgs[]{&type, &sub};
    e->name = Text(Invoke(managed::Method(itemList.Get(), "GetItemDisplayName", 2), itemList.Get(), nameArgs));
    const bool unique = Value<bool>(managed::Method(item, "isUniqueSetOrLegendary", 0), item);
    if (unique) {
        uint16_t uid = Get<uint16_t>(item, g_uniqueIdOffset); bool display = true; void* a[]{&uid, &display};
        const auto uniqueName = Text(Invoke(m_uniqueName, nullptr, a));
        if (!uniqueName.empty()) e->name = uniqueName;
    }
    e->type = type; e->subtype = sub;
    e->kind = type >= 100 ? "material" : "item";
    e->rarity = type >= 100 ? "Material" : Value<bool>(managed::Method(item, "isLegendary", 0), item) ? "Legendary" :
        Value<bool>(managed::Method(item, "isSet", 0), item) ? "Set" : unique ? "Unique" :
        Value<bool>(managed::Method(item, "isExaltedItem", 0), item) ? "Exalted" : Value<bool>(managed::Method(item, "isMagic", 0), item) ? "Magic" :
        managed::Entries(Get<void*>(item, game::FieldOffset("LE.dll", "", "ItemData", "affixes"))).empty() ? "Normal" : "Rare";
    // Affix tiers, highest first ("7 6 6"), so the log shows why a rule matched.
    std::vector<int> tiers;
    for (void* affix : managed::Entries(Get<void*>(item, game::FieldOffset("LE.dll", "", "ItemData", "affixes"))))
        if (affix) { const int t = Value<int>(managed::Method(affix, "get_DisplayTier", 0), affix); if (t >= 1 && t <= 8) tiers.push_back(t); }
    std::sort(tiers.rbegin(), tiers.rend());
    for (int t : tiers) { if (!e->tiers.empty()) e->tiers += ' '; e->tiers += std::to_string(t); }
    Root unpacked(Invoke(m_unpack, item));
    smartloot::RuleMatch rule;
    if (unpacked.Get() && smartloot::RuleFor(unpacked.Get(), &rule)) {
        e->ruleNumber = rule.number; e->rule = rule.name; e->outcome = rule.outcome;
        g_ruleOrderMin = rule.orderMin; g_ruleOrderMax = rule.orderMax;
    }
    return true;
}
void PushLoot(LootEntry e) {
    e.time = GetTickCount64();
    std::lock_guard<std::mutex> hold(g_logLock);
    e.seq = ++g_logSeq;
    g_log.push_back(std::move(e));
    while (g_log.size() > kLogCapacity) g_log.pop_front();
}
// pickupItem and pickupGold each have two overloads with the same parameter
// count; select the one whose first parameter is the GroundItemList by type,
// never by arity alone. A wrong overload would receive mismatched arguments.
game::MethodRef ListOverload(const char* cls, const char* name, uint32_t count) {
    game::MethodRef ref;
    const auto& a = il2cpp::api();
    if (!a.class_get_methods || !a.method_get_name || !a.method_get_param_count || !a.method_get_param || !a.type_get_name) return ref;
    const auto* k = game::FindClass("LE.dll", "", cls);
    if (!k) return ref;
    void* iterator = nullptr;
    while (const auto* m = a.class_get_methods(k, &iterator)) {
        if (std::string(a.method_get_name(m)) != name || a.method_get_param_count(m) != count) continue;
        char* type = a.type_get_name(a.method_get_param(m, 0));
        const bool list = type && std::strstr(type, "GroundItemList") != nullptr;
        if (type && a.free) a.free(type);
        if (!list) continue;
        if (ref.info) return {};  // ambiguous: refuse rather than guess
        ref.info = m; ref.code = *reinterpret_cast<void* const*>(m);
    }
    return ref;
}
using PickupItemFn = void (*)(void* self, void* list, void* actor, uint32_t id, int flags, const Method* m);
PickupItemFn o_pickupItem = nullptr;
void d_pickupItem(void* self, void* list, void* actor, uint32_t id, int flags, const Method* m) {
    LootEntry entry; bool described = false;
    const bool automatic = t_autoPickup;
    if (!game::Guarded([&] {
        void* record = FindRecord(list, 0, id);
        void* item = record ? managed::Get<void*>(record, g_groundItemData) : nullptr;
        if (item) described = DescribeItem(item, &entry);
    }, nullptr)) g_logFaults.fetch_add(1, std::memory_order_relaxed);
    const uint32_t previousId = t_pickupId;
    t_pickupId = id;
    o_pickupItem(self, list, actor, id, flags, m);
    t_pickupId = previousId;
    if (!described) return;
    // A refused pickup (for example a full inventory) leaves the record behind.
    bool picked = false;
    if (game::Guarded([&] { picked = FindRecord(list, 0, id) == nullptr; }, nullptr) && picked) {
        entry.automatic = automatic;
        PushLoot(std::move(entry));
    }
}
// Picking up an affix shard, rune or glyph makes the game take the nearby ones too
// (vacuumNearbyStackableItems). They do not pass through pickupItem: the records of the
// materials on the ground are read before the call, and the ones it removed are logged.
using VacuumFn = void (*)(void* self, void* actor, void* list, Position position, int flags, const Method* m);
VacuumFn o_vacuum = nullptr;
void d_vacuum(void* self, void* actor, void* list, Position position, int flags, const Method* m) {
    const bool automatic = t_autoPickup;
    std::vector<std::pair<uint32_t, std::unique_ptr<managed::Root>>> before;
    if (!game::Guarded([&] {
        void* records = list ? managed::Get<void*>(list, g_ground[0].list) : nullptr;
        if (!records) return;
        for (void* record : managed::Entries(records)) {
            void* item = record ? managed::Get<void*>(record, g_groundItemData) : nullptr;
            if (!item || managed::Get<uint8_t>(item, g_itemTypeOffset) < 100) continue;
            const uint32_t id = managed::Get<uint32_t>(record, g_ground[0].id);
            if (id != t_pickupId) before.emplace_back(id, std::make_unique<managed::Root>(item));
        }
    }, nullptr)) g_logFaults.fetch_add(1, std::memory_order_relaxed);
    o_vacuum(self, actor, list, position, flags, m);
    for (auto& [id, item] : before) {
        LootEntry entry;
        bool taken = false;
        if (!game::Guarded([&] { taken = FindRecord(list, 0, id) == nullptr && DescribeItem(item->Get(), &entry); }, nullptr)) {
            g_logFaults.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        if (taken) { entry.automatic = automatic; PushLoot(std::move(entry)); }
    }
}
using PickupGoldFn = bool (*)(void* self, void* list, void* actor, uint32_t id, const Method* m);
PickupGoldFn o_pickupGold = nullptr;
bool d_pickupGold(void* self, void* list, void* actor, uint32_t id, const Method* m) {
    int amount = 0;
    const bool automatic = t_autoPickup;
    game::Guarded([&] { if (void* r = FindRecord(list, 1, id)) amount = managed::Get<int>(r, g_goldValue); }, nullptr);
    bool picked = false;
    {
        loot::GoldPickupScope gold;  // the gold multiplier counts what the game adds in here
        picked = o_pickupGold(self, list, actor, id, m);
    }
    if (picked && amount > 0) {
        LootEntry e; e.kind = "gold"; e.name = "Gold"; e.rarity = "Gold"; e.gold = amount; e.automatic = automatic;
        PushLoot(std::move(e));
    }
    return picked;
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
// pickup calls remove elements from the list while it is being walked. At most `cap`
// entries are looked at; a longer list continues from `cursor` on the next scan
// (rules::NextWindow), so items behind hundreds of others are still reached.
template <typename F>
int SnapshotList(void* listObj, void** out, int cap, F&& keep, unsigned long long* cursor = nullptr) {
    if (!listObj) return 0;
    void* arr = *reinterpret_cast<void* const*>(static_cast<const char*>(listObj) + kListItems);
    int32_t size = *reinterpret_cast<int32_t*>(static_cast<char*>(listObj) + kListSize);
    if (!arr || size <= 0) return 0;
    const int32_t length = *reinterpret_cast<int32_t*>(static_cast<char*>(arr) + kArrayLength);
    if (size > length) size = length;  // never trust the count alone
    unsigned long long fromStart = 0;
    const auto window = rules::NextWindow(size, cap, cursor ? *cursor : fromStart);
    int n = 0;
    for (int32_t k = 0; k < window.count; ++k) {
        const int32_t i = (window.start + k) % size;
        if (void* elem = *reinterpret_cast<void* const*>(static_cast<char*>(arr) + kArrayFirst + 8 * i))
            if (keep(elem)) out[n++] = elem;
    }
    return n;
}
// Where the next scan of each list continues (main thread only): ground item
// labels, then gold, potions, XP tomes, Favor tomes and bones.
unsigned long long g_labelCursor = 0, g_listCursor[5]{};

// LE.Factions pickupables: Memory Amber, Soul Embers and XP motes. The game
// collects them when the player walks close; PickupObject is its own call.
const Field* g_pickupablesManager = nullptr;
size_t g_pickupableSetOffset = 0, g_pickupablePosition = 0, g_pickupableValue = 0;
game::MethodRef m_pickupablesOfType, m_pickupObject;
int PickPickupables(int budget, bool everywhere, const Position& player) {
    if (!g_pickupablesManager || !m_pickupablesOfType || !m_pickupObject || budget <= 0 || !smartloot::Category(5)) return 0;
    int calls = 0;
    game::Guarded([&] {
        using namespace managed;
        void* manager = game::StaticObject(g_pickupablesManager);
        if (!manager || !game::IsAlive(manager)) return;
        void* set = Get<void*>(manager, g_pickupableSetOffset);
        if (!set) return;
        Root setPin(set);
        for (uint8_t type = 1; type <= 3 && calls < budget; ++type) {
            void* typeArgs[]{&type};
            Root list(Invoke(m_pickupablesOfType, set, typeArgs));
            if (!list.Get()) continue;
            const auto objects = Entries(list.Get());
            // Removal shifts later entries; collect from the end.
            for (int index = static_cast<int>(objects.size()) - 1; index >= 0 && calls < budget; --index) {
                void* object = objects[index];
                if (!object) continue;
                if (!everywhere && g_radius.load() != 0) {
                    const auto at = Get<Position>(object, g_pickupablePosition);
                    const double x = at.x - player.x, y = at.y - player.y, z = at.z - player.z, r = g_radius.load();
                    if (x * x + y * y + z * z > r * r) continue;
                }
                Root pin(object);
                const uint32_t value = g_pickupableValue ? Get<uint32_t>(object, g_pickupableValue) : 0;
                void* args[]{set, object, &index};
                Invoke(m_pickupObject, manager, args);
                ++calls;
                LootEntry e; e.kind = "amber"; e.rarity = "Amber"; e.automatic = true; e.gold = static_cast<int>(value);
                e.name = type == 1 ? "Memory Amber" : type == 2 ? "Soul Ember" : "XP mote";
                PushLoot(std::move(e));
            }
        }
    }, nullptr);
    return calls;
}
struct PickResult { int calls = 0, candidates = 0, skippedFull = 0; };
PickResult PickAll(int maxCalls = 8, bool everywhere = false) {
    PickResult result;
    void* labels[kMaxPerScan];
    bool labelMaterial[kMaxPerScan]{};
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
                        return *reinterpret_cast<void* const*>(label) == g_groundItemLabel && (everywhere || InRadius(label,player,true)) && smartloot::Accept(label);
                    }, &g_labelCursor);
                    for (int i = 0; i < labelCount; ++i) labelMaterial[i] = smartloot::IsMaterial(labels[i]);
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
                        *count[i] = SnapshotList(listObj, out[i], kMaxPerScan, [&](void* object) { return everywhere || InRadius(object,player,false); }, &g_listCursor[i]);
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
    }, nullptr)) return result;
    result.candidates = labelCount + goldCount + potionCount + xpCount + favorCount + boneCount;
    int calls = 0;
    if (m_requestPickup) {
        for (int i = 0; i < labelCount; ++i) {
            if (calls >= maxCalls || InspectingItem()) break;
            if (!labelMaterial[i] && g_inventoryFull.load(std::memory_order_relaxed)) { ++result.skippedFull; continue; }
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
            if (calls >= maxCalls || InspectingItem()) break;
            game::Guarded([&] { if(game::IsAlive(l.list[i])) { Pickup(l.list[i],*l.pick,kind); ++calls; } },nullptr);
        }
    }

    if (calls < maxCalls && !InspectingItem()) {
        Position player{};
        bool located = everywhere || g_radius.load() == 0;
        if (!located) game::Guarded([&] {
            void* actor = managed::Invoke(m_getActor);
            if (!actor || !game::IsAlive(actor)) return;
            managed::Root transform(managed::Invoke(m_componentTransform, actor));
            player = managed::Value<Position>(m_transformPosition, transform.Get()); located = true;
        }, nullptr);
        if (located) calls += PickPickupables(maxCalls - calls, everywhere, player);
    }
    if (result.skippedFull) g_fullSkipped.fetch_add(static_cast<uint64_t>(result.skippedFull));
    {
        int materials = 0;
        for (int i = 0; i < labelCount; ++i) materials += labelMaterial[i] ? 1 : 0;
        g_lastScanNumbers = {labelSeen, labelCount, materials, goldCount, potionCount + xpCount + favorCount + boneCount, calls, result.skippedFull,
                             calls < maxCalls && InspectingItem()};
    }
    result.calls = calls;
    if (calls) g_calls.fetch_add(static_cast<uint64_t>(calls));
    if (g_autoFirst.exchange(false))
        Log("autopickup: first scan: item labels %d picked (%d seen), gold %d, potions %d, xp tomes %d, favor %d, bones %d; %d calls",
            labelCount, labelSeen, goldCount, potionCount, xpCount, favorCount, boneCount, calls);
    return result;
}

using TickFn = void (*)(void* self, float deltaTime, const Method* m);
TickFn o_distantTick = nullptr;

void d_distantTick(void* self, float deltaTime, const Method* m) {
    g_ticks.fetch_add(1, std::memory_order_relaxed);
    const bool active = g_auto.load(std::memory_order_relaxed) != 0.0 && game::IsOfflinePlay();
    // Before the hover pause, so the inventory closing while an item is hovered still counts.
    if (active) TrackInventoryFull();
    const bool inspectingBeforeTick = active && InspectingItem();
    if (inspectingBeforeTick) g_pausedInspecting.fetch_add(1, std::memory_order_relaxed);
    // This native handler does only queued distant item pickups. Its own
    // requestPickup calls also close tooltips, even when OUR scan is paused.
    // Leave queued labels untouched until inspection ends. ClickedItem and the
    // normal manual pickup paths are separate and remain available.
    if(inspectingBeforeTick) return;
    o_distantTick(self, deltaTime, m);  // the game's own distant pickup stays in charge
    if (!active) return;
    if (inspectingBeforeTick || InspectingItem()) { g_pausedInspecting.fetch_add(1, std::memory_order_relaxed); return; }  // Leave both the tooltip and scan timer alone.
    if (!smartloot::CanCollect()) { g_pausedNoCharacter.fetch_add(1, std::memory_order_relaxed); return; }
    const unsigned long now = GetTickCount();
    unsigned long last = g_lastScan.load(std::memory_order_relaxed);
    if (now - last < g_intervalMs.load(std::memory_order_relaxed)) return;
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
    g_weaversWill.cmd = "weaverswill";
    g_weaversWill.max = 10;
    g_weaversWill.hooks = {&h_rollWeaversWill};
    h_rollWeaversWill.ref = game::FindMethod("LE.dll", "", "ItemData", "RollWeaversWill", 4);
    g_lpRolls.cmd = "lprolls";
    g_lpRolls.max = lootcraft::rules::MaximumLegendaryRolls;
    g_lpRolls.hooks = {&h_rollLegendaryPotential};
    h_rollLegendaryPotential.ref = game::FindMethod("LE.dll", "", "ItemData", "RollLegendaryPotential", 7);
    // ItemData.MaxLegendaryPotential is a const (literal) field: rolling stops once a roll reaches it.
    game::Guarded([] {
        const auto* itemData = game::FindClass("LE.dll", "", "ItemData");
        void* iter = nullptr;
        while (const auto* f = itemData ? il2cpp::api().class_get_fields(itemData, &iter) : nullptr) {
            if (!(il2cpp::api().field_get_flags(f) & il2cpp::kFieldLiteral) || std::strcmp(il2cpp::api().field_get_name(f), "MaxLegendaryPotential") != 0) continue;
            int value = 0; il2cpp::api().field_static_get_value(f, &value);
            if (value > 0 && value <= 255) g_maximumLP = value;
            Log("items: ItemData.MaxLegendaryPotential %d", value);
            break;
        }
    }, nullptr);

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

    g_pickupablesManager = game::FindStaticField("LE.dll", "LE.Factions", "PickupableObjectsManager", "Instance");
    g_pickupableSetOffset = game::FieldOffset("LE.dll", "LE.Factions", "PickupableObjectsManager", "singleplayerPickupableObjects");
    g_pickupablePosition = game::FieldOffset("LE.dll", "LE.Factions", "PickupableObject", "Position");
    g_pickupableValue = game::FieldOffset("LE.dll", "LE.Factions", "PickupableObject", "Value");
    m_pickupablesOfType = game::FindMethod("LE.dll", "LE.Factions", "PickupableObjectSet", "GetPickupablesOfType", 1);
    m_pickupObject = game::FindMethod("LE.dll", "LE.Factions", "PickupableObjectsManager", "PickupObject", 3);
    Log("items: amber/ember/mote pickup %s", g_pickupablesManager && g_pickupableSetOffset && g_pickupablePosition && m_pickupablesOfType && m_pickupObject ? "ready" : "unavailable");
    m_isInventoryOpen = game::FindMethod("LE.dll", "", "UIBase", "IsInventoryOpen", 0);
    m_inventoryFull = game::FindMethod("LE.dll", "", "GroundItemManager", "showInventoryFullNofication", 1);
    std::string fullWhy = "method not found";
    const bool fullHook = m_inventoryFull && m_isInventoryOpen &&
        (hook::IsInstalled(m_inventoryFull.code) || hook::Install(m_inventoryFull.code, reinterpret_cast<void*>(&d_inventoryFull),
                                                                   reinterpret_cast<void**>(&o_inventoryFull), &fullWhy));
    Log("items: inventory-full pause %s%s", fullHook ? "ready" : "unavailable: ", fullHook ? "" : fullWhy.c_str());

    g_groundItemData = game::FieldOffset("LE.dll", "", "GroundItemManager.GroundItem", "itemData");
    g_goldValue = game::FieldOffset("LE.dll", "", "GroundItemManager.GroundGold", "goldValue");
    g_itemTypeOffset = game::FieldOffset("LE.dll", "", "ItemData", "itemType");
    g_subTypeOffset = game::FieldOffset("LE.dll", "", "ItemData", "subType");
    g_uniqueIdOffset = game::FieldOffset("LE.dll", "", "ItemData", "uniqueID");
    m_itemListGet = game::FindMethod("LE.dll", "", "ItemList", "get", 0);
    m_uniqueName = game::FindMethod("LE.dll", "", "UniqueList", "getUniqueName", 2);
    m_unpack = game::FindMethod("LE.dll", "", "ItemData", "getAsUnpacked", 0);
    m_pickupItem = ListOverload("GroundItemManager", "pickupItem", 4);
    {
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        g_logSession = (static_cast<uint64_t>(GetCurrentProcessId()) << 32) ^ static_cast<uint64_t>(counter.QuadPart);
    }
    m_pickupGold = ListOverload("GroundItemManager", "pickupGold", 3);
    std::string logWhy = "metadata unavailable";
    const bool logReady = g_vacuumReady && g_groundItemData && g_goldValue && g_itemTypeOffset && g_subTypeOffset && g_uniqueIdOffset &&
        m_itemListGet && m_uniqueName && m_unpack && m_pickupItem && m_pickupGold &&
        (hook::IsInstalled(m_pickupItem.code) || hook::Install(m_pickupItem.code, reinterpret_cast<void*>(&d_pickupItem), reinterpret_cast<void**>(&o_pickupItem), &logWhy)) &&
        (hook::IsInstalled(m_pickupGold.code) || hook::Install(m_pickupGold.code, reinterpret_cast<void*>(&d_pickupGold), reinterpret_cast<void**>(&o_pickupGold), &logWhy));
    Log("items: loot log %s%s", logReady ? "ready" : "unavailable: ", logReady ? "" : logWhy.c_str());
    const auto vacuum = game::FindMethod("LE.dll", "", "GroundItemManager", "vacuumNearbyStackableItems", 4);
    std::string vacuumWhy = "method not found";
    const bool vacuumReady = logReady && vacuum &&
        (hook::IsInstalled(vacuum.code) || hook::Install(vacuum.code, reinterpret_cast<void*>(&d_vacuum), reinterpret_cast<void**>(&o_vacuum), &vacuumWhy));
    Log("items: loot log of nearby materials taken along %s%s", vacuumReady ? "ready" : "unavailable: ", vacuumReady ? "" : vacuumWhy.c_str());

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
std::string SetWeaversWill(double m) { return feature::Set(g_weaversWill, m); }
std::string WeaversWillStatus() { return feature::Line(g_weaversWill); }
std::string SetLegendaryRolls(double n) { return feature::Set(g_lpRolls, n); }
std::string LegendaryRollsStatus() { return feature::Line(g_lpRolls); }

std::string AutoPickupStatus() { return AutoStatus(); }
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string PickupDiagnostics() {
    const auto n = g_lastScanNumbers;
    char buf[704];
    std::snprintf(buf, sizeof buf,
                  "{\"ok\":true,\"on\":%s,\"hook\":%s,\"ticks\":%llu,\"scans\":%llu,\"calls\":%llu,\"pausedInspecting\":%llu,\"inspectReason\":%d,\"staleHighlight\":%llu,"
                  "\"pausedNoCharacter\":%llu,\"missedRecords\":%llu,\"inventoryFull\":%s,\"fullSkipped\":%llu,\"intervalMs\":%u,\"radius\":%.2f,"
                  "\"last\":{\"labelsSeen\":%d,\"labels\":%d,\"materials\":%d,\"gold\":%d,\"others\":%d,\"calls\":%d,\"skippedFull\":%d,\"stoppedInspecting\":%s}}",
                  g_auto.load() > 0.5 ? "true" : "false", h_distantTick.ref && hook::IsInstalled(h_distantTick.ref.code) ? "true" : "false",
                  static_cast<unsigned long long>(g_ticks.load()), static_cast<unsigned long long>(g_scans.load()), static_cast<unsigned long long>(g_calls.load()),
                  static_cast<unsigned long long>(g_pausedInspecting.load()), g_inspectReason.load(), static_cast<unsigned long long>(g_staleHighlight.load()),
                  static_cast<unsigned long long>(g_pausedNoCharacter.load()),
                  static_cast<unsigned long long>(g_missedRecords.load()), g_inventoryFull.load() ? "true" : "false", static_cast<unsigned long long>(g_fullSkipped.load()),
                  g_intervalMs.load(), g_radius.load(), n.labelsSeen, n.labels, n.materials, n.gold, n.others, n.calls, n.skippedFull, n.stoppedInspecting ? "true" : "false");
    return buf;
}
#endif

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
    // Turning collection on tries equipment again: with the hook out nobody saw the inventory
    // being emptied. Still no room: the game shows its own message once more and equipment waits.
    g_inventoryFull = false;
    g_auto = 1.0;
    g_autoFirst = true;
    g_lastScan = 0;
    return "autopickup -> on (items, gold, potions, tomes and bones; pauses while inspecting an item)";
}

std::string Status() { return feature::Line(g_rarity) + "\n" + feature::Line(g_weaversWill) + "\n" + feature::Line(g_lpRolls) + "\n" + AutoStatus(); }

bool IsInspectingItem(){return InspectingItem();}
std::string RadiusStatus(){
    char interval[32];std::snprintf(interval,sizeof interval,"%.2f",g_intervalMs.load()/1000.0);
    return "{\"ok\":true,\"radius\":"+std::to_string(g_radius.load())+",\"allLoaded\":"+(g_radius.load()==0?"true":"false")+
        ",\"intervalSeconds\":"+interval+",\"inventoryFull\":"+(g_inventoryFull.load()?"true":"false")+
        ",\"inventoryFullEvents\":"+std::to_string(g_fullEvents.load())+",\"skippedWhileFull\":"+std::to_string(g_fullSkipped.load())+
        ",\"inventoryFullPause\":"+(o_inventoryFull?"true":"false")+"}";}
std::string LootLog(uint64_t since){
    std::ostringstream out;
    std::lock_guard<std::mutex> hold(g_logLock);
    const auto now=GetTickCount64();
    char session[24];
    std::snprintf(session, sizeof session, "%016llx", static_cast<unsigned long long>(g_logSession));
    out<<"{\"ok\":true,\"ready\":"<<(o_pickupItem&&o_pickupGold?"true":"false")<<",\"session\":\""<<session<<"\",\"seq\":"<<g_logSeq<<",\"faults\":"<<g_logFaults.load()
       <<",\"ruleOrderMin\":"<<g_ruleOrderMin.load()<<",\"ruleOrderMax\":"<<g_ruleOrderMax.load()<<",\"entries\":[";
    bool comma=false;
    for(const auto& e:g_log){
        if(e.seq<=since)continue;
        if(comma)out<<',';comma=true;
        out<<"{\"seq\":"<<e.seq<<",\"ageSeconds\":"<<(now-e.time)/1000<<",\"kind\":"<<managed::Json(e.kind)<<",\"name\":"<<managed::Json(e.name)
           <<",\"rarity\":"<<managed::Json(e.rarity)<<",\"gold\":"<<e.gold<<",\"ruleNumber\":"<<e.ruleNumber<<",\"rule\":"<<managed::Json(e.rule)
           <<",\"outcome\":"<<e.outcome<<",\"tiers\":"<<managed::Json(e.tiers)<<",\"type\":"<<e.type<<",\"subtype\":"<<e.subtype
           <<",\"automatic\":"<<(e.automatic?"true":"false")<<'}';
    }
    out<<"]}";
    return out.str();
}
std::string ClearLootLog(){std::lock_guard<std::mutex> hold(g_logLock);g_log.clear();return "{\"ok\":true}";}
std::string SetInterval(double seconds){
    if(!std::isfinite(seconds)||seconds<0.25||seconds>10)return "{\"ok\":false,\"error\":\"Pickup interval must be between 0.25 and 10 seconds.\"}";
    g_intervalMs=static_cast<unsigned>(std::lround(seconds*1000));g_lastScan=0;return RadiusStatus();
}
std::string PickupNow(){
    // One explicit sweep of every loaded drop, ignoring the radius: up to 32
    // pickups per frame so a crowded area never stalls one frame. The loot
    // filter, categories, inventory capacity and tooltip pause still apply.
    if(!smartloot::CanCollect())return "{\"ok\":false,\"error\":\"Load an offline character first.\"}";
    g_inventoryFull=false;g_inventoryWasOpen=false;
    int picked=0,steps=0,skipped=0,idle=0,stalled=0,lastCandidates=-1;std::string why;
    const bool ran=mainthread::RunSteps([&]{
        if(InspectingItem())return false;
        const auto r=PickAll(32,true);picked+=r.calls;skipped+=r.skippedFull;++steps;
        // Picked objects leave the lists over the next frames; stop after two
        // sweeps without a new pickup, after three sweeps in which nothing left
        // the ground (a refused object), or after a bounded number of frames.
        idle=r.calls?0:idle+1;
        stalled=lastCandidates>=0&&r.candidates>=lastCandidates?stalled+1:0;lastCandidates=r.candidates;
        return idle>=2||stalled>=3||steps>=240;
    },5000,&why);
    if(!ran)return std::string("{\"ok\":false,\"error\":\"")+(why.find("pick the job up")!=std::string::npos?"The game is not updating. Return to the game window and try again.":"Close the item card in the game, then try again.")+"\",\"picked\":"+std::to_string(picked)+"}";
    return "{\"ok\":true,\"picked\":"+std::to_string(picked)+",\"steps\":"+std::to_string(steps)+",\"inventoryFull\":"+(g_inventoryFull.load()?"true":"false")+",\"skippedWhileFull\":"+std::to_string(skipped)+"}";
}
std::string SetRadius(double radius){
    if(!std::isfinite(radius)||radius<0||radius>60)return "{\"ok\":false,\"error\":\"Pickup radius must be between 0 and 60.\"}";
    if(radius!=0 && (!g_labelFollowerOffset||!m_followerPosition||!m_getActor||!m_componentTransform||!m_transformPosition))return "{\"ok\":false,\"error\":\"Pickup range metadata is unavailable.\"}";
    g_radius=radius;return RadiusStatus();
}

}  // namespace ep::items
