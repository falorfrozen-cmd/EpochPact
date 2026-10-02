// EpochPact loader: a version.dll that UnityPlayer.dll loads from the game folder.
//
// It forwards every export to the real System32 version.dll (version_thunks.asm jumps
// through g_real) and, only inside Last Epoch.exe, starts EpochPact's core from
// <game>\EpochPact\EpochPact.Core.dll on a thread of its own. The loader never touches
// the game itself; a file named <game>\EpochPact\disabled keeps the core from loading.

#include <windows.h>

extern "C" void* g_real[15] = {};

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

static DWORD WINAPI StartCore(void*) {
    wchar_t path[MAX_PATH];
    const wchar_t* exe = nullptr;
    if (!HostDir(path, MAX_PATH, &exe)) return 0;
    wchar_t* tail = const_cast<wchar_t*>(exe);
    *tail = L'\0';

    wchar_t probe[MAX_PATH];
    if (wcscpy_s(probe, path) || wcscat_s(probe, L"EpochPact\\disabled")) return 0;
    if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES) return 0;

    if (wcscat_s(path, L"EpochPact\\EpochPact.Core.dll")) return 0;
    HMODULE core = LoadLibraryW(path);
    if (!core) return 0;
    using StartFn = void (*)();
    if (auto start = reinterpret_cast<StartFn>(GetProcAddress(core, "EpochPact_Start"))) start();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);

    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (n == 0 || n >= MAX_PATH || wcscat_s(sys, L"\\version.dll")) return FALSE;
    HMODULE real = LoadLibraryW(sys);
    if (!real) return FALSE;
    for (int i = 0; i < 15; ++i) {
        g_real[i] = reinterpret_cast<void*>(GetProcAddress(real, kRealNames[i]));
        if (!g_real[i]) return FALSE;
    }

    // UnityCrashHandler64.exe sits in the same folder: start the core in the game only.
    wchar_t host[MAX_PATH];
    const wchar_t* exe = nullptr;
    if (HostDir(host, MAX_PATH, &exe) && _wcsicmp(exe, L"Last Epoch.exe") == 0) {
        if (HANDLE t = CreateThread(nullptr, 0, StartCore, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
