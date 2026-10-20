// Local preferences and reports for the standalone native window. No Python dependency.
#include "ingame_window.hpp"
#include "ingame_preferences_rules.hpp"
#include <windows.h>
#include <shellapi.h>
#include <cmath>
#include <deque>
#include <limits>

namespace ep::ingame::view {
namespace {
using namespace model;
using assets::Face;
constexpr auto kProfiles = "epochpact.profiles.live";
constexpr auto kStartup = "epochpact.startup-profile.live";
constexpr auto kLast = "epochpact.last-settings.live";
constexpr auto kReplay = "epochpact.last-settings-replay.live";
constexpr auto kDisplay = "epochpact.display-preferences.v1";
std::string profileName, selectedProfile;
std::set<std::string> startupOwners;
uint64_t nextOwnerRead = 0, startupGeneration = 0;
bool startupBusy = false;
std::string Startup() { auto v = Preference(kStartup).Str("__last__"); return v.empty() ? "__last__" : v; }
J ObjPref(const char* key) { auto j = Preference(key); return j.IsObject() ? j : Object(); }
bool Save(const std::string& key, const J& value) {
    const auto text = Pack(value);
    if (StorePreference(key, &text)) return true;
    Toast(Tr("Could not save local preferences. Check the EpochPact folder permissions."), true); return false;
}
void OpenUrl(const wchar_t* url) { ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL); }
float PanelBody(const Ctx& c, const std::function<float(const Ctx&, float)>& draw) {
    auto p = OpenPanel(c.parent, kPadX, 0, kPanelW); const auto in = c.At(p.rt.Get(), kPadX, 0);
    const float h = draw(in, kPanelPad) + kPanelPad; ClosePanel(p, kPanelW, h); return h;
}
float Heading(const Ctx& c, float y, const std::string& title) { return y + H2(c.parent, kPanelPad, y, kPanelInner, Tr(title)) + 14; }
bool Contains(const J& j, const std::string& key) { return std::any_of(j.items.begin(), j.items.end(), [&](const J& v) { return v.Str() == key; }); }
void ToggleArray(J& j, const std::string& key, bool on) {
    if (!j.IsArray()) j = List();
    j.items.erase(std::remove_if(j.items.begin(), j.items.end(), [&](const J& v) { return v.Str() == key; }), j.items.end());
    if (on) j.items.push_back(Text(key));
}
std::string Owner(const J& id) {
    return On(id["offline"]) && !Id(id["player"]["id"]).empty() && id["player"]["load"].IsNumber()
        ? Pack(List({id["player"]["id"], id["player"]["name"], id["player"]["load"]})) : "";
}
struct StartupRun { std::deque<std::pair<std::string, J>> settings; std::deque<std::string> choices; std::string owner, name; uint64_t generation = 0; int count = 0; };
void StartupNext(std::shared_ptr<StartupRun> run) {
    if (run->generation != startupGeneration) { startupBusy = false; return; }
    if (run->settings.empty() && run->choices.empty()) {
        startupBusy = false;
        if (run->count) Toast(Tr("Applied ") + run->name + " (" + std::to_string(run->count) + " " + Tr("settings") + ").");
        return;
    }
    // Recheck before each change. Never replay a save action or retry a refused command.
    Send("identityread", [run](const std::string& text) {
        J id; json::Parse(text, id);
        if (run->generation != startupGeneration || !Eligible() || Owner(id) != run->owner) { startupBusy = false; return; }
        std::string command;
        if (!run->settings.empty()) {
            const auto [key, v] = run->settings.front(); run->settings.pop_front();
            const auto* c = ControlById(key);
            if (!c || !ProfileSetting(D()["controls"][key], v)) { StartupNext(run); return; }
            command = c->command;
            for (size_t at = command.find("{value}"); at != std::string::npos; at = command.find("{value}"))
                command.replace(at, 7, v.IsNull() ? "reset" : v.IsBool() ? (v.boolean ? "1" : "0") : Num(v.number));
            if (command.find('{') != std::string::npos) { StartupNext(run); return; }
        } else { command = run->choices.front(); run->choices.pop_front(); }
        Send(command, [run](const std::string& reply) {
            std::string why;
            if (Refused(reply, &why)) { startupBusy = false; Toast(PlayerError(why), true); return; }
            ++run->count; StartupNext(run);
        });
    });
}
void LanguageSection(Section& s) {
    s.build = [](Section&, const Ctx& c) { return PanelBody(c, [](const Ctx& in, float y) {
        y = Heading(in, y, "Language");
        y += Muted(in.parent, kPanelPad, y, kPanelInner, Tr("The language of the EpochPact windows. Names that come from the game — items, affixes, stats, zones — stay as the game data writes them.")) + 14;
        const std::vector<std::string> ids{"en", "tr", "ru"}; const auto current = Preference("epochpact.language").Str("en");
        const auto it = std::find(ids.begin(), ids.end(), current);
        y += SelectField(in, kPanelPad, y, kPanelInner, Tr("Language"), {"English", "Türkçe", "Русский"}, static_cast<int>(it == ids.end() ? 0 : it - ids.begin()),
            [ids](int i) { if (Save("epochpact.language", Text(ids.at(i)))) ReloadWindow(); }, "language-select");
        return y;
    }); };
}
void ProfilesSection(Section& s) {
    s.build = [](Section& sec, const Ctx& c) { const int section = sec.id; return PanelBody(c, [section](const Ctx& in, float y) {
        y = Heading(in, y, "Draft profiles");
        y += NoteBox(in.parent, kPanelPad, y, kPanelInner, Tr("Profiles store session settings only. Quests, rewards, balances and save actions are never stored or automatically applied.")) + 14;
        const float w = (kPanelInner - 14) / 2;
        std::vector<std::string> names{""}; for (const auto& [key, _] : ObjPref(kProfiles).members) names.push_back(key);
        const auto at = std::find(names.begin(), names.end(), selectedProfile);
        const float h = InputField(in, kPanelPad, y, w, Tr("Profile name"), profileName, Tr("Example: Balanced"), false, [](const std::string& v) { profileName = v; }, "profile-name");
        y += std::max(h, SelectField(in, kPanelPad + w + 14, y, w, Tr("Saved draft"), names, static_cast<int>(at == names.end() ? 0 : at - names.begin()),
            [names, section](int i) { selectedProfile = names.at(i); Rebuild(section); }, "profile-select")) + 20;
        auto save = AddButton(in, kPanelPad, y, Tr("Save draft"), Look::Normal, [section] {
            const auto first = profileName.find_first_not_of(" \t\r\n"), last = profileName.find_last_not_of(" \t\r\n");
            if (first == std::string::npos) { Toast(Tr("Enter a profile name."), true); return; }
            const auto name = profileName.substr(first, last - first + 1);
            if (name.size() > 128) { Toast(Tr("Use a shorter profile name."), true); return; }
            J snapshot = Object();
            for (const auto& [key, value] : ProfileValues()) if (ProfileSetting(D()["controls"][key], std::isfinite(value) ? Number(value) : J{}))
                Put(snapshot, key, std::isfinite(value) ? Number(value) : J{});
            auto profiles = ObjPref(kProfiles); Put(profiles, name, snapshot);
            if (Save(kProfiles, profiles)) { selectedProfile = name; Rebuild(section); Toast(Tr("Draft saved; not applied to the game.")); }
        }, false, "profile-save");
        auto load = AddButton(in, kPanelPad + save.w + 10, y, Tr("Load draft"), Look::Normal, [] {
            const auto profile = ObjPref(kProfiles)[selectedProfile];
            if (!profile.IsObject()) { Toast(Tr("Choose a saved draft."), true); return; }
            CancelAutomaticSettings();
            for (const auto& [key, value] : profile.members) if (ProfileSetting(D()["controls"][key], value))
                SetDraft(key, value.IsNull() ? std::numeric_limits<double>::quiet_NaN() : value.IsBool() ? (value.boolean ? 1 : 0) : value.number);
            Toast(Tr("Profile loaded into the draft. Use Apply changes to update the game."));
        }, false, "profile-load");
        AddButton(in, kPanelPad + save.w + load.w + 20, y, Tr("Delete profile"), Look::Normal, [section] {
            const auto name = selectedProfile;
            if (name.empty() || !ObjPref(kProfiles)[name].IsObject()) { Toast(Tr("Choose a saved draft."), true); return; }
            Confirm(Tr("Delete draft profile"), Tr("The local draft “") + name + Tr("” will be deleted. Game settings are unchanged."), [name, section] {
                auto profiles = ObjPref(kProfiles); Erase(profiles, name);
                if (Save(kProfiles, profiles)) { selectedProfile.clear(); Rebuild(section); Toast(Tr("Local draft deleted.")); }
            });
        }, false, "profile-delete");
        y += save.h + 20;
        std::vector<std::string> keys{"__off__", "__last__"}, labels{Tr("Off — start with the game's normal values"), Tr("My last used settings (recommended)")};
        for (const auto& [key, _] : ObjPref(kProfiles).members) { keys.push_back(key); labels.push_back(Tr("Profile: ") + key); }
        const auto chosen = std::find(keys.begin(), keys.end(), Startup());
        y += SelectField(in, kPanelPad, y, kPanelInner, Tr("Apply at startup"), labels, static_cast<int>(chosen == keys.end() ? 0 : chosen - keys.begin()), [keys, section](int i) {
            if (!Save(kStartup, Text(keys.at(i)))) return;
            ++startupGeneration; startupOwners.clear(); startupBusy = false;
            Rebuild(section); Toast(Tr(i == 0 ? "Startup profile turned off. EpochPact starts with the game's normal values." : "Startup profile saved. It applies when an offline character is ready."));
        }, "startup-profile") + 14;
        y += Muted(in.parent, kPanelPad, y, kPanelInner, Tr("Applied once each time an offline character is ready. Your last used settings include loot modes, collection categories, wanted affixes and the font. Profiles and window preferences stay on this device."));
        return y;
    }); };
}
void VersionSection(Section& s) {
    s.build = [](Section&, const Ctx& c) { return PanelBody(c, [](const Ctx& in, float y) {
        y = Heading(in, y, "Version");
        y += Muted(in.parent, kPanelPad, y, kPanelInner, "EpochPact v" + D()["appVersion"].Str() + " · " + Tr("In-game interface · Chronoforge")) + 14;
        y += NoteBox(in.parent, kPanelPad, y, kPanelInner, Tr("Close Last Epoch before replacing the mod DLL. Download the complete matching package; your local preferences are kept.")) + 14;
        y += AddButton(in, kPanelPad, y, Tr("Release page"), Look::Normal, [] { OpenUrl(L"https://github.com/falorfrozen-cmd/EpochPact/releases"); }, false, "release-page").h;
        return y;
    }); };
}
void ReportSection(Section& s) {
    s.reads = {"reportread"};
    s.build = [](Section&, const Ctx& c) { return PanelBody(c, [](const Ctx& in, float y) {
        y = Heading(in, y, "In-game report");
        y += Muted(in.parent, kPanelPad, y, kPanelInner, Tr("Save a report in Last Epoch\\EpochPact\\reports: a screenshot, game and EpochPact state, recent logs and a description.txt to fill in. Nothing is uploaded. Windows user and computer names are masked; reports can contain character names.")) + 14;
        const auto* r = Cached("reportread"); const auto current = r ? (*r)["key"].Str("F8") : "F8";
        const std::vector<std::string> keys{"F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "off"};
        const auto it = std::find(keys.begin(), keys.end(), current);
        y += SelectField(in, kPanelPad, y, (kPanelInner - 14) / 2, Tr("Hotkey"), keys, static_cast<int>(it == keys.end() ? 3 : it - keys.begin()), [keys](int i) {
            Send("reportkey " + keys.at(i), [](const std::string& text) { std::string why; if (Refused(text, &why)) Toast(PlayerError(why), true); Read("reportread"); });
        }, "report-key") + 20;
        const auto b = AddButton(in, kPanelPad, y, Tr("Create report now"), Look::Normal, [] {
            Send("reportnow", [](const std::string& text) { std::string why; if (Refused(text, &why)) { Toast(PlayerError(why), true); return; }
                Toast(Tr("Report saved in the game's EpochPact\\reports folder. Nothing was uploaded.")); Read("reportread"); });
        }, false, "report-now");
        AddButton(in, kPanelPad + b.w + 10, y, Tr("Refresh"), Look::Normal, [] { Read("reportread"); }, false, "report-refresh");
        y += b.h;
        if (r && !(*r)["last"].Str().empty()) { const auto path = (*r)["last"].Str(); y += 14 + Muted(in.parent, kPanelPad, y + 14, kPanelInner, Tr("Last: ") + path.substr(path.find_last_of("/\\") + 1)); }
        return y;
    }); };
}
void RecoverySection(Section& s) {
    s.build = [](Section&, const Ctx& c) { return PanelBody(c, [](const Ctx& in, float y) {
        y = Heading(in, y, "Recovery backups");
        y += NoteBox(in.parent, kPanelPad, y, kPanelInner, Tr("Changes to your game save create a recovery backup in Last Epoch\\EpochPact\\backups. Restore backups with the game closed, using the desktop recovery tools. In-game restoration is disabled to protect your loaded character."));
        return y;
    }); };
}
void NavigationSection(Section& s) {
    s.build = [](Section& sec, const Ctx& c) { const int section = sec.id; return PanelBody(c, [section](const Ctx& in, float y) {
        y = Heading(in, y, "Navigation and favorites");
        y += Muted(in.parent, kPanelPad, y, kPanelInner, Tr("Choose visible sections and pin favorites to the top. These choices stay on this device.")) + 14;
        const auto p = ObjPref(kDisplay);
        for (const auto& page : D()["pages"].items) {
            const auto key = page["id"].Str(); const bool required = key == "settings" || key == "overview";
            Style label{Face::BodyBold, 12, C("ink")}; TextBox(in.parent, "Page", kPanelPad, y + 8, 200, 24, label, Tr(page["nav"].Str()));
            if (required) Muted(in.parent, kPanelPad + 218, y + 8, 100, Tr("Visible"), 12);
            else CheckField(in, kPanelPad + 218, y + 8, Tr("Visible"), !Contains(p["hidden"], key), [key, section](bool on) {
                auto prefs = ObjPref(kDisplay), list = prefs["hidden"]; ToggleArray(list, key, !on); Put(prefs, "hidden", list);
                if (Save(kDisplay, prefs)) { NavigationChanged(); Rebuild(section); }
            }, "nav-visible:" + key);
            CheckField(in, kPanelPad + 328, y + 8, Tr("Favorite"), Contains(p["favorites"], key), [key, section](bool on) {
                auto prefs = ObjPref(kDisplay), list = prefs["favorites"]; ToggleArray(list, key, on); Put(prefs, "favorites", list);
                if (Save(kDisplay, prefs)) { NavigationChanged(); Rebuild(section); }
            }, "nav-favorite:" + key);
            Solid(in.parent, "Row border", kPanelPad, y + 39, 460, 1, C("line")); y += 40;
        }
        y += 14;
        y += CheckField(in, kPanelPad, y, Tr("Return to the same place when switching sections"), On(Preference("epochpact.remember-scroll.live")), [](bool on) { Save("epochpact.remember-scroll.live", Flag(on)); }, "remember-scroll") + 20;
        y += AddButton(in, kPanelPad, y, Tr("Restore all sections and stats"), Look::Normal, [section] {
            auto prefs = ObjPref(kDisplay); for (const auto& key : {"favorites", "hidden", "hiddenStats", "summonColumns", "summonOptions"}) Put(prefs, key, List());
            if (Save(kDisplay, prefs) && Save("epochpact.hidden-parts.v1", Object())) { NavigationChanged(); Rebuild(section); ReloadWindow(); Toast(Tr("All sections and stats are visible again.")); }
        }, false, "display-preferences-reset").h;
        return y;
    }); };
}
void HiddenSection(Section& s) {
    s.build = [](Section& sec, const Ctx& c) { const int section = sec.id; return PanelBody(c, [section](const Ctx& in, float y) {
        y = Heading(in, y, "Hidden parts");
        y += Muted(in.parent, kPanelPad, y, kPanelInner, Tr("Panels and cards you hid with their ×. Hiding only removes them from the page; a setting keeps the value it has.")) + 14;
        const auto hidden = ObjPref("epochpact.hidden-parts.v1");
        if (hidden.members.empty()) y += EmptyText(in.parent, kPanelPad, y, kPanelInner, Tr("Nothing is hidden. Point at a panel or card and select its × to hide it."));
        else {
            y += AddButton(in, kPanelPad, y, Tr("Show all"), Look::Normal, [] { if (Save("epochpact.hidden-parts.v1", Object())) ReloadWindow(); }, false, "show-all-parts").h + 14;
            for (const auto& [key, v] : hidden.members) {
                const auto b = AddButton(in, kPanelPad + kPanelInner, y, Tr("Show"), Look::Normal, [key, section] {
                    auto h = ObjPref("epochpact.hidden-parts.v1"); Erase(h, key); if (Save("epochpact.hidden-parts.v1", h)) ReloadWindow();
                }, true, "show-part:" + key);
                const float h = Muted(in.parent, kPanelPad, y + 8, kPanelInner - b.w - 14, v["label"].Str(key), 12);
                y += std::max(b.h, h + 8) + 8;
            }
        }
        return y;
    }); };
}
void TextSizeSection(Section& s) {
    s.build = [](Section&, const Ctx& c) { return PanelBody(c, [](const Ctx& in, float y) {
        y = Heading(in, y, "Text size");
        y += Muted(in.parent, kPanelPad, y, kPanelInner, Tr("Makes everything in this window larger or smaller. The loot and damage windows have their own text size in their ⚙.")) + 14;
        const std::vector<double> values{.9, 1, 1.1, 1.25, 1.4, 1.5}; const double current = Preference("epochpact.text-scale.v1").Num(1);
        int at = 1; for (size_t i = 0; i < values.size(); ++i) if (std::abs(current - values[i]) < .001) at = static_cast<int>(i);
        y += SelectField(in, kPanelPad, y, kPanelInner, Tr("Text size"), {"90%", "100%", "110%", "125%", "140%", "150%"}, at,
            [values](int i) { if (Save("epochpact.text-scale.v1", Number(values.at(i)))) ReloadWindow(); }, "text-scale");
        return y;
    }); };
}
void SetupSection(Section& s) {
    s.build = [](Section&, const Ctx& c) { return PanelBody(c, [](const Ctx& in, float y) {
        y = Heading(in, y, "Game setup");
        y += NoteBox(in.parent, kPanelPad, y, kPanelInner, Tr("The native player mod is loaded in this game. No separate EpochPact EXE or MelonLoader is required. Gameplay changes are available only for a ready offline character.")) + 14;
        wchar_t path[32768]; const auto n = GetModuleFileNameW(nullptr, path, 32768);
        std::string utf8; if (n && n < 32768) { utf8.resize(WideCharToMultiByte(CP_UTF8, 0, path, n, nullptr, 0, nullptr, nullptr)); WideCharToMultiByte(CP_UTF8, 0, path, n, utf8.data(), static_cast<int>(utf8.size()), nullptr, nullptr); }
        y += Table(in.parent, kPanelPad, y, kPanelInner, {Tr("Component"), Tr("Status")}, {{"EpochPact", "v" + D()["appVersion"].Str()}, {Tr("Interface"), Tr("In game · F6")}, {Tr("Game executable"), utf8}}, {.24f, .76f}) + 14;
        y += Muted(in.parent, kPanelPad, y, kPanelInner, Tr("Installation and removal require Last Epoch to be closed. Replace the complete matching package when updating. Existing game saves and unrelated mods are not installation targets."));
        return y;
    }); };
}
void SearchSection(Section& s) {
    s.after = 20;
    s.build = [](Section& sec, const Ctx& c) {
        auto query = Fold(SearchQuery()); const auto first = query.find_first_not_of(" \t\r\n"), last = query.find_last_not_of(" \t\r\n");
        if (first == std::string::npos) return 0.f;
        query = query.substr(first, last - first + 1);
        auto panel = std::make_shared<Panel>(OpenPanel(c.parent, kPadX, 0, kPanelW));
        const auto in = c.At(panel->rt.Get(), kPadX, 0);
        float y = kPanelPad;
            std::vector<CardMaker> makers;
            for (const auto& row : D()["searchControls"].items) {
                if (Fold(row["label"].Str() + " " + row["description"].Str() + " " + row["id"].Str()).find(query) == std::string::npos) continue;
                auto page = row["group"].Str(), tab = std::string();
                if (page == "session" || page == "recovery") page = "settings";
                if (std::none_of(D()["pages"].items.begin(), D()["pages"].items.end(), [&](const J& p) { return p["id"].Str() == page; })) page = "stats";
                if (page == "crafting") { const auto id = row["id"].Str(); tab = id.rfind("craft_materials", 0) == 0 ? "materials" : id.rfind("craft_custom", 0) == 0 ? "custom" : "forge"; }
                makers.push_back([row, page, tab](const Ctx& at, float x, float yy, float w) {
                    auto box = OpenCard(at.parent, x, yy, w); auto card = at.At(box.node.Get(), x, yy);
                    float cy = kCardPad + H3(card.parent, kCardPad, kCardPad, w - 2 * kCardPad, Tr(row["label"].Str())) + 10;
                    cy += Muted(card.parent, kCardPad, cy, w - 2 * kCardPad, Tr(row["description"].Str()), 12) + 15;
                    float badge = 0; Badge(card.parent, w - kCardPad, cy, LifetimeLabel(row["lifetime"].Str()), &badge); cy += badge + kCardPad;
                    Hit hit; hit.name = "search:" + row["id"].Str(); hit.x = x; hit.y = yy; hit.w = w; hit.h = cy;
                    hit.action = [page, tab] { SearchChanged(""); Navigate(page, tab); }; at.Add(std::move(hit));
                    CloseCard(box, w, cy); return box;
                });
            }
            y = Heading(in, y, Tr("Setting search") + " · " + std::to_string(makers.size()) + " " + Tr("results"));
            const auto base = y;
            const float grid = ScrollCards(sec, in, kPanelPad, y, kPanelInner, 0, std::move(makers), 2, 15,
                [id = sec.id, base, panel](float h) { const float height = base + h + kPanelPad; ClosePanel(*panel, kPanelW, height); if (auto* s = FindSection(id)) Resized(*s, height); });
            ClosePanel(*panel, kPanelW, y + grid + kPanelPad);
            return y + grid + kPanelPad;
    };
}
} // namespace
void RememberSetting(const Control& c, double v) {
    if (startupBusy || Startup() != "__last__") return;
    const auto value = std::isfinite(v) ? Number(v) : J{};
    if (!ProfileSetting(D()["controls"][c.id], value)) return;
    auto last = ObjPref(kLast);
    if (value.IsNull() || (c.hasDefault && v == c.def)) Erase(last, c.id); else Put(last, c.id, value);
    Save(kLast, last);
}
void RememberChoice(const std::string& command) {
    if (startupBusy) return;
    auto entries = ObjPref(kReplay); std::string key;
    if (command == "lootreset") {
        entries.members.erase(std::remove_if(entries.members.begin(), entries.members.end(), [](const auto& e) { return e.first.rfind("loot_", 0) == 0; }), entries.members.end());
    } else { auto entry = ChoiceEntry(command, &key); if (entry.IsNull()) return; Put(entries, key, entry); }
    Save(kReplay, entries);
}
void SettingsStep() {
    if (startupBusy || !Eligible() || GetTickCount64() < nextOwnerRead || Startup() == "__off__") return;
    nextOwnerRead = GetTickCount64() + 1000;
    Send("identityread", [](const std::string& text) {
        J identity; json::Parse(text, identity); const auto owner = Owner(identity);
        if (startupBusy || owner.empty() || !Eligible() || startupOwners.count(owner) || Startup() == "__off__") return;
        const auto choice = Startup(); const auto saved = choice == "__last__" ? ObjPref(kLast) : ObjPref(kProfiles)[choice];
        startupOwners.insert(owner); // once, including failures; no hidden retries of writes
        auto run = std::make_shared<StartupRun>(); run->owner = owner; run->generation = startupGeneration;
        run->name = choice == "__last__" ? Tr("your last used settings") : Tr("profile “") + choice + "”";
        for (const auto& [key, value] : saved.members) if (ProfileSetting(D()["controls"][key], value)) run->settings.emplace_back(key, value);
        if (choice == "__last__") for (const auto& [_, entry] : ObjPref(kReplay).members) { auto command = ReplayCommand(entry); if (!command.empty()) run->choices.push_back(command); }
        startupBusy = true; StartupNext(run);
    });
}
void RegisterSettingsSections() {
    RegisterSection("settings_language", LanguageSection); RegisterSection("settings_profiles", ProfilesSection);
    RegisterSection("settings_version", VersionSection); RegisterSection("settings_report", ReportSection);
    RegisterSection("settings_recovery", RecoverySection); RegisterSection("settings_navigation", NavigationSection);
    RegisterSection("settings_hidden", HiddenSection); RegisterSection("setup_status", SetupSection);
    RegisterSection("global_search", SearchSection);
    RegisterSection("settings_text_size", TextSizeSection);
}
} // namespace ep::ingame::view
