// Isolated stand-in for the core: no game hooks, memory writes or IPC.
#include <windows.h>
extern "C" __declspec(dllimport) int ProbeValue();
extern "C" __declspec(dllexport) void EpochPact_Start() {
    wchar_t path[32768];
    DWORD n = GetEnvironmentVariableW(L"EPOCHPACT_LOADER_PROBE", path, 32768);
    if (!n || n >= 32768) return;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const char value = static_cast<char>('0' + ProbeValue());
    DWORD written = 0;
    WriteFile(file, &value, 1, &written, nullptr);
    CloseHandle(file);
}
