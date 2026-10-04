#include "items.hpp"

#include "common.hpp"
#include "feature.hpp"
#include "game.hpp"
#include "hook.hpp"

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
            // Ground item labels (equipment), through the static list the UI itself keeps.
            if (g_labelListField && g_groundItemLabel) {
                if (void* dlist = game::StaticObject(g_labelListField)) {
                    void* inner = *reinterpret_cast<void* const*>(static_cast<char*>(dlist) + kListItems);
                    labelCount = SnapshotList(inner, labels, kMaxPerScan, [&](void* label) {
                        ++labelSeen;
                        return *reinterpret_cast<void* const*>(label) == g_groundItemLabel;
                    });
                }
            }
            // Gold, potions, tomes and bones, through the manager's own active lists.
            if (g_managerField) {
                if (void* mgr = game::StaticObject(g_managerField)) {
                    void** out[5] = {gold, potions, xp, favor, bones};
                    int* count[5] = {&goldCount, &potionCount, &xpCount, &favorCount, &boneCount};
                    for (int i = 0; i < 5; ++i) {
                        if (!g_activeLists[i].pick || !g_activeLists[i].offset) continue;
                        void* listObj = *reinterpret_cast<void* const*>(static_cast<char*>(mgr) + g_activeLists[i].offset);
                        *count[i] = SnapshotList(listObj, out[i], kMaxPerScan, [](void*) { return true; });
                    }
                }
            }
        },
        nullptr);

    int calls = 0;
    if (m_requestPickup) {
        for (int i = 0; i < labelCount; ++i) {
            reinterpret_cast<void (*)(void*, const Method*)>(m_requestPickup.code)(labels[i], m_requestPickup.info);
            ++calls;
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
    for (const auto& l : lists) {
        if (!l.pick->code) continue;
        for (int i = 0; i < l.count; ++i) {
            reinterpret_cast<void (*)(void*, const Method*)>(l.pick->code)(l.list[i], l.pick->info);
            ++calls;
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
    o_distantTick(self, deltaTime, m);  // the game's own distant pickup stays in charge
    if (g_auto.load(std::memory_order_relaxed) == 0.0) return;
    if (!game::IsOfflinePlay()) return;
    const unsigned long now = GetTickCount();
    unsigned long last = g_lastScan.load(std::memory_order_relaxed);
    if (now - last < 750) return;
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
    g_groundItemLabel = game::FindClass("LE.dll", "", "GroundItemLabel");
    m_requestPickup = game::FindMethod("LE.dll", "", "GroundItemLabel", "requestPickup", 0);

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

    Log("items: RollRarity %s; autopickup: tick %s, label list %s, manager %s, GroundItemLabel %s, requestPickup %s; active lists "
        "gold %d potion %d xp %d favor %d bone %d",
        h_rollRarity.ref ? "found" : "MISSING", h_distantTick.ref ? "found" : "MISSING", g_labelListField ? "found" : "MISSING",
        g_managerField ? "found" : "MISSING", g_groundItemLabel ? "found" : "MISSING", m_requestPickup ? "found" : "MISSING",
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
    if (!game::IsOfflinePlay()) return "autopickup: refused: " + game::GateText();
    if (!hook::IsInstalled(h_distantTick.ref.code)) {
        std::string why;
        if (!hook::Install(h_distantTick.ref.code, h_distantTick.detour, h_distantTick.original, &why)) {
            g_auto = 0.0;
            return "autopickup: refused: the hook did not go in: " + why;
        }
    }
    g_auto = 1.0;
    g_autoFirst = true;
    return "autopickup -> on (items, gold, potions, tomes and bones; a scan every 0.75 s)";
}

std::string Status() { return feature::Line(g_rarity) + "\n" + AutoStatus(); }

}  // namespace ep::items
