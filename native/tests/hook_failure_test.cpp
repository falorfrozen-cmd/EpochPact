// Fault injection wraps Win32 only in this fixture, never in the shipped core.
// The production hook implementation below is executed unchanged.
#include <windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <cstdio>
#include <thread>

static int restoreFailures = 0, cacheFailures = 0;
static bool denyOpen = false, denySuspend = false, denyContext = false, denyEnumeration = false;
static BOOL WINAPI TestProtect(LPVOID at, SIZE_T n, DWORD flags, PDWORD old) {
    if (flags != PAGE_EXECUTE_READWRITE && restoreFailures > 0) {
        --restoreFailures; SetLastError(ERROR_ACCESS_DENIED); return FALSE;
    }
    return VirtualProtect(at, n, flags, old);
}
static BOOL WINAPI TestFlush(HANDLE process, LPCVOID at, SIZE_T n) {
    if (cacheFailures > 0) { --cacheFailures; SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return FlushInstructionCache(process, at, n);
}
static HANDLE WINAPI TestOpen(DWORD access, BOOL inherit, DWORD id) {
    if (denyOpen) { SetLastError(ERROR_ACCESS_DENIED); return nullptr; }
    return OpenThread(access, inherit, id);
}
static DWORD WINAPI TestSuspend(HANDLE thread) {
    if (denySuspend) { SetLastError(ERROR_ACCESS_DENIED); return static_cast<DWORD>(-1); }
    return SuspendThread(thread);
}
static BOOL WINAPI TestContext(HANDLE thread, LPCONTEXT context) {
    if (denyContext) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return GetThreadContext(thread, context);
}
static BOOL WINAPI TestFirst(HANDLE snap, LPTHREADENTRY32 entry) {
    if (denyEnumeration) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return Thread32First(snap, entry);
}
#define VirtualProtect TestProtect
#define FlushInstructionCache TestFlush
#define OpenThread TestOpen
#define SuspendThread TestSuspend
#define GetThreadContext TestContext
#define Thread32First TestFirst
#include "../core/hook.cpp"
#undef VirtualProtect
#undef FlushInstructionCache
#undef OpenThread
#undef SuspendThread
#undef GetThreadContext
#undef Thread32First

extern "C" int t_plain(int);
extern "C" int t_tiny(int);
extern "C" int t_vex(int);
using Fn = int (*)(int);
static Fn original = nullptr;
static int Detour(int x) { return original(x) * 10; }

int main() {
    unsigned tests = 0, failed = 0;
    auto check = [&](bool ok, const char* name) {
        ++tests; if (!ok) ++failed; std::printf("%s %s\n", ok ? "ok" : "FAIL", name);
    };
    auto target = reinterpret_cast<void*>(&t_plain);
    std::string why;
    auto install = [&] { return ep::hook::Install(target, reinterpret_cast<void*>(&Detour),
                                                 reinterpret_cast<void**>(&original), &why); };
    check(install() && t_plain(1) == 20, "baseline install");
    check(ep::hook::Remove(target, &why) && t_plain(1) == 2, "baseline remove");

    restoreFailures = 3;
    check(!install() && why.find("code patch failed") != std::string::npos,
          "exhausted protection restore is reported");
    MEMORY_BASIC_INFORMATION info{}; VirtualQuery(target, &info, sizeof info);
    check(t_plain(1) == 2 && info.Protect == PAGE_EXECUTE_READ && !ep::hook::IsInstalled(target),
          "failed patch rolls back bytes and protection before resume");
    cacheFailures = 1;
    check(!install() && t_plain(1) == 2 && !ep::hook::IsInstalled(target),
          "instruction cache failure rolls back the patch");

    std::atomic<bool> stop{false}, started{false};
    std::thread peer([&] { started = true; while (!stop) Sleep(1); });
    while (!started) SwitchToThread();
    denyOpen = true;
    check(!install() && t_plain(1) == 2, "unopenable thread prevents patching"); denyOpen = false;
    denySuspend = true;
    check(!install() && t_plain(1) == 2, "unsuspendable thread prevents patching"); denySuspend = false;
    denyContext = true;
    check(!install() && t_plain(1) == 2, "unreadable context prevents patching"); denyContext = false;
    denyEnumeration = true;
    check(!install() && t_plain(1) == 2, "thread enumeration failure prevents patching"); denyEnumeration = false;
    stop = true; peer.join(); // Would hang if failed inspection left the peer suspended.
    check(true, "failure paths resume all suspended threads");

    check(install(), "installation still works after injected failures");
    auto* code = static_cast<unsigned char*>(target);
    const unsigned char owned = code[0]; DWORD old = 0, ignored = 0;
    VirtualProtect(target, 32, PAGE_EXECUTE_READWRITE, &old); code[0] = 0x90;
    VirtualProtect(target, 32, old, &ignored); FlushInstructionCache(GetCurrentProcess(), target, 32);
    check(!ep::hook::Remove(target, &why) && code[0] == 0x90 && ep::hook::IsInstalled(target),
          "removal preserves a foreign patch and reports refusal");
    VirtualProtect(target, 32, PAGE_EXECUTE_READWRITE, &old); code[0] = owned;
    VirtualProtect(target, 32, old, &ignored); FlushInstructionCache(GetCurrentProcess(), target, 32);
    check(ep::hook::RemoveAll(&why) && t_plain(1) == 2, "all owned patches can be restored");

    const size_t blocks = ep::hook::g_blocks.size();
    size_t used = 0; for (const auto& b : ep::hook::g_blocks) used += b.used;
    for (int i = 0; i < 100; ++i) {
        ep::hook::Install(reinterpret_cast<void*>(&t_tiny), reinterpret_cast<void*>(&Detour), nullptr, &why);
        ep::hook::Install(reinterpret_cast<void*>(&t_vex), reinterpret_cast<void*>(&Detour), nullptr, &why);
    }
    size_t after = 0; for (const auto& b : ep::hook::g_blocks) after += b.used;
    check(blocks == ep::hook::g_blocks.size() && used == after,
          "200 refused trampolines reclaim their unpublished pages");
    std::printf("hook_failure: %u/%u passed\n", tests - failed, tests);
    return failed ? 1 : 0;
}
