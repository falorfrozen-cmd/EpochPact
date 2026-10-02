#include "loot.hpp"

#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"

#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace ep::loot {

namespace {

using il2cpp::Method;

struct Hook {
    const char* name;
    game::MethodRef ref;
    void* detour;
    void** original;
};

struct Feature {
    const char* cmd;
    double max;
    std::atomic<double> value{1.0};
    std::atomic<bool> firstPending{false};
    std::atomic<uint64_t> boosted{0}, refused{0};
    std::vector<Hook*> hooks;
};

// Arms or disarms a feature: x1 removes its hooks, anything else needs offline play and
// every hook in (a partial install is rolled back).
std::string Set(Feature& f, double m) {
    char buf[256];
    for (Hook* h : f.hooks)
        if (!h->ref) {
            std::snprintf(buf, sizeof buf, "%s: refused: %s was not found in this game build", f.cmd, h->name);
            return buf;
        }
    if (!(m >= 1.0 && m <= f.max)) {
        std::snprintf(buf, sizeof buf, "%s: refused: the multiplier must be between 1 and %g", f.cmd, f.max);
        return buf;
    }
    if (m == 1.0) {
        f.value = 1.0;
        std::string why, failed;
        for (Hook* h : f.hooks)
            if (hook::IsInstalled(h->ref.code) && !hook::Remove(h->ref.code, &why)) failed += std::string(" ") + h->name + ": " + why;
        if (!failed.empty()) return std::string(f.cmd) + ": x1 set, but a hook stays:" + failed;
        std::snprintf(buf, sizeof buf, "%s -> x1 (off; hooks removed)", f.cmd);
        return buf;
    }
    if (!game::IsOfflinePlay()) return std::string(f.cmd) + ": refused: " + game::GateText();
    f.value = m;
    f.firstPending = true;
    std::vector<Hook*> fresh;
    for (Hook* h : f.hooks) {
        if (hook::IsInstalled(h->ref.code)) continue;
        std::string why;
        if (!hook::Install(h->ref.code, h->detour, h->original, &why)) {
            for (Hook* back : fresh) hook::Remove(back->ref.code, nullptr);
            f.value = 1.0;
            return std::string(f.cmd) + ": refused: the hook on " + h->name + " did not go in: " + why;
        }
        fresh.push_back(h);
    }
    std::snprintf(buf, sizeof buf, "%s -> x%g", f.cmd, m);
    return buf;
}

std::string Line(const Feature& f) {
    bool in = !f.hooks.empty();
    for (const Hook* h : f.hooks) in = in && h->ref && hook::IsInstalled(h->ref.code);
    char buf[200];
    std::snprintf(buf, sizeof buf, "%s: x%g, hooks %s, boosted %llu, refused online %llu", f.cmd, f.value.load(), in ? "in" : "out",
                  static_cast<unsigned long long>(f.boosted.load()), static_cast<unsigned long long>(f.refused.load()));
    return buf;
}

// True when a detour should change something now; counts and logs the first time.
bool Active(Feature& f, double* m) {
    *m = f.value.load(std::memory_order_relaxed);
    if (*m == 1.0) return false;
    if (!game::IsOfflinePlay()) {
        f.refused.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    f.boosted.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// ---- gold: GroundItemManager.pickupGold(GroundItemList, Actor, uint) -> GoldTracker.modifyGold(int)

Feature g_gold;
thread_local int t_pickupDepth = 0;

using PickupFn = bool (*)(void* self, void* list, void* actor, uint32_t id, const Method* m);
using ModifyGoldFn = bool (*)(void* self, int32_t change, const Method* m);
PickupFn o_pickup = nullptr;
ModifyGoldFn o_modifyGold = nullptr;

bool d_pickup(void* self, void* list, void* actor, uint32_t id, const Method* m) {
    struct Depth {
        Depth() { ++t_pickupDepth; }
        ~Depth() { --t_pickupDepth; }
    } inside;
    return o_pickup(self, list, actor, id, m);
}

bool d_modifyGold(void* self, int32_t change, const Method* m) {
    double mult = 1.0;
    if (t_pickupDepth > 0 && change > 0 && Active(g_gold, &mult)) {
        const long long v = std::llround(change * mult);
        const int32_t scaled = v >= INT32_MAX ? INT32_MAX : static_cast<int32_t>(v);
        if (g_gold.firstPending.exchange(false)) Log("gold: first boosted pickup %d -> %d (x%g)", change, scaled, mult);
        change = scaled;
    }
    return o_modifyGold(self, change, m);
}

Hook h_pickup{"GroundItemManager.pickupGold", {}, reinterpret_cast<void*>(&d_pickup), reinterpret_cast<void**>(&o_pickup)};
Hook h_modifyGold{"GoldTracker.modifyGold", {}, reinterpret_cast<void*>(&d_modifyGold), reinterpret_cast<void**>(&o_modifyGold)};

// ---- drops: static ItemDrop.DropItem(level, position, itemDropChance, ..., itemMultiplier, ...)

Feature g_drops;

using DropItemFn = void (*)(int32_t level, const void* position, float itemDropChance, bool classCompatibility, float itemMultiplier,
                            int32_t baseDropRates, bool guaranteedAdditionalRare, float goldMultiplier, float goldChance,
                            float craftingOnlyDropChance, int32_t dropFlags, int32_t scene, bool causedByEnemyDeath,
                            bool forceCraftingOnlyDrop, bool limitToOneGoldPile, int32_t goldDropType, int32_t forceCorrupt,
                            float dropRadius, const Method* m);
DropItemFn o_dropItem = nullptr;

void d_dropItem(int32_t level, const void* position, float itemDropChance, bool classCompatibility, float itemMultiplier,
                int32_t baseDropRates, bool guaranteedAdditionalRare, float goldMultiplier, float goldChance, float craftingOnlyDropChance,
                int32_t dropFlags, int32_t scene, bool causedByEnemyDeath, bool forceCraftingOnlyDrop, bool limitToOneGoldPile,
                int32_t goldDropType, int32_t forceCorrupt, float dropRadius, const Method* m) {
    double mult = 1.0;
    if (itemMultiplier > 0.0f && Active(g_drops, &mult)) {
        const float scaled = static_cast<float>(itemMultiplier * mult);
        if (g_drops.firstPending.exchange(false))
            Log("drops: first boosted drop: itemMultiplier %g -> %g (x%g, level %d)", itemMultiplier, scaled, mult, level);
        itemMultiplier = scaled;
    }
    o_dropItem(level, position, itemDropChance, classCompatibility, itemMultiplier, baseDropRates, guaranteedAdditionalRare, goldMultiplier,
               goldChance, craftingOnlyDropChance, dropFlags, scene, causedByEnemyDeath, forceCraftingOnlyDrop, limitToOneGoldPile,
               goldDropType, forceCorrupt, dropRadius, m);
}

Hook h_dropItem{"ItemDrop.DropItem (static)", {}, reinterpret_cast<void*>(&d_dropItem), reinterpret_cast<void**>(&o_dropItem)};

// ---- density: Spawner.GenerateEntitiesInternal() reads numberToSpawn to roll the pack size

Feature g_density;
size_t g_numberToSpawn = 0;  // field offset, resolved by name

using GenerateFn = void (*)(void* self, const Method* m);
GenerateFn o_generate = nullptr;

void d_generate(void* self, const Method* m) {
    auto* n = reinterpret_cast<float*>(static_cast<char*>(self) + g_numberToSpawn);
    const float saved = *n;
    double mult = 1.0;
    struct Restore {
        float* field;
        float value;
        bool armed;
        ~Restore() {
            if (armed) *field = value;
        }
    } restore{n, saved, false};
    if (g_numberToSpawn && saved > 1.5f && Active(g_density, &mult)) {  // packs only: single spawns stay single
        *n = static_cast<float>(saved * mult);
        restore.armed = true;
        if (g_density.firstPending.exchange(false)) Log("density: first boosted pack: numberToSpawn %g -> %g (x%g)", saved, *n, mult);
    }
    o_generate(self, m);
}

Hook h_generate{"Spawner.GenerateEntitiesInternal", {}, reinterpret_cast<void*>(&d_generate), reinterpret_cast<void**>(&o_generate)};

}  // namespace

bool Init() {
    g_gold.cmd = "gold";
    g_gold.max = 100;
    g_gold.hooks = {&h_pickup, &h_modifyGold};
    g_drops.cmd = "drops";
    g_drops.max = 25;
    g_drops.hooks = {&h_dropItem};
    g_density.cmd = "density";
    g_density.max = 5;
    g_density.hooks = {&h_generate};

    h_pickup.ref = game::FindMethod("LE.dll", "", "GroundItemManager", "pickupGold", 3);
    h_modifyGold.ref = game::FindMethod("LE.dll", "", "GoldTracker", "modifyGold", 1);
    h_dropItem.ref = game::FindMethod("LE.dll", "", "ItemDrop", "DropItem", 18);
    h_generate.ref = game::FindMethod("LE.dll", "", "Spawner", "GenerateEntitiesInternal", 0);
    g_numberToSpawn = game::FieldOffset("LE.dll", "", "Spawner", "numberToSpawn");
    if (!g_numberToSpawn) h_generate.ref = {};  // without the field the hook would write anywhere
    Log("loot: pickupGold %s, modifyGold %s, DropItem(18) %s, GenerateEntitiesInternal %s, numberToSpawn at +0x%zX",
        h_pickup.ref ? "found" : "MISSING", h_modifyGold.ref ? "found" : "MISSING", h_dropItem.ref ? "found" : "MISSING",
        h_generate.ref ? "found" : "MISSING", g_numberToSpawn);
    return h_pickup.ref && h_modifyGold.ref && h_dropItem.ref && h_generate.ref;
}

std::string SetGold(double m) { return Set(g_gold, m); }
std::string SetDrops(double m) { return Set(g_drops, m); }
std::string SetDensity(double m) { return Set(g_density, m); }
std::string Status() { return Line(g_gold) + "\n" + Line(g_drops) + "\n" + Line(g_density); }

}  // namespace ep::loot
