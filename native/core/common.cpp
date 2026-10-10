#include "common.hpp"
#include "file_safety.hpp"

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace ep {

static std::wstring g_pluginDir;
static std::mutex g_logLock;

const std::wstring& PluginDir() { return g_pluginDir; }

bool EnsureDir(const std::wstring& dir) {
    if (dir.empty()) return false;
    DWORD attrs = GetFileAttributesW(dir.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    size_t cut = dir.find_last_of(L'\\', dir.size() >= 2 ? dir.size() - 2 : 0);
    if (cut != std::wstring::npos && cut > 2) EnsureDir(dir.substr(0, cut));
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool InitPaths() {
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    std::wstring dir(exe, n);
    size_t slash = dir.find_last_of(L'\\');
    if (slash == std::wstring::npos) return false;
    g_pluginDir = dir.substr(0, slash + 1) + L"EpochPact\\";
    if (!EnsureDir(g_pluginDir + L"logs\\")) return false;
    files::Rotate(g_pluginDir + L"logs\\core.log", 5 * 1024 * 1024);
    return true;
}

void Log(const char* fmt, ...) {
    char line[2048];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int head = std::snprintf(line, sizeof line, "%02u:%02u:%02u.%03u ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line + head, sizeof line - head, fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> hold(g_logLock);
    FILE* f = nullptr;
    if (_wfopen_s(&f, (g_pluginDir + L"logs\\core.log").c_str(), L"ab") != 0 || !f) return;
    std::fputs(line, f);
    std::fputs("\r\n", f);
    std::fclose(f);
}

}  // namespace ep
