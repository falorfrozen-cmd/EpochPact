#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace ep::files {
namespace fs = std::filesystem;
inline bool Linked(const fs::path& path) {
    const auto attrs = GetFileAttributesW(path.c_str());
    // Fail closed on inaccessible paths and all native reparse points.
    return attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}
inline bool PlainTree(const fs::path& path) {
    if (Linked(path)) return false;
    for (const auto& item : fs::recursive_directory_iterator(path))
        if (Linked(item.path())) return false;
    return true;
}
inline size_t Prune(const fs::path& root, size_t keep, const fs::path& newest) {
    if (!keep || Linked(root)) return 0;
    const auto boundary = fs::canonical(root);
    struct Candidate { fs::path path; fs::file_time_type time; };
    std::vector<Candidate> candidates;
    for (const auto& item : fs::directory_iterator(root)) {
        try {
            if (!item.is_directory() || Linked(item.path()) || fs::canonical(item.path()).parent_path() != boundary) continue;
            const auto marker = item.path() / L".epochpact-retention";
            const auto manifest = item.path() / L"manifest.json";
            if (Linked(marker) || Linked(manifest) || !fs::is_regular_file(marker) || !fs::is_regular_file(manifest)) continue;
            if (fs::file_size(marker) != 2) continue;
            std::ifstream in(marker, std::ios::binary);
            std::string version(2, '\0'); in.read(version.data(), 2);
            if (!in || version != "1\n" || !PlainTree(item.path())) continue;
            candidates.push_back({item.path(), fs::last_write_time(manifest)});
        } catch (const fs::filesystem_error&) { /* keep an inaccessible backup */ }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.time == b.time ? a.path > b.path : a.time > b.time;
    });
    size_t removed = 0;
    for (size_t i = keep; i < candidates.size(); ++i) {
        const auto& path = candidates[i].path;
        // Recheck the absolute boundary immediately before recursive removal.
        if (fs::equivalent(path, newest) || Linked(path) || fs::canonical(path).parent_path() != boundary || !PlainTree(path)) continue;
        fs::remove_all(path); ++removed;
    }
    return removed;
}
inline bool Rotate(const fs::path& path, uintmax_t limit) {
    std::error_code error;
    if (!fs::is_regular_file(path, error) || Linked(path) || fs::file_size(path, error) <= limit || error) return false;
    const auto old = path.parent_path() / (path.stem().wstring() + L".old" + path.extension().wstring());
    if (fs::exists(old) && Linked(old)) return false;
    return MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
// Atomic claim precedes every read. Deletion always targets the claimed packet,
// leaving any next cmd.txt untouched, including after a producer timeout.
inline std::string ClaimPacket(const std::wstring& path, const std::function<void(const char*)>& log,
                               const std::function<void()>& afterClaim = {}) {
    const auto claimed = path + L".claimed";
    if (!MoveFileExW(path.c_str(), claimed.c_str(), MOVEFILE_REPLACE_EXISTING)) return {};
    if (afterClaim) afterClaim();
    HANDLE file = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 250; ++attempt) {
        file = CreateFileW(claimed.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        const auto error = GetLastError();
        if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) break;
        Sleep(2);
    }
    if (file == INVALID_HANDLE_VALUE) { log("IPC: claimed packet open failed; command was not executed"); return {}; }
    std::string text;
    char buffer[4096]; DWORD n = 0; bool readable = true;
    while (true) {
        if (!ReadFile(file, buffer, sizeof buffer, &n, nullptr)) { readable = false; break; }
        if (!n) break;
        text.append(buffer, n);
        if (text.size() > 128 * 1024) { readable = false; break; }
    }
    CloseHandle(file);
    if (!readable) { text.clear(); log("IPC: unreadable or oversized packet refused; command was not executed"); }
    DeleteFileW(claimed.c_str());
    return text;
}
}
