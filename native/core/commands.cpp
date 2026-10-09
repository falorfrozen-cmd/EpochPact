#include "commands.hpp"

#include "common.hpp"
#include "file_safety.hpp"
#include "cof.hpp"
#include "crafting.hpp"
#include "map_view.hpp"
#include "collection.hpp"
#include "smart_loot.hpp"
#include "cof_tuning.hpp"
#include "density.hpp"
#include "factions.hpp"
#include "game.hpp"
#include "items.hpp"
#include "loot.hpp"
#include "mainthread.hpp"
#include "managed.hpp"
#include "monolith.hpp"
#include "player.hpp"
#include "progression.hpp"
#include "stat_editor.hpp"
#include "version.hpp"
#include "xp.hpp"
#ifdef EPOCHPACT_RESEARCH
#include "research.hpp"
#endif

#include <cstdio>
#include <cstdlib>
#include <charconv>
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
    return files::ClaimPacket(path, [](const char* message) { Log("%s", message); });
}

void Append(const std::wstring& path, const std::string& text) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"ab") != 0 || !f) return;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

void Publish(const std::wstring& path, const std::string& text) {
    const auto tmp = path + L".tmp";
    FILE* f = nullptr;
    if (_wfopen_s(&f, tmp.c_str(), L"wb") || !f) return;
    const bool written = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    if (written) {
        for (unsigned attempt = 0; attempt < 250; ++attempt) {
            if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return;
            const auto error = GetLastError();
            if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) break;
            Sleep(2); // The client may briefly hold the old file. Never rerun the command.
        }
        Log("IPC: atomic reply publication failed: error %lu", GetLastError());
    }
}

}  // namespace

std::string Execute(const std::string& line) {
    const std::vector<std::string> parts = Split(line);
    if (parts.empty()) return {};
    const std::string& cmd = parts[0];
    const std::vector<std::string> args(parts.begin() + 1, parts.end());
    if(cmd=="atlasread" || cmd=="stashread") {
        const auto error=[](const char* s){return "{\"ok\":false,\"error\":"+managed::Json(s)+"}";};
        if(cmd=="atlasread")return args.empty()?collection::Catalog():error("atlasread expects no arguments");
        int offset=0;
        if(args.size()>1)return error("use stashread [page offset]");
        if(!args.empty()) {const auto [end,ec]=std::from_chars(args[0].data(),args[0].data()+args[0].size(),offset);if(ec!=std::errc{}||end!=args[0].data()+args[0].size()||offset<0||offset>65536)return error("invalid collection page offset");}
        return collection::Items(offset);
    }
    if (cmd == "lootread" || cmd == "lootmode" || cmd == "lootlp" || cmd == "loott7" || cmd == "lootfilter" || cmd == "lootaffixes" || cmd == "lootcategory" || cmd == "lootreset" ||
        cmd == "craftread" || cmd == "craftfp" || cmd == "crafthope" || cmd == "craftdespair" || cmd == "craftshards" || cmd == "craftrunes" || cmd == "craftglyphs" || cmd == "craftlevel" || cmd == "craftreset") {
        const auto error=[](const char* message){return "{\"ok\":false,\"error\":"+managed::Json(message)+"}";};
        if(cmd=="lootread" || cmd=="craftread") {
            if(!args.empty()) return error("read expects no arguments");
            return cmd=="lootread" ? smartloot::Read() : crafting::Read();
        }
        if(cmd=="lootreset" || cmd=="craftreset") {
            if(!args.empty()) return error("reset expects no arguments");
            return cmd=="lootreset" ? smartloot::Set("reset","") : crafting::Set("reset",0);
        }
        if(cmd=="lootcategory") return args.size()==2 ? smartloot::Set(args[0],args[1]) : error("use lootcategory materials/gold/potions/xp/favor/bones 0|1");
        if(args.size()!=1) return error("expected exactly one setting value");
        if(cmd.starts_with("loot")) return smartloot::Set(cmd.substr(4),args[0]);
        double value=0;
        if((cmd=="crafthope"||cmd=="craftdespair") && args[0]=="reset") value=-1;
        else if(!statedit::Number(args[0],&value) || value<0) return error("expected finite nonnegative crafting value or glyph reset");
        return crafting::Set(cmd.substr(5),value);
    }
    if(cmd=="craftforge") return args.size()==1 ? crafting::Forge(args[0]) : "{\"ok\":false,\"error\":\"use craftforge offline-save-id\"}";
    if(cmd=="mapread") return args.empty() ? mapview::Read() : "{\"ok\":false,\"error\":\"mapread expects no arguments\"}";
    if(cmd=="mapreveal") {
        double value=0;
        return args.size()==1 && statedit::Number(args[0],&value) ? mapview::Set(value) : "{\"ok\":false,\"error\":\"use mapreveal 0|1\"}";
    }
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
    if(cmd=="crafttest") return args.size()==2 ? crafting::Test(args[0],args[1]) : "{\"ok\":false,\"error\":\"use crafttest isolated-id action\"}";
    if(cmd=="mapcapture") return args.empty() ? mapview::Capture() : "{\"ok\":false,\"error\":\"mapcapture expects no arguments\"}";
#endif
#ifdef EPOCHPACT_RESEARCH
    if(cmd=="loottest") return args.size()==2 ? smartloot::Test(args[0],args[1]) : "{\"ok\":false,\"error\":\"use loottest isolated-id action\"}";
#endif
    if (cmd == "sessionread") return managed::Run([] {
        const auto s = managed::SessionState();
        const char* names[]{"None", "SystemLoading", "Login", "CharacterSelect", "InGame"};
        return "{\"ok\":true,\"state\":" + managed::Json(s.state >= 0 && s.state < 5 ? names[s.state] : "Unknown") +
            ",\"transitioning\":" + managed::Boolean(s.transitioning) + "}";
    });
    if (cmd == "frameread" || cmd == "framereset") return args.empty() ? mainthread::Telemetry(cmd == "framereset") : "frame: refused: no arguments expected";

    if (cmd == "status") {
        std::string s = std::string("EpochPact ") + kVersion + "; gate: " + game::GateText() + "\n" + xp::Status() + "\n" +
                        loot::Status() + "\n" + density::Status() + "\n" + monolith::Status() + "\n" + cof::Status() + "\n" + items::Status() + "\n" + player::Status() + "\nframe hook: " +
                        (mainthread::HookInstalled() ? "in" : "out (idle)");
#ifdef EPOCHPACT_RESEARCH
        s += "\n" + research::Status();
#endif
        return s;
    }
    if (cmd == "density") {
        if (args.empty()) return density::Status();
        double value = 0;
        if (args.size() != 1 || !statedit::Number(args[0], &value)) return "density: refused: use density <finite multiplier 1-5>";
        return density::Set(value);
    }
    if (cmd == "densityread") return args.empty() ? density::Read() : "densityread: refused: no arguments expected";
    if (cmd == "progressread") return args.empty() ? progression::Read() : "progressread: refused: no arguments expected";
    if (cmd == "identityread") return args.empty() ? progression::Identity() : "identityread: refused: no arguments expected";
    if (cmd == "questscomplete") return args.size() == 1 ? progression::Complete(args[0]) : "questscomplete: refused: use questscomplete <offline save id from progressread>";
    if (cmd == "waypointsunlock") return args.size() == 1 ? progression::UnlockWaypoints(args[0]) : "waypointsunlock: refused: use waypointsunlock <offline save id from progressread>";
    if (cmd == "cofchargemult" || cmd == "cofrewardmult" || cmd == "cofexaltedmult" || cmd == "coft7mult" || cmd == "coflpmult" || cmd == "cofdouble" || cmd == "coflensmult") {
        const auto error = [](const char* text) { return "{\"ok\":false,\"error\":" + managed::Json(text) + "}"; };
        const bool named = cmd == "cofdouble" || cmd == "coflensmult";
        if (args.size() != (named ? 2u : 1u)) return error("expected CoF tuning value; double/lens also require a target");
        std::string setting;
        if (named) {
            setting = args[0];
            if (cmd == "cofdouble" && setting != "enemy" && setting != "echo") return error("double target must be enemy or echo");
            if (cmd == "coflensmult" && setting != "celerity" && setting != "charity" && setting != "duplication") return error("lens target must be celerity, charity or duplication");
        } else setting = cmd == "cofchargemult" ? "charge" : cmd == "cofrewardmult" ? "reward" : cmd == "cofexaltedmult" ? "exalted" : cmd == "coft7mult" ? "t7" : "lp";
        double value = 0;
        if (cmd == "cofdouble" && args[1] == "reset") value = -1;
        else if (!statedit::Number(args.back(), &value) || (cmd == "cofdouble" && value < 0)) return error("expected a finite CoF tuning value");
        return cof::tuning::Set(setting, value);
    }
    if (cmd == "cofread" || cmd == "cofjoin" || cmd == "cofrank" || cmd == "coffavor" || cmd == "cofreputation" ||
        cmd == "coflenses" || cmd == "cofprophecy" || cmd == "cofpreview" || cmd == "cofcharges" || cmd == "coffavormult" || cmd == "cofrepmult"
#ifdef EPOCHPACT_RESEARCH
        || cmd == "coftestgain" || cmd == "coftestgainrep" || cmd == "coftestrep" || cmd == "coftestotherrep" || cmd == "coftestspend" || cmd == "coftestcharge" || cmd == "coftestreward" || cmd == "coftestloot" || cmd == "coftestground"
#endif
    ) {
        const auto error = [](const char* text) { return "{\"ok\":false,\"error\":" + managed::Json(text) + "}"; };
        const auto integer = [](const std::string& s, int& value) {
            const auto [end, result] = std::from_chars(s.data(), s.data() + s.size(), value);
            return result == std::errc{} && end == s.data() + s.size();
        };
        if (cmd == "cofread") return args.empty() ? cof::Read() : error("cofread expects no arguments");
        if (cmd == "coffavormult" || cmd == "cofrepmult") {
            double value = 0;
            if (args.size() != 1 || !statedit::Number(args[0], &value)) return error("use coffavormult/cofrepmult <finite multiplier 1..100>");
            return cmd == "cofrepmult" ? cof::ReputationMultiplier(value) : cof::Multiplier(value);
        }
        if (cmd == "cofjoin") {
            if (args.empty() || args.size() > 2 || (args.size() == 2 && args[1] != "switch")) return error("use cofjoin <offline id> [switch]");
            return cof::Join(args[0], args.size() == 2);
        }
        if (cmd == "coflenses") return args.size() == 1 ? cof::UnlockLenses(args[0]) : error("use coflenses <offline id>");
        int value = 0;
        if (cmd == "cofprophecy" || cmd == "cofpreview") {
            int reward = -1, lens = -1;
            if (args.size() != 4 || !integer(args[1], value) ||
                (args[2] != "none" && !integer(args[2], reward)) || (args[3] != "none" && !integer(args[3], lens)))
                return error("use cofprophecy/cofpreview <offline id> <slot 0..3> <reward id|none> <lens id|none>");
            return cof::Configure(args[0], value, reward, lens, cmd == "cofpreview");
        }
        if (cmd == "cofcharges") {
            int count = 0;
            if (args.size() != 3 || !integer(args[1], value) || !integer(args[2], count)) return error("use cofcharges <offline id> <slot 0..3> <charges 0..99>");
            return cof::Charges(args[0], value, count);
        }
#ifdef EPOCHPACT_RESEARCH
        if (cmd == "coftestground") return args.size() == 1 ? cof::TestGround(args[0]) : error("ground probe expects isolated offline id");
        if (cmd == "coftestloot") {
            if (args.size() != 3 || !integer(args[2], value)) return error("loot probe expects isolated id, enemy/echo/lp and count");
            return cof::TestLoot(args[0], args[1], value);
        }
        if (cmd == "coftestcharge" || cmd == "coftestreward") {
            int amount = 0;
            if (args.size() != 3 || !integer(args[1], value) || !integer(args[2], amount)) return error("expected isolated save id, slot and amount/target");
            return cmd == "coftestcharge" ? cof::TestCharge(args[0], value, amount) : cof::TestReward(args[0], value, amount);
        }
#endif
        if (args.size() != 2 || !integer(args[1], value)) return error("expected offline save id and an integer");
        if (cmd == "cofrank") return cof::Rank(args[0], value);
        if (cmd == "coffavor") return cof::Favor(args[0], value);
        if (cmd == "cofreputation") return cof::Reputation(args[0], value);
#ifdef EPOCHPACT_RESEARCH
        if (cmd == "coftestrep" || cmd == "coftestotherrep") return cof::TestReputation(args[0], value, cmd == "coftestotherrep");
        if (cmd == "coftestspend") return cof::TestSpend(args[0], value);
        return cof::TestGain(args[0], value, cmd == "coftestgainrep");
#endif
    }
    if (cmd == "monolithread") return args.empty() ? monolith::Read() : "monolithread: refused: no arguments expected";
    if (cmd == "factionread") return args.empty() ? factions::Read() : "factionread: refused: no arguments expected";
    if (cmd == "monolithunlock") return args.size() == 1 ? monolith::Unlock(args[0]) : "monolithunlock: refused: use monolithunlock <offline save id>";
    if (cmd == "stabilitymult") {
        if (args.empty()) return monolith::Status();
        double value = 0;
        if (args.size() != 1 || !statedit::Number(args[0], &value)) return "stabilitymult: refused: use stabilitymult <finite multiplier 1-100>";
        return monolith::SetMultiplier(value);
    }
    if (cmd == "monolithselect" || cmd == "monolithpanelready" || cmd == "echoread" || cmd == "echofocus" || cmd == "corruption" || cmd == "stability"
#ifdef EPOCHPACT_RESEARCH
        || cmd == "monolithtestgain"
#endif
    ) {
        const bool select = cmd == "monolithselect" || cmd == "monolithpanelready" || cmd == "echoread";
        if (args.size() != (select ? 3u : 4u)) return cmd + ": refused: use " + cmd + " <offline save id> <timeline id> <normal|empowered>" + (select ? "" : " <integer>");
        const auto integer = [](const std::string& s, int& v) {
            const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), v);
            return error == std::errc{} && end == s.data() + s.size();
        };
        int timeline = 0, value = 0;
        const int difficulty = args[2] == "normal" ? 0 : args[2] == "empowered" ? 1 : -1;
        if (!integer(args[1], timeline) || difficulty < 0 || (!select && !integer(args[3], value))) return cmd + ": refused: invalid timeline, difficulty or integer";
        if (cmd == "echoread") return monolith::Echoes(args[0], timeline, difficulty);
        if (cmd == "monolithpanelready") return monolith::PanelReady(args[0], timeline, difficulty);
        if (cmd == "echofocus") return monolith::FocusEcho(args[0], timeline, difficulty, value);
        if (select) return monolith::Select(args[0], timeline, difficulty);
        if (cmd == "corruption") return monolith::Corruption(args[0], timeline, difficulty, value);
        if (cmd == "stability") return monolith::Stability(args[0], timeline, difficulty, value);
#ifdef EPOCHPACT_RESEARCH
        return monolith::TestGain(args[0], timeline, difficulty, value);
#endif
    }
    if (cmd == "xp" || cmd == "gold" || cmd == "drops" || cmd == "rarity" || cmd == "speed" ||
        cmd == "cooldown") {
        if (args.empty()) {
            if (cmd == "xp") return xp::Status();
            if (cmd == "rarity") return items::RarityStatus();
            if (cmd == "speed" || cmd == "cooldown") return player::Status();
            return loot::Status();
        }
        char* end = nullptr;
        const double m = std::strtod(args[0].c_str(), &end);
        if (end == args[0].c_str() || *end) return cmd + ": refused: not a number: " + args[0];
        if (cmd == "xp") return xp::Set(m);
        if (cmd == "gold") return loot::SetGold(m);
        if (cmd == "drops") return loot::SetDrops(m);
        if (cmd == "rarity") return items::SetRarity(m);
        if (cmd == "speed") return player::SetSpeed(m);
        return player::SetCooldown(m);
    }
    if (cmd == "autopickup") {
        if (args.empty()) return items::AutoPickupStatus();
        char* end = nullptr;
        const double on = std::strtod(args[0].c_str(), &end);
        if (end == args[0].c_str() || *end) return "autopickup: refused: not a number: " + args[0];
        return items::SetAuto(on);
    }
    if (cmd == "stat") {
        if (args.empty()) return player::StatList();
        if (args.size() == 1) return player::ReadStat(args[0]);
        if (args.size() != 2) return "stat: refused: use stat <name> [value]";
        double v = 0;
        if (!statedit::Number(args[1], &v)) return "stat: refused: expected a finite float without overflow/underflow: " + args[1];
        return player::SetStat(args[0], v);
    }
    if (cmd == "sheetread") return player::SheetProbe();
    if (cmd == "sheetstats") return args.empty() ? statedit::Catalog() : "sheetstats: refused: no arguments expected";
    if (cmd == "statraw") return statedit::RawCommand(args);
    if (cmd == "sheetstat") return statedit::SheetCommand(args);
    if (cmd == "statreset") return args.empty() ? statedit::ResetAll() : "statreset: refused: no arguments expected";
    if (cmd == "playerread") return args.empty() ? statedit::PlayerRead() : "playerread: refused: no arguments expected";
    if (cmd == "characters") return args.empty() ? statedit::Characters() : "characters: refused: no arguments expected";
    if (cmd == "playoffline") return args.empty() ? statedit::PlayOffline() : "playoffline: refused: no arguments expected";
    if (cmd == "sheetopen") {
        if (args.size() != 1 || (args[0] != "0" && args[0] != "1")) return "sheetopen: refused: use sheetopen 0|1";
        return statedit::SheetOpen(args[0] == "1");
    }
    if (cmd == "loadname") {
        int64_t level = -1;
        if (args.empty() || args.size() > 2 || (args.size() == 2 && !statedit::Integer(args[1], 1, 100, &level)))
            return "loadname: refused: use loadname <exact name without spaces> [level]";
        return statedit::Characters(args[0], true, static_cast<int>(level));
    }
    if (cmd == "monolithrest") return args.size() == 1 ? statedit::Waypoints("M_Rest", args[0]) : "monolithrest: refused: use monolithrest <loaded offline save id>";
#ifdef EPOCHPACT_RESEARCH
    if (cmd == "waypoints") return args.empty() ? statedit::Waypoints() : "waypoints: refused: no arguments expected";
    if (cmd == "travel") return args.size() == 1 ? statedit::Waypoints(args[0]) : "travel: refused: use travel <unlocked waypoint scene>";
    if (cmd == "exits") return args.empty() ? statedit::Exits() : "exits: refused: no arguments expected";
    if (cmd == "exit") return args.size() == 1 ? statedit::Exits(args[0]) : "exit: refused: use exit <active scene destination>";
    std::string reply;
    if (research::Handle(cmd, args, &reply)) return reply;
#endif
    return "unknown command: " + cmd;
}

void Loop(il2cpp::Domain* domain) {
    const std::wstring dir = PluginDir() + L"ipc\\";
    EnsureDir(dir);
    const std::wstring in = dir + L"cmd.txt", out = dir + L"out.txt";
    files::Rotate(out, 1024 * 1024);
    Publish(dir + L"protocol.json", "{\"version\":2,\"pid\":" + std::to_string(GetCurrentProcessId()) + "}");
    const il2cpp::Api& a = il2cpp::api();
    for (;;) {
        mainthread::Housekeep();
#ifdef EPOCHPACT_RESEARCH
        research::Housekeep();
#endif
        if (GetFileAttributesW(in.c_str()) != INVALID_FILE_ATTRIBUTES) {
            // Attached to IL2CPP only while commands run: the runtime waits for attached
            // threads when the game quits, and this loop never ends.
            void* thread = a.thread_attach(domain);
            std::istringstream lines(ReadAndDelete(in));
            for (std::string line; std::getline(lines, line);) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.find_first_not_of(" \t") == std::string::npos) continue;
                std::string nonce;
                if (line[0] == '@') {
                    const auto space = line.find(' ');
                    if (space == std::string::npos || space < 2 || space > 65) continue;
                    nonce = line.substr(1, space - 1);
                    if (nonce.find_first_not_of("0123456789abcdef") != std::string::npos) continue;
                    line = line.substr(space + 1);
                }
                const std::string reply = Execute(line);
                Log("command: %s", line.c_str());
                if (nonce.empty()) Append(out, "> " + line + "\r\n" + reply + "\r\n");
                if (!nonce.empty()) Publish(dir + L"reply.json", "{\"nonce\":" + managed::Json(nonce) +
                    ",\"reply\":" + managed::Json(reply) + "}");
            }
            if (thread) a.thread_detach(thread);
        }
        Sleep(10);
    }
}

}  // namespace ep::commands
