#include "spawn_items.hpp"
#include "managed.hpp"
#include <cmath>
#include <map>
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
struct Base { int type, sub, level; void* entry; };
std::vector<Base> Bases(void* list) {
    std::vector<Base> bases;
    for (void* base : ArrayEntries(Get<void*>(list, F("ItemList", "EquippableItems")))) {
        if (!base || Get<bool>(base, F("ItemList.BaseItem", "obsoleteItemType"))) continue;
        const int type = Get<int>(base, F("ItemList.BaseItem", "baseTypeID"));
        if (!SpawnableType(type)) continue;
        for (void* sub : Entries(Get<void*>(base, F("ItemList.BaseEquipmentItem", "subItems")))) {
            if (!sub || Get<bool>(sub, F("ItemList.SubItem", "obsoleteItem")) || Get<bool>(sub, F("ItemList.SubItem", "isLegacySubType")) ||
                Value<bool>(Method(sub, "get_IsUniqueOnlyBase", 0), sub)) continue;
            bases.push_back({type, Get<int>(sub, F("ItemList.SubItem", "subTypeID")), Get<int>(sub, F("ItemList.SubItem", "levelRequirement")), sub});
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
}  // namespace

std::string List() {
    return Run([] {
        const auto c = Current();
        Root types(Invoke(M("ItemList", "get", 0))), uniques(Invoke(M("UniqueList", "get", 0)));
        if (!types.Get() || !uniques.Get()) throw std::runtime_error("The game's item lists are not loaded yet.");
        std::ostringstream out;
        out << "{\"ok\":true,\"player\":" << Player(c) << ",\"bases\":[";
        bool first = true;
        for (const auto& b : Bases(types.Get())) {
            if (!first) out << ',';
            first = false;
            out << "{\"type\":" << b.type << ",\"sub\":" << b.sub << ",\"name\":" << Json(ItemName(types.Get(), b.type, b.sub))
                << ",\"baseType\":" << Json(BaseName(types.Get(), b.type)) << ",\"level\":" << b.level << '}';
        }
        out << "],\"uniques\":[";
        first = true;
        for (void* e : Entries(Get<void*>(uniques.Get(), F("UniqueList", "uniques")))) {
            if (!Shown(e)) continue;
            if (!first) out << ',';
            first = false;
            const int type = Get<uint8_t>(e, F("UniqueList.Entry", "baseType"));
            out << "{\"id\":" << Get<uint16_t>(e, F("UniqueList.Entry", "uniqueID")) << ",\"name\":" << Json(Text(Invoke(Method(e, "getDisplayName", 0), e)))
                << ",\"baseType\":" << Json(BaseName(types.Get(), type)) << ",\"set\":" << Boolean(Get<bool>(e, F("UniqueList.Entry", "isSetItem")))
                << ",\"weaver\":" << Boolean(Get<int>(e, F("UniqueList.Entry", "legendaryType")) == 1)
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
        if (!types.Get()) throw std::runtime_error("The game's item list is not loaded yet.");
        // b<type>.<subtype>: a base item of the chosen rarity; u<id>: a Unique or Set item.
        int type = -1, sub = -1, uniqueId = -1, gameRarity = 0;
        bool exalted = false, weaver = false, set = false;
        std::string name;
        if (item.size() > 1 && item[0] == 'b' && item.find('.') != std::string::npos) {
            const auto dot = item.find('.');
            if (!Number(item.substr(1, dot - 1), &type) || !Number(item.substr(dot + 1), &sub)) throw std::runtime_error("Unknown item.");
            bool known = false;
            for (const auto& b : Bases(types.Get())) known |= b.type == type && b.sub == sub;
            if (!known) throw std::runtime_error("This item cannot be spawned.");
            static constexpr int rarities[]{0, 2, 4, 4};  // the number of affixes: Normal 0, Magic 2, Rare 4; Exalted: Rare with an exalted affix
            gameRarity = rarities[rarity];
            exalted = rarity == 3;
            name = ItemName(types.Get(), type, sub);
        } else if (item.size() > 1 && item[0] == 'u') {
            if (!Number(item.substr(1), &uniqueId) || uniqueId > 65535) throw std::runtime_error("Unknown item.");
            auto id = static_cast<uint16_t>(uniqueId);
            void* a[]{&id};
            Root entry(Invoke(M("UniqueList", "getUnique", 1), nullptr, a));
            if (!Shown(entry.Get()) || Get<uint16_t>(entry.Get(), F("UniqueList.Entry", "uniqueID")) != id) throw std::runtime_error("This item cannot be spawned.");
            set = Get<bool>(entry.Get(), F("UniqueList.Entry", "isSetItem"));
            weaver = Get<int>(entry.Get(), F("UniqueList.Entry", "legendaryType")) == 1;
            gameRarity = set ? 8 : 7;
            name = Text(Invoke(Method(entry.Get(), "getDisplayName", 0), entry.Get()));
        } else {
            throw std::runtime_error("Unknown item.");
        }
        void* generator = Get<void*>(c.actor, F("Actor", "generateItems"));
        void* manager = game::StaticObject(game::FindStaticField("LE.dll", "", "GroundItemManager", "instance"));
        if (!generator || !manager) throw std::runtime_error("The game's item generator is not ready.");
        const auto generate = M("GenerateItems", "initialiseRandomItemData", 26);
        const auto drop = M("GroundItemManager", "dropItemForPlayer", 4);
        Root transform(Invoke(M("Component", "get_transform", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), c.actor));
        const Vec3 at = Value<Vec3>(M("Transform", "get_position", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), transform.Get());
        std::ostringstream out;
        out << "{\"ok\":true,\"player\":" << Player(c) << ",\"name\":" << Json(name) << ",\"items\":[";
        int spawned = 0;
        for (int i = 0; i < count; ++i) {
            bool no = false, forceExalted = exalted;
            int ilvl = level, location = 0, t = type, s = sub, r = gameRarity, sockets = 0, u = uniqueId, minimumLP = 0, rank = 0, legendaryType = 0, corrupt = 0;
            alignas(8) unsigned char faction[8]{}, replacement[8]{}, corruption[8]{}, effectiveness[8]{}, special[8]{}, minSpecial[8]{}, overrideLP[8]{};
            void* args[]{&no, &ilvl, &no, &location, &t, &s, &r, &sockets, &u, &forceExalted, &minimumLP, faction, &rank, &no, &legendaryType, &no,
                         replacement, corruption, &corrupt, effectiveness, nullptr, nullptr, special, &no, minSpecial, overrideLP};
            Root made(Invoke(generate, generator, args));
            if (!made.Get()) break;
            if (uniqueId >= 0 && !set && !weaver && lp >= 0)
                *reinterpret_cast<uint8_t*>(static_cast<char*>(made.Get()) + F("ItemData", "legendaryPotential")) = static_cast<uint8_t>(lp);
            // around the character, one step apart
            const double angle = count == 1 ? 0.0 : 6.283185307179586 * i / count;
            Vec3 p{at.x + static_cast<float>(1.5 * std::cos(angle)), at.y, at.z + static_cast<float>(1.5 * std::sin(angle))};
            bool sound = i == 0;
            void* dropArgs[]{c.actor, made.Get(), &p, &sound};
            Invoke(drop, manager, dropArgs);
            void* affixes = Get<void*>(made.Get(), F("ItemData", "affixes"));
            if (spawned) out << ',';
            out << "{\"type\":" << static_cast<int>(Get<uint8_t>(made.Get(), F("ItemData", "itemType")))
                << ",\"sub\":" << Get<uint16_t>(made.Get(), F("ItemData", "subType"))
                << ",\"rarity\":" << static_cast<int>(Get<uint8_t>(made.Get(), F("ItemData", "rarity")))
                << ",\"uniqueId\":" << Get<uint16_t>(made.Get(), F("ItemData", "uniqueID"))
                << ",\"affixes\":" << (affixes ? Entries(affixes).size() : 0)
                << ",\"exalted\":" << Boolean(Value<bool>(Method(made.Get(), "isExaltedItem", 0), made.Get()))
                << ",\"lp\":" << static_cast<int>(Get<uint8_t>(made.Get(), F("ItemData", "legendaryPotential")))
                << ",\"weaversWill\":" << static_cast<int>(Get<uint8_t>(made.Get(), F("ItemData", "weaversWill"))) << '}';
            ++spawned;
        }
        if (!spawned) throw std::runtime_error("The game did not generate the item.");
        out << "],\"spawned\":" << spawned << '}';
        return out.str();
    });
}

}  // namespace ep::spawn
