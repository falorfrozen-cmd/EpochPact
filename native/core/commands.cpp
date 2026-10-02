#include "commands.hpp"

#include "common.hpp"
#include "game.hpp"
#include "loot.hpp"
#include "mainthread.hpp"
#include "version.hpp"
#include "xp.hpp"
#ifdef EPOCHPACT_RESEARCH
#include "research.hpp"
#endif

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace ep::commands {

namespace {

std::vector<std::string> Split(const std::string& line) {
    std::vector<std::string> parts;
    std::istringstream in(line);
    for (std::string w; in >> w;) parts.push_back(w);
    return parts;
}

std::string ReadAndDelete(const std::wstring& path) {
    std::string text;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return text;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    DeleteFileW(path.c_str());
    return text;
}

void Append(const std::wstring& path, const std::string& text) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"ab") != 0 || !f) return;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

}  // namespace

std::string Execute(const std::string& line) {
    const std::vector<std::string> parts = Split(line);
    if (parts.empty()) return {};
    const std::string& cmd = parts[0];
    const std::vector<std::string> args(parts.begin() + 1, parts.end());

    if (cmd == "status") {
        std::string s = std::string("EpochPact ") + kVersion + "; gate: " + game::GateText() + "\n" + xp::Status() + "\n" +
                        loot::Status() + "\nframe hook: " + (mainthread::HookInstalled() ? "in" : "out (idle)");
#ifdef EPOCHPACT_RESEARCH
        s += "\n" + research::Status();
#endif
        return s;
    }
    if (cmd == "xp" || cmd == "gold" || cmd == "drops" || cmd == "density") {
        if (args.empty()) return cmd == "xp" ? xp::Status() : loot::Status();
        char* end = nullptr;
        const double m = std::strtod(args[0].c_str(), &end);
        if (end == args[0].c_str() || *end) return cmd + ": refused: not a number: " + args[0];
        if (cmd == "xp") return xp::Set(m);
        if (cmd == "gold") return loot::SetGold(m);
        if (cmd == "drops") return loot::SetDrops(m);
        return loot::SetDensity(m);
    }
#ifdef EPOCHPACT_RESEARCH
    std::string reply;
    if (research::Handle(cmd, args, &reply)) return reply;
#endif
    return "unknown command: " + cmd;
}

void Loop(il2cpp::Domain* domain) {
    const std::wstring dir = PluginDir() + L"ipc\\";
    EnsureDir(dir);
    const std::wstring in = dir + L"cmd.txt", out = dir + L"out.txt";
    const il2cpp::Api& a = il2cpp::api();
    for (;;) {
        mainthread::Housekeep();
        if (GetFileAttributesW(in.c_str()) != INVALID_FILE_ATTRIBUTES) {
            // Attached to IL2CPP only while commands run: the runtime waits for attached
            // threads when the game quits, and this loop never ends.
            void* thread = a.thread_attach(domain);
            std::istringstream lines(ReadAndDelete(in));
            for (std::string line; std::getline(lines, line);) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.find_first_not_of(" \t") == std::string::npos) continue;
                const std::string reply = Execute(line);
                Log("command: %s", line.c_str());
                Append(out, "> " + line + "\r\n" + reply + "\r\n");
            }
            if (thread) a.thread_detach(thread);
        }
        Sleep(100);
    }
}

}  // namespace ep::commands
