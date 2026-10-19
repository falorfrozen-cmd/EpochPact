// EpochPact loader: a version.dll that UnityPlayer.dll loads from the game folder.
//
// It forwards every export to the real System32 version.dll (version_thunks.asm jumps
// through g_real) and, only inside Last Epoch.exe, starts EpochPact's core from
// <game>\EpochPact\EpochPact.Core.dll on a thread of its own. The loader never touches
// the game itself; a file named <game>\EpochPact\disabled keeps the core from loading.
//
// The core thread is created when this DLL is attached, not on the first version API
// call: whether UnityPlayer calls those APIs at all depends on the machine, and a core
// that never starts leaves no trace. DllMain only creates the thread; the thread loads
// the core after the loader lock is released. Each step is written to
// <game>\EpochPact\logs\loader.log (or %TEMP%\EpochPact-loader.log).

#include <windows.h>
#include <winver.h>

#include <cstdarg>
#include <cstdio>

#include "..\version_info.h"

#ifdef EPOCHPACT_MERGED_LOADER
extern "C" void EpochPact_Start();  // native\core\core.cpp, linked into this DLL
// The installer (tools/le_session.py LOADER_MARKS) recognises this loader by this exact text,
// because the "EpochPact\EpochPact.Core.dll" path below is not compiled into it.
static const wchar_t kLoaderMark[] = L"EpochPact loader with linked core";
// Older EpochPact apps (before the core was linked in) recognise their loader by the path it loaded
// the separate core from. Keeping that text in this file lets such an app still see it as EpochPact's
// own, so installing or removing an older release over this one works instead of being refused as
// "another mod loader". The linker keeps the symbol; it is data only and exports nothing.
extern "C" extern const wchar_t kLegacyLoaderMark[];
extern "C" const wchar_t kLegacyLoaderMark[] = L"EpochPact\\EpochPact.Core.dll";
#pragma comment(linker, "/INCLUDE:kLegacyLoaderMark")
#endif

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

static bool IsGameHost() {
    wchar_t host[MAX_PATH];
    const wchar_t* exe = nullptr;
    return HostDir(host, MAX_PATH, &exe) && _wcsicmp(exe, L"Last Epoch.exe") == 0;
}

// Appends one UTF-8 line to <game>\EpochPact\logs\loader.log, falling back to
// %TEMP%\EpochPact-loader.log when that folder cannot be written. Core thread only.
static void Trace(const wchar_t* fmt, ...) {
    wchar_t text[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(text, _TRUNCATE, fmt, args);
    va_end(args);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t line[1200];
    _snwprintf_s(line, _TRUNCATE, L"%04u-%02u-%02u %02u:%02u:%02u.%03u pid %lu: %s\r\n", t.wYear, t.wMonth, t.wDay,
                 t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentProcessId(), text);
    char utf8[4096];
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof utf8, nullptr, nullptr);
    if (bytes <= 1) return;

    wchar_t paths[2][MAX_PATH]{};
    const wchar_t* exe = nullptr;
    if (HostDir(paths[0], MAX_PATH, &exe)) {
        *const_cast<wchar_t*>(exe) = L'\0';
        wchar_t dir[MAX_PATH];
        if (!wcscpy_s(dir, paths[0]) && !wcscat_s(dir, L"EpochPact")) CreateDirectoryW(dir, nullptr);
        if (!wcscat_s(dir, L"\\logs")) CreateDirectoryW(dir, nullptr);
        if (wcscat_s(paths[0], L"EpochPact\\logs\\loader.log")) paths[0][0] = L'\0';
    }
    const DWORD temp = GetTempPathW(MAX_PATH, paths[1]);
    if (temp == 0 || temp >= MAX_PATH || wcscat_s(paths[1], L"EpochPact-loader.log")) paths[1][0] = L'\0';
    for (const wchar_t* path : paths) {
        if (!*path) continue;
        WIN32_FILE_ATTRIBUTE_DATA info{};
        const bool large = GetFileAttributesExW(path, GetFileExInfoStandard, &info) &&
                           (info.nFileSizeHigh || info.nFileSizeLow > 256 * 1024);
        HANDLE file = CreateFileW(path, large ? GENERIC_WRITE : FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, large ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) continue;
        DWORD written = 0;
        const bool ok = WriteFile(file, utf8, static_cast<DWORD>(bytes - 1), &written, nullptr) != 0;
        CloseHandle(file);
        if (ok) return;
    }
}

static void TraceError(const wchar_t* what, DWORD error) {
    wchar_t message[256] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, message,
                   static_cast<DWORD>(sizeof message / sizeof message[0]), nullptr);
    for (wchar_t* c = message; *c; ++c)
        if (*c == L'\r' || *c == L'\n') *c = L' ';
    Trace(L"%s: Windows error %lu %s", what, error, message);
}

static DWORD WINAPI LoadCore() {
    wchar_t path[MAX_PATH];
    const wchar_t* exe = nullptr;
    if (!HostDir(path, MAX_PATH, &exe)) {
        Trace(L"loader %hs: the game path is too long; core not loaded", EP_VERSION_STR);
        return 0;
    }
    Trace(L"loader %hs started in %s", EP_VERSION_STR, path);
    wchar_t* tail = const_cast<wchar_t*>(exe);
    *tail = L'\0';

    wchar_t probe[MAX_PATH];
    if (wcscpy_s(probe, path) || wcscat_s(probe, L"EpochPact\\disabled")) return 0;
    if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES) {
        Trace(L"EpochPact\\disabled exists; core not loaded");
        return 0;
    }

#ifdef EPOCHPACT_MERGED_LOADER
    // The core is linked into this DLL, so no other module is loaded.
    EpochPact_Start();
    Trace(L"core started (%s); see core.log", kLoaderMark);
    return 0;
#else
    if (wcscat_s(path, L"EpochPact\\EpochPact.Core.dll")) return 0;
    // Resolve dependencies beside the explicit core DLL or in System32. Never
    // search the current directory or PATH for DLLs supplied by another program.
    HMODULE core = LoadLibraryExW(path, nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!core) {
        TraceError(L"core load failed", GetLastError());
        return 0;
    }
    using StartFn = void (*)();
    auto start = reinterpret_cast<StartFn>(GetProcAddress(core, "EpochPact_Start"));
    if (!start) {
        Trace(L"core loaded, but EpochPact_Start is missing (mixed EpochPact versions?)");
        return 0;
    }
    start();
    Trace(L"core loaded and started; see core.log");
    return 0;
#endif
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
        // UnityCrashHandler64.exe sits in the same folder: start the core in the game only.
        // Only CreateThread here (no library loads); the thread runs after the loader lock.
        if (IsGameHost()) {
            HMODULE lease = nullptr;
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                  reinterpret_cast<LPCWSTR>(g_self), &lease)) {
                HANDLE thread = CreateThread(nullptr, 0, StartCore, lease, 0, nullptr);
                if (thread) CloseHandle(thread);
                else FreeLibrary(lease);
            }
        }
    }
    return TRUE;
}
