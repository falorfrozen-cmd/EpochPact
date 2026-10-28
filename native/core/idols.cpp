#include "idols.hpp"

#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "managed.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ep::idols {

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
    static std::atomic<uint64_t> s{0x3C6EF372FE94F82Bull};
    uint64_t x = s.fetch_add(0x9E3779B97F4A7C15ull, std::memory_order_relaxed);
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 27; x *= 0x94D049BB133111EBull; x ^= x >> 31;
    return (x >> 11) * (1.0 / 9007199254740992.0);
}
int Below(int n) { return (std::min)(n - 1, static_cast<int>(Uniform() * n)); }

constexpr int kFirstIdol = 25, kLastIdol = 33;  // EquipmentType: the idols (altars are 41)
constexpr int kMagic = 2;                       // every non-unique idol comes out Magic
constexpr int kPrefix = 0, kSuffix = 1;         // AffixList.AffixType
constexpr int kStandard = 0;                    // AffixList.SpecialAffixType
constexpr int kMaximum = 3;

std::atomic<int> g_prefixes{1}, g_suffixes{1};
std::atomic<uint64_t> g_idols{0}, g_added{0}, g_faults{0};

game::MethodRef m_isValid, m_weighting, m_rollTier, m_already, m_rebuild, m_setValues, m_itemList, m_equipment, m_isWeaver,
    m_isHeretical, m_affixList, m_allAffixes, m_affixCtor;
const il2cpp::Class* g_itemAffix = nullptr;
size_t o_itemType = 0, o_subType = 0, o_rarity = 0, o_affixes = 0, o_affixType = 0, o_affixSpecial = 0, o_listId = 0, o_listType = 0,
       o_listSpecial = 0, o_canRollOn = 0;

// A List<EquipmentType> (int values) holds the value.
bool Holds(void* list, int value) {
    if (!list) return false;
    const auto* cls = il2cpp::api().object_get_class(list);
    const auto* size = il2cpp::api().class_get_field_from_name(cls, "_size");
    const auto* items = il2cpp::api().class_get_field_from_name(cls, "_items");
    if (!size || !items) return false;
    const int n = Get<int>(list, il2cpp::api().field_get_offset(size));
    void* array = Get<void*>(list, il2cpp::api().field_get_offset(items));
    if (n < 0 || !array || static_cast<uintptr_t>(n) > Get<uintptr_t>(array, 0x18)) return false;
    for (int i = 0; i < n; ++i)
        if (Get<int>(array, 0x20 + 4 * i) == value) return true;
    return false;
}

// What one idol needs while affixes are added to it.
struct Idol {
    void* generator;
    void* item;
    int ilvl, context, type;
    void* modifiers;
    void* equipment = nullptr;
    float effect = 1;
    void* existing = nullptr;  // List<int>: the affix IDs already on the idol
};

bool Valid(const Idol& d, void* affix) {
    int ilvl = d.ilvl, none = 0;
    bool idol = true, no = false;
    void* a[]{affix, d.item, d.equipment, &ilvl, d.existing, &none, &none, &idol, &no, &no, &no};
    return Value<bool>(m_isValid, d.generator, a);
}

// One more affix of the kind (prefix or suffix), picked as the game picks; false when none is left.
bool AddOne(const Idol& d, const std::vector<void*>& all, int kind) {
    std::vector<void*> pool;
    std::vector<float> weights;
    float total = 0;
    for (void* affix : all) {
        if (!affix || Get<int>(affix, o_listType) != kind || Get<int>(affix, o_listSpecial) != kStandard ||
            !Holds(Get<void*>(affix, o_canRollOn), d.type) || !Valid(d, affix))
            continue;
        int context = d.context;
        void* a[]{affix, d.item, &context, d.modifiers};
        const float w = Value<float>(m_weighting, d.generator, a);
        if (!(w > 0) || !std::isfinite(w)) continue;
        pool.push_back(affix);
        weights.push_back(w);
        total += w;
    }
    if (pool.empty()) return false;
    float pick = static_cast<float>(Uniform()) * total;
    size_t chosen = pool.size() - 1;
    for (size_t i = 0; i < pool.size(); ++i) {
        if (pick < weights[i]) { chosen = i; break; }
        pick -= weights[i];
    }
    void* affix = pool[chosen];
    int ilvl = d.ilvl, rarity = kMagic, context = d.context;
    bool no = false;
    float effect = d.effect;
    void* t[]{d.item, affix, &ilvl, &no, &rarity, &effect, &context, d.modifiers};
    const int tier = Value<int>(m_rollTier, d.generator, t);
    int id = Get<int>(affix, o_listId);
    uint16_t affixId = static_cast<uint16_t>(id);
    uint8_t affixTier = static_cast<uint8_t>((std::max)(0, tier)), roll = static_cast<uint8_t>(Below(256)), itemType = static_cast<uint8_t>(d.type);
    int sealed = 0;
    Root made(il2cpp::api().object_new(g_itemAffix));
    void* c[]{&affixId, &affixTier, &roll, &itemType, &sealed};
    Invoke(m_affixCtor, made.Get(), c);
    void* list = Get<void*>(d.item, o_affixes);
    void* add[]{made.Get()};
    Invoke(Method(list, "Add", 1), list, add);
    void* known[]{&id};
    Invoke(Method(d.existing, "Add", 1), d.existing, known);
    return true;
}

void AddAffixes(void* generator, void* item, int ilvl, int context, void* modifiers, int prefixes, int suffixes) {
    const int type = Get<uint8_t>(item, o_itemType);
    if (type < kFirstIdol || type > kLastIdol || Get<uint8_t>(item, o_rarity) != kMagic) return;
    void* a[]{item};
    if (Value<bool>(m_isWeaver, nullptr, a) || Value<bool>(m_isHeretical, nullptr, a)) return;
    // the idol's own affixes: what it has of each kind now
    int havePrefixes = 0, haveSuffixes = 0;
    for (void* affix : Entries(Get<void*>(item, o_affixes))) {
        if (!affix || Get<int>(affix, o_affixSpecial) != kStandard) continue;
        const int kind = Get<int>(affix, o_affixType);
        havePrefixes += kind == kPrefix;
        haveSuffixes += kind == kSuffix;
    }
    const int wantPrefixes = 1 + Below(prefixes), wantSuffixes = 1 + Below(suffixes);
    if (havePrefixes >= wantPrefixes && haveSuffixes >= wantSuffixes) return;
    Idol d{generator, item, ilvl, context, type, modifiers};
    Root types(Invoke(m_itemList));
    int t = type, sub = Get<uint16_t>(item, o_subType);
    void* e[]{&t, &sub, &d.effect};
    d.equipment = Invoke(m_equipment, types.Get(), e);
    if (!d.equipment) return;
    int freePrefixes = 0, freeSuffixes = 0;
    void* f[]{&freePrefixes, &freeSuffixes};
    Root existing(Invoke(m_already, item, f));
    d.existing = existing.Get();
    if (!d.existing) return;
    Root catalog(Invoke(m_affixList));
    Root everything(Invoke(m_allAffixes, catalog.Get()));
    const auto all = Entries(everything.Get());
    int added = 0;
    for (; havePrefixes < wantPrefixes && AddOne(d, all, kPrefix); ++havePrefixes) ++added;
    for (; haveSuffixes < wantSuffixes && AddOne(d, all, kSuffix); ++haveSuffixes) ++added;
    if (!added) return;
    Invoke(m_rebuild, item);
    Invoke(m_setValues, item);
    g_idols.fetch_add(1, std::memory_order_relaxed);
    g_added.fetch_add(added, std::memory_order_relaxed);
}

// ---- GenerateItems.GenerateAffixes(ref ItemDataUnpacked item, int ilvl, bool canRollUncraftableTiers,
// GenerationContext, ref FactionID? forcedFaction, bool forceExalted, ItemRollModifiers,
// List<ushort> forbiddenUniqueDroppableAffixes, bool forcePotentialConversion). Every argument is passed through.

enum Context { kShop = 1, kGambler = 2, kSoulGambler = 3 };

using GenerateAffixesFn = void (*)(void*, void**, uintptr_t, uintptr_t, uintptr_t, void*, uintptr_t, void*, void*, uintptr_t, MethodInfo);
GenerateAffixesFn o_generateAffixes = nullptr;

void d_generateAffixes(void* self, void** item, uintptr_t ilvl, uintptr_t uncraftable, uintptr_t context, void* faction, uintptr_t forceExalted,
                       void* modifiers, void* forbidden, uintptr_t conversion, MethodInfo m) {
    o_generateAffixes(self, item, ilvl, uncraftable, context, faction, forceExalted, modifiers, forbidden, conversion, m);
    const int prefixes = g_prefixes.load(std::memory_order_relaxed), suffixes = g_suffixes.load(std::memory_order_relaxed);
    const int where = static_cast<int>(static_cast<uint32_t>(context));
    if ((prefixes <= 1 && suffixes <= 1) || !self || !item || !*item || where == kShop || where == kGambler || where == kSoulGambler ||
        !game::IsOfflinePlay())
        return;
    std::string why;
    bool failed = false;
    const bool ran = game::Guarded([&] {
        try {
            AddAffixes(self, *item, static_cast<int>(static_cast<uint32_t>(ilvl)), where, modifiers, prefixes, suffixes);
        } catch (const std::exception& e) {
            failed = true;
            why = e.what();
        }
    }, &why);
    if (!ran || failed) {
        if (g_faults.fetch_add(1, std::memory_order_relaxed) < 5) Log("idols: affixes not added: %s", why.c_str());
    }
}

Hook h_generate{"GenerateItems.GenerateAffixes", {}, reinterpret_cast<void*>(&d_generateAffixes), reinterpret_cast<void**>(&o_generateAffixes)};

bool ready = false;

std::string Set(const char* cmd, std::atomic<int>& slot, double n) {
    if (!(n >= 1 && n <= kMaximum) || n != std::floor(n)) return std::string(cmd) + ": refused: the number must be 1, 2 or 3";
    const int value = static_cast<int>(n);
    std::string why;
    if (value == 1) {
        slot = 1;
        if (g_prefixes.load() == 1 && g_suffixes.load() == 1 && !Take(h_generate, &why)) return std::string(cmd) + ": x1, but the hook stays: " + why;
        return std::string(cmd) + " -> x1 (the game's own)";
    }
    if (!ready) return std::string(cmd) + ": refused: the game's affix methods were not found in this game build";
    if (!game::IsOfflinePlay()) return std::string(cmd) + ": refused: " + game::GateText();
    if (!Put(h_generate, &why)) return std::string(cmd) + ": refused: " + why;
    slot = value;
    return std::string(cmd) + " -> x" + std::to_string(value);
}

}  // namespace

bool Init() {
    h_generate.ref = game::FindMethod("LE.dll", "", "GenerateItems", "GenerateAffixes", 9);
    m_isValid = game::FindMethod("LE.dll", "", "GenerateItems", "IsValidAffix", 11);
    m_weighting = game::FindMethod("LE.dll", "", "GenerateItems", "AffixWeighting", 4);
    m_rollTier = game::FindMethod("LE.dll", "", "GenerateItems", "RollTier", 8);
    m_already = game::FindMethod("LE.dll", "", "ItemData", "AffixIDsAlreadyOnItem", 2);
    m_rebuild = game::FindMethod("LE.dll", "", "ItemData", "RebuildID", 0);
    m_setValues = game::FindMethod("LE.dll", "", "ItemData", "setValuesFromSerialisation", 0);
    m_itemList = game::FindMethod("LE.dll", "", "ItemList", "get", 0);
    m_equipment = game::FindMethod("LE.dll", "", "ItemList", "GetEquipmentSubItemAndAffixEffectModifier", 3);
    m_isWeaver = game::FindMethod("LE.dll", "", "ItemList", "IsWeaverIdol", 1);
    m_isHeretical = game::FindMethod("LE.dll", "", "ItemList", "IsHereticalIdol", 1);
    m_affixList = game::FindMethod("LE.dll", "", "AffixList", "get", 0);
    m_allAffixes = game::FindMethod("LE.dll", "", "AffixList", "get_AllAffixes", 0);
    m_affixCtor = game::FindMethod("LE.dll", "", "ItemAffix", ".ctor", 5);
    g_itemAffix = game::FindClass("LE.dll", "", "ItemAffix");
    o_itemType = game::FieldOffset("LE.dll", "", "ItemData", "itemType");
    o_subType = game::FieldOffset("LE.dll", "", "ItemData", "subType");
    o_rarity = game::FieldOffset("LE.dll", "", "ItemData", "rarity");
    o_affixes = game::FieldOffset("LE.dll", "", "ItemData", "affixes");
    o_affixType = game::FieldOffset("LE.dll", "", "ItemAffix", "affixType");
    o_affixSpecial = game::FieldOffset("LE.dll", "", "ItemAffix", "specialAffixType");
    o_listId = game::FieldOffset("LE.dll", "", "AffixList.Affix", "affixId");
    o_listType = game::FieldOffset("LE.dll", "", "AffixList.Affix", "type");
    o_listSpecial = game::FieldOffset("LE.dll", "", "AffixList.Affix", "specialAffixType");
    o_canRollOn = game::FieldOffset("LE.dll", "", "AffixList.Affix", "canRollOn");
    ready = h_generate.ref && m_isValid && m_weighting && m_rollTier && m_already && m_rebuild && m_setValues && m_itemList && m_equipment &&
            m_isWeaver && m_isHeretical && m_affixList && m_allAffixes && m_affixCtor && g_itemAffix && o_itemType && o_subType && o_rarity &&
            o_affixes && o_affixType && o_affixSpecial && o_listId && o_listType && o_listSpecial && o_canRollOn;
    Log("idols: affix hook %s", ready ? "found" : "missing");
    return ready;
}

std::string SetPrefixes(double n) { return Set("idolprefixes", g_prefixes, n); }
std::string SetSuffixes(double n) { return Set("idolsuffixes", g_suffixes, n); }

std::string Status() {
    char buf[320];
    std::snprintf(buf, sizeof buf, "idolprefixes: x%d, hook %s, idols %llu, affixes added %llu, faults %llu\nidolsuffixes: x%d", g_prefixes.load(),
                  In(h_generate) ? "in" : "out", static_cast<unsigned long long>(g_idols.load()), static_cast<unsigned long long>(g_added.load()),
                  static_cast<unsigned long long>(g_faults.load()), g_suffixes.load());
    return buf;
}

}  // namespace ep::idols
