// Named Last Epoch.exe only inside a temporary verifier directory. This is not
// a game executable and never loads the actual EpochPact core or player saves.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <atomic>
#include <thread>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 10;
    const int expected = _wtoi(argv[2]);
    HMODULE loader = LoadLibraryW(argv[1]);
    if (!loader) return 11;
    wchar_t marker[32768];
    if (!GetEnvironmentVariableW(L"EPOCHPACT_LOADER_PROBE", marker, 32768)) return 17;
    Sleep(30);
    if (GetFileAttributesW(marker) != INVALID_FILE_ATTRIBUTES) return 23;
    const char* exports[] = {"GetFileVersionInfoA", "GetFileVersionInfoByHandle",
        "GetFileVersionInfoExA", "GetFileVersionInfoExW", "GetFileVersionInfoSizeA",
        "GetFileVersionInfoSizeExA", "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",
        "GetFileVersionInfoW", "VerFindFileA", "VerFindFileW", "VerInstallFileA",
        "VerInstallFileW", "VerQueryValueA", "VerQueryValueW", "VerLanguageNameA", "VerLanguageNameW"};
    for (const char* name : exports) if (!GetProcAddress(loader, name)) return 12;

    wchar_t system[32768];
    if (!GetSystemDirectoryW(system, 32768) || wcscat_s(system, L"\\version.dll")) return 13;
    HMODULE real = LoadLibraryW(system);
    if (!real) return 14;
    using SizeFn = DWORD (WINAPI*)(LPCWSTR, LPDWORD);
    auto size = reinterpret_cast<SizeFn>(GetProcAddress(loader, "GetFileVersionInfoSizeW"));
    auto realSize = reinterpret_cast<SizeFn>(GetProcAddress(real, "GetFileVersionInfoSizeW"));
    DWORD ignored = 0;
    const DWORD bytes = realSize(system, &ignored);
    if (!bytes) return 15;
    // First calls race the resolver. ExW has a fifth, stack-passed argument;
    // its output proves that the generic thunk preserved that part of the ABI.
    using ExFn = BOOL (WINAPI*)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
    auto ex = reinterpret_cast<ExFn>(GetProcAddress(loader, "GetFileVersionInfoExW"));
    auto realEx = reinterpret_cast<ExFn>(GetProcAddress(real, "GetFileVersionInfoExW"));
    std::vector<unsigned char> expectedEx(bytes);
    if (!realEx(FILE_VER_GET_NEUTRAL, system, 0, bytes, expectedEx.data())) return 20;
    std::atomic<int> waiting{0}, failures{0}; std::atomic<bool> go{false};
    std::vector<std::thread> callers;
    for (int i = 0; i < 16; ++i) callers.emplace_back([&, i] {
        std::vector<unsigned char> data(bytes); DWORD h = 0;
        ++waiting; while (!go.load()) SwitchToThread();
        if (i % 2 == 0) {
            if (!ex(FILE_VER_GET_NEUTRAL, system, 0, bytes, data.data()) || data != expectedEx) ++failures;
        } else if (size(system, &h) != bytes) ++failures;
    });
    while (waiting != 16) SwitchToThread();
    go = true; for (auto& thread : callers) thread.join();
    if (failures) return 21;
    DWORD referenceError = 0, actualError = 0;
    SetLastError(0x1234); realSize(system, &ignored); referenceError = GetLastError();
    SetLastError(0x1234); size(system, &ignored); actualError = GetLastError();
    if (actualError != referenceError) return 22;
    using InfoFn = BOOL (WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    auto info = reinterpret_cast<InfoFn>(GetProcAddress(loader, "GetFileVersionInfoW"));
    auto realInfo = reinterpret_cast<InfoFn>(GetProcAddress(real, "GetFileVersionInfoW"));
    std::vector<unsigned char> actual(bytes), reference(bytes);
    if (!info(system, 0, bytes, actual.data()) || !realInfo(system, 0, bytes, reference.data())
            || actual != reference) return 16;

    for (int i = 0; i < 250; ++i) {
        HANDLE file = CreateFileW(marker, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            char value = 0; DWORD read = 0;
            ReadFile(file, &value, 1, &read, nullptr); CloseHandle(file);
            if (read != 1 || expected == 0 || value != '0' + expected) return 18;
            puts("All 17 exports present; version forwarding and trusted dependency resolved.");
            return 0;
        }
        Sleep(10);
    }
    if (expected != 0) return 19;
    puts("All 17 exports present; no core probe ran (dependency refused or disabled).");
    return 0;
}
