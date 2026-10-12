// In-game support report. The hotkey is read with GetAsyncKeyState only while
// the game window is in front; no input is sent to the game. The screenshot is
// taken by the game itself (ScreenCapture), and the game's own notification
// tells the player where the report was saved.
#include "report.hpp"
#include "commands.hpp"
#include "common.hpp"
#include "game.hpp"
#include "lifecycle.hpp"
#include "mainthread.hpp"
#include "managed.hpp"
#include "../version_info.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace ep::report {
namespace {
using namespace managed;
std::atomic<int> hotkey{VK_F8};
std::atomic<uint64_t> created{0};
std::mutex lastLock;
std::string lastFolder;
std::atomic<bool> busy{false};

std::filesystem::path ReportsRoot() { return std::filesystem::path(PluginDir()) / L"reports"; }
std::filesystem::path KeyFile() { return std::filesystem::path(PluginDir()) / L"report-key.txt"; }
std::string KeyName(int vk) { return vk >= VK_F5 && vk <= VK_F12 ? "F" + std::to_string(vk - VK_F1 + 1) : "off"; }
int KeyCode(const std::string& name) {
    if (name == "off") return 0;
    if (name.size() >= 2 && (name[0] == 'F' || name[0] == 'f')) {
        const int n = std::atoi(name.c_str() + 1);
        if (n >= 5 && n <= 12) return VK_F1 + n - 1;
    }
    return -1;
}
std::wstring Env(const wchar_t* name) {
    wchar_t buffer[1024]; const DWORD n = GetEnvironmentVariableW(name, buffer, 1024);
    return n && n < 1024 ? std::wstring(buffer, n) : std::wstring();
}
std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}
// The Windows user name, computer name and profile path never leave the PC unmasked.
std::string Mask(std::string text) {
    const std::string home = Utf8(Env(L"USERPROFILE"));
    auto replaceAll = [&](const std::string& what, const std::string& with) {
        if (what.size() < 2) return;
        std::string lowerText = text, lowerWhat = what;
        for (auto& c : lowerText) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (auto& c : lowerWhat) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string out; size_t from = 0, at;
        while ((at = lowerText.find(lowerWhat, from)) != std::string::npos) { out += text.substr(from, at - from) + with; from = at + what.size(); }
        text = out + text.substr(from);
    };
    std::string slashed = home; for (auto& c : slashed) if (c == '\\') c = '/';
    replaceAll(home, "%USERPROFILE%");
    replaceAll(slashed, "%USERPROFILE%");
    replaceAll(Utf8(Env(L"USERNAME")), "<user>");
    replaceAll(Utf8(Env(L"COMPUTERNAME")), "<computer>");
    return text;
}
std::string Tail(const std::filesystem::path& path, size_t limit = 512 * 1024) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(in.tellg());
    in.seekg(size > limit ? static_cast<std::streamoff>(size - limit) : 0);
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (size > limit) { const auto nl = data.find('\n'); data = "[first " + std::to_string(size - limit) + " bytes omitted]\n" + (nl == std::string::npos ? data : data.substr(nl + 1)); }
    return data;
}
void Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}
bool GameInFront() {
    DWORD pid = 0;
    HWND window = GetForegroundWindow();
    if (!window) return false;
    GetWindowThreadProcessId(window, &pid);
    return pid == GetCurrentProcessId();
}
void Notify(const std::string& title, const std::string& text) {
    mainthread::Post([title, text] {
        game::Guarded([&] {
            auto* cls = game::FindClass("LE.dll", "", "Notifications");
            if (!cls) return;
            void* iterator = nullptr;
            while (auto* m = il2cpp::api().class_get_methods(cls, &iterator)) {
                if (std::strcmp(il2cpp::api().method_get_name(m), "ShowGenericNotification") || il2cpp::api().method_get_param_count(m) != 4) continue;
                char* type = il2cpp::api().type_get_name(il2cpp::api().method_get_param(m, 2));
                const bool sprite = type && std::string(type) == "UnityEngine.Sprite";
                if (type) il2cpp::api().free(type);
                if (!sprite) continue;
                Root t(il2cpp::api().string_new(title.c_str())), x(il2cpp::api().string_new(text.c_str()));
                int green = 1;  // LE.UI.Components.NotificationColor.Green
                void* a[]{t.Get(), x.Get(), nullptr, &green};
                Invoke(m, nullptr, a);
                return;
            }
        }, nullptr);
    });
}
std::string Stamp() {
    const auto now = std::time(nullptr);
    std::tm local{}; localtime_s(&local, &now);
    char buffer[32]; std::strftime(buffer, sizeof buffer, "%Y-%m-%d_%H%M%S", &local);
    return buffer;
}
void Watch() {
    bool down = false;
    auto lastReport = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    while (!lifecycle::Stopping()) {
        Sleep(50);
        const int vk = hotkey.load();
        if (!vk) { down = false; continue; }
        const bool pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (pressed && !down && GameInFront() && std::chrono::steady_clock::now() - lastReport > std::chrono::seconds(3)) {
            lastReport = std::chrono::steady_clock::now();
            // Read-only game commands below call into IL2CPP: attach only while writing.
            auto* thread = il2cpp::api().thread_attach(il2cpp::api().domain_get());
            const std::string result = Create("hotkey");
            if (thread) il2cpp::api().thread_detach(thread);
            Log("report: %s", result.c_str());
        }
        down = pressed;
    }
}
}  // namespace

bool Init() {
    std::ifstream in(KeyFile());
    std::string saved;
    if (in >> saved) { const int code = KeyCode(saved); if (code >= 0) hotkey = code; }
    std::thread(Watch).detach();
    Log("report: hotkey %s", KeyName(hotkey.load()).c_str());
    return true;
}

std::string Read() {
    std::lock_guard<std::mutex> hold(lastLock);
    return "{\"ok\":true,\"key\":" + Json(KeyName(hotkey.load())) + ",\"reports\":" + std::to_string(created.load()) +
        ",\"folder\":" + Json(Utf8(ReportsRoot().wstring())) + ",\"last\":" + Json(lastFolder) + "}";
}

std::string SetKey(const std::string& key) {
    const int code = KeyCode(key);
    if (code < 0) return "{\"ok\":false,\"error\":\"use reportkey F5-F12 or off\"}";
    hotkey = code;
    std::error_code ignored;
    std::filesystem::create_directories(KeyFile().parent_path(), ignored);
    Write(KeyFile(), KeyName(code) + "\n");
    return Read();
}

std::string Create(const char* trigger) {
    if (busy.exchange(true)) return "{\"ok\":false,\"error\":\"A report is already being written.\"}";
    struct Release { ~Release() { busy = false; } } release;
    try {
        const auto folder = ReportsRoot() / std::filesystem::path(Stamp());
        std::filesystem::create_directories(folder);
        // The game writes the screenshot at the end of the current frame.
        std::string screenshot = "requested";
        const std::string imagePath = Utf8((folder / L"screenshot.png").wstring());
        std::string why;
        if (!mainthread::Run([&] {
            Root name(il2cpp::api().string_new(imagePath.c_str())); int scale = 1; void* a[]{name.Get(), &scale};
            Invoke(game::FindMethod("UnityEngine.ScreenCaptureModule.dll", "UnityEngine", "ScreenCapture", "CaptureScreenshot", 2), nullptr, a);
        }, 3000, &why)) screenshot = "unavailable: " + why;
        // State through the same read-only commands the app uses.
        std::ostringstream info;
        info << "EpochPact " << EP_VERSION_STR << " in-game report (" << trigger << ")\n";
        for (const char* command : {"sessionread", "identityread", "qolread", "pickupradius", "mapsettings", "tierread", "reportread"}) {
            std::string reply;
            try { reply = commands::Execute(command); } catch (const std::exception& e) { reply = std::string("error: ") + e.what(); }
            if (reply.size() > 8000) reply = reply.substr(0, 8000) + "...";
            info << "\n[" << command << "]\n" << reply << "\n";
        }
        info << "\n[screenshot]\n" << screenshot << "\n";
        Write(folder / L"info.txt", Mask(info.str()));
        Write(folder / L"core-log.txt", Mask(Tail(std::filesystem::path(PluginDir()) / L"logs" / L"core.log")));
        const auto appLogs = std::filesystem::path(Env(L"LOCALAPPDATA")) / L"EpochPact" / L"logs";
        std::error_code ec;
        if (std::filesystem::is_directory(appLogs, ec))
            for (const auto& entry : std::filesystem::directory_iterator(appLogs, ec))
                if (entry.is_regular_file() && entry.path().extension() == L".log")
                    Write(folder / (L"app-" + entry.path().filename().wstring() + L".txt"), Mask(Tail(entry.path(), 256 * 1024)));
        Write(folder / L"description.txt",
            "What did you do, what did you expect, and what happened instead?\r\n\r\n\r\n"
            "Share this whole folder (zip it) in the EpochPact Discord. Your Windows user and computer names are masked; logs can contain character names.\r\n");
        const std::string name = Utf8(folder.filename().wstring());
        { std::lock_guard<std::mutex> hold(lastLock); lastFolder = Utf8(folder.wstring()); }
        ++created;
        // TextMeshPro reads a literal "\r" in a path as a line break: show it with "/".
        Notify("EpochPact report saved", "EpochPact/reports/" + name);
        return "{\"ok\":true,\"folder\":" + Json(Utf8(folder.wstring())) + ",\"screenshot\":" + Json(screenshot) + "}";
    } catch (const std::exception& e) {
        return "{\"ok\":false,\"error\":" + Json(e.what()) + "}";
    }
}

}  // namespace ep::report
