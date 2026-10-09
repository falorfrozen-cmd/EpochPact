// Named Last Epoch.exe only inside a temporary verifier directory. This is not
// a game executable and never loads the actual EpochPact core or player saves.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 10;
    const int expected = _wtoi(argv[2]);
    HMODULE loader = LoadLibraryW(argv[1]);
    if (!loader) return 11;
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
    if (!bytes || size(system, &ignored) != bytes) return 15;
    using InfoFn = BOOL (WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    auto info = reinterpret_cast<InfoFn>(GetProcAddress(loader, "GetFileVersionInfoW"));
    auto realInfo = reinterpret_cast<InfoFn>(GetProcAddress(real, "GetFileVersionInfoW"));
    std::vector<unsigned char> actual(bytes), reference(bytes);
    if (!info(system, 0, bytes, actual.data()) || !realInfo(system, 0, bytes, reference.data())
            || actual != reference) return 16;

    wchar_t marker[32768];
    if (!GetEnvironmentVariableW(L"EPOCHPACT_LOADER_PROBE", marker, 32768)) return 17;
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
