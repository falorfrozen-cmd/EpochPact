// What EpochPact knows about the running game: methods found by name, the offline gate,
// guarded calls into game code, and objects kept alive through GC handles.
#pragma once

#include "il2cpp_api.hpp"

#include <cstdint>
#include <string>

namespace ep::game {

// A method found by name: its MethodInfo (passed as the hidden last argument of every
// IL2CPP call) and its code address (what a hook patches and what a call jumps to).
struct MethodRef {
    const il2cpp::Method* info = nullptr;
    void* code = nullptr;
    explicit operator bool() const { return code != nullptr; }
};

// Call once from a thread attached to the domain.
bool Init(il2cpp::Domain* domain);

// `image` like "LE.dll"; `ns` may be "" for the global namespace.
MethodRef FindMethod(const char* image, const char* ns, const char* cls, const char* name, int args);

// A class by name, for checks like "is this object a GroundItemLabel".
const il2cpp::Class* FindClass(const char* image, const char* ns, const char* cls);

// An instance field's offset inside its object, found by name; 0 when it is not there
// (no instance field sits at 0: the object header comes first).
size_t FieldOffset(const char* image, const char* ns, const char* cls, const char* field);

// A static field, for reading a singleton or a global list through StaticObject.
const il2cpp::Field* FindStaticField(const char* image, const char* ns, const char* cls, const char* field);

// The object a static reference field points at, or null.
void* StaticObject(const il2cpp::Field* field);
// Distinguish a successful null reference from an unreadable reference.
bool TryStaticObject(const il2cpp::Field* field, void** value);

// The offline gate. Online means the game is connected to Eleventh Hour Games' servers
// (EHG.Multiplayer.GameplayEnvironment._isOnlinePlay). `known` is false when the field
// could not be read, which every caller treats as "not offline".
bool IsOfflinePlay(bool* known = nullptr);
std::string GateText();

// Runs `fn` and turns a managed exception or an access violation inside game code into
// `false`, so a call into the game can never take the game down with it.
template <typename F>
bool Guarded(F&& fn, std::string* why);

// A strong GC handle, so the object is not collected while EpochPact holds it.
class Handle {
public:
    Handle() = default;
    void Set(void* obj);
    void* Get() const;  // null when unset or when the Unity object behind it was destroyed
    void Reset();

private:
    uintptr_t handle_ = 0;  // pointer-sized: IL2CPP's GC handles are pointers since Unity 2021, not uint32
};

// UnityEngine.Object's implicit bool: false once the native object is destroyed.
bool IsAlive(void* unityObject);
// `false` means unknown, not a destroyed Unity object.
bool TryIsAlive(void* unityObject, bool* alive);

}  // namespace ep::game

#include "game_guard.inl"
