// Character browser and stat modal, matching renderStats/openStat and stat-model.js.
// Metadata is generated from the desktop model; no speculative stat mappings live here.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include "ingame_stat_rules.hpp"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace ep::ingame::view {
namespace {
using J = json::Value;
struct Browser {
    std::string tab = "sheet", query, category = "All";
    bool favoritesOnly = false, showHidden = false;
    int page = 0, filters = -1, results = -1;
    uint64_t revision = 0;
    std::set<std::string> favorites, hidden;
} browser;
struct Editor {
    J row, config;
    std::string mode, value = "0", owner, ownerName, message;
    bool busy = false, ready = false, advanced = false, first = true;
    stats::Current current;
    Pin preview, input, currentLabel, sliderThumb;
} editor;
std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(c < 128 ? std::tolower(c) : c); });
    return s;
}
std::string Array(const std::set<std::string>& items) {
    std::string out = "[";
    for (const auto& item : items) out += (out.size() > 1 ? "," : "") + managed::Json(item);
    return out + "]";
}
std::set<std::string> Strings(const J& value) {
    std::set<std::string> out;
    for (const auto& item : value.items) if (item.IsString()) out.insert(item.text);
    return out;
}
void Preferences() {
    browser.favorites = Strings(Preference("epochpact.stat-favorites.v1"));
    browser.hidden = Strings(Preference("epochpact.display-preferences.v1")["hiddenStats"]);
}
void Refilter() { browser.page = 0; if (browser.results >= 0) Rebuild(browser.results); }
void SaveMarker(const std::string& id, bool favorite) {
    auto values = favorite ? browser.favorites : browser.hidden;
    if (!values.erase(id)) values.insert(id);
    std::string text;
    if (favorite) text = Array(values);
    else {
        const J prefs = Preference("epochpact.display-preferences.v1");
        text = "{";
        for (const auto& [key, value] : prefs.members) {
            if (key == "hiddenStats" || !value.IsArray()) continue;
            text += (text.size() > 1 ? "," : "") + managed::Json(key) + ":" + Array(Strings(value));
        }
        text += (text.size() > 1 ? "," : "") + std::string("\"hiddenStats\":") + Array(values) + "}";
    }
    if (!StorePreference(favorite ? "epochpact.stat-favorites.v1" : "epochpact.display-preferences.v1", &text)) {
        Toast(Tr("Favorites could not be saved on this device."), true); return;
    }
    (favorite ? browser.favorites : browser.hidden) = std::move(values);
    Rebuild(browser.results);
}
const J& Rows() { return D()["statBrowser"][browser.tab]; }
std::string Id(const J& player) { const J& id = player["id"]; return id.IsString() ? id.text : id.IsNumber() ? Num(id.number) : ""; }
std::string Signed(double value, const std::string& suffix) { return (value > 0 ? "+" : "") + Num(value) + suffix; }
const J& Unit() { return editor.config["units"][editor.mode]; }
std::string Preview() {
    double value = 0;
    if (!stats::Number(editor.value, &value)) return Tr("Enter a bonus");
    return Signed(value, Unit()["suffix"].Str()) + (editor.mode == "increased" ? " increased" : editor.mode == "more" ? " more" : "") + " " + editor.row["name"].Str();
}
std::string Summary() {
    if (!editor.message.empty()) return editor.message;
    if (!editor.current.known) return Tr("Not available");
    std::string out;
    for (size_t i = 0; i < stats::modes.size(); ++i) {
        if (editor.current.values[i] == 0) continue;
        const std::string mode = stats::modes[i];
        // Aliases can restrict editing to one mode; summaries still include every owned mode.
        const auto& info = editor.config["units"][mode];
        const double scale = info.IsObject() ? info["scale"].Num(1) : mode == "added" ? 1 : 100;
        const std::string suffix = info.IsObject() ? info["suffix"].Str() : mode == "added" ? "" : "%";
        out += (out.empty() ? "" : " · ") + Signed(editor.current.values[i] * scale, suffix) + (i == 1 ? " increased" : i == 2 ? " more" : "");
    }
    if (out.empty()) out = Tr("No bonus");
    if (!editor.current.attached) out += " · " + Tr("Not active");
    if (!editor.current.game.empty()) out += " · " + Fmt("In game: {0}", {editor.current.game});
    return out;
}
void DrawEditor();
void Execute(const std::string& command, bool write) {
    if (editor.busy || !Eligible()) return;
    editor.busy = true;
    editor.message = Tr("Reading…");
    DrawEditor();
    const uint64_t token = DialogToken();
    const std::string expected = editor.owner, expectedName = editor.ownerName;
    Send("identityread", [token, command, write, expected, expectedName](const std::string& reply) {
        if (DialogToken() != token) return;
        J identity;
        std::string why;
        const bool valid = json::Parse(reply, identity) && identity["offline"].IsBool() && identity["offline"].boolean && !Id(identity["player"]).empty();
        if (!valid || Refused(reply, &why) || (!expected.empty() && (Id(identity["player"]) != expected || identity["player"]["name"].Str() != expectedName))) {
            editor.busy = false; editor.ready = false;
            editor.message = Tr("The loaded character has changed. Refresh this section and try again.");
            DrawEditor(); return;
        }
        editor.owner = Id(identity["player"]); editor.ownerName = identity["player"]["name"].Str();
        Send(command, [token, write](const std::string& response) {
            if (DialogToken() != token) return;
            editor.busy = false;
            std::string why;
            if (Refused(response, &why)) { editor.message = PlayerError(why); editor.ready = false; DrawEditor(); return; }
            if (write) {
                Toast(Tr("Character sheet stat editor updated."));
                editor.message.clear();
                Execute(stats::Key(editor.config), false);
                return;
            }
            editor.current = stats::Contributions(response);
            editor.ready = editor.current.known;
            editor.message.clear();
            if (editor.first && editor.ready) {
                int chosen = -1;
                for (size_t i = 0; i < stats::modes.size(); ++i) if (editor.mode == stats::modes[i] && editor.current.values[i] != 0) chosen = static_cast<int>(i);
                if (chosen < 0) for (const auto& m : editor.config["modes"].items)
                    for (size_t i = 0; i < stats::modes.size(); ++i) if (m.Str() == stats::modes[i] && editor.current.values[i] != 0 && chosen < 0) { editor.mode = m.Str(); chosen = static_cast<int>(i); }
                if (chosen >= 0) editor.value = Num(editor.current.values[chosen] * Unit()["scale"].Num(1));
                editor.first = false;
            }
            DrawEditor();
        });
    });
}
void ChangeMode(const std::string& mode) { editor.mode = mode; editor.value = "0"; DrawEditor(); }
void DrawEditor() {
    ShowCustomDialog(740, [](const Ctx& c, float width) {
        const float pad = 30, w = width - 2 * pad;
        float y = 27;
        Style eyebrow{Face::Body, 10, C("accent")}; eyebrow.spacing = 2;
        TextBox(c.parent, "Stat eyebrow", pad, y, w - 55, 14, eyebrow, Tr("CHARACTER BONUS"));
        AddButton(c, width - pad, y, "×", Look::Normal, [] { CloseDialog(); }, true, "stat-close");
        y += 20;
        Style title{Face::Serif, 32, C("ink")}; title.wrap = true;
        y += Paragraph(c.parent, "Stat title", pad, y, w - 55, title, Tr(editor.row["name"].Str()) + (editor.config["secondary"].Num() ? " · " + Tr("Extra") : ""));
        std::string note = Tr("Customize this stat without changing your equipment.");
        std::vector<std::string> shared;
        if (!editor.config["secondary"].Num()) for (const auto& name : editor.row["shared"].items) if (name.Str() != editor.row["name"].Str()) shared.push_back(Tr(name.Str()));
        if (!shared.empty()) {
            std::string list;
            for (size_t i = 0; i < std::min(size_t(3), shared.size()); ++i) list += (i ? ", " : "") + shared[i];
            note = Fmt("Shares its bonus with {0}", {list}) + (shared.size() > 3 ? Tr(" and other linked stats.") : ".");
        }
        y += 13 + Muted(c.parent, pad, y + 13, w, note, 12) + 20;
        Fill(c.parent, "Current background", pad, y, w, 62, Hex("#00000022"), 4);
        Outline(c.parent, "Current border", pad, y, w, 62, C("line"), 4);
        Style small{Face::Body, 9, C("muted")}; small.spacing = 1.3f;
        TextBox(c.parent, "Current caption", pad + 17, y + 10, w - 155, 15, small, Tr("CURRENT BONUS"));
        Style strong{Face::Body, 14, C("ink")};
        editor.currentLabel.Set(TextBox(c.parent, "Current bonus", pad + 17, y + 29, w - 155, 23, strong, Summary()));
        auto refresh = AddButton(c, width - pad - 12, y + 12, Tr("↻ Refresh"), Look::Quiet, [] { Execute(stats::Key(editor.config), false); }, true, "stat-read");
        SetEnabled(c, refresh, !editor.busy && Eligible()); y += 82;
        float bx = pad;
        for (const auto& mode : editor.config["modes"].items) {
            if (mode.Str() == "more") continue;
            const std::string label = mode.Str() == "increased" ? Tr("Increase %") : editor.config["units"]["added"]["points"].Num() ? Tr("Add percentage points") : Tr("Flat bonus");
            auto b = AddButton(c, bx, y, label, Look::Quiet, [m = mode.Str()] { ChangeMode(m); }, false, "stat-mode-" + mode.Str());
            SetSelected(c, b, editor.mode == mode.Str()); SetEnabled(c, b, !editor.busy); bx += b.w + 8;
        }
        y += 48;
        const float cardY = y;
        void* bg = Fill(c.parent, "Bonus card", pad, y, w, 10, C("soft"), 5);
        void* border = Outline(c.parent, "Bonus border", pad, y, w, 10, C("line"), 5);
        y += 20;
        y += Muted(c.parent, pad + 22, y, w - 44, Tr("Your bonus"), 11) + 9;
        Style preview{Face::Serif, 25, C("accent")}; preview.line = 1.4f;
        editor.preview.Set(TextBox(c.parent, "Bonus preview", pad + 22, y, w - 44, 36, preview, Preview()));
        y += 63;
        const auto change = [](double amount) {
            double value = 0; stats::Number(editor.value, &value);
            editor.value = Num(std::clamp(value + amount, Unit()["minimum"].Num(), Unit()["maximum"].Num())); DrawEditor();
        };
        const float trackX = pad + 22, trackW = w - 44 - 200 - 15, stepperX = trackX + trackW + 15;
        Fill(c.parent, "Stat slider", trackX, y + 18, trackW, 8, C("glow"), 4);
        Outline(c.parent, "Stat slider border", trackX, y + 18, trackW, 8, C("gold"), 4);
        double v = 0; stats::Number(editor.value, &v);
        const double lo = Unit()["sliderMin"].Num(), hi = Unit()["sliderMax"].Num();
        const float position = hi > lo ? static_cast<float>(std::clamp((v - lo) / (hi - lo), 0.0, 1.0)) : 0;
        void* thumb = Fill(c.parent, "Stat slider thumb", trackX + position * trackW - 9, y + 12, 19, 19, C("accent"));
        editor.sliderThumb.Set(TransformOf(thumb)); Turn(editor.sliderThumb.Get(), 45);
        const auto slider = [trackW, trackX, y, lo, hi](float x) {
            if (editor.busy || hi <= lo) return;
            const double fraction = std::clamp(static_cast<double>(x / trackW), 0.0, 1.0);
            editor.value = Num(std::round(lo + fraction * (hi - lo)));
            SetText(editor.preview.Get(), Preview());
            Move(editor.sliderThumb.Get(), trackX + static_cast<float>(fraction) * trackW - 9, y + 12);
            if (const auto* set = Find("Unity.TextMeshPro.dll", "TMPro", "TMP_InputField", "set_text", 1)) {
                managed::Root text(il2cpp::api().string_new(editor.value.c_str())); void* args[]{text.Get()}; managed::Invoke(set, editor.input.Get(), args);
            }
        };
        Hit range; range.kind = HitKind::Track; range.x = trackX; range.y = y; range.w = trackW; range.h = 44; range.enabled = !editor.busy; range.name = "stat-range"; range.drag = slider; c.Add(std::move(range));
        auto minus = AddButton(c, stepperX, y, "−", Look::Normal, [change] { change(-1); }, false, "stat-minus");
        InputLook look{Hex("#00000033"), C("line")};
        Style input{Face::Body, 17, C("ink")}; input.align = kLeft;
        editor.input.Set(MakeInput(c, stepperX + minus.w, y, 102, 44, input, look, editor.value, "0", true, [](const std::string& text) {
            editor.value = text; SetText(editor.preview.Get(), Preview());
        }, "stat-value"));
        Enable(editor.input.Get(), !editor.busy);
        auto plus = AddButton(c, stepperX + minus.w + 102, y, "+", Look::Normal, [change] { change(1); }, false, "stat-plus");
        SetEnabled(c, minus, !editor.busy); SetEnabled(c, plus, !editor.busy);
        TextBox(c.parent, "Bonus unit", stepperX + minus.w + 102 + plus.w + 7, y, 20, 44, strong, Unit()["suffix"].Str());
        y += 67;
        const std::string help = editor.mode == "more" ? "A separate multiplier. +50% More multiplies this stat by ×1.5." : Unit()["points"].Num() ?
            "Percentage points add directly: a 5% stat with +10 points becomes 15%, before game caps." : editor.mode == "increased" ?
            "Adds to this stat's existing percentage increases. Equipment bonuses still apply." : "Adds this amount to the stat. Zero removes this bonus amount.";
        y += Muted(c.parent, pad + 22, y, w - 44, Tr(help), 12) + 6;
        y += Muted(c.parent, pad + 22, y, w - 44, Fmt("Slider {0}–{1}{2}. You can type values outside this slider range.",
            {Num(Unit()["sliderMin"].Num()), Num(Unit()["sliderMax"].Num()), Unit()["suffix"].Str()}), 10) + 20;
        Resize(TransformOf(bg), w, y - cardY); Resize(TransformOf(border), w, y - cardY);
        y += 15;
        y += Details(c, pad, y, Tr("Advanced"), editor.advanced, [] { editor.advanced = !editor.advanced; DrawEditor(); }) + 10;
        if (editor.advanced) {
            std::vector<std::string> options;
            int at = 0;
            for (const auto& m : editor.config["modes"].items) {
                if (m.Str() == editor.mode) at = static_cast<int>(options.size());
                options.push_back(Tr(m.Str() == "added" ? "Flat / direct bonus" : m.Str() == "increased" ? "Increase %" : "More % · separate multiplier"));
            }
            y += SelectField(c, pad, y, w, Tr("Bonus calculation"), options, at, [](int index) { ChangeMode(editor.config["modes"].items[index].Str()); }, "stat-mode") + 10;
            y += Muted(c.parent, pad, y, w, Tr("More applies a separate multiplier. Reset bonus removes all EpochPact bonuses affecting this same stat. The game still handles derived values and caps."), 12) + 12;
        }
        Solid(c.parent, "Action divider", pad, y, w, 1, C("line")); y += 17;
        auto reset = AddButton(c, pad, y, Tr("Reset bonus"), Look::Normal, [] { editor.value = "0"; Execute(stats::Key(editor.config) + " reset", true); }, false, "stat-reset");
        auto apply = AddButton(c, width - pad, y, Tr("Apply bonus") + " ◇", Look::Primary, [] {
            std::string command;
            if (!stats::Command(editor.config, editor.mode, editor.value, &command)) { Toast(Tr("Enter a bonus"), true); return; }
            Execute(command, true);
        }, true, "stat-apply");
        SetEnabled(c, reset, editor.ready && !editor.busy && Eligible());
        SetEnabled(c, apply, editor.ready && !editor.busy && Eligible());
        y += std::max(reset.h, apply.h) + 13;
        y += Muted(c.parent, pad, y, w, Tr("Applies to this character. Your equipment and base stats are kept."), 10) + 20;
        return y;
    });
}
void OpenEditor(const J& row, bool secondary) {
    editor.row = row; editor.config = row[secondary ? "extra" : "editor"];
    if (!editor.config.IsObject()) return;
    editor.mode = editor.config["preferred"].Str("added"); editor.value = "0";
    editor.owner.clear(); editor.ownerName.clear(); editor.message.clear(); editor.current = {};
    editor.ready = editor.busy = editor.advanced = false; editor.first = true;
    DrawEditor(); Execute(stats::Key(editor.config), false);
}
void MakeFilters(Section& s) {
    browser.filters = s.id;
    s.build = [](Section& section, const Ctx& outer) {
        auto frame = std::make_shared<Panel>(OpenPanel(outer.parent, kPadX, 0, kPanelW));
        section.frame = frame;
        section.after = 0;
        const Ctx c = outer.At(frame->rt.Get(), kPadX, 0);
        Preferences();
        float y = kPanelPad;
        y += H2(c.parent, kPanelPad, y, kPanelInner - 195, Tr("Character bonuses")) + 14;
        AddButton(c, kPanelPad + kPanelInner, kPanelPad, Tr("Reset all bonuses"), Look::Quiet, [] {
            Confirm(Tr("Remove all contributions"), Tr("Removes only EpochPact stat contributions. Equipment stats are kept. Controls sharing the same full stat key, including movement speed, are refreshed."), [] {
                ActionRun run; run.command = "statreset"; run.success = [](const J&) { return Tr("All character bonuses reset."); }; RunAction(std::move(run));
            });
        }, true, "stat-reset-all");
        y += Muted(c.parent, kPanelPad, y, kPanelInner, Tr("Choose a stat, adjust your bonus, then apply.")) + 20;
        float x = kPanelPad;
        for (const auto& [tab, label] : std::vector<std::pair<std::string, std::string>>{{"sheet", "Character sheet"}, {"aliases", "Common stats"}, {"raw", "All stats"}}) {
            auto b = AddButton(c, x, y, Tr(label) + "  " + std::to_string(D()["statBrowser"][tab].items.size()), Look::Normal, [tab] {
                browser.tab = tab; browser.category = "All"; Refilter(); Rebuild(browser.filters);
            }, false, "stat-tab-" + tab);
            SetSelected(c, b, browser.tab == tab); x += b.w + 8;
        }
        y += 62;
        InputLook look{Hex("#080e0e"), Hex("#7b745a99")}; Style input{Face::Body, 14, C("ink")}; input.align = kLeft;
        MakeInput(c, kPanelPad, y, kPanelInner - 160, 42, input, look, browser.query, Tr("Find health, damage, resistance…"), false, [](const std::string& text) {
            browser.query = text;
            const uint64_t revision = ++browser.revision;
            After(200, [revision] { if (revision == browser.revision) Refilter(); });
        }, "stat-search");
        auto favorite = AddButton(c, kPanelPad + kPanelInner, y, Tr("★ Favorites"), Look::Normal, [] { browser.favoritesOnly = !browser.favoritesOnly; Refilter(); Rebuild(browser.filters); }, true, "stat-favorites");
        SetSelected(c, favorite, browser.favoritesOnly); y += 56;
        std::vector<std::string> categories{"All"};
        for (const auto& row : Rows().items) if (std::find(categories.begin(), categories.end(), row["category"].Str()) == categories.end()) categories.push_back(row["category"].Str());
        x = kPanelPad;
        for (const auto& category : categories) {
            // MakeButton measures the app's text; wrap the category chips rather than clipping.
            const float estimate = TextAdvance(Face::Body, 12, Tr(category)) + 26;
            if (x + estimate > kPanelPad + kPanelInner) { x = kPanelPad; y += 38; }
            auto b = AddButton(c, x, y, Tr(category), Look::Quiet, [category] { browser.category = category; Refilter(); Rebuild(browser.filters); }, false, "stat-category-" + category);
            SetSelected(c, b, browser.category == category); x += b.w + 6;
        }
        return y + 38;
    };
}
void MakeResults(Section& s) {
    browser.results = s.id;
    s.build = [](Section&, const Ctx& outer) {
        const Node body = New("Stat results", outer.parent, kPadX, 0, kPanelW, 1400);
        const Ctx c = outer.At(body.rt, kPadX, 0);
        std::vector<const J*> matches;
        for (const auto& row : Rows().items) {
            const std::string id = row["favorite"].Str();
            if (!browser.query.empty() && row["search"].Str().find(Lower(browser.query)) == std::string::npos) continue;
            if (browser.category != "All" && row["category"].Str() != browser.category) continue;
            if (browser.favoritesOnly && !browser.favorites.count(id)) continue;
            if (!browser.showHidden && browser.hidden.count(id)) continue;
            matches.push_back(&row);
        }
        const int pages = std::max(1, static_cast<int>((matches.size() + 23) / 24));
        browser.page = std::clamp(browser.page, 0, pages - 1);
        float y = 12;
        Muted(c.parent, kPanelPad, y, kPanelInner, Fmt(matches.size() == 1 ? "{0} stat" : "{0} stats", {std::to_string(matches.size())}), 11);
        y += 31;
        auto hidden = AddButton(c, kPanelPad + kPanelInner, y, Fmt(browser.showHidden ? "Hide hidden stats ({0})" : "Show hidden stats ({0})", {std::to_string(browser.hidden.size())}), Look::Quiet,
            [] { browser.showHidden = !browser.showHidden; Refilter(); }, true, "stat-hidden");
        y += hidden.h + 18;
        if (matches.empty()) {
            y += EmptyText(c.parent, kPanelPad, y, kPanelInner, Tr(browser.favoritesOnly ? "No matching favorites" : "No matching stats"));
            y += AddButton(c, kPanelPad, y, Tr("Show all stats"), Look::Normal, [] {
                browser.query.clear(); browser.category = "All"; browser.favoritesOnly = false; browser.showHidden = false;
                Refilter(); Rebuild(browser.filters);
            }, false, "stat-clear").h + 24;
            return y;
        }
        const float cellW = (kPanelInner - 8) / 2;
        const size_t end = std::min(matches.size(), size_t((browser.page + 1) * 24));
        for (size_t i = size_t(browser.page * 24); i < end; ++i) {
            const J row = *matches[i]; const std::string id = row["favorite"].Str();
            const float x = kPanelPad + ((i % 2) ? cellW + 8.f : 0.f);
            Fill(c.parent, "Stat entry", x, y, cellW, 74, Hex("#0a121080"), 4);
            Outline(c.parent, "Stat entry border", x, y, cellW, 74, Hex("#5d675366"), 4);
            const float actionW = row["extra"].IsObject() ? 185 : 130;
            Style name{Face::BodySemi, 13, C("ink")}; name.wrap = true;
            Paragraph(c.parent, "Stat name", x + 15, y + 12, cellW - actionW - 20, name, Tr(row["name"].Str()));
            Muted(c.parent, x + 15, y + 43, cellW - actionW - 20, Tr(row["category"].Str()), 11, 1.2f);
            Hit open; open.x = x; open.y = y; open.w = cellW - actionW; open.h = 74; open.name = "stat-open-" + std::to_string(static_cast<int>(row["index"].Num())); open.action = [row] { OpenEditor(row, false); }; c.Add(std::move(open));
            float right = x + cellW - 8;
            auto hide = AddButton(c, right, y + 18, Tr(browser.hidden.count(id) ? "Show" : "Hide"), Look::Quiet, [id] { SaveMarker(id, false); }, true, "stat-hide-" + std::to_string(static_cast<int>(row["index"].Num()))); right -= hide.w + 3;
            auto star = AddButton(c, right, y + 18, browser.favorites.count(id) ? "★" : "☆", Look::Quiet, [id] { SaveMarker(id, true); }, true, "stat-star-" + std::to_string(static_cast<int>(row["index"].Num()))); right -= star.w + 3;
            if (row["extra"].IsObject()) AddButton(c, right, y + 18, Tr("Extra"), Look::Quiet, [row] { OpenEditor(row, true); }, true, "stat-extra-" + std::to_string(static_cast<int>(row["index"].Num())));
            if (i % 2 || i + 1 == end) y += 82;
        }
        y += 18;
        auto previous = AddButton(c, kPanelPad + kPanelInner / 2 - 65, y, "←", Look::Normal, [] { --browser.page; Rebuild(browser.results); }, true, "stat-prev");
        Style count{Face::Body, 12, C("ink")}; count.align = kCenter;
        TextBox(c.parent, "Stat page", kPanelPad + kPanelInner / 2 - 60, y, 120, previous.h, count, std::to_string(browser.page + 1) + " / " + std::to_string(pages));
        auto next = AddButton(c, kPanelPad + kPanelInner / 2 + 65, y, "→", Look::Normal, [] { ++browser.page; Rebuild(browser.results); }, false, "stat-next");
        SetEnabled(c, previous, browser.page > 0); SetEnabled(c, next, browser.page + 1 < pages);
        return y + previous.h + 24;
    };
}
} // namespace
void RegisterStatSections() { RegisterSection("stats_filters", MakeFilters); RegisterSection("stats_results", MakeResults); }
void ReleaseStatRoots() {
    // These roots outlive each modal rebuild, but must never call gchandle_free from a global
    // destructor after the game's IL2CPP runtime has already shut down.
    editor.preview.Reset();
    editor.input.Reset();
    editor.currentLabel.Reset();
    editor.sliderThumb.Reset();
}
} // namespace ep::ingame::view
