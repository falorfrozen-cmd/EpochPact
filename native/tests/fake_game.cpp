// The game side of xp_test.exe: game.hpp and common.hpp answered by the test, so the real
// xp.cpp and hook engine run against t_gain (xp_targets.asm) with a gate the test sets.

#include "fake_game.hpp"

#include "../core/common.hpp"
#include "../core/game.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C" void t_gain(void*, long long, const void*);

namespace fake {
bool offline = true;   // what IsOfflinePlay answers
bool known = true;     // whether the flag was readable
bool found = true;     // whether FindMethod finds the experience method
std::vector<std::string> log;
int methodInfo = 0;    // stands in for the MethodInfo the game would pass
}  // namespace fake

namespace ep {

const std::wstring& PluginDir() {
    static const std::wstring dir = L".\\";
    return dir;
}
bool InitPaths() { return true; }
bool EnsureDir(const std::wstring&) { return true; }
void Log(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    fake::log.emplace_back(buf);
}

namespace game {

bool Init(il2cpp::Domain*) { return true; }

MethodRef FindMethod(const char*, const char*, const char* cls, const char* name, int) {
    MethodRef ref;
    if (fake::found && std::strcmp(cls, "ExperienceTracker") == 0 && std::strcmp(name, "GainExpFromEnemyOrMote") == 0) {
        ref.info = reinterpret_cast<const il2cpp::Method*>(&fake::methodInfo);
        ref.code = reinterpret_cast<void*>(&t_gain);
    }
    return ref;
}

bool IsOfflinePlay(bool* known) {
    if (known) *known = fake::known;
    return fake::known && fake::offline;
}

std::string GateText() {
    if (!fake::known) return "unknown (online/offline flag unreadable: everything stays off)";
    return fake::offline ? "offline play" : "ONLINE play (every feature refuses)";
}

bool IsAlive(void*) { return false; }
void Handle::Set(void*) {}
void* Handle::Get() const { return nullptr; }
void Handle::Reset() {}

}  // namespace game
}  // namespace ep
