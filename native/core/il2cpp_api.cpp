#include "il2cpp_api.hpp"

#include <windows.h>
#include <psapi.h>

namespace ep::il2cpp {

static Api g_api;

const Api& api() { return g_api; }

bool Resolve(const char** missing) {
    HMODULE ga = GetModuleHandleW(L"GameAssembly.dll");
    if (!ga) {
        if (missing) *missing = "GameAssembly.dll";
        return false;
    }
    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), ga, &info, sizeof info)) {
        if (missing) *missing = "GetModuleInformation";
        return false;
    }
    g_api.base = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
    g_api.size = info.SizeOfImage;

#define EP_RESOLVE(name, ret, params)                                                       \
    g_api.name = reinterpret_cast<ret(*) params>(GetProcAddress(ga, "il2cpp_" #name));      \
    if (!g_api.name) {                                                                      \
        if (missing) *missing = "il2cpp_" #name;                                            \
        return false;                                                                       \
    }
    EP_IL2CPP_API(EP_RESOLVE)
#undef EP_RESOLVE
    return true;
}

}  // namespace ep::il2cpp
