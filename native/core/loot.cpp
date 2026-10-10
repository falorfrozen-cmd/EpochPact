#include "loot.hpp"
#include "cof_tuning.hpp"

#include "common.hpp"
#include "feature.hpp"
#include "game.hpp"
#include "hook.hpp"

#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ep::loot {

namespace {

using il2cpp::Method;

using feature::Active;
using feature::Feature;
using feature::Hook;
using feature::Line;
using feature::Set;

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

game::MethodRef GoldPickupMethod() {
    // The public Actor,uint,interaction overload returns void and has the same
    // arity. Hook the bool/list overload used by every normal pickup route.
    // Name + argument count alone selects the wrong ABI on Last Epoch 1.5.2.
    const auto& a = il2cpp::api();
    const auto* cls = game::FindClass("LE.dll", "", "GroundItemManager");
    if (!cls) return {};
    const auto named = [&](const il2cpp::Type* type, const char* expected) {
        char* name = a.type_get_name(type);
        const bool match = name && std::strcmp(name, expected) == 0;
        if (name) a.free(name);
        return match;
    };
    game::MethodRef found;
    void* iter = nullptr;
    while (const auto* method = a.class_get_methods(cls, &iter)) {
        if (std::strcmp(a.method_get_name(method), "pickupGold") != 0 ||
            a.method_get_param_count(method) != 3 ||
            (a.method_get_flags(method, nullptr) & il2cpp::kMethodStatic)) continue;
        const auto* first = a.method_get_param(method, 0);
        if (!(named(first, "GroundItemManager.GroundItemList") || named(first, "GroundItemManager/GroundItemList")) ||
            !named(a.method_get_param(method, 1), "Actor") ||
            !named(a.method_get_param(method, 2), "System.UInt32") ||
            !named(a.method_get_return_type(method), "System.Boolean")) continue;
        if (found) return {}; // Ambiguous metadata must fail closed.
        found = {method, *reinterpret_cast<void* const*>(method)};
    }
    return found;
}

// ---- drops: static ItemDrop.DropItem(level, position, itemDropChance, ..., itemMultiplier, ...)

Feature g_drops;
std::atomic<bool> cofDropDependency{false};

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
    const auto original = [&] {
        o_dropItem(level, position, itemDropChance, classCompatibility, itemMultiplier, baseDropRates, guaranteedAdditionalRare, goldMultiplier,
            goldChance, craftingOnlyDropChance, dropFlags, scene, causedByEnemyDeath, forceCraftingOnlyDrop, limitToOneGoldPile,
            goldDropType, forceCorrupt, dropRadius, m);
    };
    if (cofDropDependency) cof::tuning::WithEnemyLoot(causedByEnemyDeath, original); else original();
}

Hook h_dropItem{"ItemDrop.DropItem (static)", {}, reinterpret_cast<void*>(&d_dropItem), reinterpret_cast<void**>(&o_dropItem)};

}  // namespace

bool Init() {
    g_gold.cmd = "gold";
    g_gold.max = 100;
    g_gold.hooks = {&h_pickup, &h_modifyGold};
    g_drops.cmd = "drops";
    g_drops.max = 25;
    g_drops.hooks = {&h_dropItem};

    h_pickup.ref = GoldPickupMethod();
    h_modifyGold.ref = game::FindMethod("LE.dll", "", "GoldTracker", "modifyGold", 1);
    h_dropItem.ref = game::FindMethod("LE.dll", "", "ItemDrop", "DropItem", 18);
    Log("loot: pickupGold %s, modifyGold %s, DropItem(18) %s",
        h_pickup.ref ? "found" : "MISSING", h_modifyGold.ref ? "found" : "MISSING", h_dropItem.ref ? "found" : "MISSING");
    return h_pickup.ref && h_modifyGold.ref && h_dropItem.ref;
}

std::string SetGold(double m) { return Set(g_gold, m); }
std::string SetDrops(double m) {
    if (m == 1 && cofDropDependency) {
        g_drops.value = 1; return "drops -> x1 (off; shared CoF enemy hook remains)";
    }
    return Set(g_drops, m);
}
bool SetCoFDropDependency(bool enabled, std::string* why) {
    if (!h_dropItem.ref) { if (why) *why = "ItemDrop.DropItem unavailable"; return false; }
    if (enabled && !hook::IsInstalled(h_dropItem.ref.code) &&
        !hook::Install(h_dropItem.ref.code, h_dropItem.detour, h_dropItem.original, why)) return false;
    if (!enabled && g_drops.value.load() == 1 && hook::IsInstalled(h_dropItem.ref.code) &&
        !hook::Remove(h_dropItem.ref.code, why)) return false;
    cofDropDependency = enabled; return true;
}
std::string Status() { return Line(g_gold) + "\n" + Line(g_drops); }

}  // namespace ep::loot
