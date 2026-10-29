#include "spawn_items.hpp"
#include "managed.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <sstream>

// Spawn items: the game's own item generator (GenerateItems.initialiseRandomItemData) makes the chosen
// base item, Unique or Set, and GroundItemManager.dropItemForPlayer drops it next to the character, as a
// monster drop would. Nothing is written to the save until the player picks the item up.
namespace ep::spawn {
namespace {
using namespace managed;
size_t F(const char* cls, const char* field, const char* ns = "") {
    static std::map<std::string, size_t> fields;
    const std::string key = std::string(ns) + ":" + cls + ":" + field;
    auto it = fields.find(key);
    if (it != fields.end()) return it->second;
    return fields.emplace(key, Offset(cls, field, ns)).first->second;
}
game::MethodRef M(const char* cls, const char* name, int arity, const char* ns = "", const char* image = "LE.dll") {
    const auto m = game::FindMethod(image, ns, cls, name, arity);
    if (!m) throw std::runtime_error(std::string("spawn method unavailable: ") + cls + "." + name);
    return m;
}
struct Vec3 { float x, y, z; };
struct Context { void* actor; std::string id, name; };
Context Current() {
    if (!game::IsOfflinePlay()) throw std::runtime_error(game::GateText());
    RequireSession(4);
    void* actor = Invoke(M("PlayerFinder", "getPlayerActor", 0));
    if (!actor || !game::IsAlive(actor)) throw std::runtime_error("Load an offline character first.");
    void* tracker = Get<void*>(actor, F("Actor", "characterDataTracker"));
    void* data = Get<void*>(tracker, F("CharacterDataTracker", "charData"));
    if (!Value<bool>(Method(data, "get_IsOffline", 0), data)) throw std::runtime_error("Offline character required.");
    return {actor, Text(Invoke(Method(data, "get_Id", 0), data)), Text(Get<void*>(data, F("CharacterData", "<CharacterName>k__BackingField", "LE.Data")))};
}
std::string Player(const Context& c) { return "{\"id\":" + Json(c.id) + ",\"name\":" + Json(c.name) + "}"; }
std::vector<void*> ArrayEntries(void* array) {
    if (!array) throw std::runtime_error("required managed array unavailable");
    const auto n = Get<uintptr_t>(array, 0x18);
    if (n > 1024) throw std::runtime_error("invalid managed array bounds");
    std::vector<void*> result;
    for (uintptr_t i = 0; i < n; ++i) result.push_back(Get<void*>(array, 0x20 + sizeof(void*) * i));
    return result;
}
// Gear and idols (EquipmentType 0-33) and idol altars (41); blessings and lenses are not ground items.
bool SpawnableType(int type) { return (type >= 0 && type <= 33) || type == 41; }
struct Base { int type, sub, level, maxAffixes; };
std::vector<Base> Bases(void* list) {
    std::vector<Base> bases;
    for (void* base : ArrayEntries(Get<void*>(list, F("ItemList", "EquippableItems")))) {
        if (!base || Get<bool>(base, F("ItemList.BaseItem", "obsoleteItemType"))) continue;
        const int type = Get<int>(base, F("ItemList.BaseItem", "baseTypeID"));
        if (!SpawnableType(type)) continue;
        const int maxAffixes = Get<int>(base, F("ItemList.BaseItem", "maximumAffixes"));
        for (void* sub : Entries(Get<void*>(base, F("ItemList.BaseEquipmentItem", "subItems")))) {
            if (!sub || Get<bool>(sub, F("ItemList.SubItem", "obsoleteItem")) || Get<bool>(sub, F("ItemList.SubItem", "isLegacySubType")) ||
                Value<bool>(Method(sub, "get_IsUniqueOnlyBase", 0), sub)) continue;
            bases.push_back({type, Get<int>(sub, F("ItemList.SubItem", "subTypeID")), Get<int>(sub, F("ItemList.SubItem", "levelRequirement")), maxAffixes});
        }
    }
    return bases;
}
bool Shown(void* unique) {
    return unique && !Get<bool>(unique, F("UniqueList.Entry", "hideFromPlayers")) && SpawnableType(Get<uint8_t>(unique, F("UniqueList.Entry", "baseType")));
}
std::string BaseName(void* types, int type) { void* a[]{&type}; return Text(Invoke(Method(types, "GetBaseTypeDisplayName", 1), types, a)); }
std::string ItemName(void* types, int type, int sub) { void* a[]{&type, &sub}; return Text(Invoke(Method(types, "GetItemDisplayName", 2), types, a)); }
bool Number(const std::string& s, int* out) {
    if (s.empty() || s.size() > 6) return false;
    int v = 0;
    for (char ch : s) { if (ch < '0' || ch > '9') return false; v = v * 10 + (ch - '0'); }
    *out = v;
    return true;
}
// The UI's rarity as the game's: Normal 0, Magic 2 and Rare 4 are numbers of affixes, never more than the
// base can have (idols fewer, altars none); Exalted is the Rare roll with an exalted affix forced in.
int GameRarity(int rarity, int maxAffixes) {
    static constexpr int wanted[]{0, 2, 4, 4};
    return std::clamp(wanted[rarity], 0, std::max(0, maxAffixes));
}

// What the player asked for, checked against the game's lists.
struct Request {
    int type = -1, sub = -1, uniqueId = -1, gameRarity = 0;
    bool exalted = false, weaver = false, set = false;
    void* entry = nullptr;  // the Unique's UniqueList.Entry (the game's list lives as long as the game)
    std::string name;
};
Request Resolve(void* types, const std::string& item, int rarity) {
    Request r;
    if (item.size() > 1 && item[0] == 'b' && item.find('.') != std::string::npos) {
        const auto dot = item.find('.');
        if (!Number(item.substr(1, dot - 1), &r.type) || !Number(item.substr(dot + 1), &r.sub)) throw std::runtime_error("Unknown item.");
        const auto bases = Bases(types);
        const auto base = std::find_if(bases.begin(), bases.end(), [&](const Base& b) { return b.type == r.type && b.sub == r.sub; });
        if (base == bases.end()) throw std::runtime_error("This item cannot be spawned.");
        r.gameRarity = GameRarity(rarity, base->maxAffixes);
        r.exalted = rarity == 3 && r.gameRarity == 4;
        r.name = ItemName(types, r.type, r.sub);
        return r;
    }
    if (item.size() > 1 && item[0] == 'u') {
        if (!Number(item.substr(1), &r.uniqueId) || r.uniqueId > 65535) throw std::runtime_error("Unknown item.");
        auto id = static_cast<uint16_t>(r.uniqueId);
        void* a[]{&id};
        void* entry = Invoke(M("UniqueList", "getUnique", 1), nullptr, a);
        if (!Shown(entry) || Get<uint16_t>(entry, F("UniqueList.Entry", "uniqueID")) != id) throw std::runtime_error("This item cannot be spawned.");
        r.entry = entry;
        r.set = Get<bool>(entry, F("UniqueList.Entry", "isSetItem"));
        r.weaver = Get<int>(entry, F("UniqueList.Entry", "legendaryType")) == 1;
        r.gameRarity = r.set ? 8 : 7;
        r.name = Text(Invoke(Method(entry, "getDisplayName", 0), entry));
        return r;
    }
    throw std::runtime_error("Unknown item.");
}
// One item from the character's own generator, as it comes out of it.
std::unique_ptr<Root> Generate(void* generator, const game::MethodRef& generate, const Request& r, int level) {
    bool no = false, forceExalted = r.exalted;
    int ilvl = level, location = 0, t = r.type, s = r.sub, rarity = r.gameRarity, sockets = 0, u = r.uniqueId, minimumLP = 0, rank = 0, legendaryType = 0, corrupt = 0;
    alignas(8) unsigned char faction[8]{}, replacement[8]{}, corruption[8]{}, effectiveness[8]{}, special[8]{}, minSpecial[8]{}, overrideLP[8]{};
    void* args[]{&no, &ilvl, &no, &location, &t, &s, &rarity, &sockets, &u, &forceExalted, &minimumLP, faction, &rank, &no, &legendaryType, &no,
                 replacement, corruption, &corrupt, effectiveness, nullptr, nullptr, special, &no, minSpecial, overrideLP};
    void* made = Invoke(generate, generator, args);
    return made ? std::make_unique<Root>(made) : nullptr;
}
// The game's own check: a Unique (not a Set, Legendary or idol) whose legendary type is Legendary Potential.
bool LegendaryPotentialItem(void* item) { return Value<bool>(Method(item, "isLegendaryPotentialItem", 0), item); }
// The level the game rolls a Unique's Legendary Potential against; 200 marks the Uniques that never get any
// (Cocooned items, Gambler's Fallacy, Avarice, ...); the others are 0-105 in 1.5.3.
constexpr int kNoLegendaryPotential = 200;
int LegendaryLevel(void* entry, int subType) {
    auto sub = static_cast<uint8_t>(subType);
    void* a[]{&sub};
    return Value<uint8_t>(Method(entry, "getEffectiveLevelForLegendaryPotential", 1), entry, a);
}
// lp 0-4 on a Legendary Potential Unique, then the saved form (ItemData.id) rebuilt from the item and read back,
// so what is dropped is exactly what the game saves when it is picked up.
void Finish(void* item, const Request& r, int lp) {
    if (r.entry && lp >= 0 && LegendaryPotentialItem(item) &&
        LegendaryLevel(r.entry, Get<uint16_t>(item, F("ItemData", "subType"))) < kNoLegendaryPotential)
        *reinterpret_cast<uint8_t*>(static_cast<char*>(item) + F("ItemData", "legendaryPotential")) = static_cast<uint8_t>(lp);
    Invoke(Method(item, "RebuildID", 0), item);
    Invoke(Method(item, "setValuesFromSerialisation", 0), item);
}
std::unique_ptr<Root> Make(void* generator, const game::MethodRef& generate, const Request& r, int level, int lp) {
    auto root = Generate(generator, generate, r, level);
    if (root) Finish(root->Get(), r, lp);
    return root;
}
std::string Describe(void* made) {
    void* affixes = Get<void*>(made, F("ItemData", "affixes"));
    std::ostringstream out;
    out << "{\"type\":" << static_cast<int>(Get<uint8_t>(made, F("ItemData", "itemType"))) << ",\"sub\":" << Get<uint16_t>(made, F("ItemData", "subType"))
        << ",\"rarity\":" << static_cast<int>(Get<uint8_t>(made, F("ItemData", "rarity"))) << ",\"uniqueId\":" << Get<uint16_t>(made, F("ItemData", "uniqueID"))
        << ",\"affixes\":" << (affixes ? Entries(affixes).size() : 0) << ",\"exalted\":" << Boolean(Value<bool>(Method(made, "isExaltedItem", 0), made))
        << ",\"lp\":" << static_cast<int>(Get<uint8_t>(made, F("ItemData", "legendaryPotential")))
        << ",\"weaversWill\":" << static_cast<int>(Get<uint8_t>(made, F("ItemData", "weaversWill"))) << '}';
    return out.str();
}
}  // namespace

std::string List() {
    return Run([] {
        const auto c = Current();
        Root types(Invoke(M("ItemList", "get", 0))), uniques(Invoke(M("UniqueList", "get", 0)));
        std::ostringstream out;
        out << "{\"ok\":true,\"player\":" << Player(c) << ",\"bases\":[";
        bool first = true;
        for (const auto& b : Bases(types.Get())) {
            if (!first) out << ',';
            first = false;
            out << "{\"type\":" << b.type << ",\"sub\":" << b.sub << ",\"name\":" << Json(ItemName(types.Get(), b.type, b.sub))
                << ",\"baseType\":" << Json(BaseName(types.Get(), b.type)) << ",\"level\":" << b.level << ",\"maxAffixes\":" << b.maxAffixes << '}';
        }
        out << "],\"uniques\":[";
        first = true;
        for (void* e : Entries(Get<void*>(uniques.Get(), F("UniqueList", "uniques")))) {
            if (!Shown(e)) continue;
            if (!first) out << ',';
            first = false;
            const int type = Get<uint8_t>(e, F("UniqueList.Entry", "baseType"));
            const bool set = Get<bool>(e, F("UniqueList.Entry", "isSetItem"));
            const bool weaver = Get<int>(e, F("UniqueList.Entry", "legendaryType")) == 1;
            // as isLegendaryPotentialItem and the level check above, without making the item (the level 200
            // marker is the Unique's own, whatever the subtype)
            const bool lp = !set && !weaver && !(type >= 25 && type <= 33) && LegendaryLevel(e, 0) < kNoLegendaryPotential;
            out << "{\"id\":" << Get<uint16_t>(e, F("UniqueList.Entry", "uniqueID")) << ",\"name\":" << Json(Text(Invoke(Method(e, "getDisplayName", 0), e)))
                << ",\"baseType\":" << Json(BaseName(types.Get(), type)) << ",\"set\":" << Boolean(set)
                << ",\"weaver\":" << Boolean(weaver) << ",\"lp\":" << Boolean(lp)
                << ",\"level\":" << static_cast<int>(Value<uint8_t>(Method(e, "getDefaultLevelRequirement", 0), e)) << '}';
        }
        if (first) throw std::runtime_error("The game's Unique list is not loaded yet.");
        return out.str() + "]}";
    });
}

std::string Spawn(const std::string& saveId, const std::string& item, int rarity, int lp, int level, int count) {
    return Run([=] {
        if (rarity < 0 || rarity > 3) throw std::runtime_error("Rarity must be 0 (Normal), 1 (Magic), 2 (Rare) or 3 (Exalted).");
        if (lp < -1 || lp > 4) throw std::runtime_error("Legendary Potential must be -1 (the game's roll) or 0 to 4.");
        if (level < 1 || level > 100) throw std::runtime_error("Item level must be between 1 and 100.");
        if (count < 1 || count > kMaximumCount) throw std::runtime_error("Count must be between 1 and " + std::to_string(kMaximumCount) + ".");
        const auto c = Current();
        if (c.id != saveId) throw std::runtime_error("The loaded character changed. Load the item list again.");
        Root types(Invoke(M("ItemList", "get", 0)));
        const Request request = Resolve(types.Get(), item, rarity);
        void* generator = Get<void*>(c.actor, F("Actor", "generateItems"));
        void* manager = game::StaticObject(game::FindStaticField("LE.dll", "", "GroundItemManager", "instance"));
        if (!generator || !manager) throw std::runtime_error("The game's item generator is not ready.");
        const auto generate = M("GenerateItems", "initialiseRandomItemData", 26);
        const auto drop = M("GroundItemManager", "dropItemForPlayer", 4);
        Root transform(Invoke(M("Component", "get_transform", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), c.actor));
        const Vec3 at = Value<Vec3>(M("Transform", "get_position", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), transform.Get());
        std::ostringstream out;
        out << "{\"ok\":true,\"player\":" << Player(c) << ",\"name\":" << Json(request.name) << ",\"items\":[";
        int spawned = 0;
        for (int i = 0; i < count; ++i) {
            const auto made = Make(generator, generate, request, level, lp);
            if (!made) break;
            // around the character, one step apart
            const double angle = count == 1 ? 0.0 : 6.283185307179586 * i / count;
            Vec3 p{at.x + static_cast<float>(1.5 * std::cos(angle)), at.y, at.z + static_cast<float>(1.5 * std::sin(angle))};
            bool sound = i == 0;
            void* dropArgs[]{c.actor, made->Get(), &p, &sound};
            Invoke(drop, manager, dropArgs);
            out << (spawned ? "," : "") << Describe(made->Get());
            ++spawned;
        }
        if (!spawned) throw std::runtime_error("The game did not generate the item.");
        out << "],\"spawned\":" << spawned << '}';
        return out.str();
    });
}

#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Check(int rarity, int lp, int from, int count) {
    return Run([=] {
        if (rarity < 0 || rarity > 3 || lp < -1 || lp > 4 || from < 0 || count < 1 || count > 200) throw std::runtime_error("use spawncheck rarity lp from count");
        const auto c = Current();
        Root types(Invoke(M("ItemList", "get", 0))), uniques(Invoke(M("UniqueList", "get", 0)));
        std::vector<std::string> items;
        for (const auto& b : Bases(types.Get())) items.push_back("b" + std::to_string(b.type) + "." + std::to_string(b.sub));
        for (void* e : Entries(Get<void*>(uniques.Get(), F("UniqueList", "uniques"))))
            if (Shown(e)) items.push_back("u" + std::to_string(Get<uint16_t>(e, F("UniqueList.Entry", "uniqueID"))));
        void* generator = Get<void*>(c.actor, F("Actor", "generateItems"));
        const auto generate = M("GenerateItems", "initialiseRandomItemData", 26);
        std::ostringstream out;
        out << "{\"ok\":true,\"total\":" << items.size() << ",\"results\":[";
        for (int i = from; i < from + count && i < static_cast<int>(items.size()); ++i) {
            const Request r = Resolve(types.Get(), items[i], rarity);
            std::string made = "null", generated = "null", facts;
            if (const auto root = Generate(generator, generate, r, 100)) {
                void* item = root->Get();
                generated = Describe(item);
                if (r.entry) {
                    auto sub = static_cast<uint8_t>(Get<uint16_t>(item, F("ItemData", "subType")));
                    void* a[]{&sub};
                    facts = ",\"lpItem\":" + std::string(Boolean(LegendaryPotentialItem(item))) +
                            ",\"effectiveLevel\":" + std::to_string(Value<uint8_t>(Method(r.entry, "getEffectiveLevelForLegendaryPotential", 1), r.entry, a)) +
                            ",\"overrideLevel\":" + Boolean(Get<bool>(r.entry, F("UniqueList.Entry", "overrideEffectiveLevelForLegendaryPotential"))) +
                            ",\"canDropAsLegendary\":" + Boolean(Get<bool>(r.entry, F("UniqueList.Entry", "canDropAsLegendary"))) +
                            ",\"cocooned\":" + Boolean(Get<bool>(r.entry, F("UniqueList.Entry", "isCocoonedItem"))) +
                            ",\"primordial\":" + Boolean(Get<bool>(r.entry, F("UniqueList.Entry", "isPrimordialItem")));
                }
                Finish(item, r, lp);
                made = Describe(item);
            }
            out << (i == from ? "" : ",") << "{\"item\":" << Json(items[i]) << ",\"wanted\":{\"rarity\":" << r.gameRarity << ",\"exalted\":" << Boolean(r.exalted)
                << "},\"generated\":" << generated << ",\"made\":" << made << facts << '}';
        }
        return out.str() + "]}";
    });
}
std::string IdolCheck(int count, int level) {
    return Run([=] {
        const auto c = Current();
        Root types(Invoke(M("ItemList", "get", 0))), catalog(Invoke(M("AffixList", "get", 0)));
        void* generator = Get<void*>(c.actor, F("Actor", "generateItems"));
        const auto generate = M("GenerateItems", "initialiseRandomItemData", 26);
        // each affix: kind, ID, tier, roll, special type, and the classes it belongs to (AffixList.ClassSpecificity flags)
        std::map<int, int> classes;
        const auto affixes = [&](void* item) {
            std::ostringstream o;
            o << '[';
            bool first = true;
            for (void* a : Entries(Get<void*>(item, F("ItemData", "affixes")))) {
                if (!a) continue;
                const int id = Get<uint16_t>(a, F("ItemAffix", "affixId"));
                if (!classes.count(id)) {
                    int arg = id;
                    void* g[]{&arg};
                    void* known = Invoke(Method(catalog.Get(), "GetAffix", 1), catalog.Get(), g);
                    classes[id] = known ? Get<int>(known, F("AffixList.Affix", "classSpecificity")) : -1;
                }
                o << (first ? "" : ",") << '[' << Get<int>(a, F("ItemAffix", "affixType")) << ',' << id << ','
                  << static_cast<int>(Get<uint8_t>(a, F("ItemAffix", "affixTier"))) << ',' << static_cast<int>(Get<uint8_t>(a, F("ItemAffix", "affixRoll")))
                  << ',' << Get<int>(a, F("ItemAffix", "specialAffixType")) << ',' << classes[id] << ']';
                first = false;
            }
            return o.str() + "]";
        };
        std::map<int, std::string> names;
        std::map<std::string, int> shapes;
        int made = 0, same = 0, repeats = 0;
        std::ostringstream out;
        out << "{\"ok\":true,\"items\":[";
        for (const auto& b : Bases(types.Get())) {
            if (b.type < 25 || b.type > 33) continue;
            const Request r = Resolve(types.Get(), "b" + std::to_string(b.type) + "." + std::to_string(b.sub), 1);
            for (int i = 0; i < count; ++i) {
                const auto root = Generate(generator, generate, r, level);
                if (!root) continue;
                void* item = root->Get();
                const std::string before = affixes(item);
                Invoke(Method(item, "RebuildID", 0), item);
                Invoke(Method(item, "setValuesFromSerialisation", 0), item);
                const std::string after = affixes(item);
                int prefixes = 0, suffixes = 0;
                std::set<int> ids;
                for (void* a : Entries(Get<void*>(item, F("ItemData", "affixes")))) {
                    if (!a) continue;
                    const int id = Get<uint16_t>(a, F("ItemAffix", "affixId")), kind = Get<int>(a, F("ItemAffix", "affixType"));
                    prefixes += kind == 0;
                    suffixes += kind == 1;
                    if (!ids.insert(id).second) ++repeats;
                    if (!names.count(id)) {
                        int arg = id;
                        void* a2[]{&arg};
                        names[id] = Text(Invoke(Method(catalog.Get(), "GetAffixName", 1), catalog.Get(), a2));
                    }
                }
                ++shapes[std::to_string(prefixes) + "p" + std::to_string(suffixes) + "s"];
                ++made;
                same += before == after;
                out << (made == 1 ? "" : ",") << "{\"t\":" << b.type << ",\"s\":" << b.sub << ",\"rarity\":" << static_cast<int>(Get<uint8_t>(item, F("ItemData", "rarity")))
                    << ",\"a\":" << after << ",\"same\":" << Boolean(before == after) << '}';
            }
        }
        out << "],\"made\":" << made << ",\"same\":" << same << ",\"repeats\":" << repeats << ",\"shapes\":{";
        bool first = true;
        for (const auto& [shape, n] : shapes) { out << (first ? "" : ",") << Json(shape) << ':' << n; first = false; }
        out << "},\"names\":{";
        first = true;
        for (const auto& [id, name] : names) { out << (first ? "" : ",") << '"' << id << "\":" << Json(name); first = false; }
        return out.str() + "}}";
    });
}
std::string ForgingCheck(int count, int level) {
    return Run([=] {
        const auto c = Current();
        Root types(Invoke(M("ItemList", "get", 0))), uniques(Invoke(M("UniqueList", "get", 0)));
        void* generator = Get<void*>(c.actor, F("Actor", "generateItems"));
        const auto generate = M("GenerateItems", "initialiseRandomItemData", 26);
        const auto roll = M("GenerateItems", "rollForgingPotential", 4);
        std::vector<Base> gear;
        for (const auto& b : Bases(types.Get())) if (b.type <= 24) gear.push_back(b);
        if (gear.empty()) throw std::runtime_error("no gear bases");
        const auto fp = [&](void* item) { return static_cast<int>(Get<uint8_t>(item, F("ItemData", "forgingPotential"))); };
        struct Stats { int n = 0, min = 999, max = -1, same = 0; long long sum = 0; std::map<int, int> kinds; };
        std::ostringstream out;
        out << "{\"ok\":true,\"rva\":\"0x" << std::hex
            << (reinterpret_cast<uintptr_t>(roll.code) - reinterpret_cast<uintptr_t>(GetModuleHandleW(L"GameAssembly.dll"))) << std::dec << "\",\"rarities\":[";
        // the game's generator, gear bases in turn: Forging Potential as made, and after the saved form is
        // rebuilt and read back (what a pick-up saves)
        std::vector<std::unique_ptr<Root>> samples;
        for (int rarity = 0; rarity <= 3; ++rarity) {
            Stats s;
            for (int i = 0; i < count; ++i) {
                const auto& b = gear[(i * 7 + rarity) % gear.size()];
                const Request r = Resolve(types.Get(), "b" + std::to_string(b.type) + "." + std::to_string(b.sub), rarity);
                auto root = Generate(generator, generate, r, level);
                if (!root) continue;
                void* item = root->Get();
                const int made = fp(item);
                Invoke(Method(item, "RebuildID", 0), item);
                Invoke(Method(item, "setValuesFromSerialisation", 0), item);
                const int saved = fp(item);
                ++s.n; s.sum += made; s.min = (std::min)(s.min, made); s.max = (std::max)(s.max, made); s.same += made == saved;
                ++s.kinds[Get<int>(item, F("ItemData", "forgingPotentialType"))];
                if (i == 0) samples.push_back(std::move(root));
            }
            out << (rarity ? "," : "") << "{\"rarity\":" << rarity << ",\"n\":" << s.n << ",\"min\":" << s.min << ",\"max\":" << s.max
                << ",\"mean\":" << (s.n ? static_cast<double>(s.sum) / s.n : 0) << ",\"same\":" << s.same << ",\"kinds\":{";
            bool first = true;
            for (const auto& [k, n] : s.kinds) { out << (first ? "" : ",") << '"' << k << "\":" << n; first = false; }
            out << "}}";
        }
        // above 63: the game's own setter, and the byte written directly and taken through the saved form
        out << "],\"wide\":[";
        for (size_t i = 0; i < samples.size(); ++i) {
            void* item = samples[i]->Get();
            int asked = 120;
            void* a[]{&asked};
            Invoke(Method(item, "SetForgingPotential", 1), item, a);
            const int setter = fp(item);
            out << (i ? "," : "") << "{\"setter120\":" << setter;
            for (int raw : {64, 120, 200, 255}) {
                *reinterpret_cast<uint8_t*>(static_cast<char*>(item) + F("ItemData", "forgingPotential")) = static_cast<uint8_t>(raw);
                Invoke(Method(item, "RebuildID", 0), item);
                Invoke(Method(item, "setValuesFromSerialisation", 0), item);
                out << ",\"saved" << raw << "\":" << fp(item);
            }
            out << '}';
        }
        // the roll itself on a Rare, once per GenerationContext (Normal 0 ... RageOfMorditas 5)
        out << "],\"contexts\":[";
        void* rare = samples.size() > 2 ? samples[2]->Get() : nullptr;
        for (int context = 0; context <= 5 && rare; ++context) {
            long long sum = 0;
            int high = 0;
            for (int i = 0; i < count; ++i) {
                int ilvl = level, ctx = context;
                bool uncraftable = false;
                void* a[]{rare, &ilvl, &uncraftable, &ctx};
                const int v = Value<int>(roll, nullptr, a);
                sum += v;
                high = (std::max)(high, v);
            }
            out << (context ? "," : "") << "{\"context\":" << context << ",\"mean\":" << static_cast<double>(sum) / count << ",\"max\":" << high << '}';
        }
        // Uniques and Sets have none
        out << "],\"uniques\":[";
        int shown = 0;
        for (void* e : Entries(Get<void*>(uniques.Get(), F("UniqueList", "uniques")))) {
            if (!Shown(e) || shown >= 6) continue;
            const Request r = Resolve(types.Get(), "u" + std::to_string(Get<uint16_t>(e, F("UniqueList.Entry", "uniqueID"))), 0);
            if (const auto root = Generate(generator, generate, r, level))
                out << (shown++ ? "," : "") << "{\"id\":" << r.uniqueId << ",\"set\":" << Boolean(r.set) << ",\"fp\":" << fp(root->Get()) << '}';
        }
        return out.str() + "]}";
    });
}
// Unique and Set rolls from the game's generator (and from the Soul Gambler): how good each roll is toward the end
// the game calls best (0-255), the implicit rolls, the saved form read back, and the modifiers whose best is the low end.
std::string UniqueCheck(int count, int level) {
    return Run([=] {
        const auto c = Current();
        Root types(Invoke(M("ItemList", "get", 0))), uniques(Invoke(M("UniqueList", "get", 0)));
        void* generator = Get<void*>(c.actor, F("Actor", "generateItems"));
        const auto generate = M("GenerateItems", "initialiseRandomItemData", 26);
        const auto maxIsBest = M("ItemData", "MaxRollIsBestForUniqueRollID", 2);
        const auto getValue = M("UniqueItemMod", "getValue", 1);
        std::ostringstream out;
        out << "{\"ok\":true,\"rva\":{";
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"GameAssembly.dll"));
        struct Named { const char* cls; const char* name; int arity; };
        bool comma = false;
        for (const Named& n : {Named{"ItemData", "randomiseUniqueRolls", 0}, Named{"ItemData", "randomiseImplicitRolls", 0},
                               Named{"ItemData", "MaxRollIsBestForUniqueRollID", 2}, Named{"ItemData", "SetAllUniqueRolls", 1},
                               Named{"ItemData", "RerollImplicitRollsButKeepOldIfHigher", 0}, Named{"GenerateItems", "rollItemForGambling", 4},
                               Named{"GenerateItems", "rollItemForSoulGambling", 4}, Named{"GenerateItems", "rollItemForFactionGambling", 8}}) {
            const auto m = game::FindMethod("LE.dll", "", n.cls, n.name, n.arity);
            out << (comma ? "," : "") << '"' << n.cls << '.' << n.name << "\":\"0x" << std::hex << (m ? reinterpret_cast<uintptr_t>(m.code) - base : 0) << std::dec << '"';
            comma = true;
        }
        out << '}';
        std::vector<void*> shown;
        for (void* e : Entries(Get<void*>(uniques.Get(), F("UniqueList", "uniques"))))
            if (Shown(e)) shown.push_back(e);
        if (shown.empty()) throw std::runtime_error("The game's Unique list is not loaded yet.");
        struct Stats { int items = 0, rolls = 0, perfect = 0, low = 255, implicits = 0, implicitPerfect = 0, implicitLow = 255, same = 0, values = 0;
                       long long sum = 0, implicitSum = 0; std::map<int, int> rarities; };
        std::set<std::string> lowBest;
        std::vector<std::string> misses;  // [uniqueID, rarity, rollID, roll, maxIsBest, rolls] below the best end (drops only)
        std::ostringstream examples, lowExamples;
        const auto bytes = [](void* array) {
            std::vector<int> v;
            if (array) for (uintptr_t i = 0, n = Get<uintptr_t>(array, 0x18); i < n && i < 255; ++i) v.push_back(Get<uint8_t>(array, 0x20 + i));
            return v;
        };
        // one item: its rolls toward the best end, its implicits, the saved form, and (first few) each modifier's value
        const auto measure = [&](void* item, void* entry, Stats& s, bool show) {
            ++s.items;
            ++s.rarities[Get<uint8_t>(item, F("ItemData", "rarity"))];
            const auto rolls = bytes(Get<void*>(item, F("ItemData", "uniqueRolls"))), implicit = bytes(Get<void*>(item, F("ItemData", "implicitRolls")));
            void* mods = Get<void*>(entry, F("UniqueList.Entry", "mods"));
            std::set<int> done;
            for (void* mod : Entries(mods)) {
                if (!mod || !Get<bool>(mod, F("UniqueItemMod", "canRoll"))) continue;
                int id = Get<uint8_t>(mod, F("UniqueItemMod", "rollID"));
                if (id >= static_cast<int>(rolls.size()) || !done.insert(id).second) continue;
                void* a[]{mods, &id};
                const bool best = Value<bool>(maxIsBest, item, a);
                const int q = best ? rolls[id] : 255 - rolls[id];
                if (q < 255 && show && misses.size() < 40)
                    misses.push_back("[" + std::to_string(Get<uint16_t>(item, F("ItemData", "uniqueID"))) + "," +
                                     std::to_string(Get<uint8_t>(item, F("ItemData", "rarity"))) + "," + std::to_string(id) + "," +
                                     std::to_string(rolls[id]) + "," + (best ? "1" : "0") + "," + std::to_string(rolls.size()) + "]");
                ++s.rolls; s.sum += q; s.perfect += q == 255; s.low = (std::min)(s.low, q);
                const std::string name = Text(Invoke(Method(entry, "getDisplayName", 0), entry));
                if (!best && lowBest.insert(name + "#" + std::to_string(id)).second && lowBest.size() <= 12)
                    lowExamples << (lowBest.size() > 1 ? "," : "") << "{\"unique\":" << Json(name) << ",\"rollId\":" << id << ",\"property\":"
                                << static_cast<int>(Get<uint8_t>(mod, F("UniqueItemMod", "property"))) << ",\"value\":" << Get<float>(mod, F("UniqueItemMod", "value"))
                                << ",\"maxValue\":" << Get<float>(mod, F("UniqueItemMod", "maxValue")) << ",\"type\":" << Get<int>(mod, F("UniqueItemMod", "type")) << '}';
            }
            for (int r : implicit) { ++s.implicits; s.implicitSum += r; s.implicitPerfect += r == 255; s.implicitLow = (std::min)(s.implicitLow, r); }
            if (show && s.values < 3) {
                examples << (s.values++ ? "," : "") << "{\"unique\":" << Json(Text(Invoke(Method(entry, "getDisplayName", 0), entry))) << ",\"mods\":[";
                bool first = true;
                for (void* mod : Entries(mods)) {
                    if (!mod || !Get<bool>(mod, F("UniqueItemMod", "canRoll"))) continue;
                    const int id = Get<uint8_t>(mod, F("UniqueItemMod", "rollID"));
                    if (id >= static_cast<int>(rolls.size())) continue;
                    auto roll = static_cast<uint8_t>(rolls[id]);
                    void* g[]{&roll};
                    examples << (first ? "" : ",") << "[" << id << ',' << rolls[id] << ',' << Get<float>(mod, F("UniqueItemMod", "value")) << ','
                             << Get<float>(mod, F("UniqueItemMod", "maxValue")) << ',' << Value<float>(getValue, mod, g) << ']';
                    first = false;
                }
                examples << "]}";
            }
            Invoke(Method(item, "RebuildID", 0), item);
            Invoke(Method(item, "setValuesFromSerialisation", 0), item);
            s.same += rolls == bytes(Get<void*>(item, F("ItemData", "uniqueRolls"))) && implicit == bytes(Get<void*>(item, F("ItemData", "implicitRolls")));
        };
        const auto line = [](const Stats& s) {
            std::ostringstream o;
            o << "{\"items\":" << s.items << ",\"same\":" << s.same << ",\"rolls\":" << s.rolls << ",\"rollMean\":" << (s.rolls ? static_cast<double>(s.sum) / s.rolls : 0)
              << ",\"rollLow\":" << s.low << ",\"rollPerfect\":" << s.perfect << ",\"implicits\":" << s.implicits
              << ",\"implicitMean\":" << (s.implicits ? static_cast<double>(s.implicitSum) / s.implicits : 0) << ",\"implicitLow\":" << s.implicitLow
              << ",\"implicitPerfect\":" << s.implicitPerfect << ",\"rarities\":{";
            bool first = true;
            for (const auto& [k, n] : s.rarities) { o << (first ? "" : ",") << '"' << k << "\":" << n; first = false; }
            return o.str() + "}}";
        };
        Stats drops;
        std::string dropsError;
        for (int i = 0; i < count; ++i) {
            void* e = shown[(static_cast<size_t>(i) * 37) % shown.size()];  // spread over the list
            try {
                const Request r = Resolve(types.Get(), "u" + std::to_string(Get<uint16_t>(e, F("UniqueList.Entry", "uniqueID"))), 0);
                if (const auto root = Generate(generator, generate, r, level)) measure(root->Get(), e, drops, true);
            } catch (const std::exception& x) {
                if (dropsError.empty()) dropsError = std::to_string(Get<uint16_t>(e, F("UniqueList.Entry", "uniqueID"))) + ": " + x.what();
            }
        }
        out << ",\"dropsError\":" << Json(dropsError);
        // the Soul Gambler's Uniques (PresetRarityType Unique 4), the base types the shown Uniques have, in turn
        Stats gambler;
        std::string gamblerError;
        std::vector<int> baseTypes;
        for (void* e : shown) {
            const int t = Get<uint8_t>(e, F("UniqueList.Entry", "baseType"));
            if (t <= 24 && std::find(baseTypes.begin(), baseTypes.end(), t) == baseTypes.end()) baseTypes.push_back(t);
        }
        const auto soul = M("GenerateItems", "rollItemForSoulGambling", 4);
        for (int i = 0; i < count / 2 && !baseTypes.empty(); ++i) {
            try {
                bool giftable = false;
                int baseType = baseTypes[static_cast<size_t>(i) % baseTypes.size()], ilvl = level, preset = 4;
                void* a[]{&giftable, &baseType, &ilvl, &preset};
                void* made = Invoke(soul, generator, a);
                if (!made) continue;
                Root pin(made);
                const int rarity = Get<uint8_t>(made, F("ItemData", "rarity"));
                if (rarity < 7) { ++gambler.rarities[rarity]; continue; }
                auto id = Get<uint16_t>(made, F("ItemData", "uniqueID"));
                void* u[]{&id};
                if (void* entry = Invoke(M("UniqueList", "getUnique", 1), nullptr, u)) measure(made, entry, gambler, false);
            } catch (const std::exception& e) {
                if (gamblerError.empty()) gamblerError = std::to_string(i) + ": " + e.what();
            }
        }
        // Weaver cocoons (UniqueList.Entry.isCocoonedItem), made by the generator and opened with the game's own RevealCocoonedUnique
        Stats cocoons;
        std::string cocoonError;
        int cocoonsMade = 0, revealed = 0;
        for (void* e : Entries(Get<void*>(uniques.Get(), F("UniqueList", "uniques")))) {
            if (!e || !Get<bool>(e, F("UniqueList.Entry", "isCocoonedItem")) || cocoonsMade >= (std::max)(1, count / 4)) continue;
            try {
                Request r;
                r.uniqueId = Get<uint16_t>(e, F("UniqueList.Entry", "uniqueID"));
                r.gameRarity = 7;
                r.entry = e;
                const auto root = Generate(generator, generate, r, level);
                if (!root) continue;
                ++cocoonsMade;
                void* item = root->Get();
                void* a[]{c.actor};
                if (!Value<bool>(Method(item, "RevealCocoonedUnique", 1), item, a)) continue;
                ++revealed;
                auto id = Get<uint16_t>(item, F("ItemData", "uniqueID"));
                void* u[]{&id};
                if (void* entry = Invoke(M("UniqueList", "getUnique", 1), nullptr, u)) measure(item, entry, cocoons, false);
            } catch (const std::exception& x) {
                if (cocoonError.empty()) cocoonError = x.what();
            }
        }
        out << ",\"cocoons\":{\"made\":" << cocoonsMade << ",\"revealed\":" << revealed << ",\"rolls\":" << line(cocoons) << ",\"error\":" << Json(cocoonError) << '}';
        out << ",\"drops\":" << line(drops) << ",\"soulGambler\":" << line(gambler) << ",\"soulGamblerError\":" << Json(gamblerError) << ",\"lowBest\":" << lowBest.size()
            << ",\"lowBestExamples\":[" << lowExamples.str() << "],\"values\":[" << examples.str() << "],\"misses\":[";
        for (size_t i = 0; i < misses.size(); ++i) out << (i ? "," : "") << misses[i];
        out << "]}";
        return out.str();
    });
}
// Idol altars from the generator at each raw rarity (0, 2, 4 and 4 exalted): their affixes (kind, ID, tier,
// special type, name) and the base's maximum affixes; nothing dropped.
std::string AltarCheck(int count, int level) {
    return Run([=] {
        const auto c = Current();
        Root types(Invoke(M("ItemList", "get", 0))), catalog(Invoke(M("AffixList", "get", 0)));
        void* generator = Get<void*>(c.actor, F("Actor", "generateItems"));
        const auto generate = M("GenerateItems", "initialiseRandomItemData", 26);
        std::map<int, std::string> names;
        std::ostringstream out;
        out << "{\"ok\":true,\"items\":[";
        bool first = true;
        for (const auto& b : Bases(types.Get())) {
            if (b.type != 41) continue;
            for (int variant = 0; variant < 4; ++variant) {
                Request r;
                r.type = b.type;
                r.sub = b.sub;
                r.gameRarity = variant == 0 ? 0 : variant == 1 ? 2 : 4;
                r.exalted = variant == 3;
                for (int i = 0; i < count; ++i) {
                    const auto root = Generate(generator, generate, r, level);
                    if (!root) continue;
                    void* item = root->Get();
                    const auto ids = [&] {
                        std::string s;
                        for (void* a : Entries(Get<void*>(item, F("ItemData", "affixes"))))
                            if (a) s += std::to_string(Get<uint16_t>(a, F("ItemAffix", "affixId"))) + "/" + std::to_string(Get<uint8_t>(a, F("ItemAffix", "affixTier"))) + ",";
                        return s;
                    };
                    const std::string before = ids();
                    Invoke(Method(item, "RebuildID", 0), item);
                    Invoke(Method(item, "setValuesFromSerialisation", 0), item);
                    out << (first ? "" : ",") << "{\"same\":" << Boolean(before == ids()) << ",\"s\":" << b.sub << ",\"max\":" << b.maxAffixes << ",\"asked\":" << r.gameRarity
                        << ",\"exalted\":" << Boolean(r.exalted) << ",\"rarity\":" << static_cast<int>(Get<uint8_t>(item, F("ItemData", "rarity"))) << ",\"a\":[";
                    first = false;
                    bool a1 = true;
                    for (void* a : Entries(Get<void*>(item, F("ItemData", "affixes")))) {
                        if (!a) continue;
                        const int id = Get<uint16_t>(a, F("ItemAffix", "affixId"));
                        if (!names.count(id)) {
                            int arg = id;
                            void* g[]{&arg};
                            names[id] = Text(Invoke(Method(catalog.Get(), "GetAffixName", 1), catalog.Get(), g));
                        }
                        out << (a1 ? "" : ",") << '[' << Get<int>(a, F("ItemAffix", "affixType")) << ',' << id << ','
                            << static_cast<int>(Get<uint8_t>(a, F("ItemAffix", "affixTier"))) << ',' << Get<int>(a, F("ItemAffix", "specialAffixType")) << ']';
                        a1 = false;
                    }
                    out << "]}";
                }
            }
        }
        out << "],\"names\":{";
        first = true;
        for (const auto& [id, name] : names) { out << (first ? "" : ",") << '"' << id << "\":" << Json(name); first = false; }
        return out.str() + "}}";
    });
}
// A pile of Ancient Bones dropped next to the character by GroundItemManager.dropAncientBoneForPlayer (the
// path every bone drop takes), and the bones the character holds; amount 0 only reads.
std::string BoneDrop(int amount) {
    return Run([=] {
        const auto c = Current();
        void* tracker = Get<void*>(c.actor, F("Actor", "ancientBonesTracker"));
        if (amount > 0) {
            void* manager = game::StaticObject(game::FindStaticField("LE.dll", "", "GroundItemManager", "instance"));
            if (!manager) throw std::runtime_error("ground items unavailable");
            Root transform(Invoke(M("Component", "get_transform", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), c.actor));
            Vec3 at = Value<Vec3>(M("Transform", "get_position", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), transform.Get());
            at.x += 1.5f;
            int n = amount;
            bool sound = true, randomise = false;
            void* a[]{c.actor, &n, &at, &sound, &randomise};
            Invoke(M("GroundItemManager", "dropAncientBoneForPlayer", 5), manager, a);
        }
        return "{\"ok\":true,\"bones\":" + std::to_string(tracker ? Get<int>(tracker, F("AncientBonesTracker", "value")) : -1) + "}";
    });
}
// The inventory's idols and idol altars: type, subtype, rarity and affixes (kind, ID, special type).
std::string InventoryAffixes() {
    return Run([] {
        const auto c = Current();
        void* manager = Get<void*>(c.actor, F("Actor", "itemContainersManager"));
        void* inventory = Get<void*>(manager, F("ItemContainersManager", "inventory"));
        Root content(Invoke(Method(inventory, "GetContent", 0), inventory));
        std::ostringstream out;
        out << "{\"ok\":true,\"items\":[";
        bool first = true;
        for (void* e : Entries(content.Get())) {
            void* data = e ? Get<void*>(e, F("ItemContainerEntry", "data")) : nullptr;
            if (!data) continue;
            const int type = Get<uint8_t>(data, F("ItemData", "itemType"));
            if (!((type >= 25 && type <= 33) || type == 41)) continue;
            out << (first ? "" : ",") << "{\"t\":" << type << ",\"s\":" << Get<uint16_t>(data, F("ItemData", "subType"))
                << ",\"rarity\":" << static_cast<int>(Get<uint8_t>(data, F("ItemData", "rarity"))) << ",\"a\":[";
            first = false;
            bool a1 = true;
            for (void* a : Entries(Get<void*>(data, F("ItemData", "affixes")))) {
                if (!a) continue;
                out << (a1 ? "" : ",") << '[' << Get<int>(a, F("ItemAffix", "affixType")) << ',' << Get<uint16_t>(a, F("ItemAffix", "affixId")) << ','
                    << static_cast<int>(Get<uint8_t>(a, F("ItemAffix", "affixTier"))) << ',' << Get<int>(a, F("ItemAffix", "specialAffixType")) << ']';
                a1 = false;
            }
            out << "]}";
        }
        return out.str() + "]}";
    });
}
// Every method of that name on an LE.dll class ("Namespace:Class" or "Class"): arity and RVA in this GameAssembly.
std::string MethodRva(const std::string& klass, const std::string& name) {
    std::string ns, cls = klass;
    if (const auto colon = cls.find(':'); colon != std::string::npos) { ns = cls.substr(0, colon); cls = cls.substr(colon + 1); }
    const auto* k = game::FindClass("LE.dll", ns.c_str(), cls.c_str());
    if (!k) return "{\"ok\":false,\"error\":\"class not found\"}";
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"GameAssembly.dll"));
    std::ostringstream out;
    out << "{\"ok\":true,\"methods\":[";
    bool first = true;
    void* iter = nullptr;
    while (const auto* m = il2cpp::api().class_get_methods(k, &iter)) {
        if (name != il2cpp::api().method_get_name(m)) continue;
        const auto code = reinterpret_cast<uintptr_t>(*reinterpret_cast<void* const*>(m));
        out << (first ? "" : ",") << "{\"arity\":" << il2cpp::api().method_get_param_count(m) << ",\"rva\":\"0x" << std::hex << (code ? code - base : 0) << std::dec << "\"}";
        first = false;
    }
    return out.str() + "]}";
}
#endif

}  // namespace ep::spawn
