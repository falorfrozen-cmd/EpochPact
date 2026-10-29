#include "transmog.hpp"

#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "managed.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

namespace ep::transmog {

namespace {

using namespace managed;
using MethodInfo = const il2cpp::Method*;

constexpr int kSlots = 7, kWeapon = 5, kOffhand = 6;
constexpr const char* kKeys[kSlots]{"helmet", "body", "gloves", "boots", "belt", "weapon", "offhand"};
constexpr const char* kFields[kSlots]{"helmet", "armor", "gloves", "boots", "belt", "weapon", "offhand"};  // ItemContainersManager.PaperDollContainer

// ReceiveEquipGear's item types (EquipmentType HELMET 0, BODY_ARMOR 1, BELT 2, BOOTS 3, GLOVES 4) to slots.
int GearSlot(int itemType) {
    switch (itemType) {
        case 0: return 0;
        case 1: return 1;
        case 4: return 2;
        case 3: return 3;
        case 2: return 4;
        default: return -1;
    }
}
int SlotIndex(const std::string& key) {
    for (int i = 0; i < kSlots; ++i)
        if (key == kKeys[i]) return i;
    return -1;
}

struct Look { int type = -1, sub = 0, rarity = 0, uid = 0; };  // rarity 0 a base item, 7 a Unique, 8 a Set item
bool Unique(const Look& l) { return l.rarity >= 7; }
std::string Key(const Look& l) { return Unique(l) ? "u" + std::to_string(l.uid) : "b" + std::to_string(l.type) + "." + std::to_string(l.sub); }

std::mutex g_lock;
std::map<std::string, std::array<Look, kSlots>> g_looks;  // character id -> look per slot
bool g_loaded = false;
std::atomic<uint64_t> g_gearCalls{0}, g_weaponCalls{0}, g_changed{0}, g_faults{0};
struct Seen { int type = -1, sub = 0, uid = 0, effect = 0; };
Seen g_weaponSeen[2];  // the last weapon visuals the game sent per hand, as the item itself (main thread)

game::MethodRef m_getActor, m_gear, m_weapon;

std::filesystem::path File() { return std::filesystem::path(PluginDir()) / L"transmog.txt"; }
void LoadLocked() {
    if (g_loaded) return;
    g_loaded = true;
    std::ifstream in(File());
    std::string id, slot;
    Look l;
    while (in >> id >> slot >> l.type >> l.sub >> l.rarity >> l.uid) {
        const int s = SlotIndex(slot);
        if (s >= 0 && l.type >= 0) g_looks[id][s] = l;
    }
}
void SaveLocked() {
    std::ofstream out(File(), std::ios::trunc);
    for (const auto& [id, looks] : g_looks)
        for (int i = 0; i < kSlots; ++i)
            if (looks[i].type >= 0) out << id << ' ' << kKeys[i] << ' ' << looks[i].type << ' ' << looks[i].sub << ' ' << looks[i].rarity << ' ' << looks[i].uid << '\n';
}
bool Find(const std::string& id, int slot, Look* out) {
    std::lock_guard<std::mutex> hold(g_lock);
    LoadLocked();
    const auto it = g_looks.find(id);
    if (it == g_looks.end() || it->second[static_cast<size_t>(slot)].type < 0) return false;
    *out = it->second[static_cast<size_t>(slot)];
    return true;
}

template <class F> void Guard(F&& work) {
    std::string why;
    bool failed = false;
    const bool ran = game::Guarded([&] {
        try {
            work();
        } catch (const std::exception& e) {
            failed = true;
            why = e.what();
        }
    }, &why);
    if ((!ran || failed) && g_faults.fetch_add(1, std::memory_order_relaxed) < 5) Log("transmog: look not applied: %s", why.c_str());
}

void* LocalSync(void* actor) {
    void* sync = nullptr;
    void* a[]{&sync};
    return Value<bool>(Method(actor, "TryGetPlayerActorSync", 1), actor, a) ? sync : nullptr;
}
void* CharacterData(void* actor) {
    return Get<void*>(Get<void*>(actor, Offset("Actor", "characterDataTracker")), Offset("CharacterDataTracker", "charData"));
}
// The loaded offline character's id when `sync` is its PlayerActorSync; empty otherwise.
std::string LocalId(void* sync) {
    if (!game::IsOfflinePlay()) return {};
    void* actor = Invoke(m_getActor);
    if (!actor || !game::IsAlive(actor) || LocalSync(actor) != sync) return {};
    void* data = CharacterData(actor);
    if (!data || !Value<bool>(Method(data, "get_IsOffline", 0), data)) return {};
    return Text(Invoke(Method(data, "get_Id", 0), data));
}

// ---- PlayerActorSync.ReceiveEquipGear(byte itemType, byte subType, bool isUnique, ushort uniqueID)
using GearFn = void (*)(void* self, uint8_t type, uint8_t sub, bool unique, uint16_t uid, MethodInfo m);
GearFn o_gear = nullptr;
void d_gear(void* self, uint8_t type, uint8_t sub, bool unique, uint16_t uid, MethodInfo m) {
    g_gearCalls.fetch_add(1, std::memory_order_relaxed);
    if (self) Guard([&] {
        const int slot = GearSlot(type);
        Look look;
        if (slot < 0) return;
        const std::string id = LocalId(self);
        if (id.empty() || !Find(id, slot, &look) || look.type != type) return;
        sub = static_cast<uint8_t>(look.sub);
        unique = Unique(look);
        uid = static_cast<uint16_t>(look.uid);
        g_changed.fetch_add(1, std::memory_order_relaxed);
    });
    o_gear(self, type, sub, unique, uid, m);
}

// ---- PlayerActorSync.ReceiveEquipWeapon(byte itemType, byte subType, byte rarity, ushort uniqueID, byte slotType,
// byte weaponEffect); slotType 0 main hand, 1 off hand
using WeaponFn = void (*)(void* self, uint8_t type, uint8_t sub, uint8_t rarity, uint16_t uid, uint8_t slotType, uint8_t effect, MethodInfo m);
WeaponFn o_weapon = nullptr;
void d_weapon(void* self, uint8_t type, uint8_t sub, uint8_t rarity, uint16_t uid, uint8_t slotType, uint8_t effect, MethodInfo m) {
    g_weaponCalls.fetch_add(1, std::memory_order_relaxed);
    if (self && slotType <= 1) Guard([&] {
        const std::string id = LocalId(self);
        if (id.empty()) return;
        g_weaponSeen[slotType] = {type, sub, uid, effect};
        Look look;
        if (!Find(id, slotType == 0 ? kWeapon : kOffhand, &look) || look.type != type) return;
        sub = static_cast<uint8_t>(look.sub);
        uid = static_cast<uint16_t>(look.uid);
        rarity = static_cast<uint8_t>(Unique(look) ? look.rarity : rarity >= 7 ? 4 : rarity);
        g_changed.fetch_add(1, std::memory_order_relaxed);
    });
    o_weapon(self, type, sub, rarity, uid, slotType, effect, m);
}

bool ready = false;

struct Ctx { void* actor; void* sync; void* paperDoll; std::string id; };
Ctx Current() {
    if (!ready) throw std::runtime_error("Item looks are not available in this game build.");
    if (!game::IsOfflinePlay()) throw std::runtime_error(game::GateText());
    RequireSession(4);
    void* actor = Invoke(m_getActor);
    if (!actor || !game::IsAlive(actor)) throw std::runtime_error("Load an offline character first.");
    void* data = CharacterData(actor);
    if (!data || !Value<bool>(Method(data, "get_IsOffline", 0), data)) throw std::runtime_error("Offline character required.");
    void* sync = LocalSync(actor);
    void* manager = Get<void*>(actor, Offset("Actor", "itemContainersManager"));
    void* doll = manager ? Get<void*>(manager, Offset("ItemContainersManager", "equipment")) : nullptr;
    if (!sync || !doll) throw std::runtime_error("Wait for the character to finish loading.");
    return {actor, sync, doll, Text(Invoke(Method(data, "get_Id", 0), data))};
}
void* Equipped(const Ctx& c, int slot) {
    void* container = Get<void*>(c.paperDoll, Offset("ItemContainersManager.PaperDollContainer", kFields[slot]));
    if (!container || !Value<bool>(Method(container, "HasContent", 0), container)) return nullptr;
    Root content(Invoke(Method(container, "GetContent", 0), container));
    for (void* e : Entries(content.Get()))
        if (void* data = e ? Get<void*>(e, Offset("ItemContainerEntry", "data")) : nullptr) return data;
    return nullptr;
}
struct Item { int type, sub, rarity, uid; };
Item Values(void* item) {
    return {Get<uint8_t>(item, Offset("ItemData", "itemType")), Get<uint16_t>(item, Offset("ItemData", "subType")),
            Get<uint8_t>(item, Offset("ItemData", "rarity")), Get<uint16_t>(item, Offset("ItemData", "uniqueID"))};
}
// The game sends the slot's visuals again with the item's own values; the hook puts the look in.
void Show(const Ctx& c, int slot) {
    void* item = Equipped(c, slot);
    if (!item) return;
    const Item v = Values(item);
    if (slot < kWeapon) {
        uint8_t t = static_cast<uint8_t>(v.type), s = static_cast<uint8_t>(v.sub);
        bool u = v.rarity >= 7;
        uint16_t id = static_cast<uint16_t>(v.uid);
        void* a[]{&t, &s, &u, &id};
        Invoke(m_gear, c.sync, a);
        return;
    }
    const int hand = slot == kWeapon ? 0 : 1;
    const Seen& seen = g_weaponSeen[hand];
    uint8_t t = static_cast<uint8_t>(v.type), s = static_cast<uint8_t>(v.sub), r = static_cast<uint8_t>(v.rarity), st = static_cast<uint8_t>(hand),
            e = static_cast<uint8_t>(seen.type == v.type && seen.sub == v.sub && seen.uid == v.uid ? seen.effect : 0);
    uint16_t id = static_cast<uint16_t>(v.uid);
    void* a[]{&t, &s, &r, &id, &st, &e};
    Invoke(m_weapon, c.sync, a);
}

int FirstByte(void* list) {
    if (!list) return 0;
    const auto* cls = il2cpp::api().object_get_class(list);
    const auto* size = il2cpp::api().class_get_field_from_name(cls, "_size");
    const auto* items = il2cpp::api().class_get_field_from_name(cls, "_items");
    if (!size || !items || Get<int>(list, il2cpp::api().field_get_offset(size)) < 1) return 0;
    void* array = Get<void*>(list, il2cpp::api().field_get_offset(items));
    return array ? Get<uint8_t>(array, 0x20) : 0;
}
game::MethodRef M(const char* cls, const char* name, int arity) {
    const auto m = game::FindMethod("LE.dll", "", cls, name, arity);
    if (!m) throw std::runtime_error(std::string("missing method: ") + cls + "." + name);
    return m;
}
std::string ItemName(void* types, int type, int sub) {
    void* a[]{&type, &sub};
    return Text(Invoke(Method(types, "GetItemDisplayName", 2), types, a));
}
void* UniqueEntry(int uid) {
    auto id = static_cast<uint16_t>(uid);
    void* a[]{&id};
    void* entry = Invoke(M("UniqueList", "getUnique", 1), nullptr, a);
    return entry && Get<uint16_t>(entry, Offset("UniqueList.Entry", "uniqueID")) == id ? entry : nullptr;
}
std::string LookName(void* types, const Look& l) {
    if (!Unique(l)) return ItemName(types, l.type, l.sub);
    void* entry = UniqueEntry(l.uid);
    return entry ? Text(Invoke(Method(entry, "getDisplayName", 0), entry)) : "Unique " + std::to_string(l.uid);
}
// The looks that fit an item type: its base items (no obsolete, legacy or Unique-only bases) and its Uniques and Sets.
std::vector<Look> Options(void* types, void* uniques, int type) {
    std::vector<Look> out;
    void* array = Get<void*>(types, Offset("ItemList", "EquippableItems"));
    for (uintptr_t i = 0, n = array ? Get<uintptr_t>(array, 0x18) : 0; i < n && i < 256; ++i) {
        void* base = Get<void*>(array, 0x20 + i * sizeof(void*));
        if (!base || Get<int>(base, Offset("ItemList.BaseItem", "baseTypeID")) != type || Get<bool>(base, Offset("ItemList.BaseItem", "obsoleteItemType"))) continue;
        for (void* sub : Entries(Get<void*>(base, Offset("ItemList.BaseEquipmentItem", "subItems")))) {
            if (!sub || Get<bool>(sub, Offset("ItemList.SubItem", "obsoleteItem")) || Get<bool>(sub, Offset("ItemList.SubItem", "isLegacySubType")) ||
                Value<bool>(Method(sub, "get_IsUniqueOnlyBase", 0), sub)) continue;
            out.push_back({type, Get<int>(sub, Offset("ItemList.SubItem", "subTypeID")), 0, 0});
        }
    }
    for (void* e : Entries(Get<void*>(uniques, Offset("UniqueList", "uniques")))) {
        if (!e || Get<bool>(e, Offset("UniqueList.Entry", "hideFromPlayers")) || Get<uint8_t>(e, Offset("UniqueList.Entry", "baseType")) != type) continue;
        out.push_back({type, FirstByte(Get<void*>(e, Offset("UniqueList.Entry", "subTypes"))), Get<bool>(e, Offset("UniqueList.Entry", "isSetItem")) ? 8 : 7,
                       Get<uint16_t>(e, Offset("UniqueList.Entry", "uniqueID"))});
    }
    return out;
}
bool Parse(const std::string& key, Look* out) {
    try {
        if (key.size() > 1 && key[0] == 'u') {
            const int uid = std::stoi(key.substr(1));
            void* entry = uid >= 0 && uid <= 65535 ? UniqueEntry(uid) : nullptr;
            if (!entry || Get<bool>(entry, Offset("UniqueList.Entry", "hideFromPlayers"))) return false;
            *out = {Get<uint8_t>(entry, Offset("UniqueList.Entry", "baseType")), FirstByte(Get<void*>(entry, Offset("UniqueList.Entry", "subTypes"))),
                    Get<bool>(entry, Offset("UniqueList.Entry", "isSetItem")) ? 8 : 7, uid};
            return true;
        }
        const auto dot = key.find('.');
        if (key.size() > 1 && key[0] == 'b' && dot != std::string::npos) {
            *out = {std::stoi(key.substr(1, dot - 1)), std::stoi(key.substr(dot + 1)), 0, 0};
            return out->type >= 0 && out->sub >= 0;
        }
    } catch (...) {}
    return false;
}

std::string ReadNow() {
    const Ctx c = Current();
    Root types(Invoke(M("ItemList", "get", 0))), uniques(Invoke(M("UniqueList", "get", 0)));
    std::ostringstream out;
    out << "{\"ok\":true,\"player\":{\"id\":" << Json(c.id) << "},\"slots\":[";
    for (int slot = 0; slot < kSlots; ++slot) {
        out << (slot ? "," : "") << "{\"slot\":" << Json(kKeys[slot]);
        void* item = Equipped(c, slot);
        Look look;
        const bool has = Find(c.id, slot, &look);
        if (!item) {
            out << ",\"item\":null,\"look\":" << (has ? "{\"key\":" + Json(Key(look)) + ",\"name\":" + Json(LookName(types.Get(), look)) + "}" : "null")
                << ",\"options\":[]}";
            continue;
        }
        const Item v = Values(item);
        Look own{v.type, v.sub, v.rarity >= 7 ? v.rarity : 0, v.uid};
        out << ",\"item\":{\"type\":" << v.type << ",\"sub\":" << v.sub << ",\"rarity\":" << v.rarity << ",\"uniqueId\":" << v.uid
            << ",\"key\":" << Json(Key(own)) << ",\"name\":" << Json(LookName(types.Get(), own)) << "},\"look\":";
        if (has) out << "{\"key\":" << Json(Key(look)) << ",\"name\":" << Json(LookName(types.Get(), look)) << ",\"shown\":" << Boolean(look.type == v.type) << '}';
        else out << "null";
        out << ",\"options\":[";
        bool first = true;
        for (const Look& o : Options(types.Get(), uniques.Get(), v.type)) {
            out << (first ? "" : ",") << "{\"key\":" << Json(Key(o)) << ",\"name\":" << Json(LookName(types.Get(), o)) << ",\"unique\":" << Boolean(Unique(o)) << '}';
            first = false;
        }
        out << "]}";
    }
    return out.str() + "]}";
}

}  // namespace

bool Init() {
    m_getActor = game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0);
    m_gear = game::FindMethod("LE.dll", "", "PlayerActorSync", "ReceiveEquipGear", 4);
    m_weapon = game::FindMethod("LE.dll", "", "PlayerActorSync", "ReceiveEquipWeapon", 6);
    ready = m_getActor && m_gear && m_weapon;
    std::string why;
    // In from the start: the game sends the character's visuals once when it loads, and a weapon's effect only then.
    if (ready && !hook::Install(m_gear.code, reinterpret_cast<void*>(&d_gear), reinterpret_cast<void**>(&o_gear), &why)) ready = false;
    if (ready && !hook::Install(m_weapon.code, reinterpret_cast<void*>(&d_weapon), reinterpret_cast<void**>(&o_weapon), &why)) {
        hook::Remove(m_gear.code, nullptr);
        ready = false;
    }
    Log("transmog: %s%s", ready ? "ready" : "unavailable", why.empty() ? "" : (": " + why).c_str());
    return ready;
}

std::string Read() { return Run([] { return ReadNow(); }); }

std::string Set(const std::string& slotKey, const std::string& lookKey) {
    return Run([=] {
        const int slot = SlotIndex(slotKey);
        if (slot < 0) throw std::runtime_error("Unknown slot.");
        const Ctx c = Current();
        Look look;
        const bool clear = lookKey == "none";
        if (!clear) {
            void* item = Equipped(c, slot);
            if (!item) throw std::runtime_error("Equip an item in this slot first.");
            if (!Parse(lookKey, &look)) throw std::runtime_error("Unknown look.");
            if (look.type != Values(item).type) throw std::runtime_error("The look must be an item of the same type as the equipped one.");
        }
        {
            std::lock_guard<std::mutex> hold(g_lock);
            LoadLocked();
            g_looks[c.id][static_cast<size_t>(slot)] = clear ? Look{} : look;
            SaveLocked();
        }
        Show(c, slot);
        return ReadNow();
    });
}

std::string Status() {
    size_t looks = 0;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        LoadLocked();
        for (const auto& [id, slots] : g_looks)
            for (const Look& l : slots) looks += l.type >= 0;
    }
    char buf[220];
    std::snprintf(buf, sizeof buf, "transmog: looks %zu, hooks %s, gear calls %llu, weapon calls %llu, changed %llu, faults %llu", looks,
                  ready && hook::IsInstalled(m_gear.code) ? "in" : "out", static_cast<unsigned long long>(g_gearCalls.load()),
                  static_cast<unsigned long long>(g_weaponCalls.load()), static_cast<unsigned long long>(g_changed.load()),
                  static_cast<unsigned long long>(g_faults.load()));
    return buf;
}

}  // namespace ep::transmog
