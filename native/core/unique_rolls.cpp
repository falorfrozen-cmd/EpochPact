#include "unique_rolls.hpp"

#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "loot_crafting_rules.hpp"
#include "managed.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace ep::uniquerolls {

namespace {

using MethodInfo = const il2cpp::Method*;
using namespace managed;

struct Hook {
    const char* name;
    game::MethodRef ref;
    void* detour;
    void** original;
};

bool In(const Hook& h) { return h.ref && hook::IsInstalled(h.ref.code); }
bool Put(Hook& h, std::string* why) {
    if (!h.ref) { if (why) *why = std::string(h.name) + " was not found in this game build"; return false; }
    return hook::IsInstalled(h.ref.code) || hook::Install(h.ref.code, h.detour, h.original, why);
}
bool Take(Hook& h, std::string* why) { return !In(h) || hook::Remove(h.ref.code, why); }

constexpr int kUnique = 7, kSet = 8, kLegendary = 9;  // ItemData.rarity

std::atomic<double> g_percent{0};
std::atomic<uint64_t> g_items{0}, g_rolls{0}, g_faults{0};
thread_local int t_gambling = 0;  // inside one of the game's gambler rolls

game::MethodRef m_getUnique, m_maxIsBest, m_rebuild, m_setValues;
size_t o_rarity = 0, o_uniqueId = 0, o_uniqueRolls = 0, o_implicitRolls = 0, o_mods = 0, o_canRoll = 0, o_rollId = 0;

bool Applies(void* item) {
    if (!item || t_gambling > 0 || g_percent.load(std::memory_order_relaxed) <= 0 || !game::IsOfflinePlay()) return false;
    const int rarity = Get<uint8_t>(item, o_rarity);
    return rarity == kUnique || rarity == kSet || rarity == kLegendary;
}

// A managed byte[]: its bytes and how many.
uint8_t* Bytes(void* array, uintptr_t* n) {
    *n = array ? Get<uintptr_t>(array, 0x18) : 0;
    return array && *n > 0 && *n <= 255 ? static_cast<uint8_t*>(array) + 0x20 : nullptr;
}

template <class F>
void Guard(F&& f) {
    std::string why;
    bool failed = false;
    const bool ran = game::Guarded([&] {
        try {
            f();
        } catch (const std::exception& e) {
            failed = true;
            why = e.what();
        }
    }, &why);
    if ((!ran || failed) && g_faults.fetch_add(1, std::memory_order_relaxed) < 5) Log("uniqueroll: rolls left as rolled: %s", why.c_str());
}

// A finished Unique, Set or Legendary: each roll its rollable modifiers use (several can share one), toward the end
// the game calls best for it, and the implicits toward the higher end (the game keeps the higher implicit roll when
// it rerolls them, RerollImplicitRollsButKeepOldIfHigher).
void Raise(void* item) {
    const double percent = g_percent.load(std::memory_order_relaxed);
    uintptr_t n = 0;
    if (uint8_t* rolls = Bytes(Get<void*>(item, o_uniqueRolls), &n)) {
        auto id = Get<uint16_t>(item, o_uniqueId);
        void* a[]{&id};
        void* entry = Invoke(m_getUnique, nullptr, a);
        void* mods = entry ? Get<void*>(entry, o_mods) : nullptr;
        bool done[256]{};
        for (void* mod : mods ? Entries(mods) : std::vector<void*>{}) {
            if (!mod || !Get<bool>(mod, o_canRoll)) continue;
            int roll = Get<uint8_t>(mod, o_rollId);
            if (static_cast<uintptr_t>(roll) >= n || done[roll]) continue;
            done[roll] = true;
            void* b[]{mods, &roll};
            const bool maxIsBest = Value<bool>(m_maxIsBest, item, b);
            rolls[roll] = static_cast<uint8_t>(lootcraft::rules::MinimumRoll(rolls[roll], percent, maxIsBest));
            g_rolls.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (uint8_t* rolls = Bytes(Get<void*>(item, o_implicitRolls), &n))
        for (uintptr_t i = 0; i < n; ++i) rolls[i] = static_cast<uint8_t>(lootcraft::rules::MinimumRoll(rolls[i], percent, true));
    g_items.fetch_add(1, std::memory_order_relaxed);
}

// GenerateItems.RollBaseItem(ref ItemDataUnpacked item, 20 more) makes every generated item: it randomises the rolls
// before it picks the Unique (uniqueID is written after ItemData.randomiseUniqueRolls), so the rolls are raised once it
// returns. Every parameter is a pointer or fits in one (ints, bools, enums, Nullable<int>), so they pass through as is.
using RollBaseItemFn = void (*)(void* self, void** item, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                uintptr_t, MethodInfo m);
RollBaseItemFn o_rollBaseItem = nullptr;
void d_rollBaseItem(void* self, void** item, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7,
                    uintptr_t a8, uintptr_t a9, uintptr_t a10, uintptr_t a11, uintptr_t a12, uintptr_t a13, uintptr_t a14, uintptr_t a15, uintptr_t a16,
                    uintptr_t a17, uintptr_t a18, uintptr_t a19, uintptr_t a20, MethodInfo m) {
    o_rollBaseItem(self, item, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, m);
    if (item && Applies(*item)) Guard([&] { Raise(*item); });
}

// ItemData.RevealCocoonedUnique(Actor) -> bool turns a Weaver cocoon into its Unique and keeps the cocoon's rolls; a
// revealed Unique gets its rolls raised the same way, and its saved form rebuilt (the item is already the player's).
using RevealFn = bool (*)(void* item, void* actor, MethodInfo m);
RevealFn o_reveal = nullptr;
bool d_reveal(void* item, void* actor, MethodInfo m) {
    const bool revealed = o_reveal(item, actor, m);
    if (revealed && Applies(item)) Guard([&] {
        Raise(item);
        Invoke(m_rebuild, item);
        Invoke(m_setValues, item);
    });
    return revealed;
}

// The gamblers make their items through the same rolls; while one runs, its rolls are left alone.
struct Gambling {
    Gambling() { ++t_gambling; }
    ~Gambling() { --t_gambling; }
};
using Gamble4Fn = void* (*)(void* self, uintptr_t, uintptr_t, uintptr_t, uintptr_t, MethodInfo m);
using Gamble8Fn = void* (*)(void* self, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, MethodInfo m);
Gamble4Fn o_gambling = nullptr, o_soulGambling = nullptr;
Gamble8Fn o_factionGambling = nullptr;
void* d_gambling(void* self, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, MethodInfo m) { Gambling g; return o_gambling(self, a, b, c, d, m); }
void* d_soulGambling(void* self, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, MethodInfo m) { Gambling g; return o_soulGambling(self, a, b, c, d, m); }
void* d_factionGambling(void* self, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f, uintptr_t g2, uintptr_t h,
                        MethodInfo m) {
    Gambling g;
    return o_factionGambling(self, a, b, c, d, e, f, g2, h, m);
}

Hook h_rollBaseItem{"GenerateItems.RollBaseItem", {}, reinterpret_cast<void*>(&d_rollBaseItem), reinterpret_cast<void**>(&o_rollBaseItem)};
Hook h_reveal{"ItemData.RevealCocoonedUnique", {}, reinterpret_cast<void*>(&d_reveal), reinterpret_cast<void**>(&o_reveal)};
Hook h_gambling{"GenerateItems.rollItemForGambling", {}, reinterpret_cast<void*>(&d_gambling), reinterpret_cast<void**>(&o_gambling)};
Hook h_soulGambling{"GenerateItems.rollItemForSoulGambling", {}, reinterpret_cast<void*>(&d_soulGambling), reinterpret_cast<void**>(&o_soulGambling)};
Hook h_factionGambling{"GenerateItems.rollItemForFactionGambling", {}, reinterpret_cast<void*>(&d_factionGambling),
                       reinterpret_cast<void**>(&o_factionGambling)};
// The gambler guards go in first and come out last, so a gambler's roll is never boosted.
Hook* const kHooks[]{&h_gambling, &h_soulGambling, &h_factionGambling, &h_rollBaseItem, &h_reveal};

bool ready = false;

std::string Percent(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g%%", v);
    return buf;
}

}  // namespace

bool Init() {
    h_rollBaseItem.ref = game::FindMethod("LE.dll", "", "GenerateItems", "RollBaseItem", 21);
    h_reveal.ref = game::FindMethod("LE.dll", "", "ItemData", "RevealCocoonedUnique", 1);
    m_rebuild = game::FindMethod("LE.dll", "", "ItemData", "RebuildID", 0);
    m_setValues = game::FindMethod("LE.dll", "", "ItemData", "setValuesFromSerialisation", 0);
    h_gambling.ref = game::FindMethod("LE.dll", "", "GenerateItems", "rollItemForGambling", 4);
    h_soulGambling.ref = game::FindMethod("LE.dll", "", "GenerateItems", "rollItemForSoulGambling", 4);
    h_factionGambling.ref = game::FindMethod("LE.dll", "", "GenerateItems", "rollItemForFactionGambling", 8);
    m_getUnique = game::FindMethod("LE.dll", "", "UniqueList", "getUnique", 1);
    m_maxIsBest = game::FindMethod("LE.dll", "", "ItemData", "MaxRollIsBestForUniqueRollID", 2);
    o_rarity = game::FieldOffset("LE.dll", "", "ItemData", "rarity");
    o_uniqueId = game::FieldOffset("LE.dll", "", "ItemData", "uniqueID");
    o_uniqueRolls = game::FieldOffset("LE.dll", "", "ItemData", "uniqueRolls");
    o_implicitRolls = game::FieldOffset("LE.dll", "", "ItemData", "implicitRolls");
    o_mods = game::FieldOffset("LE.dll", "", "UniqueList.Entry", "mods");
    o_canRoll = game::FieldOffset("LE.dll", "", "UniqueItemMod", "canRoll");
    o_rollId = game::FieldOffset("LE.dll", "", "UniqueItemMod", "rollID");
    ready = true;
    for (const Hook* h : kHooks) ready = ready && h->ref;
    ready = ready && m_getUnique && m_maxIsBest && m_rebuild && m_setValues && o_rarity && o_uniqueId && o_uniqueRolls && o_implicitRolls && o_mods && o_canRoll && o_rollId;
    Log("uniqueroll: roll hooks %s", ready ? "found" : "missing");
    return ready;
}

std::string Set(double percent) {
    if (!lootcraft::rules::ValidMinimumRoll(percent)) return "uniqueroll: refused: the minimum roll must be a percent from 0 to 100";
    std::string why;
    if (percent == 0) {
        g_percent = 0;
        std::string failed;
        for (auto it = std::rbegin(kHooks); it != std::rend(kHooks); ++it)
            if (!Take(**it, &why)) failed += std::string(" ") + (*it)->name + ": " + why;
        if (!failed.empty()) return "uniqueroll: 0%, but a hook stays:" + failed;
        return "uniqueroll -> 0% (off; hooks removed)";
    }
    if (!ready) return "uniqueroll: refused: the game's unique roll methods were not found in this game build";
    if (!game::IsOfflinePlay()) return "uniqueroll: refused: " + game::GateText();
    std::vector<Hook*> fresh;
    for (Hook* h : kHooks) {
        if (In(*h)) continue;
        if (!Put(*h, &why)) {
            for (auto it = fresh.rbegin(); it != fresh.rend(); ++it) Take(**it, nullptr);
            return "uniqueroll: refused: " + why;
        }
        fresh.push_back(h);
    }
    g_percent = percent;
    return "uniqueroll -> " + Percent(percent);
}

std::string Status() {
    bool in = true;
    for (const Hook* h : kHooks) in = in && In(*h);
    char buf[200];
    std::snprintf(buf, sizeof buf, "uniqueroll: %s, hooks %s, items %llu, rolls %llu, faults %llu", Percent(g_percent.load()).c_str(), in ? "in" : "out",
                  static_cast<unsigned long long>(g_items.load()), static_cast<unsigned long long>(g_rolls.load()),
                  static_cast<unsigned long long>(g_faults.load()));
    return buf;
}

}  // namespace ep::uniquerolls
