#include "game.hpp"

#include "common.hpp"

#include <cstring>
#include <map>
#include <mutex>
#include <string>

namespace ep::game {

namespace {

using namespace il2cpp;

std::map<std::string, const Image*> g_images;  // "LE.dll" -> image
const Field* g_isOnlinePlay = nullptr;          // EHG.Multiplayer.GameplayEnvironment._isOnlinePlay
const Field* g_isServer = nullptr;              // EHG.Multiplayer.GameplayEnvironment.IsServer (diagnostics)
MethodRef g_opImplicit;                         // UnityEngine.Object.op_Implicit(Object)
std::mutex g_lock;

}  // namespace

bool Init(Domain* domain) {
    const Api& a = api();
    size_t count = 0;
    const Assembly** list = a.domain_get_assemblies(domain, &count);
    for (size_t i = 0; i < count; ++i) {
        const Image* img = a.assembly_get_image(list[i]);
        if (const char* name = a.image_get_name(img)) g_images[name] = img;
    }
    auto le = g_images.find("LE.dll");
    if (le != g_images.end())
        if (const Class* env = a.class_from_name(le->second, "EHG.Multiplayer", "GameplayEnvironment")) {
            g_isOnlinePlay = a.class_get_field_from_name(env, "_isOnlinePlay");
            g_isServer = a.class_get_field_from_name(env, "IsServer");
        }
    g_opImplicit = FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "op_Implicit", 1);
    Log("game: %zu images; online flag %s; Object.op_Implicit %s", g_images.size(), g_isOnlinePlay ? "found" : "MISSING",
        g_opImplicit ? "found" : "MISSING");
    return le != g_images.end() && g_isOnlinePlay && g_opImplicit;
}

MethodRef FindMethod(const char* image, const char* ns, const char* cls, const char* name, int args) {
    MethodRef ref;
    auto it = g_images.find(image);
    if (it == g_images.end()) return ref;
    const Api& a = api();
    const Class* k = FindClass(image, ns, cls);
    if (!k) return ref;
    ref.info = a.class_get_method_from_name(k, name, args);
    if (ref.info) ref.code = *reinterpret_cast<void* const*>(ref.info);  // MethodInfo keeps the code pointer first
    return ref;
}

const Class* FindClass(const char* image, const char* ns, const char* cls) {
    auto it = g_images.find(image);
    if (it == g_images.end()) return nullptr;
    const Api& a = api();
    if (const Class* k = a.class_from_name(it->second, ns, cls)) return k;
    // IL2CPP class_from_name does not resolve C# nested names such as Stats.Stat.
    const char* dot = std::strchr(cls, '.');
    if (!dot) return nullptr;
    const std::string outer(cls, dot);
    const Class* parent = a.class_from_name(it->second, ns, outer.c_str());
    while (parent && dot) {
        const char* start = dot + 1;
        dot = std::strchr(start, '.');
        const std::string name = dot ? std::string(start, dot) : std::string(start);
        const Class* match = nullptr;
        void* iter = nullptr;
        while (const Class* nested = a.class_get_nested_types(parent, &iter)) {
            if (name == a.class_get_name(nested)) { match = nested; break; }
        }
        parent = match;
    }
    return parent;
}

size_t FieldOffset(const char* image, const char* ns, const char* cls, const char* field) {
    const Class* k = FindClass(image, ns, cls);
    const Field* f = k ? api().class_get_field_from_name(k, field) : nullptr;
    if (!f || (api().field_get_flags(f) & kFieldStatic)) return 0;
    return api().field_get_offset(f);
}

const Field* FindStaticField(const char* image, const char* ns, const char* cls, const char* field) {
    const Class* k = FindClass(image, ns, cls);
    const Field* f = k ? api().class_get_field_from_name(k, field) : nullptr;
    if (!f || !(api().field_get_flags(f) & kFieldStatic)) return nullptr;
    return f;
}

void* StaticObject(const Field* field) {
    void* value = nullptr;
    if (field) Guarded([&] { api().field_static_get_value(field, &value); }, nullptr);
    return value;
}

bool IsOfflinePlay(bool* known) {
    if (!g_isOnlinePlay) {
        if (known) *known = false;
        return false;
    }
    unsigned long long raw = 0;
    std::string why;
    const bool ok = Guarded([&] { api().field_static_get_value(g_isOnlinePlay, &raw); }, &why);
    if (known) *known = ok;
    return ok && (raw & 0xFF) == 0;
}

std::string GateText() {
    bool known = false;
    const bool offline = IsOfflinePlay(&known);
    if (!known) return "unknown (online/offline flag unreadable: everything stays off)";
    std::string text = offline ? "offline play" : "ONLINE play (every feature refuses)";
    if (g_isServer) {  // context for the live checks: offline play runs the game's server locally
        unsigned long long raw = 0;
        if (Guarded([&] { api().field_static_get_value(g_isServer, &raw); }, nullptr))
            text += (raw & 0xFF) ? " [local server]" : " [client only]";
    }
    return text;
}

bool IsAlive(void* obj) {
    if (!obj || !g_opImplicit) return false;
    bool alive = false;
    using Fn = bool (*)(void*, const Method*);
    Guarded([&] { alive = reinterpret_cast<Fn>(g_opImplicit.code)(obj, g_opImplicit.info); }, nullptr);
    return alive;
}

void Handle::Set(void* obj) {
    std::lock_guard<std::mutex> hold(g_lock);
    if (handle_) api().gchandle_free(handle_);
    handle_ = obj ? api().gchandle_new(obj, false) : 0;
}

void* Handle::Get() const {
    if (!handle_) return nullptr;
    void* obj = api().gchandle_get_target(handle_);
    return IsAlive(obj) ? obj : nullptr;
}

void Handle::Reset() { Set(nullptr); }

}  // namespace ep::game
