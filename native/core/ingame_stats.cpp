// Character browser and stat modal, matching renderStats/openStat and stat-model.js.
// Metadata is generated from the desktop model; no speculative stat mappings live here.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include "ingame_stat_rules.hpp"
#include "ingame_browser_rules.hpp"
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
    stats::Drafts drafts;
    Pin preview, input, currentLabel, sliderThumb;
    float trackX = 0, trackY = 0, trackW = 0;
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
        J prefs = Preference("epochpact.display-preferences.v1"); if (!prefs.IsObject()) prefs = model::Object();
        auto hidden = model::List(); for (const auto& value : values) hidden.items.push_back(model::Text(value));
        model::Put(prefs, "hiddenStats", hidden); text = model::Pack(prefs);
    }
    if (!StorePreference(favorite ? "epochpact.stat-favorites.v1" : "epochpact.display-preferences.v1", &text)) {
        Toast(Tr("Favorites could not be saved on this device."), true); return;
    }
    (favorite ? browser.favorites : browser.hidden) = std::move(values);
    Rebuild(browser.results);
}
const J& Rows() { return D()["statBrowser"][browser.tab]; }
std::string Id(const J& player) { const J& id = player["id"]; return id.IsString() ? id.text : id.IsNumber() ? Num(id.number) : ""; }
std::string Signed(double value, const std::string& suffix) { return (value > 0 ? "+" : "") + stats::Display(value) + suffix; }
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
        const auto& info = editor.config["summaryUnits"][mode];
        const double scale = info["scale"].Num(1);
        const std::string suffix = info["suffix"].Str();
        out += (out.empty() ? "" : " · ") + Signed(editor.current.values[i] * scale, suffix) + (i == 1 ? " increased" : i == 2 ? " more" : "");
    }
    if (out.empty()) out = Tr("No bonus");
    if (!editor.current.attached) out += " · " + Tr("Not active");
    if (!editor.current.game.empty()) out += " · " + Fmt("In game: {0}", {editor.current.game});
    return out;
}
void DrawEditor();
void SetValue(const std::string& text) {
    editor.value = text;
    editor.drafts.Edit(editor.mode, text);
    if (editor.preview) SetText(editor.preview.Get(), Preview());
    double value = 0;
    const double lo = Unit()["sliderMin"].Num(), hi = Unit()["sliderMax"].Num();
    if (editor.sliderThumb && hi > lo && stats::Number(text, &value)) {
        const float fraction = static_cast<float>(std::clamp((value - lo) / (hi - lo), 0.0, 1.0));
        Move(editor.sliderThumb.Get(), editor.trackX + fraction * (editor.trackW - 19) - 12.5f, editor.trackY);
    }
}
// Stat-specific CSS buttons have different dimensions and backgrounds from the generic kit.
enum class StatLook { Mode, Chip, Star, Step };
Button StatButton(const Ctx& c, float x, float y, float w, float h, const std::string& label,
                  StatLook look, bool selected, Action action, const std::string& id) {
    const bool mode = look == StatLook::Mode, chip = look == StatLook::Chip, star = look == StatLook::Star;
    Button b; b.w = w; b.h = h;
    b.node = New("Stat button", c.parent, x, y, w, h);
    Hit hit; hit.name = id; hit.x = x; hit.y = y; hit.w = w; hit.h = h; hit.action = std::move(action);
    void* bg = Fill(b.node.rt, "Background", 0, 0, w, h, kWhite, star || look == StatLook::Step ? 0 : 3);
    const Color base = selected && !star ? C("soft") : chip ? Hex("#00000022") : Alpha(C("soft"), 0);
    AddHover(hit, bg, base, star ? base : C("soft"));
    if (mode || look == StatLook::Step) Picture(b.node.rt, "Gradient", 0, 0, w, h, "grad-button.png", false, kWhite);
    if (look != StatLook::Step) {
        void* border = Outline(b.node.rt, "Border", 0, 0, w, h, kWhite, 3);
        const Color line = selected && !star ? C("accent") : mode ? C("line") : Alpha(C("line"), 0);
        AddHover(hit, border, line, star ? line : C("accent"));
    }
    if (selected && mode) Solid(b.node.rt, "Selected inset", 1, h - 3, w - 2, 2, C("accent"));
    b.labelColor = star ? (selected ? C("gold") : C("muted")) : selected ? C("accent") : C("ink");
    Style text{Face::Body, chip ? 11.f : star || look == StatLook::Step ? 20.f : 12.f, b.labelColor}; text.align = kCenter;
    b.label.Set(TextBox(b.node.rt, "Label", 0, 0, w, h, text, label));
    b.hit = c.Add(std::move(hit));
    return b;
}
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
        Send(command, [token, write, command](const std::string& response) {
            if (DialogToken() != token) return;
            editor.busy = false;
            std::string why;
            if (Refused(response, &why)) { editor.message = PlayerError(why); editor.ready = false; DrawEditor(); return; }
            if (write) {
                if (command.size() >= 6 && command.compare(command.size() - 6, 6, " reset") == 0) {
                    editor.drafts.values.clear(); editor.value = "0";
                } else editor.drafts.values.erase(editor.mode);
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
                editor.first = false;
            }
            if (editor.ready) editor.value = editor.drafts.Value(editor.config, editor.current, editor.mode);
            if (editor.ready && stats::Key(editor.config) == "statraw 9 0 0 0") Keep("statraw 9 0 0 0", response);
            DrawEditor();
        });
    });
}
void ChangeMode(const std::string& mode) {
    if (editor.busy) return;
    if (std::none_of(editor.config["modes"].items.begin(), editor.config["modes"].items.end(), [&](const J& m) { return m.Str() == mode; })) return;
    editor.mode = mode; editor.first = false;
    editor.value = editor.drafts.Value(editor.config, editor.current, mode);
    DrawEditor();
}
void DrawEditor() {
    ShowCustomDialog(740, [](const Ctx& c, float width) {
        // dialog border + form padding; Chromium reserves a stable 15px scrollbar gutter.
        const float pad = 31, w = width - 2 * pad - kBar, right = pad + w;
        float y = 28;
        Style eyebrow{Face::Body, 10, C("accent")}; eyebrow.spacing = 2;
        TextBox(c.parent, "Stat eyebrow", pad, y, w - 55, 14, eyebrow, Tr("CHARACTER BONUS"));
        AddButton(c, right, y, "×", Look::Normal, [] { CloseDialog(); }, true, "stat-close");
        y += 20;
        Style title{Face::Serif, 32, C("ink")}; title.wrap = true;
        y += Paragraph(c.parent, "Stat title", pad, y, w - 55, title, Tr(editor.row["name"].Str()) + (editor.config["secondary"].Num() ? " · " + Tr("Extra") : ""));
        std::string note = Tr("Customize this stat without changing your equipment.");
        std::vector<std::string> shared;
        if (editor.row["tab"].Str() == "sheet" && !editor.config["secondary"].Num()) for (const auto& name : editor.row["shared"].items) if (name.Str() != editor.row["name"].Str()) shared.push_back(Tr(name.Str()));
        if (!shared.empty()) {
            std::string list;
            for (size_t i = 0; i < std::min(size_t(3), shared.size()); ++i) list += (i ? ", " : "") + shared[i];
            note = Fmt("Shares its bonus with {0}", {list}) + (shared.size() > 3 ? Tr(" and other linked stats.") : ".");
        }
        y += 13 + Muted(c.parent, pad, y + 13, w, note, 12) + 20;
        Fill(c.parent, "Current background", pad, y, w, 65, Hex("#00000022"), 4);
        Outline(c.parent, "Current border", pad, y, w, 65, C("line"), 4);
        Style small{Face::Body, 9, C("muted")}; small.spacing = 1.3f;
        TextBox(c.parent, "Current caption", pad + 18, y + 15, w - 155, 11, small, Tr("CURRENT BONUS"));
        Style strong{Face::Body, 14, C("ink")};
        editor.currentLabel.Set(TextBox(c.parent, "Current bonus", pad + 18, y + 33, w - 155, 18, strong, Summary()));
        auto refresh = AddButton(c, right - 18, y + 14, Tr("↻ Refresh"), Look::Quiet, [] { Execute(stats::Key(editor.config), false); }, true, "stat-read");
        SetEnabled(c, refresh, !editor.busy && Eligible()); y += 85;
        size_t basicCount = 0;
        for (const auto& mode : editor.config["modes"].items) basicCount += mode.Str() != "more";
        const float modeW = basicCount ? (w - 8 * static_cast<float>(basicCount - 1)) / static_cast<float>(basicCount) : w;
        Style basicStyle{Face::Body, 12, C("ink")};
        const float modeH = LineHeight(basicStyle) + 24;
        float bx = pad;
        for (const auto& mode : editor.config["modes"].items) {
            if (mode.Str() == "more") continue;
            const std::string label = mode.Str() == "increased" ? Tr("Increase %") : editor.config["units"]["added"]["points"].Num() ? Tr("Add percentage points") : Tr("Flat bonus");
            auto b = StatButton(c, bx, y, modeW, modeH, label, StatLook::Mode, editor.mode == mode.Str(), [m = mode.Str()] { ChangeMode(m); }, "stat-mode-" + mode.Str());
            SetEnabled(c, b, !editor.busy); bx += b.w + 8;
        }
        y += basicCount ? modeH + 12 : 12;
        const float cardY = y;
        void* bg = Picture(c.parent, "Bonus card", pad, y, w, 10, "grad-stat-value.png", false, kWhite);
        void* border = Outline(c.parent, "Bonus border", pad, y, w, 10, C("line"), 5);
        y += 23;
        y += Muted(c.parent, pad + 23, y, w - 46, Tr("Your bonus"), 11) + 9;
        Style preview{Face::Serif, 25, C("accent")}; preview.line = 1.4f;
        editor.preview.Set(TextBox(c.parent, "Bonus preview", pad + 23, y, w - 46, 35, preview, Preview()));
        y += 62;
        const auto change = [](double amount) {
            double value = 0; stats::Number(editor.value, &value);
            SetValue(Num(std::clamp(value + amount, Unit()["minimum"].Num(), Unit()["maximum"].Num()))); DrawEditor();
        };
        const std::string unit = Unit()["points"].Num() ? "pp" : Unit()["suffix"].Str();
        const float unitW = std::max(14.f, TextAdvance(Face::Body, 13, unit));
        const float trackX = pad + 25, trackW = w - 46 - 184 - unitW - 30 - 4, stepperX = trackX + trackW + 17;
        Picture(c.parent, "Stat slider", trackX, y + 18, trackW, 8, "track.png", false, kWhite);
        double v = 0; stats::Number(editor.value, &v);
        const double lo = Unit()["sliderMin"].Num(), hi = Unit()["sliderMax"].Num();
        const float position = hi > lo ? static_cast<float>(std::clamp((v - lo) / (hi - lo), 0.0, 1.0)) : 0;
        editor.trackX = trackX; editor.trackY = y; editor.trackW = trackW;
        void* thumb = Picture(c.parent, "Stat slider thumb", trackX + position * (trackW - 19) - 12.5f, y, 44, 44, "thumb.png", false, kWhite);
        editor.sliderThumb.Set(TransformOf(thumb));
        const auto slider = [trackW, lo, hi](float x) {
            if (editor.busy || hi <= lo) return;
            const double fraction = std::clamp(static_cast<double>((x - 9.5f) / (trackW - 19)), 0.0, 1.0);
            const double step = Unit()["step"].Num(1);
            SetValue(Num(std::clamp(lo + std::round(fraction * (hi - lo) / step) * step, lo, hi)));
            if (const auto* set = Find("Unity.TextMeshPro.dll", "TMPro", "TMP_InputField", "set_text", 1)) {
                managed::Root text(il2cpp::api().string_new(editor.value.c_str())); void* args[]{text.Get()}; managed::Invoke(set, editor.input.Get(), args);
            }
        };
        Hit range; range.kind = HitKind::Track; range.x = trackX; range.y = y; range.w = trackW; range.h = 44; range.enabled = !editor.busy; range.name = "stat-range"; range.drag = slider; c.Add(std::move(range));
        Fill(c.parent, "Stepper background", stepperX, y, 184, 44, Hex("#00000033"), 3);
        Outline(c.parent, "Stepper border", stepperX, y, 184, 44, C("line"), 3);
        auto minus = StatButton(c, stepperX + 1, y + 1, 40, 42, "−", StatLook::Step, false, [change] { change(-1); }, "stat-minus");
        InputLook look; look.filled = look.framed = false; look.padX = 5;
        Style input{Face::Body, 17, C("ink")}; input.align = kCenter;
        editor.input.Set(MakeInput(c, stepperX + 41, y + 1, 102, 42, input, look, editor.value, "0", true, [](const std::string& text) {
            SetValue(text);
        }, "stat-value"));
        Enable(editor.input.Get(), !editor.busy);
        auto plus = StatButton(c, stepperX + 143, y + 1, 40, 42, "+", StatLook::Step, false, [change] { change(1); }, "stat-plus");
        SetEnabled(c, minus, !editor.busy); SetEnabled(c, plus, !editor.busy);
        Style unitStyle{Face::Body, 13, C("gold")}; unitStyle.align = kLeft;
        TextBox(c.parent, "Bonus unit", stepperX + 199, y, unitW, 44, unitStyle, unit);
        y += 67;
        const std::string help = editor.mode == "more" ? "A separate multiplier. +50% More multiplies this stat by ×1.5." : Unit()["points"].Num() ?
            "Percentage points add directly: a 5% stat with +10 points becomes 15%, before game caps." : editor.mode == "increased" ?
            "Adds to this stat's existing percentage increases. Equipment bonuses still apply." : "Adds this amount to the stat. Zero removes this bonus amount.";
        y += Muted(c.parent, pad + 22, y, w - 44, Tr(help), 12) + 6;
        y += Muted(c.parent, pad + 22, y, w - 44, Fmt("Slider {0}–{1}{2}. You can type values outside this slider range.",
            {Num(Unit()["sliderMin"].Num()), Num(Unit()["sliderMax"].Num()), Unit()["suffix"].Str()}), 10) + 20;
        Resize(TransformOf(bg), w, y - cardY); Resize(TransformOf(border), w, y - cardY);
        y += 15;
        y += Details(c, pad, y, Tr("Advanced"), editor.advanced, [] { if (!editor.busy) { editor.advanced = !editor.advanced; DrawEditor(); } }) + 10;
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
        auto reset = AddButton(c, pad, y, Tr("Reset bonus"), Look::Normal, [] { Execute(stats::Key(editor.config) + " reset", true); }, false, "stat-reset");
        auto apply = AddButton(c, right, y, Tr("Apply bonus") + " ◇", Look::Primary, [] {
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
    editor.drafts.values.clear();
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
        y += Muted(c.parent, kPanelPad, y, kPanelInner, Tr("Choose a stat, adjust your bonus, then apply.")) + 4;
        y += Muted(c.parent, kPanelPad, y, kPanelInner, Tr("Your bonuses are remembered for this character and applied again when it loads, unless Apply at startup is Off. Reset all bonuses removes them.")) + 20;
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
            const float estimate = std::ceil(TextAdvance(Face::Body, 11, Tr(category))) + 22;
            if (x + estimate > kPanelPad + kPanelInner) { x = kPanelPad; y += 36; }
            auto b = StatButton(c, x, y, estimate, 30, Tr(category), StatLook::Chip, browser.category == category,
                [category] { browser.category = category; Refilter(); Rebuild(browser.filters); }, "stat-category-" + category);
            x += b.w + 6;
        }
        return y + 30;
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
        float y = 20;
        Muted(c.parent, kPanelPad, y, kPanelInner, Fmt(matches.size() == 1 ? "{0} stat" : "{0} stats", {std::to_string(matches.size())}), 11);
        Style help{Face::Body, 11, C("muted")}; help.align = kTopRight;
        TextBox(c.parent, "Favorites hint", kPanelPad, y, kPanelInner, LineHeight(help), help, Tr("☆ Save your most-used stats"));
        y += LineHeight(help) + 28;
        auto hidden = AddButton(c, kPanelPad, y, Fmt(browser.showHidden ? "Hide hidden stats ({0})" : "Show hidden stats ({0})", {std::to_string(browser.hidden.size())}), Look::Normal,
            [] { browser.showHidden = !browser.showHidden; Refilter(); }, false, "stat-hidden");
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
            Muted(c.parent, x + 15, y + 43, cellW - actionW - 20, Tr(row["category"].Str()) + (row["derived"].IsBool() && row["derived"].boolean ? " · " + Tr("Calculated by the game") : ""), 11, 1.2f);
            Hit open; open.x = x; open.y = y; open.w = cellW - actionW; open.h = 74; open.name = "stat-open-" + std::to_string(static_cast<int>(row["index"].Num())); open.action = [row] { OpenEditor(row, false); }; c.Add(std::move(open));
            float right = x + cellW - 8;
            auto hide = AddButton(c, right, y + 18, Tr(browser.hidden.count(id) ? "Show" : "Hide"), Look::Quiet, [id] { SaveMarker(id, false); }, true, "stat-hide-" + std::to_string(static_cast<int>(row["index"].Num()))); right -= hide.w + 3;
            auto star = StatButton(c, right - 40, y + 17, 40, 40, browser.favorites.count(id) ? "★" : "☆", StatLook::Star, browser.favorites.count(id) != 0,
                [id] { SaveMarker(id, true); }, "stat-star-" + std::to_string(static_cast<int>(row["index"].Num()))); right -= star.w + 3;
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
void MakeMinionBonuses(Section& s) {
    s.build = [](Section&, const Ctx& outer) {
        Panel panel = OpenPanel(outer.parent, kPadX, 0, kPanelW);
        const Ctx c = outer.At(panel.rt.Get(), kPadX, 0);
        float y = kPanelPad;
        y += H2(c.parent, kPanelPad, y, kPanelInner, Tr("Minion and companion bonuses")) + 14;
        y += Muted(c.parent, kPanelPad, y, kPanelInner, Tr("These controls use the existing character stat editor. Minion tags keep these bonuses separate from your own damage and health.")) + 14;
        std::vector<J> rows;
        for (const auto& row : D()["statBrowser"]["aliases"].items)
            if (row["summon"].IsBool() && row["summon"].boolean) rows.push_back(row);
        const float w = (kPanelInner - kGap) / 2;
        for (size_t i = 0; i < rows.size(); i += 2) {
            float rowHeight = 0;
            std::vector<Node> cells;
            std::vector<void*> frames;
            std::vector<int> hits;
            for (size_t j = i; j < std::min(rows.size(), i + 2); ++j) {
                const J row = rows[j];
                const float x = kPanelPad + static_cast<float>(j - i) * (w + kGap);
                Node cell = New("Minion bonus", c.parent, x, y, w, 10);
                void* bg = Fill(cell.rt, "Background", 0, 0, w, 10, kWhite, 3);
                void* border = Outline(cell.rt, "Border", 0, 0, w, 10, kWhite, 3);
                float cy = 27;
                cy += H3(cell.rt, 23, cy, w - 46, row["name"].Str()) + 10;
                cy += Muted(cell.rt, 23, cy, w - 46, Tr("Edit bonus →")) + 23;
                rowHeight = std::max(rowHeight, cy);
                Hit hit; hit.x = x; hit.y = y; hit.w = w; hit.name = "summon-stat-" + Num(row["index"].Num());
                hit.action = [row] { OpenEditor(row, false); };
                AddHover(hit, bg, C("panel"), C("soft")); AddHover(hit, border, C("line"), C("accent"));
                cells.push_back(cell); frames.push_back(bg); frames.push_back(border); hits.push_back(c.Add(std::move(hit)));
            }
            for (const auto& cell : cells) Resize(cell.rt, w, rowHeight);
            for (void* graphic : frames) Resize(TransformOf(graphic), w, rowHeight);
            for (int hit : hits) (*c.hits)[hit].h = rowHeight;
            y += rowHeight + kGap;
        }
        if (!rows.empty()) y -= kGap;
        y += kPanelPad;
        ClosePanel(panel, kPanelW, y);
        return y;
    };
}
} // namespace
bool OpenStatView(const std::string& address) {
    stats::Location at;
    if (!stats::ViewLocation(address, &at)) return false;
    const auto& rows = D()["statBrowser"][at.tab].items;
    if (at.index >= static_cast<int>(rows.size()) || (at.extra && !rows[at.index]["extra"].IsObject())) return false;
    CloseDialog();
    OpenEditor(rows[at.index], at.extra);
    return true;
}
bool AdvancedStatView() { if (editor.busy || editor.row.IsNull()) return false; editor.advanced = true; DrawEditor(); return true; }
void RegisterStatSections() {
    RegisterSection("stats_filters", MakeFilters);
    RegisterSection("stats_results", MakeResults);
    RegisterSection("minion_bonuses", MakeMinionBonuses);
}
void ReleaseStatRoots() {
    // These roots outlive each modal rebuild, but must never call gchandle_free from a global
    // destructor after the game's IL2CPP runtime has already shut down.
    editor.preview.Reset();
    editor.input.Reset();
    editor.currentLabel.Reset();
    editor.sliderThumb.Reset();
}
} // namespace ep::ingame::view
