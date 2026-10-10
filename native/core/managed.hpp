#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "common.hpp"
#include "game.hpp"
#include "mainthread.hpp"
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ep::managed {
inline std::string Text(void* object);
struct Root {
    uintptr_t handle = 0;
    explicit Root(void* object) {
        if (!object || !(handle = il2cpp::api().gchandle_new(object, false)))
            throw std::runtime_error("managed object/root unavailable");
    }
    Root(const Root&) = delete;
    ~Root() { if (handle) il2cpp::api().gchandle_free(handle); }
    void* Get() const { return il2cpp::api().gchandle_get_target(handle); }
};
template<class T> T Get(void* object, size_t offset) {
    if (!object || !offset) throw std::runtime_error("required object/field unavailable");
    return *reinterpret_cast<const T*>(static_cast<const char*>(object) + offset);
}
inline size_t Offset(const char* cls, const char* field, const char* ns = "") {
    const auto result = game::FieldOffset("LE.dll", ns, cls, field);
    if (!result) throw std::runtime_error(std::string("missing field: ") + cls + "." + field);
    return result;
}
inline const il2cpp::Method* Method(void* object, const char* name, int arity) {
    if (!object) throw std::runtime_error("required managed object unavailable");
    const auto* method = il2cpp::api().class_get_method_from_name(il2cpp::api().object_get_class(object), name, arity);
    if (!method) throw std::runtime_error(std::string("missing method: ") + name);
    return method;
}
inline void* Invoke(const il2cpp::Method* method, void* self = nullptr, void** args = nullptr) {
    if (!method) throw std::runtime_error("required method unavailable");
    void* exception = nullptr;
    void* result = il2cpp::api().runtime_invoke(method, self, args, &exception);
    if (exception) {
        const auto* cls = il2cpp::api().object_get_class(exception);
        std::string message = std::string("managed exception: ") + il2cpp::api().class_get_name(cls);
        const auto* getter = il2cpp::api().class_get_method_from_name(cls, "get_Message", 0);
        if (getter) {
            void* second = nullptr;
            void* detail = il2cpp::api().runtime_invoke(getter, exception, nullptr, &second);
            if (detail && !second) message += ": " + Text(detail);
        }
        if (const auto* trace = il2cpp::api().class_get_method_from_name(cls, "get_StackTrace", 0)) {
            void* second = nullptr;
            void* detail = il2cpp::api().runtime_invoke(trace, exception, nullptr, &second);
            if (detail && !second) Log("managed call failed: %s\n%s", message.c_str(), Text(detail).c_str());
        }
        throw std::runtime_error(message);
    }
    return result;
}
inline void* Invoke(game::MethodRef method, void* self = nullptr, void** args = nullptr) { return Invoke(method.info, self, args); }
template<class T> T Value(const il2cpp::Method* method, void* self = nullptr, void** args = nullptr) {
    Root result(Invoke(method, self, args));
    return *static_cast<T*>(il2cpp::api().object_unbox(result.Get()));
}
template<class T> T Value(game::MethodRef method, void* self = nullptr, void** args = nullptr) { return Value<T>(method.info, self, args); }
inline std::string Text(void* object) {
    if (!object) return {};
    const int length = Get<int>(object, 0x10);
    if (length < 0 || length > 16 * 1024 * 1024) throw std::runtime_error("invalid managed string");
    const auto* chars = reinterpret_cast<const wchar_t*>(static_cast<const char*>(object) + 0x14);
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, length, nullptr, 0, nullptr, nullptr);
    if (length && !bytes) throw std::runtime_error("invalid string encoding");
    std::string text(bytes, '\0');
    if (bytes) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, length, text.data(), bytes, nullptr, nullptr);
    return text;
}
inline std::string Json(const std::string& value) {
    std::string result = "\"";
    for (unsigned char c : value) {
        if (c == '\\' || c == '"') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 0x20) { char escaped[7]; std::snprintf(escaped, sizeof escaped, "\\u%04x", c); result += escaped; }
        else result += static_cast<char>(c);
    }
    return result + '"';
}
inline const char* Boolean(bool value) { return value ? "true" : "false"; }
struct Session { int state; bool transitioning; };
inline Session SessionState() {
    auto method = game::FindMethod("LE.dll", "ClientAppState", "ClientStateManager", "get_CurrentClientAppStateType", 0);
    const auto* field = game::FindStaticField("LE.dll", "ClientAppState", "ClientStateManager", "_activeTransition");
    if (!method || !field) throw std::runtime_error("client session state unavailable");
    // Nullable<UniTask>: HasValue is the first byte; leave room for its full value.
    alignas(void*) unsigned char active[64]{};
    il2cpp::api().field_static_get_value(field, active);
    return {Value<int>(method), active[0] != 0};
}
inline void RequireSession(int expected) {
    const auto s = SessionState();
    if (s.state != expected || s.transitioning) throw std::runtime_error("wait until the normal client state transition finishes");
}
inline std::vector<void*> Entries(void* list) {
    if (!list) throw std::runtime_error("required managed list unavailable");
    const auto* cls = il2cpp::api().object_get_class(list);
    const auto* size = il2cpp::api().class_get_field_from_name(cls, "_size");
    const auto* items = il2cpp::api().class_get_field_from_name(cls, "_items");
    if (!size || !items) throw std::runtime_error("managed list layout unavailable");
    const int n = Get<int>(list, il2cpp::api().field_get_offset(size));
    void* array = Get<void*>(list, il2cpp::api().field_get_offset(items));
    if (n < 0 || n > 8192 || !array || static_cast<uintptr_t>(n) > Get<uintptr_t>(array, 0x18))
        throw std::runtime_error("invalid managed list bounds");
    std::vector<void*> result;
    for (int i = 0; i < n; ++i) result.push_back(Get<void*>(array, 0x20 + sizeof(void*) * i));
    return result;
}
inline std::string Run(std::function<std::string()> work) {
    std::string reply, why;
    if (!mainthread::Run([&] {
        try { reply = work(); }
        catch (const std::exception& e) { reply = "{\"ok\":false,\"error\":" + Json(e.what()) + "}"; }
    }, 5000, &why)) return "{\"ok\":false,\"error\":" + Json(why) + "}";
    return reply.empty() ? "{\"ok\":false,\"error\":\"guarded failure; see core.log\"}" : reply;
}
}
