// EpochPact core entry: started by the version.dll loader on its own thread.
//
// Waits until the game is up (its window exists and IL2CPP lists its assemblies), attaches
// to the runtime, finds what the features need by name, then serves the command channel.
// Nothing is hooked until a feature is switched on (research builds also install their
// capture hooks and write the metadata dump once per game build).

#include "commands.hpp"
#include "common.hpp"
#include "game.hpp"
#include "il2cpp_api.hpp"
#include "items.hpp"
#include "loot.hpp"
#include "mainthread.hpp"
#include "player.hpp"
#include "version.hpp"
#include "xp.hpp"
#ifdef EPOCHPACT_RESEARCH
#include "dumper.hpp"
#include "research.hpp"
#endif

#include <cstring>

namespace {

struct WindowSearch {
    DWORD pid;
    HWND found;
};

BOOL CALLBACK FindGameWindow(HWND w, LPARAM p) {
    auto* s = reinterpret_cast<WindowSearch*>(p);
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    char cls[64];
    if (pid == s->pid && GetClassNameA(w, cls, sizeof cls) && std::strcmp(cls, "UnityWndClass") == 0) {
        s->found = w;
        return FALSE;
    }
    return TRUE;
}

// Polls `ready` every `stepMs` until it returns true or `limitMs` passes.
template <typename F>
bool WaitFor(F ready, DWORD limitMs, DWORD stepMs) {
    for (DWORD waited = 0; waited <= limitMs; waited += stepMs) {
        if (ready()) return true;
        Sleep(stepMs);
    }
    return false;
}

DWORD WINAPI Worker(void*) {
    using namespace ep;
    if (!InitPaths()) return 0;
    Log("EpochPact core %s loaded (pid %lu)", kVersion, GetCurrentProcessId());

    WindowSearch search{GetCurrentProcessId(), nullptr};
    if (!WaitFor([&] { EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&search)); return search.found != nullptr; },
                 600000, 100)) {
        Log("the game window never appeared; stopping");
        return 0;
    }

    const char* missing = nullptr;
    if (!WaitFor([&] { return il2cpp::Resolve(&missing); }, 60000, 100)) {
        Log("IL2CPP API not available: %s missing; stopping", missing ? missing : "?");
        return 0;
    }
    const il2cpp::Api& a = il2cpp::api();
    il2cpp::Domain* domain = nullptr;
    size_t count = 0;
    if (!WaitFor([&] {
            domain = a.domain_get();
            if (domain) a.domain_get_assemblies(domain, &count);
            return domain && count > 0;
        }, 120000, 100)) {
        Log("IL2CPP lists no assemblies; stopping");
        return 0;
    }
    a.thread_attach(domain);
    Log("attached to the IL2CPP domain: %zu assemblies (GameAssembly.dll at 0x%llX)", count, static_cast<unsigned long long>(a.base));

    if (!game::Init(domain)) Log("game: something the features need is missing; they will refuse");
#ifdef EPOCHPACT_RESEARCH
    research::Init();
    const std::wstring dumpDir = PluginDir() + L"dump\\";
    if (GetFileAttributesW((dumpDir + L"done.txt").c_str()) == INVALID_FILE_ATTRIBUTES) RunDump(domain, dumpDir);
#endif
    mainthread::Init();
#ifdef EPOCHPACT_RESEARCH
    research::KeepTicking();
#endif
    xp::Init();
    loot::Init();
    items::Init();
    player::Init();
    Log("ready: command channel at EpochPact\\ipc\\cmd.txt");
    // Detached until a command arrives: IL2CPP waits for attached threads when the game quits.
    if (void* self = a.thread_current()) a.thread_detach(self);
    commands::Loop(domain);
    return 0;
}

}  // namespace

extern "C" __declspec(dllexport) void EpochPact_Start() {
    if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr)) CloseHandle(t);
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(self);
    return TRUE;
}
