#include "enchanter.hpp"

#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "managed.hpp"

#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <sstream>

namespace ep::enchanter {

namespace {

using namespace managed;
using MethodInfo = const il2cpp::Method*;

constexpr int kIdolEnchantment = 3;  // ZoneRestrictedCraftingManager.CraftingType: the Woven Enchanter
constexpr int kMaximum = 40;

std::atomic<int> g_times{1};
std::atomic<uint64_t> g_raised{0}, g_faults{0};
std::atomic<int> g_lastNatural{-1}, g_lastUses{-1};

game::MethodRef m_calculate, m_find, m_remaining;
const il2cpp::Class* g_manager = nullptr;
size_t o_type = 0, o_limited = 0, o_max = 0, o_changed = 0;

// The game's own maxUses of each manager this has changed and what it set (main thread only), so a
// second CalculateMaxUses that leaves the field alone is not multiplied twice.
struct Seen { int natural, set; };
std::map<void*, Seen> g_seen;

// maxUses = the game's own x the setting on a Woven Enchanter (x1 puts the game's own back).
void Apply(void* manager) {
    if (Get<int>(manager, o_type) != kIdolEnchantment) return;
    int* uses = reinterpret_cast<int*>(static_cast<char*>(manager) + o_max);
    const auto known = g_seen.find(manager);
    const int natural = known != g_seen.end() && *uses == known->second.set ? known->second.natural : *uses;
    const int n = game::IsOfflinePlay() ? g_times.load(std::memory_order_relaxed) : 1;
    const long long v = static_cast<long long>(natural) * n;
    *uses = v >= INT_MAX ? INT_MAX : static_cast<int>(v);
    if (g_seen.size() > 64) g_seen.clear();
    g_seen[manager] = {natural, *uses};
    g_lastNatural = natural;
    g_lastUses = *uses;
    if (*uses != natural) g_raised.fetch_add(1, std::memory_order_relaxed);
}

// The uses line of an open Enchanter window and the "no more uses" objects listen to OnUsesChanged.
void Announce(void* manager) {
    void* changed = Get<void*>(manager, o_changed);
    if (!changed) return;
    int remaining = Value<int>(m_remaining, manager, std::array<void*, 1>{nullptr}.data());
    int uses = Get<int>(manager, o_max);
    bool limited = Get<bool>(manager, o_limited);
    void* a[]{&remaining, &uses, &limited};
    Invoke(Method(changed, "Invoke", 3), changed, a);
}

using CalculateFn = void (*)(void* self, MethodInfo m);
CalculateFn o_calculate = nullptr;

void d_calculate(void* self, MethodInfo m) {
    o_calculate(self, m);
    if (!self) return;
    std::string why;
    bool failed = false;
    const bool ran = game::Guarded([&] {
        try {
            Apply(self);
        } catch (const std::exception& e) {
            failed = true;
            why = e.what();
        }
    }, &why);
    if ((!ran || failed) && g_faults.fetch_add(1, std::memory_order_relaxed) < 5) Log("enchanter: uses not changed: %s", why.c_str());
}

// Every Woven Enchanter in the loaded scenes, now: the setting applies to the one in front of you too.
int Refresh() {
    int found = 0;
    Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(g_manager)));
    bool inactive = true;
    void* a[]{type.Get(), &inactive};
    Root all(Invoke(m_find, nullptr, a));
    const auto n = Get<uintptr_t>(all.Get(), 0x18);
    for (uintptr_t i = 0; i < n && i < 256; ++i) {
        void* manager = Get<void*>(all.Get(), 0x20 + i * sizeof(void*));
        if (!manager || Get<int>(manager, o_type) != kIdolEnchantment) continue;
        Apply(manager);
        Announce(manager);
        ++found;
    }
    return found;
}

bool ready = false;

std::string Refreshed() {
    std::string note;
    const std::string reply = Run([&] {
        const int found = Refresh();
        if (found) note = ", " + std::to_string(found) + " Enchanter(s) here updated";
        return std::string("ok");
    });
    return reply == "ok" ? note : std::string();
}

}  // namespace

bool Init() {
    m_calculate = game::FindMethod("LE.dll", "", "ZoneRestrictedCraftingManager", "CalculateMaxUses", 0);
    m_remaining = game::FindMethod("LE.dll", "", "ZoneRestrictedCraftingManager", "UsesRemaining", 1);
    m_find = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2);
    g_manager = game::FindClass("LE.dll", "", "ZoneRestrictedCraftingManager");
    o_type = game::FieldOffset("LE.dll", "", "ZoneRestrictedCraftingManager", "craftingType");
    o_limited = game::FieldOffset("LE.dll", "", "ZoneRestrictedCraftingManager", "limitedUses");
    o_max = game::FieldOffset("LE.dll", "", "ZoneRestrictedCraftingManager", "maxUses");
    o_changed = game::FieldOffset("LE.dll", "", "ZoneRestrictedCraftingManager", "OnUsesChanged");
    ready = m_calculate && m_remaining && m_find && g_manager && o_type && o_limited && o_max && o_changed;
    Log("enchanter: %s", ready ? "found" : "missing");
    return ready;
}

std::string SetUses(double n) {
    if (!(n >= 1 && n <= kMaximum) || n != std::floor(n)) return "enchanteruses: refused: the number must be a whole number from 1 to 40";
    if (!ready) return "enchanteruses: refused: ZoneRestrictedCraftingManager was not found in this game build";
    const int value = static_cast<int>(n);
    std::string why;
    if (value == 1) {
        g_times = 1;
        // nothing to put back while the hook is out (also no scene search when a profile sends x1)
        const std::string note = hook::IsInstalled(m_calculate.code) ? Refreshed() : std::string();
        if (hook::IsInstalled(m_calculate.code) && !hook::Remove(m_calculate.code, &why)) return "enchanteruses: x1, but the hook stays: " + why;
        return "enchanteruses -> x1 (the game's own)" + note;
    }
    if (!game::IsOfflinePlay()) return "enchanteruses: refused: " + game::GateText();
    if (!hook::IsInstalled(m_calculate.code) &&
        !hook::Install(m_calculate.code, reinterpret_cast<void*>(&d_calculate), reinterpret_cast<void**>(&o_calculate), &why))
        return "enchanteruses: refused: the hook did not go in: " + why;
    g_times = value;
    return "enchanteruses -> x" + std::to_string(value) + Refreshed();
}

std::string Status() {
    char buf[240];
    std::snprintf(buf, sizeof buf, "enchanteruses: x%d, hook %s, raised %llu, last %d -> %d, faults %llu", g_times.load(),
                  m_calculate && hook::IsInstalled(m_calculate.code) ? "in" : "out", static_cast<unsigned long long>(g_raised.load()),
                  g_lastNatural.load(), g_lastUses.load(), static_cast<unsigned long long>(g_faults.load()));
    return buf;
}

#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Check(int limitType) {
    return Run([=] {
        if (!ready) throw std::runtime_error("enchanter metadata missing");
        if (limitType != 0 && limitType != 1) throw std::runtime_error("limit type must be 0 or 1");
        const auto* cls = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
        Root go(il2cpp::api().object_new(cls));
        void* name = il2cpp::api().string_new("EpochPact isolated enchanter check");
        void* ctor[]{name};
        Invoke(Method(go.Get(), ".ctor", 1), go.Get(), ctor);
        struct Destroy {
            void* object;
            ~Destroy() {
                void* args[]{object};
                try { Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "DestroyImmediate", 1), nullptr, args); } catch (...) {}
            }
        } destroy{go.Get()};
        bool off = false, on = true;
        void* offArgs[]{&off};
        Invoke(Method(go.Get(), "SetActive", 1), go.Get(), offArgs);
        void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(g_manager));
        void* addArgs[]{type};
        Root manager(Invoke(Method(go.Get(), "AddComponent", 1), go.Get(), addArgs));
        void* m = manager.Get();
        const auto set = [&](const char* field, void* value) {
            const auto* f = il2cpp::api().class_get_field_from_name(g_manager, field);
            if (!f) throw std::runtime_error(std::string("test field unavailable: ") + field);
            il2cpp::api().field_set_value(m, f, value);
        };
        int craft = kIdolEnchantment, limit = limitType, uses = 1;
        bool limited = true;
        set("craftingType", &craft);
        set("limitedUses", &limited);
        set("useLimitType", &limit);
        set("maxUses", &uses);
        void* onArgs[]{&on};
        Invoke(Method(go.Get(), "SetActive", 1), go.Get(), onArgs);
        const auto snapshot = [&](const char* label) {
            std::ostringstream o;
            int t = kIdolEnchantment;
            void* none = nullptr;
            void* canArgs[]{&t, none};
            void* remainingArgs[]{none};
            Root ui(Invoke(game::FindMethod("LE.dll", "", "ZoneRestrictedCraftingManager", "GetUsesValuesForUI", 1), nullptr, std::array<void*, 1>{&t}.data()));
            struct Tuple { int remaining, max; bool limited; };
            const Tuple v = *static_cast<Tuple*>(il2cpp::api().object_unbox(ui.Get()));
            o << Json(label) << ":{\"maxUses\":" << Get<int>(m, o_max)
              << ",\"timesUsed\":" << Get<int>(m, Offset("ZoneRestrictedCraftingManager", "localTimesUsed"))
              << ",\"remaining\":" << Value<int>(m_remaining, m, remainingArgs)
              << ",\"ui\":[" << v.remaining << ',' << v.max << ',' << Boolean(v.limited) << ']'
              << ",\"canCraft\":" << Boolean(Value<bool>(game::FindMethod("LE.dll", "", "ZoneRestrictedCraftingManager", "CanCraft", 2), nullptr, canArgs)) << '}';
            return o.str();
        };
        std::ostringstream out;
        out << "{\"ok\":true,\"limitType\":" << limitType << ",\"setting\":" << g_times.load() << ',' << snapshot("awake");
        Invoke(m_calculate, m);
        out << ',' << snapshot("calculated");
        Invoke(m_calculate, m);
        out << ',' << snapshot("calculatedTwice");
        int t = kIdolEnchantment;
        void* useArgs[]{nullptr, &t};
        Invoke(game::FindMethod("LE.dll", "", "ZoneRestrictedCraftingManager", "OnUse", 2), nullptr, useArgs);
        out << ',' << snapshot("afterOneUse");
        out << ",\"refreshed\":" << Refresh() << ',' << snapshot("refreshed");
        return out.str() + "}";
    });
}
#endif

}  // namespace ep::enchanter
