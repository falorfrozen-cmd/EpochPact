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
#endif

}  // namespace ep::spawn
