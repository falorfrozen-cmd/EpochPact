// EpochPact core entry: started by the version.dll loader on its own thread.
//
// Research build: waits until the game is up (its window exists and IL2CPP lists its
// assemblies), attaches to the runtime and writes the research dump once per game build.
// It reads metadata only; no game code is called and nothing is hooked.

#include "common.hpp"
#include "dumper.hpp"
#include "il2cpp_api.hpp"

#include <cstring>

namespace {

constexpr const char* kVersion = "0.0.1-research";

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

bool FileExists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

DWORD WINAPI Worker(void*) {
    using namespace ep;
    if (!InitPaths()) return 0;
    Log("EpochPact core %s loaded (pid %lu)", kVersion, GetCurrentProcessId());

    WindowSearch search{GetCurrentProcessId(), nullptr};
    if (!WaitFor([&] { EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&search)); return search.found != nullptr; },
                 600000, 250)) {
        Log("the game window never appeared; stopping");
        return 0;
    }
    Log("game window found");

    const char* missing = nullptr;
    if (!WaitFor([&] { return il2cpp::Resolve(&missing); }, 60000, 250)) {
        Log("IL2CPP API not available: %s missing; stopping", missing ? missing : "?");
        return 0;
    }
    const il2cpp::Api& a = il2cpp::api();
    Log("IL2CPP API resolved (GameAssembly.dll at 0x%llX, %zu bytes)", static_cast<unsigned long long>(a.base), a.size);

    il2cpp::Domain* domain = nullptr;
    size_t count = 0;
    if (!WaitFor([&] {
            domain = a.domain_get();
            if (domain) a.domain_get_assemblies(domain, &count);
            return domain && count > 0;
        }, 120000, 250)) {
        Log("IL2CPP lists no assemblies; stopping");
        return 0;
    }
    a.thread_attach(domain);
    Log("attached to the IL2CPP domain: %zu assemblies", count);

    const std::wstring dumpDir = PluginDir() + L"dump\\";
    if (FileExists(dumpDir + L"done.txt")) {
        Log("dump: already written (delete dump\\done.txt to write it again)");
        return 0;
    }
    RunDump(domain, dumpDir);
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
