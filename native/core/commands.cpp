#include "commands.hpp"

#include "common.hpp"
#include "file_safety.hpp"
#include "cof.hpp"
#include "crafting.hpp"
#include "workshop.hpp"
#include "map_view.hpp"
#include "collection.hpp"
#include "smart_loot.hpp"
#include "spawn_items.hpp"
#include "cof_tuning.hpp"
#include "density.hpp"
#include "factions.hpp"
#include "game.hpp"
#include "items.hpp"
#include "loot.hpp"
#include "mainthread.hpp"
#include "lifecycle.hpp"
#include "managed.hpp"
#include "monolith.hpp"
#include "player.hpp"
#include "progression.hpp"
#include "stat_editor.hpp"
#include "version.hpp"
#include "xp.hpp"
#include "build_controls.hpp"
#include "item_tiers.hpp"
#include "stash_rules.hpp"
#include "fonts.hpp"
#include "report.hpp"
#include "convenience.hpp"
#include "inventory_tools.hpp"
#include "rage.hpp"
#include "weaver.hpp"
#include "idols.hpp"
#include "effects.hpp"
#include "ingame_panel.hpp"
#include "ipc_protocol.hpp"
#include <mutex>
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
    if (cmd == "connectionping") return args.empty() ? ipc::Ping(ipc::CurrentOwner()) :
        "{\"ok\":false,\"error\":\"connectionping expects no arguments\"}";
    if(cmd=="buildread")return args.empty()?builds::Read():"{\"ok\":false,\"error\":\"buildread expects no arguments\"}";
    if(cmd=="treerespec")return args.size()==2?builds::Respec(args[0],args[1]):"{\"ok\":false,\"error\":\"use treerespec save-id skills|passives|weaver\"}";
    if(cmd=="inventoryread")return args.empty()?convenience::InventoryRead():"{\"ok\":false,\"error\":\"inventoryread expects no arguments\"}";
    if(cmd=="qolread")return args.empty()?convenience::Read():"{\"ok\":false,\"error\":\"qolread expects no arguments\"}";
    if(cmd=="panel")return args.size()==1?ingame::Panel(args[0]):"{\"ok\":false,\"error\":\"use panel read|0|1\"}";
    if(cmd=="panelkey")return args.size()==1?ingame::SetKey(args[0]):"{\"ok\":false,\"error\":\"use panelkey F5-F12|off\"}";
    if(cmd=="effectsread")return args.empty()?effects::Read():"{\"ok\":false,\"error\":\"effectsread expects no arguments\"}";
    if(cmd=="effectsset"){double value=0;return args.size()==2&&statedit::Number(args[1],&value)?effects::Set(args[0],value):"{\"ok\":false,\"error\":\"use effectsset setting value\"}";}
    if(cmd=="damagesession")return args.size()<=1?convenience::DamageSession(args.empty()?"":args[0]):"{\"ok\":false,\"error\":\"use damagesession [start|stop|reset]\"}";
    if(cmd=="deathrecap")return args.size()<=1?convenience::DeathRecap(args.empty()?"":args[0]):"{\"ok\":false,\"error\":\"use deathrecap [0|1|clear]\"}";
    if(cmd=="damageicon")return args.size()==1?convenience::DamageIcon(args[0]):"{\"ok\":false,\"error\":\"use damageicon a:<skill ID>|c:<class>:<mastery>\"}";
    if(cmd=="summonread")return args.empty()?convenience::Summons():"{\"ok\":false,\"error\":\"summonread expects no arguments\"}";
    if(cmd=="qolarea")return args.size()==2?convenience::SetArea(args[0],args[1]):"{\"ok\":false,\"error\":\"use qolarea radius|shape|show|color value\"}";
    if(cmd=="qolset"){double value=0;return args.size()==2&&statedit::Number(args[1],&value)?convenience::Set(args[0],value):"{\"ok\":false,\"error\":\"use qolset setting value\"}";}
    if(cmd=="tierread")return args.empty()?tiers::Read():"{\"ok\":false,\"error\":\"tierread expects no arguments\"}";
    if(cmd=="tierset"){int value=-1;if(args.size()==2){const auto r=std::from_chars(args[1].data(),args[1].data()+args[1].size(),value);if(r.ec!=std::errc{}||r.ptr!=args[1].data()+args[1].size()||value<0||value>15)value=-1;}
        return value>=0?tiers::Set(args[0],value):"{\"ok\":false,\"error\":\"use tierset icons|tooltips|lines|rules|ruleplain|special|namecolors|filtercolors 0|1, tierset rulepos 0|1|2 or tierset ruleparts 2..15\"}";}
    if(cmd=="tiercolor")return args.size()==1&&args[0]=="reset"?tiers::SetColor("reset",""):args.size()==2?tiers::SetColor(args[0],args[1]):"{\"ok\":false,\"error\":\"use tiercolor <1-8> <RRGGBB> or tiercolor reset\"}";
    if(cmd=="pickupinterval"){double value=0;return args.size()==1&&statedit::Number(args[0],&value)?items::SetInterval(value):"{\"ok\":false,\"error\":\"use pickupinterval 0.25..10\"}";}
    if(cmd=="lootlog"){unsigned long long since=0;if(args.size()>1||(args.size()==1&&std::from_chars(args[0].data(),args[0].data()+args[0].size(),since).ec!=std::errc{}))return "{\"ok\":false,\"error\":\"use lootlog [since]\"}";return items::LootLog(since);}
    if(cmd=="lootlogclear")return args.empty()?items::ClearLootLog():"{\"ok\":false,\"error\":\"lootlogclear expects no arguments\"}";
    if(cmd=="pickupnow")return args.empty()?items::PickupNow():"{\"ok\":false,\"error\":\"pickupnow expects no arguments\"}";
    if(cmd=="pickupradius"){double value=0;if(args.empty())return items::RadiusStatus();return args.size()==1&&statedit::Number(args[0],&value)?items::SetRadius(value):"{\"ok\":false,\"error\":\"use pickupradius 0..60\"}";}
    if(cmd=="reportread")return args.empty()?report::Read():"{\"ok\":false,\"error\":\"reportread expects no arguments\"}";
    if(cmd=="reportkey")return args.size()==1?report::SetKey(args[0]):"{\"ok\":false,\"error\":\"use reportkey F5-F12|off\"}";
    if(cmd=="reportnow")return args.empty()?report::Create("app"):"{\"ok\":false,\"error\":\"reportnow expects no arguments\"}";
    if(cmd=="fontread")return args.empty()?fonts::Read():"{\"ok\":false,\"error\":\"fontread expects no arguments\"}";
    if(cmd=="fontset")return args.size()==2?fonts::Set(args[0],args[1]):"{\"ok\":false,\"error\":\"use fontset family <name|game> | fontset scale 0.7-1.5 | fontset case 0|1\"}";
    if(cmd=="stashrules")return args.size()==1?stashrules::Read(args[0]):"{\"ok\":false,\"error\":\"use stashrules save-id\"}";
    if(cmd=="stashrulescreate")return args.size()==3&&(args[1]=="0"||args[1]=="1")&&(args[2]=="0"||args[2]=="1")?stashrules::Create(args[0],args[1]=="1",args[2]=="1"):"{\"ok\":false,\"error\":\"use stashrulescreate save-id hidden(0|1) disabled(0|1)\"}";
    if(cmd=="stashruleassign"){
        int64_t tab=0,rule=0;
        return args.size()==3&&statedit::Integer(args[1],0,1000000,&tab)&&statedit::Integer(args[2],0,10000,&rule)?stashrules::Assign(args[0],static_cast<int>(tab),static_cast<int>(rule)):"{\"ok\":false,\"error\":\"use stashruleassign save-id tab-id rule-number(0 clears)\"}";
    }
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
    if(cmd=="pickupdiag")return items::PickupDiagnostics();
    if(cmd=="stashrulestest")return args.size()==2?stashrules::Test(args[0],args[1]):"{\"ok\":false,\"error\":\"use stashrulestest save-id action\"}";
#endif
    if(cmd=="stashrulesort")return args.size()==1?stashrules::Sort(args[0]):"{\"ok\":false,\"error\":\"use stashrulesort save-id\"}";
    if(cmd=="inventoryaction")return args.size()==2?convenience::Inventory(args[0],args[1]):"{\"ok\":false,\"error\":\"use inventoryaction save-id stash|vendor|stashall|sellall\"}";
    if(cmd=="invread")return args.empty()?invtools::Read():"{\"ok\":false,\"error\":\"invread expects no arguments\"}";
    if(cmd=="invset"){long long value=-1;return args.size()==2&&statedit::Integer(args[1],0,2,&value)?invtools::Set(args[0],static_cast<int>(value)):"{\"ok\":false,\"error\":\"use invset autosort|autosortall 0|1 or invset sellkeep 0|1|2\"}";}
    if(cmd=="invsorttab"){long long tab=-1,on=-1;return args.size()==2&&statedit::Integer(args[0],0,999999,&tab)&&statedit::Integer(args[1],0,1,&on)?invtools::SetTab(static_cast<int>(tab),on==1):"{\"ok\":false,\"error\":\"use invsorttab <tab ID> 0|1\"}";}
    if(cmd=="craftmaterialsread")return args.empty()?crafting::MaterialsRead():"{\"ok\":false,\"error\":\"craftmaterialsread expects no arguments\"}";
    if(cmd=="craftmaterials") {
        int target=0;
        if(args.size()==2){const auto parsed=std::from_chars(args[1].data(),args[1].data()+args[1].size(),target);if(parsed.ec==std::errc{}&&parsed.ptr==args[1].data()+args[1].size())return crafting::MaximizeMaterials(args[0],target);}
        return "{\"ok\":false,\"error\":\"use craftmaterials save-id target-1-to-1000000\"}";
    }
    if(cmd=="affixinfo"){int id=-1;if(args.size()==1){const auto r=std::from_chars(args[0].data(),args[0].data()+args[0].size(),id);if(r.ec!=std::errc{}||r.ptr!=args[0].data()+args[0].size())id=-1;}return smartloot::AffixInfo(id);}
    if(cmd=="spawnlist")return args.empty()?spawn::List():"{\"ok\":false,\"error\":\"spawnlist expects no arguments\"}";
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
    if(cmd=="spawncheck"){long long rarity=0,lp=0,from=0,count=0;
        if(args.size()==4&&statedit::Integer(args[0],0,3,&rarity)&&statedit::Integer(args[1],-1,4,&lp)&&statedit::Integer(args[2],0,100000,&from)&&statedit::Integer(args[3],1,200,&count))
            return spawn::Check(static_cast<int>(rarity),static_cast<int>(lp),static_cast<int>(from),static_cast<int>(count));
        return "{\"ok\":false,\"error\":\"use spawncheck rarity lp from count\"}";}
    if(cmd=="idolcheck"){long long count=0,level=0;
        if(args.size()==2&&statedit::Integer(args[0],1,100,&count)&&statedit::Integer(args[1],1,100,&level))return spawn::IdolCheck(static_cast<int>(count),static_cast<int>(level));
        return "{\"ok\":false,\"error\":\"use idolcheck count level\"}";}
    if(cmd=="fpcheck"){long long count=0,level=0;
        if(args.size()==2&&statedit::Integer(args[0],1,200,&count)&&statedit::Integer(args[1],1,100,&level))return spawn::ForgingCheck(static_cast<int>(count),static_cast<int>(level));
        return "{\"ok\":false,\"error\":\"use fpcheck count level\"}";}
#endif
    if(cmd=="spawnitem") {
        long long rarity=0,lp=0,level=0,count=0;
        if(args.size()==6&&args[1].size()<=16&&statedit::Integer(args[2],0,3,&rarity)&&statedit::Integer(args[3],-1,4,&lp)&&statedit::Integer(args[4],1,100,&level)&&
           statedit::Integer(args[5],1,spawn::kMaximumCount,&count))
            return spawn::Spawn(args[0],args[1],static_cast<int>(rarity),static_cast<int>(lp),static_cast<int>(level),static_cast<int>(count));
        return "{\"ok\":false,\"error\":\"use spawnitem save-id b<type>.<subtype>|u<unique id> rarity-0-3 lp-(-1)-4 level-1-100 count-1-20\"}";
    }
    if(cmd=="craftmods")return args.empty()?workshop::Catalog():"{\"ok\":false,\"error\":\"craftmods expects no arguments\"}";
    if(cmd=="craftmix") {
        int templateId=0,perfect=0;
        try{if(args.size()==5){size_t a=0,b=0;templateId=std::stoi(args[1],&a);perfect=std::stoi(args[3],&b);if(a==args[1].size()&&b==args[3].size()&&(perfect==0||perfect==1))return crafting::Custom(args[0],templateId,args[2],perfect,args[4]=="none"?"":args[4]);}}catch(const std::exception&){}
        return "{\"ok\":false,\"error\":\"use craftmix save-id template-id affixes perfect modifier-lines\"}";
    }
    if(cmd=="craftcustom") {
        int id=-2,perfect=-1;
        if(args.size()==4){const auto a=std::from_chars(args[1].data(),args[1].data()+args[1].size(),id),b=std::from_chars(args[3].data(),args[3].data()+args[3].size(),perfect);if(a.ec==std::errc{}&&a.ptr==args[1].data()+args[1].size()&&b.ec==std::errc{}&&b.ptr==args[3].data()+args[3].size()&&(perfect==0||perfect==1))return crafting::Custom(args[0],id,args[2],perfect!=0);}
        return "{\"ok\":false,\"error\":\"use craftcustom save-id template-id|-1 affix-ids|keep perfect-0|1\"}";
    }
    if(cmd=="buildset") {
        double value=0;
        return args.size()==2 && statedit::Number(args[1],&value)?builds::Set(args[0],value):"{\"ok\":false,\"error\":\"use buildset setting value\"}";
    }
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
    if(cmd=="effectstest")return args.size()==2?effects::Test(args[0],args[1]):"{\"ok\":false,\"error\":\"use effectstest isolated-id action\"}";
    if(cmd=="ragetest")return args.size()==2?rage::Test(args[0],args[1]):"{\"ok\":false,\"error\":\"use ragetest isolated-id action\"}";
    if(cmd=="qoltest")return args.size()==2?convenience::Test(args[0],args[1]):"{\"ok\":false,\"error\":\"use qoltest isolated-id action\"}";
    if(cmd=="masterytest")return args.size()==2?progression::TestMastery(args[0],args[1]):"{\"ok\":false,\"error\":\"use masterytest isolated-id read|close|select|confirm|cancel\"}";
    if(cmd=="buildtest")return args.size()==2?builds::Test(args[0],args[1]):"{\"ok\":false,\"error\":\"use buildtest isolated-id action\"}";
#endif
    if(cmd=="atlasread" || cmd=="stashread") {
        const auto error=[](const char* s){return "{\"ok\":false,\"error\":"+managed::Json(s)+"}";};
        if(cmd=="atlasread")return args.empty()?collection::Catalog():error("atlasread expects no arguments");
        int offset=0;
        if(args.size()>1)return error("use stashread [page offset]");
        if(!args.empty()) {const auto [end,ec]=std::from_chars(args[0].data(),args[0].data()+args[0].size(),offset);if(ec!=std::errc{}||end!=args[0].data()+args[0].size()||offset<0||offset>65536)return error("invalid collection page offset");}
        return collection::Items(offset);
    }
    if (cmd == "lootread" || cmd == "lootmode" || cmd == "lootlp" || cmd == "lootww" || cmd == "loott7" || cmd == "lootfilter" || cmd == "lootaffixes" || cmd == "lootcategory" || cmd == "lootreset" ||
        cmd == "craftread" || cmd == "craftfp" || cmd == "crafthope" || cmd == "craftdespair" || cmd == "craftshards" || cmd == "craftrunes" || cmd == "craftglyphs" || cmd == "craftlevel" || cmd == "craftt7" || cmd == "craftreset") {
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
    if(cmd=="mapsettings") return args.empty() ? mapview::SettingsRead() : "{\"ok\":false,\"error\":\"mapsettings expects no arguments\"}";
    if(cmd=="mapset") {
        double value=0;
        if(args.size()==1&&args[0]=="reset") return mapview::SetSetting("reset",0);
        return args.size()==2 && statedit::Number(args[1],&value) ? mapview::SetSetting(args[0],value) : "{\"ok\":false,\"error\":\"use mapset <key> <value> or mapset reset\"}";
    }
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

    // Rage of Morditas (Season 5): rageread; rageset spawn|hunttime|charges|buffs|forging <value>.
    if (cmd == "rageread") return args.empty() ? rage::Read() : "{\"ok\":false,\"error\":\"rageread expects no arguments\"}";
    if (cmd == "rageset") {
        char* end = nullptr;
        const double v = args.size() == 2 ? std::strtod(args[1].c_str(), &end) : 0;
        if (args.size() != 2 || end == args[1].c_str() || *end) return "{\"ok\":false,\"error\":\"use rageset spawn|hunttime|charges|buffs|forging <value>\"}";
        return rage::Set(args[0], v);
    }
    if (cmd == "weaverread") return args.empty() ? weaver::Read() : "{\"ok\":false,\"error\":\"weaverread expects no arguments\"}";
    if (cmd == "weaveraffixes") return args.size() == 1 ? weaver::SetAffixes(args[0]) : "{\"ok\":false,\"error\":\"use weaveraffixes <id,id,...|none>\"}";
    if (cmd == "weaversdrop" || cmd == "weaversupgrades") {
        if (args.empty()) return weaver::Status();
        char* end = nullptr;
        const double v = std::strtod(args[0].c_str(), &end);
        if (args.size() != 1 || end == args[0].c_str() || *end) return cmd + ": refused: not a number: " + (args.empty() ? std::string() : args[0]);
        return cmd == "weaversdrop" ? weaver::SetShare(v) : weaver::SetSpeed(v);
    }
    if (cmd == "idolprefixes" || cmd == "idolsuffixes") {
        if (args.empty()) return idols::Status();
        char* end = nullptr;
        const double v = std::strtod(args[0].c_str(), &end);
        if (args.size() != 1 || end == args[0].c_str() || *end) return cmd + ": refused: not a number: " + (args.empty() ? std::string() : args[0]);
        return cmd == "idolprefixes" ? idols::SetPrefixes(v) : idols::SetSuffixes(v);
    }
    if (cmd == "status") {
        std::string s = std::string("EpochPact ") + kVersion + "; gate: " + game::GateText() + "\n" + xp::Status() + "\n" +
                        loot::Status() + "\n" + density::Status() + "\n" + monolith::Status() + "\n" + cof::Status() + "\n" + items::Status() + "\n" + weaver::Status() + "\n" + idols::Status() + "\n" + player::Status() + "\nframe hook: " +
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
    if (cmd == "masteryopen") return args.size() == 1 ? progression::OpenMastery(args[0]) : "{\"ok\":false,\"error\":\"use masteryopen <offline save id from progressread>\"}";
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
        cmd == "cooldown" || cmd == "amber" || cmd == "weaverswill" || cmd == "lprolls" || cmd == "fpdrops") {
        if (args.empty()) {
            if (cmd == "xp") return xp::Status();
            if (cmd == "amber") return cof::AmberStatus();
            if (cmd == "rarity") return items::RarityStatus();
            if (cmd == "weaverswill") return items::WeaversWillStatus();
            if (cmd == "lprolls") return items::LegendaryRollsStatus();
            if (cmd == "fpdrops") return items::ForgingPotentialStatus();
            if (cmd == "speed" || cmd == "cooldown") return player::Status();
            return loot::Status();
        }
        char* end = nullptr;
        const double m = std::strtod(args[0].c_str(), &end);
        if (end == args[0].c_str() || *end) return cmd + ": refused: not a number: " + args[0];
        if (cmd == "xp") return xp::Set(m);
        if (cmd == "gold") return loot::SetGold(m);
        if (cmd == "amber") return cof::AmberMultiplier(m);
        if (cmd == "drops") return loot::SetDrops(m);
        if (cmd == "rarity") return items::SetRarity(m);
        if (cmd == "weaverswill") return items::SetWeaversWill(m);
        if (cmd == "lprolls") return items::SetLegendaryRolls(m);
        if (cmd == "fpdrops") return items::SetForgingPotential(m);
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
    if (cmd == "statrestore") return args.empty() ? statedit::Restore() : "statrestore: refused: no arguments expected";
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

namespace {
std::mutex uiQueueMutex;
std::string uiCommand;
std::function<void(const std::string&)> uiCompleted;
bool uiExecuting=false;
}
bool SubmitUi(std::string line,std::function<void(const std::string&)> completed){
    std::lock_guard<std::mutex> lock(uiQueueMutex);
    if(lifecycle::Stopping()||uiExecuting||!uiCommand.empty())return false;
    uiCommand=std::move(line);uiCompleted=std::move(completed);return true;
}
void Loop(il2cpp::Domain* domain) {
    const std::wstring dir = PluginDir() + L"ipc\\";
    EnsureDir(dir);
    const std::wstring in = dir + L"cmd.txt", out = dir + L"out.txt";
    files::Rotate(out, 1024 * 1024);
    const auto owner = ipc::CurrentOwner();
    // An announcement is optional. Replies themselves prove process ownership;
    // a protected/stale announcement cannot prevent a read-only handshake.
    Publish(dir + L"protocol.json", "{\"version\":3,\"pid\":" + std::to_string(owner.pid) +
        ",\"sessionKey\":\"" + owner.Key() + "\"}");
    const il2cpp::Api& a = il2cpp::api();
    while (!lifecycle::Stopping()) {
        mainthread::Housekeep();
        std::string click;std::function<void(const std::string&)> completed;
        {std::lock_guard<std::mutex> lock(uiQueueMutex);click=std::move(uiCommand);completed=std::move(uiCompleted);if(!click.empty())uiExecuting=true;}
        if(!click.empty()){
            void* thread=a.thread_attach(domain);std::string result;
            std::string why;if(!game::Guarded([&]{result=Execute(click);},&why))result="{\"ok\":false,\"error\":"+managed::Json(why)+"}";
            if(completed)completed(result);
            if(thread)a.thread_detach(thread);
            {std::lock_guard<std::mutex> lock(uiQueueMutex);uiExecuting=false;}
        }
#ifdef EPOCHPACT_RESEARCH
        research::Housekeep();
#endif
        if (GetFileAttributesW(in.c_str()) != INVALID_FILE_ATTRIBUTES) {
            // Attached to IL2CPP only while commands run: the runtime waits for attached
            // threads when the game quits, and this loop never ends.
            void* thread = a.thread_attach(domain);
            std::istringstream lines(ReadAndDelete(in));
            for (std::string line; std::getline(lines, line);) {
                if (lifecycle::Stopping()) break;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.find_first_not_of(" \t") == std::string::npos) continue;
                const auto packet = ipc::Parse(line, owner.Key());
                if (!packet.accepted && packet.error.empty()) continue;
                const std::string reply = packet.accepted ? Execute(packet.command) :
                    "{\"ok\":false,\"error\":" + managed::Json(packet.error) + "}";
                Log("command%s: %s", packet.accepted ? "" : " rejected", packet.command.c_str());
                if (packet.nonce.empty()) Append(out, "> " + packet.command + "\r\n" + reply + "\r\n");
                else Publish(dir + L"reply.json", ipc::Reply(owner, packet.nonce, managed::Json(reply)));
            }
            if (thread) a.thread_detach(thread);
        }
        if (!lifecycle::Stopping()) Sleep(10);
    }
}

}  // namespace ep::commands
