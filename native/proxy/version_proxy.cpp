// EpochPact loader: a version.dll that UnityPlayer.dll loads from the game folder.
//
// It forwards every export to the real System32 version.dll (version_thunks.asm jumps
// through g_real) and, only inside Last Epoch.exe, starts EpochPact's core from
// <game>\EpochPact\EpochPact.Core.dll on a thread of its own. The loader never touches
// the game itself; a file named <game>\EpochPact\disabled keeps the core from loading.

#include <windows.h>
#include <winver.h>

extern "C" void* g_real[15] = {};
static INIT_ONCE g_forwarding = INIT_ONCE_STATIC_INIT;
static HINSTANCE g_self = nullptr;

static const char* const kRealNames[15] = {
    "GetFileVersionInfoA",     "GetFileVersionInfoByHandle", "GetFileVersionInfoExA",
    "GetFileVersionInfoExW",   "GetFileVersionInfoSizeA",    "GetFileVersionInfoSizeExA",
    "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",  "GetFileVersionInfoW",
    "VerFindFileA",            "VerFindFileW",               "VerInstallFileA",
    "VerInstallFileW",         "VerQueryValueA",             "VerQueryValueW",
};

// The folder of the host executable, with a trailing backslash; false if it does not fit.
static bool HostDir(wchar_t* out, DWORD cap, const wchar_t** fileName) {
    DWORD n = GetModuleFileNameW(nullptr, out, cap);
    if (n == 0 || n >= cap) return false;
    wchar_t* slash = wcsrchr(out, L'\\');
    if (!slash) return false;
    *fileName = slash + 1;
    return true;
}

static DWORD WINAPI LoadCore() {
    wchar_t path[MAX_PATH];
    const wchar_t* exe = nullptr;
    if (!HostDir(path, MAX_PATH, &exe)) return 0;
    wchar_t* tail = const_cast<wchar_t*>(exe);
    *tail = L'\0';

    wchar_t probe[MAX_PATH];
    if (wcscpy_s(probe, path) || wcscat_s(probe, L"EpochPact\\disabled")) return 0;
    if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES) return 0;

    if (wcscat_s(path, L"EpochPact\\EpochPact.Core.dll")) return 0;
    // Resolve dependencies beside the explicit core DLL or in System32. Never
    // search the current directory or PATH for DLLs supplied by another program.
    HMODULE core = LoadLibraryExW(path, nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!core) return 0;
    using StartFn = void (*)();
    if (auto start = reinterpret_cast<StartFn>(GetProcAddress(core, "EpochPact_Start"))) start();
    return 0;
}

static DWORD WINAPI StartCore(void* lease) {
    const DWORD result = LoadCore();
    // The thread owns a loader reference, even if its host releases version.dll.
    FreeLibraryAndExitThread(static_cast<HMODULE>(lease), result);
}

// These exports have different failure conventions; never jump through nullptr
// or report a successful install when the real Windows DLL cannot be resolved.
extern "C" DWORD ep_VersionUnavailable() { SetLastError(ERROR_DLL_INIT_FAILED); return 0; }
extern "C" DWORD ep_FindUnavailable() { SetLastError(ERROR_DLL_INIT_FAILED); return VFF_BUFFTOOSMALL; }
extern "C" DWORD ep_InstallUnavailable() { SetLastError(ERROR_DLL_INIT_FAILED); return VIF_CANNOTREADSRC; }

static BOOL CALLBACK ResolveForwarding(PINIT_ONCE, PVOID, PVOID*) {
    void* functions[15]{};
    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    HMODULE real = nullptr;
    bool ok = n != 0 && n < MAX_PATH && wcscat_s(sys, L"\\version.dll") == 0;
    if (ok) {
        // This runs on the first version API call, outside this DLL's DllMain.
        real = LoadLibraryExW(sys, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        ok = real != nullptr;
    }
    for (int i = 0; ok && i < 15; ++i) {
        functions[i] = reinterpret_cast<void*>(GetProcAddress(real, kRealNames[i]));
        ok = functions[i] != nullptr;
    }
    if (!ok && real) FreeLibrary(real);

    // UnityCrashHandler64.exe sits in the same folder: start the core in the game only.
    wchar_t host[MAX_PATH];
    const wchar_t* exe = nullptr;
    if (ok && HostDir(host, MAX_PATH, &exe) && _wcsicmp(exe, L"Last Epoch.exe") == 0) {
        HMODULE lease = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                              reinterpret_cast<LPCWSTR>(g_self), &lease)) {
            HANDLE thread = CreateThread(nullptr, 0, StartCore, lease, 0, nullptr);
            if (thread) CloseHandle(thread);
            else FreeLibrary(lease);
        }
    }
    // Complete even on failure, with explicit failing functions. InitOnce must
    // not keep retrying and spawning workers on every version API call.
    for (int i = 0; i < 15; ++i)
        g_real[i] = ok ? functions[i] : reinterpret_cast<void*>(
            i == 9 || i == 10 ? &ep_FindUnavailable :
            i == 11 || i == 12 ? &ep_InstallUnavailable : &ep_VersionUnavailable);
    return TRUE;
}

extern "C" void* ep_ResolveVersion(unsigned index) {
    const DWORD saved = GetLastError();
    InitOnceExecuteOnce(&g_forwarding, ResolveForwarding, nullptr, nullptr);
    void* result = index < 15 ? g_real[index] : reinterpret_cast<void*>(&ep_VersionUnavailable);
    SetLastError(saved);
    return result;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = self;
        DisableThreadLibraryCalls(self);
    }
    return TRUE;
}
