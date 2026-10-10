#include "../core/file_safety.hpp"
#include "../core/mutation_transaction.hpp"
#include "../core/cooldown_rules.hpp"
#include <cmath>
#include <iostream>

namespace fs = std::filesystem;
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* label) { ++checks; if (!ok) { ++failures; std::cerr << "FAIL " << label << '\n'; } }
std::string Quote(const std::string& s) { return '"' + s + '"'; }
void Write(const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; }
void Transactions() {
    for (int scenario = 0; scenario < 9; ++scenario) {
        bool main = false; int writes = 0, applies = 0, runs = 0;
        auto run = [&](auto fn) {
            ++runs; main = true; std::string reply;
            try { reply = fn(); } catch (const std::exception&) { reply = "{\"ok\":false,\"error\":\"refused\"}"; }
            main = false;
            if (scenario == 4 && runs == 2) return std::string("{\"ok\":false,\"error\":\"timeout\"}");
            if (scenario == 8 && runs == 1) return std::string("{\"ok\":false}");
            return reply;
        };
        auto result = ep::transaction::Execute(run, [&] {
            Check(main, "capture runs on executor");
            if (scenario == 1) throw std::runtime_error("invalid identity");
            return scenario == 2 ? std::string("{\"ok\":true,\"changed\":false}") : std::string();
        }, [&] {
            ++writes; Check(!main, "disk write is outside main executor");
            if (scenario == 3) throw std::runtime_error("disk failure");
            return scenario == 7 ? std::string() : std::string("backup");
        }, [&] {
            Check(main, "apply runs on executor");
            if (scenario == 5) throw std::runtime_error("actor changed during backup");
            ++applies;
            return scenario == 6 ? std::string("broken") : std::string("{\"ok\":true}");
        }, Quote);
        Check(writes == ((scenario == 1 || scenario == 2 || scenario == 8) ? 0 : 1), "preparation failures do not write");
        Check(applies == ((scenario == 0 || scenario == 4 || scenario == 6) ? 1 : 0), "mutation is never repeated or applied without backup");
        Check((result.find("\"backup\"") != std::string::npos) == (scenario == 0 || scenario == 4 || scenario == 5 || scenario == 6), "post-backup errors preserve recovery path");
    }
}
void Cooldowns() {
    for (double multiplier : {1., 2., 5., 10.}) {
        const float delta = ep::player::rules::CooldownDelta(.02f, multiplier, true);
        Check(std::abs(delta - .02f * multiplier) < 1e-6, "player delta scales once");
        Check(ep::player::rules::CooldownDelta(.02f, multiplier, false) == .02f, "non-owner remains unchanged");
        float remaining = 1;
        int ticks = 0;
        while (remaining > 1e-6 && ticks < 100) { remaining -= delta; ++ticks; }
        Check(ticks == static_cast<int>(50 / multiplier), "countdown finishes at N not N squared");
    }
    Check(ep::player::rules::CooldownDelta(.02f, 11, true) == .02f, "invalid multiplier unchanged");
    Check(ep::player::rules::CooldownDelta(-1, 5, true) == -1, "negative delta unchanged");
}
int main() {
    Transactions(); Cooldowns();
    wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH, temp);
    const auto root = fs::path(temp) / (L"epochpact-review-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    fs::create_directory(root);
    const auto backups = root / "backups"; fs::create_directory(backups);
    for (int i = 0; i < 35; ++i) {
        auto path = backups / std::to_string(i); fs::create_directory(path);
        Write(path / ".epochpact-retention", "1\n"); Write(path / "manifest.json", "{}");
        Write(path / "save", "owner save");
        fs::last_write_time(path / "manifest.json", fs::file_time_type::clock::now() + std::chrono::seconds(i));
    }
    auto legacy = backups / "legacy"; fs::create_directory(legacy); Write(legacy / "manifest.json", "{}");
    auto incomplete = backups / "incomplete"; fs::create_directory(incomplete); Write(incomplete / ".epochpact-retention", "1\n");
    auto unknown = backups / "unknown"; fs::create_directory(unknown); Write(unknown / "manifest.json", "{}"); Write(unknown / ".epochpact-retention", "2\n");
    Check(ep::files::Prune(backups, 30, backups / "34") == 5, "30 managed completed snapshots retained");
    Check(fs::exists(legacy / "manifest.json"), "historical backup protected");
    Check(fs::exists(incomplete), "uncommitted snapshot protected");
    Check(fs::exists(unknown), "unknown retention version protected");
    Check(!fs::exists(backups / "4") && fs::exists(backups / "5"), "oldest managed entries expire");
    Check(ep::files::Prune(backups, 1, backups / "5") == 28 && fs::exists(backups / "5"), "current snapshot protected even with old timestamp");
    const auto packet = root / "cmd.txt"; Write(packet, "first\n");
    Check(ep::files::ClaimPacket(packet.wstring(), [](const char*) {}, [&] { Write(packet, "next\n"); }) == "first\n", "claimed first packet read");
    Check(fs::exists(packet) && ep::files::ClaimPacket(packet.wstring(), [](const char*) {}) == "next\n", "producer's next packet survives read/delete race");
    Write(packet.wstring() + L".claimed", "stale");
    Check(ep::files::ClaimPacket(packet.wstring(), [](const char*) {}).empty(), "stale claimed packet never replayed");
    Write(packet, std::string(128 * 1024 + 1, 'x'));
    Check(ep::files::ClaimPacket(packet.wstring(), [](const char*) {}).empty(), "oversized packet refused");
    const auto log = root / "core.log"; Write(log, std::string(40, 'x'));
    Check(!ep::files::Rotate(log, 50) && fs::exists(log), "small log kept");
    Check(ep::files::Rotate(log, 30) && fs::file_size(root / "core.old.log") == 40, "large log rotated");
    Write(log, std::string(60, 'y'));
    Check(ep::files::Rotate(log, 30) && fs::file_size(root / "core.old.log") == 60, "only one rotated log retained");
    // Recursive cleanup is restricted to this explicitly created temp fixture.
    if (fs::canonical(root).parent_path() == fs::canonical(fs::path(temp))) fs::remove_all(root);
    else Check(false, "temp fixture boundary");
    std::cout << "review: " << checks - failures << '/' << checks << " passed\n";
    return failures ? 1 : 0;
}
