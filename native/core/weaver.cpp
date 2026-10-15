#include "weaver.hpp"

#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "managed.hpp"
#include "weaver_rules.hpp"

#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace ep::weaver {

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

// xorshift64*, seeded per process; the atomic keeps two threads from reusing a state.
double Uniform() {
    static std::atomic<uint64_t> s{0x6A09E667F3BCC909ull};
    uint64_t x = s.fetch_add(0x9E3779B97F4A7C15ull, std::memory_order_relaxed);
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 27; x *= 0x94D049BB133111EBull; x ^= x >> 31;
    return (x >> 11) * (1.0 / 9007199254740992.0);
}

// ---- Weaver's Will Unique share: UniqueList.rollUnique(ref ushort uniqueID, byte maxLevelRequirement,
// SetRequirement, bool requireBaseType, byte baseType, bool requireSubType, byte subType, bool mustBeRandomDrop,
// bool mustBeEquippableByBaseClass, int classID, bool mustBeEquippableBySubclass, int subClassID, bool failOnReroll,
// bool requireOneOfMultipleBaseTypes, List<byte> acceptableBaseTypes, bool requireLegendaryType,
// LegendaryType requiredLegendaryType, ItemRollModifiers) -> bool. Every argument is passed through in its slot.

std::atomic<double> g_share{0};
std::atomic<uint64_t> g_rolls{0}, g_asked{0}, g_converted{0};

using RollUniqueFn = bool (*)(uint16_t*, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                              uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, void*, uintptr_t, uintptr_t, void*, MethodInfo);
RollUniqueFn o_rollUnique = nullptr;

bool d_rollUnique(uint16_t* id, uintptr_t maxLevel, uintptr_t setRequirement, uintptr_t requireBase, uintptr_t base,
                  uintptr_t requireSub, uintptr_t sub, uintptr_t randomDrop, uintptr_t baseClass, uintptr_t classId,
                  uintptr_t subclass, uintptr_t subclassId, uintptr_t failOnReroll, uintptr_t multipleBases, void* bases,
                  uintptr_t requireType, uintptr_t type, void* modifiers, MethodInfo m) {
    const double share = g_share.load(std::memory_order_relaxed);
    if (share > 0 && (requireType & 0xFF) == 0 && game::IsOfflinePlay()) {
        g_rolls.fetch_add(1, std::memory_order_relaxed);
        if (rules::ForceWeaver(share, Uniform())) {
            g_asked.fetch_add(1, std::memory_order_relaxed);
            // The same request, for a Weaver's Will Unique; when none fits, the game's own roll follows.
            if (o_rollUnique(id, maxLevel, setRequirement, requireBase, base, requireSub, sub, randomDrop, baseClass, classId, subclass,
                             subclassId, failOnReroll, multipleBases, bases, 1, 1, modifiers, m)) {
                g_converted.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }
    }
    return o_rollUnique(id, maxLevel, setRequirement, requireBase, base, requireSub, sub, randomDrop, baseClass, classId, subclass,
                        subclassId, failOnReroll, multipleBases, bases, requireType, type, modifiers, m);
}

Hook h_rollUnique{"UniqueList.rollUnique", {}, reinterpret_cast<void*>(&d_rollUnique), reinterpret_cast<void**>(&o_rollUnique)};

// ---- Upgrade speed: CharacterMutator.UpgradeRandomWeaversWillItem(), called by the game the frame after a
// nearby kill won its roll (at most once per 15 s); each call spends 1 Weaver's Will on one equipped item.

std::atomic<double> g_speed{1};
std::atomic<uint64_t> g_grants{0}, g_runs{0};

using UpgradeFn = void (*)(void* mutator, MethodInfo m);
UpgradeFn o_upgrade = nullptr;

void d_upgrade(void* mutator, MethodInfo m) {
    const double speed = g_speed.load(std::memory_order_relaxed);
    const int calls = speed > 1 && game::IsOfflinePlay() ? rules::UpgradeCalls(speed, Uniform()) : 1;
    g_grants.fetch_add(1, std::memory_order_relaxed);
    for (int i = 0; i < calls; ++i) {
        o_upgrade(mutator, m);
        g_runs.fetch_add(1, std::memory_order_relaxed);
    }
}

Hook h_upgrade{"CharacterMutator.UpgradeRandomWeaversWillItem", {}, reinterpret_cast<void*>(&d_upgrade), reinterpret_cast<void**>(&o_upgrade)};

// ---- Wanted affixes: ItemData.SpendWeaversWillToUpgrade(ref List<Stat>, ref bool addedNewAffix, ref int affixID)
// either raises one affix below T7 by a tier (a random pick among them) or adds a new affix (a weighted pick from
// ItemData.GetEligibleAffixes). While it runs, unwanted affixes below T7 are held at T7 so the game's pick can only
// fall on a wanted one, and the eligible list it is given keeps only the wanted affixes that fit the item. The real
// tiers are written back by affix id and the item's serialised data is rebuilt.

std::vector<int> g_wanted;  // main thread only: set inside managed::Run, read by the detours on the main thread
std::atomic<std::size_t> g_wantedCount{0};  // for Status() on the command thread
bool g_spending = false;
std::atomic<uint64_t> g_spends{0}, g_steered{0}, g_filtered{0}, g_unfiltered{0}, g_faults{0};
size_t g_itemAffixes = 0, g_affixId = 0, g_affixTier = 0, g_listAffixId = 0;
game::MethodRef m_rebuild, m_setValues;

using SpendFn = void (*)(void* item, void* stats, void* added, void* affixId, MethodInfo m);
using EligibleFn = void* (*)(void* result, void* item, void* already, int32_t special, MethodInfo m);
SpendFn o_spend = nullptr;
EligibleFn o_eligible = nullptr;

uint8_t& Tier(void* affix) { return *reinterpret_cast<uint8_t*>(static_cast<char*>(affix) + g_affixTier); }
int AffixId(void* affix) { return *reinterpret_cast<uint16_t*>(static_cast<char*>(affix) + g_affixId); }

std::vector<void*> ItemAffixes(void* item) {
    void* list = *reinterpret_cast<void**>(static_cast<char*>(item) + g_itemAffixes);
    std::vector<void*> out;
    if (list) for (void* a : Entries(list)) if (a) out.push_back(a);
    return out;
}

struct Held {
    void* item = nullptr;
    std::vector<std::pair<int, uint8_t>> tiers;  // affix id -> its real tier
    ~Held() {
        if (tiers.empty()) return;
        if (!game::Guarded([&] {
                for (void* a : ItemAffixes(item))
                    for (const auto& [id, tier] : tiers)
                        if (AffixId(a) == id) Tier(a) = tier;
                reinterpret_cast<void (*)(void*, MethodInfo)>(m_rebuild.code)(item, m_rebuild.info);
                reinterpret_cast<void (*)(void*, MethodInfo)>(m_setValues.code)(item, m_setValues.info);
            }, nullptr))
            g_faults.fetch_add(1, std::memory_order_relaxed);
    }
};

void d_spend(void* item, void* stats, void* added, void* affixId, MethodInfo m) {
    if (g_wanted.empty() || !item || !game::IsOfflinePlay()) { o_spend(item, stats, added, affixId, m); return; }
    g_spends.fetch_add(1, std::memory_order_relaxed);
    Held held;
    held.item = item;
    if (!game::Guarded([&] {
            const auto affixes = ItemAffixes(item);
            std::vector<rules::Affix> plain;
            for (void* a : affixes) plain.push_back({AffixId(a), Tier(a)});
            for (std::size_t i : rules::HeldForUpgrade(plain, g_wanted)) {
                held.tiers.push_back({plain[i].id, static_cast<uint8_t>(plain[i].tier)});
                Tier(affixes[i]) = rules::TopTier;
            }
        }, nullptr))
        g_faults.fetch_add(1, std::memory_order_relaxed);
    if (!held.tiers.empty()) g_steered.fetch_add(1, std::memory_order_relaxed);
    struct Flag { Flag() { g_spending = true; } ~Flag() { g_spending = false; } } flag;
    o_spend(item, stats, added, affixId, m);
}

void* d_eligible(void* result, void* item, void* already, int32_t special, MethodInfo m) {
    void* r = o_eligible(result, item, already, special, m);
    if (!g_spending || g_wanted.empty() || !result) return r;
    if (!game::Guarded([&] {
            // Two fresh lists (prefixes, suffixes) made for this call.
            void** lists = static_cast<void**>(result);
            std::vector<int> ids;
            std::vector<std::pair<void*, int>> where;
            for (int k = 0; k < 2; ++k) {
                if (!lists[k]) continue;
                const auto entries = Entries(lists[k]);
                for (std::size_t i = 0; i < entries.size(); ++i) {
                    ids.push_back(entries[i] ? *reinterpret_cast<int*>(static_cast<char*>(entries[i]) + g_listAffixId) : -1);
                    where.push_back({lists[k], static_cast<int>(i)});
                }
            }
            const auto keep = rules::KeepEligible(ids, g_wanted);
            bool removed = false;
            for (std::size_t j = ids.size(); j-- > 0;) {
                if (keep[j]) continue;
                int index = where[j].second;
                void* args[]{&index};
                Invoke(Method(where[j].first, "RemoveAt", 1), where[j].first, args);
                removed = true;
            }
            (removed ? g_filtered : g_unfiltered).fetch_add(1, std::memory_order_relaxed);
        }, nullptr))
        g_faults.fetch_add(1, std::memory_order_relaxed);
    return r;
}

Hook h_spend{"ItemData.SpendWeaversWillToUpgrade", {}, reinterpret_cast<void*>(&d_spend), reinterpret_cast<void**>(&o_spend)};
Hook h_eligible{"ItemData.GetEligibleAffixes", {}, reinterpret_cast<void*>(&d_eligible), reinterpret_cast<void**>(&o_eligible)};

bool ready = false;

std::string Number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

std::string ReadNow();

std::string WantedJson() {
    std::string out = "[";
    for (std::size_t i = 0; i < g_wanted.size(); ++i) out += (i ? "," : "") + std::to_string(g_wanted[i]);
    return out + "]";
}

}  // namespace

bool Init() {
    h_rollUnique.ref = game::FindMethod("LE.dll", "", "UniqueList", "rollUnique", 18);
    h_upgrade.ref = game::FindMethod("LE.dll", "", "CharacterMutator", "UpgradeRandomWeaversWillItem", 0);
    h_spend.ref = game::FindMethod("LE.dll", "", "ItemData", "SpendWeaversWillToUpgrade", 3);
    h_eligible.ref = game::FindMethod("LE.dll", "", "ItemData", "GetEligibleAffixes", 2);
    m_rebuild = game::FindMethod("LE.dll", "", "ItemData", "RebuildID", 0);
    m_setValues = game::FindMethod("LE.dll", "", "ItemData", "setValuesFromSerialisation", 0);
    g_itemAffixes = game::FieldOffset("LE.dll", "", "ItemData", "affixes");
    g_affixId = game::FieldOffset("LE.dll", "", "ItemAffix", "affixId");
    g_affixTier = game::FieldOffset("LE.dll", "", "ItemAffix", "affixTier");
    g_listAffixId = game::FieldOffset("LE.dll", "", "AffixList.Affix", "affixId");
    ready = h_spend.ref && h_eligible.ref && m_rebuild && m_setValues && g_itemAffixes && g_affixId && g_affixTier && g_listAffixId;
    Log("weaver: share hook %s, upgrade hook %s, affix hooks %s", h_rollUnique.ref ? "found" : "missing",
        h_upgrade.ref ? "found" : "missing", ready ? "found" : "missing");
    return ready;
}

std::string SetShare(double percent) {
    if (!rules::ValidShare(percent)) return "weaversdrop: refused: the share must be a percent from 0 to 100";
    std::string why;
    if (percent == 0) {
        g_share = 0;
        if (!Take(h_rollUnique, &why)) return "weaversdrop: 0%, but the hook stays: " + why;
        return "weaversdrop -> 0% (off; hook removed)";
    }
    if (!game::IsOfflinePlay()) return "weaversdrop: refused: " + game::GateText();
    if (!Put(h_rollUnique, &why)) return "weaversdrop: refused: " + why;
    g_share = percent;
    return "weaversdrop -> " + Number(percent) + "%";
}

std::string SetSpeed(double multiplier) {
    if (!rules::ValidSpeed(multiplier)) return "weaversupgrades: refused: the multiplier must be between 1 and 10";
    std::string why;
    if (multiplier == 1) {
        g_speed = 1;
        if (!Take(h_upgrade, &why)) return "weaversupgrades: x1, but the hook stays: " + why;
        return "weaversupgrades -> x1 (off; hook removed)";
    }
    if (!game::IsOfflinePlay()) return "weaversupgrades: refused: " + game::GateText();
    if (!Put(h_upgrade, &why)) return "weaversupgrades: refused: " + why;
    g_speed = multiplier;
    return "weaversupgrades -> x" + Number(multiplier);
}

std::string SetAffixes(const std::string& value) {
    return Run([=] {
        if (!ready) throw std::runtime_error("Weaver's Will affix hooks are unavailable in this game build");
        std::vector<int> next;
        if (value != "none") {
            std::set<int> known;
            Root data(Invoke(game::FindMethod("LE.dll", "", "AffixList", "get", 0)));
            Root all(Invoke(Method(data.Get(), "get_AllAffixes", 0), data.Get()));
            for (void* a : Entries(all.Get())) if (a) known.insert(*reinterpret_cast<int*>(static_cast<char*>(a) + g_listAffixId));
            if (value.empty() || value.back() == ',') throw std::runtime_error("invalid affix IDs");
            std::istringstream in(value);
            std::string part;
            while (std::getline(in, part, ',')) {
                int id = 0;
                const auto [end, ec] = std::from_chars(part.data(), part.data() + part.size(), id);
                if (ec != std::errc{} || end != part.data() + part.size() || !known.contains(id))
                    throw std::runtime_error("unknown affix ID: " + part);
                if (!rules::Wanted(next, id)) next.push_back(id);
                if (static_cast<int>(next.size()) > rules::MaximumAffixes) throw std::runtime_error("choose at most 64 affixes");
            }
        }
        std::string why;
        if (next.empty()) {
            g_wanted.clear();
            g_wantedCount = 0;
            if (!Take(h_spend, &why) || !Take(h_eligible, &why)) throw std::runtime_error("a hook stays: " + why);
        } else {
            if (!game::IsOfflinePlay()) throw std::runtime_error(game::GateText());
            if (!Put(h_eligible, &why) || !Put(h_spend, &why)) {
                Take(h_spend, nullptr); Take(h_eligible, nullptr);
                throw std::runtime_error("the hooks did not go in: " + why);
            }
            g_wanted = next;
            g_wantedCount = next.size();
        }
        return ReadNow();
    });
}

std::string Read() { return Run([] { return ReadNow(); }); }

namespace {
std::string ReadNow() {
    std::ostringstream out;
    out << "{\"ok\":true,\"ready\":" << Boolean(ready)
        << ",\"share\":{\"percent\":" << Number(g_share.load()) << ",\"hook\":" << Boolean(In(h_rollUnique))
        << ",\"uniqueRolls\":" << g_rolls.load() << ",\"asked\":" << g_asked.load() << ",\"converted\":" << g_converted.load() << '}'
        << ",\"speed\":{\"multiplier\":" << Number(g_speed.load()) << ",\"hook\":" << Boolean(In(h_upgrade))
        << ",\"grants\":" << g_grants.load() << ",\"upgrades\":" << g_runs.load() << '}'
        << ",\"affixes\":{\"ids\":" << WantedJson() << ",\"hooks\":" << Boolean(In(h_spend) && In(h_eligible))
        << ",\"spends\":" << g_spends.load() << ",\"upgradesSteered\":" << g_steered.load()
        << ",\"addsFiltered\":" << g_filtered.load() << ",\"addsUnfiltered\":" << g_unfiltered.load()
        << ",\"faults\":" << g_faults.load() << "}}";
    return out.str();
}
}  // namespace

std::string Status() {
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "weaversdrop: %s%%, hook %s, unique rolls %llu, asked %llu, converted %llu\n"
                  "weaversupgrades: x%s, hook %s, grants %llu, upgrades %llu\n"
                  "weaveraffixes: %zu wanted, hooks %s, spends %llu, upgrades steered %llu, adds filtered %llu, faults %llu",
                  Number(g_share.load()).c_str(), In(h_rollUnique) ? "in" : "out", static_cast<unsigned long long>(g_rolls.load()),
                  static_cast<unsigned long long>(g_asked.load()), static_cast<unsigned long long>(g_converted.load()),
                  Number(g_speed.load()).c_str(), In(h_upgrade) ? "in" : "out", static_cast<unsigned long long>(g_grants.load()),
                  static_cast<unsigned long long>(g_runs.load()), g_wantedCount.load(), In(h_spend) && In(h_eligible) ? "in" : "out",
                  static_cast<unsigned long long>(g_spends.load()), static_cast<unsigned long long>(g_steered.load()),
                  static_cast<unsigned long long>(g_filtered.load()), static_cast<unsigned long long>(g_faults.load()));
    return buf;
}

}  // namespace ep::weaver
