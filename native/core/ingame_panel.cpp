// The EpochPact window inside the game (F6). See ingame_panel.hpp.
//
// It is the desktop interface (ui/index.html, ui/style.css, the Chronoforge theme) drawn with the
// game's own Unity UI: the same 1440x900 layout, colours, fonts, artwork and texts, scaled to fit
// the screen. Pages, settings and translations come from the embedded ui.json, which
// tools/build_ingame_assets.py writes from the same catalog and texts the desktop app uses.
//
// Speed. Everything is built ahead of time, a few milliseconds per frame, once a character is in
// game. Opening and closing switch one Canvas component; Unity keeps every mesh, so nothing is
// rebuilt. Each page has a Canvas of its own that is on only while that page is shown. Clipping
// uses a stencil Mask, which costs nothing per frame. Clicks, hover, sliders and scrolling use our
// own copy of the layout, so an open window makes few calls into the game and a closed one none.
//
// A page is a column of sections (ingame_window.hpp): the setting panels built here, and native
// sections (ingame_sections*.cpp) that read live data, are built again when it changes, and move
// the sections below them. Settings go through commands::SubmitUi as ordinary command lines, so
// each command's own limits and the offline-only gate apply unchanged. As in the desktop app,
// automatic settings are sent 250 ms after the last change and the others wait as a draft for
// Apply changes; game-save actions confirm first and check the loaded character.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_panel.hpp"
#include "ingame_window.hpp"
#include "commands.hpp"
#include "fonts.hpp"
#include "lifecycle.hpp"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#pragma comment(lib, "shell32.lib")

namespace ep::ingame {
namespace {
using namespace managed;
using namespace view;
using JValue = json::Value;
using assets::Face;

std::map<std::string, Control> controls;
void LoadControls() {
    controls.clear();
    for (const auto& [id, v] : D()["controls"].members) {
        Control c;
        c.id = id;
        c.widget = v["widget"].Str();
        c.label = v["label"].Str(id);
        c.description = v["description"].Str();
        c.lifetime = v["lifetime"].Str();
        c.command = v["command"].Str();
        c.unit = v["unit"].Str();
        c.icon = v["icon"].Str("default");
        c.defaultText = v["defaultText"].Str();
        c.min = v["min"].Num(0);
        c.max = v["max"].Num(1);
        c.integer = v["integer"].IsBool() && v["integer"].boolean;
        c.hasDefault = v["default"].IsNumber() || v["default"].IsBool();
        c.def = v["default"].Num(0);
        for (const auto& x : v["choices"].items) c.choices.push_back(x.Num());
        for (const auto& x : v["choiceLabels"].items) c.choiceLabels.push_back(x.Str());
        for (const auto& p : v["params"].items) {
            Param param;
            param.name = p["name"].Str();
            param.type = p["type"].Str();
            param.hasMin = p["minimum"].IsNumber();
            param.hasMax = p["maximum"].IsNumber();
            param.min = p["minimum"].Num();
            param.max = p["maximum"].Num();
            for (const auto& x : p["choices"].items) param.choices.push_back(x.IsString() ? x.text : Num(x.Num()));
            for (const auto& x : p["choiceLabels"].items) param.choiceLabels.push_back(x.Str());
            c.params.push_back(std::move(param));
        }
        c.read = v["source"]["read"].Str();
        c.path = v["source"]["path"].Str();
        controls.emplace(id, std::move(c));
    }
}

// The values and the state of each setting, as app.js keeps them.
std::map<std::string, double> values, appliedValues;
struct Due { double value; uint64_t at; };
std::map<std::string, Due> pending;       // automatic settings waiting for their 250 ms
std::set<std::string> drafts, applied;    // manual drafts; settings applied this session
std::map<std::string, std::string> errors;
std::string sendingId;
std::string sessionState;                 // "InGame", from sessionread
bool statusKnown = false, offlinePlay = false, transitioning = false;
bool eligibleBuilt = false;  // what the sections' action reasons were built with
std::vector<std::pair<uint64_t, std::function<void()>>> timers;  // After(): due time, work
uint64_t nextEligibleCheck = 0;
// Reads kept for the sections: the reply text and its JSON.
struct Kept { std::string text; std::shared_ptr<JValue> json; };
std::map<std::string, Kept> cache;

// ------------------------------------------------------------------------------ the window
struct Card {
    const Control* c = nullptr;
    bool automatic = true;
    int section = -1;
    float x = 0, y = 0, h = 0;           // section coords; natural height
    float toolsY = 0;                    // the tools row, pushed down when a neighbour is taller
    float trackX = 0, trackW = 0;        // card coords
    Pin node, frame, grad, tools, value, thumb, knob, knobImage, switchFill, switchFrame, onOff, feedback, choiceText;
    std::vector<int> hits;               // in its section's hits
    float w = kCardW;                    // full width: the only card of a Crafting panel
    std::string valueShown = "\x01", feedbackShown = "\x01";
    bool placeholder = false;
    float knobFrom = 4, knobTo = 4;
    double knobStart = -1;
};
struct Page {
    std::string id, nav, symbol, symbolFace, title, subtitle;
    std::string tab, draftTab;           // the shown tab; the tab whose drafts Apply changes sends
    bool compact = false;                // the hero of a page with tabs (Crafting): 156px
    float heroH = kHeroH;
    Pin root, canvas;
    float height = 0;
    bool built = false, queued = false;
    std::vector<int> sections;
};
struct Watched { int section = -1; Pin field; std::string last, name; std::function<void(const std::string&)> onChange; };
struct Window {
    Pin rootGo, rootCanvas, windowRt, listener, mainContent, sideContent, mainThumb, sideThumb, crumb, connection, draftLabel, applyGroup, applyGo, discardGo,
        heroEyebrow, heroTitle, heroSub, heroArt, heroArtCompact, heroBorder, popup, toast, dialog, raycaster, applyLabel;
    std::vector<Pin> navFill, navFrame, navBar;
    std::vector<Page> pages;
    std::vector<std::unique_ptr<Section>> sections;
    std::vector<Card> cards;
    std::vector<Hit> hits, popupHits, dialogHits;  // the window's own (shell, sidebar), the open list, the dialog
    std::vector<Watched> inputs;
    int page = 0, applyHit = -1, discardHit = -1;
    bool built = false, shown = false, dialogOpen = false, listOpen = false;
    bool parked = false;  // the root canvas stays on, drawn on a display nobody uses while closed
    bool heroCompact = false;
    std::function<void(float)> popupWheel;  // the open list scrolls (a long one)
    uint64_t toastUntil = 0;
    float scale = 1, mainScroll = 0, mainTarget = 0, sideScroll = 0, sideTarget = 0, sideHeight = 0;
    float thumbsAt[2] = {-1, -1};
    std::string lang;
} win;

std::map<std::string, std::string>& RememberedTabs() {
    static std::map<std::string, std::string> tabs;  // page id -> tab (app.js craftingSection)
    return tabs;
}
std::map<std::string, SectionMaker>& Makers() {
    static std::map<std::string, SectionMaker> makers;
    return makers;
}

std::deque<std::function<void()>> jobs;
std::atomic<int> hotkey{VK_F6};
std::atomic<bool> toggleRequested{false};
std::atomic<uint64_t> opens{0}, closes{0}, faults{0};
bool broken = false;   // repeated faults: nothing more until the next F6 or "panel 1"
std::string lastError;
std::string stage;     // what the window was doing, for a fault report

double Ms() {
    static LARGE_INTEGER frequency{};
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}
uint64_t Now() { return GetTickCount64(); }

// Timing: our own work and the game's frames, closed, open and right after opening or closing.
struct Timing {
    double openMs = 0, closeMs = 0, pageMs = 0, buildMs = 0, firstBuildMs = 0, rebuildMs = 0;
    double lastFrame = 0, frameAvgClosed = 0, frameAvgOpen = 0, stepAvgOpen = 0, stepMaxOpen = 0, idleCostMax = 0;
    double spikeAfterOpen = 0, spikeAfterClose = 0;
    int framesSinceOpen = 1000, framesSinceClose = 1000;
    uint64_t framesOpen = 0, framesClosed = 0, slowOpen = 0, slowClosed = 0;  // slow: over 50 ms, away from a toggle
    std::string slowestStage, slowestIdleStage;  // what the slowest step was doing last
    std::map<std::string, double> phases;        // the longest time of each part of a step, and of each build job over 4 ms
} timing;
void Record(const std::string& name, double ms) {
    double& longest = timing.phases[name];
    longest = std::max(longest, ms);
}

// -------------------------------------------------------------------------------- commands
// One command at a time through SubmitUi; replies arrive on the IPC worker and are handled on
// the main thread, in Frame.
struct Job { std::string line; Reply done; };
std::deque<Job> commandQueue;
bool commandInFlight = false;
std::mutex replyLock;
std::vector<std::pair<Reply, std::string>> replies;

void PumpCommands() {
    std::vector<std::pair<Reply, std::string>> ready;
    { std::lock_guard<std::mutex> hold(replyLock); ready.swap(replies); }
    for (auto& r : ready) { commandInFlight = false; if (r.first) r.first(r.second); }
    if (commandInFlight || commandQueue.empty()) return;
    Job job = commandQueue.front();
    auto done = job.done;
    if (commands::SubmitUi(job.line, [done](const std::string& reply) {
            std::lock_guard<std::mutex> hold(replyLock);
            replies.emplace_back(done, reply);
        })) {
        commandQueue.pop_front();
        commandInFlight = true;
    }
}

// ---------------------------------------------------------------------------- value display
void UpdateFooter();
void Paint(Card& k, bool animate);
void PaintId(const std::string& id) {
    for (auto& k : win.cards) if (k.c->id == id) Paint(k, true);
    for (auto& s : win.sections)
        if (s->built && s->onSetting && s->settings.count(id)) s->onSetting(*s, id);
    UpdateFooter();
}
double Shown(const Control& c) {
    if (auto it = values.find(c.id); it != values.end()) return it->second;
    return c.hasDefault ? c.def : c.min;
}
std::string ChoiceLabel(const Control& c, size_t i) {
    if (i < c.choiceLabels.size() && !c.choiceLabels[i].empty()) return Tr(c.choiceLabels[i]);
    return c.choices[i] == 0 ? Tr("Normal game choices") : Fmt("{0} choices", {Num(c.choices[i])});
}
void Paint(Card& k, bool animate) {
    const Control& c = *k.c;
    const double v = Shown(c);
    if (c.widget == "number") {
        // the stepper's input: the value, or its "Rank" placeholder
        const bool known = values.count(c.id) || c.hasDefault;
        const std::string text = known ? Num(c.id == "speed" ? std::round(v * 1e6) / 1e6 : v) : Tr("Rank");
        if (text != k.valueShown) {
            SetText(k.value.Get(), text);
            if (known == k.placeholder) Ink(k.value.Get(), known ? C("ink") : Hex("#8b8f8c"));
            k.placeholder = !known;
            k.valueShown = text;
        }
        const double t = c.max > c.min ? std::clamp((v - c.min) / (c.max - c.min), 0.0, 1.0) : 0;
        Move(k.thumb.Get(), static_cast<float>(k.trackX + 1 + 9.5 + t * (k.trackW - 2 - 19)) - 22, 15 + 4 - 22);
    } else if (c.widget == "toggle") {
        const bool on = v != 0;
        const std::string text = Tr(on ? "On" : "Off");
        if (text != k.valueShown) {
            SetText(k.onOff.Get(), text);
            k.valueShown = text;
            Fade(k.knobImage.Get(), on ? C("accent") : Hex("#c6c7bd"), 0);
            Fade(k.switchFill.Get(), on ? C("soft") : Hex("#202623"), 0);
            Fade(k.switchFrame.Get(), on ? C("accent") : Hex("#7b745a99"), 0);
            const float to = on ? 26.f : 4.f;
            if (animate && k.knobTo != to) { k.knobFrom = k.knobTo; k.knobStart = Ms(); }
            else Move(k.knob.Get(), to, 4);
            k.knobTo = to;
        }
    } else if (c.widget == "choice") {
        std::string label = Num(v);
        for (size_t i = 0; i < c.choices.size(); ++i) if (c.choices[i] == v) label = ChoiceLabel(c, i);
        if (label != k.valueShown) { SetText(k.choiceText.Get(), label); k.valueShown = label; }
    }
    // settingFeedback: the error, Applying…, Draft or Applied
    std::string fb;
    Color color = C("accent");
    float size = 10;
    if (auto e = errors.find(c.id); e != errors.end()) { fb = e->second.substr(0, e->second.find('\n')); color = Hex("#f2a594"); }
    else if (pending.count(c.id) || sendingId == c.id) { fb = Tr("Applying…"); size = 11; }
    else if (drafts.count(c.id)) { fb = Tr("Draft"); size = 11; }
    else if (applied.count(c.id)) fb = Tr("Applied");
    if (fb != k.feedbackShown) {
        SetText(k.feedback.Get(), fb);
        Ink(k.feedback.Get(), color);
        void* za[]{&size};
        Invoke(T("set_fontSize", 1), k.feedback.Get(), za);
        k.feedbackShown = fb;
    }
}

// ----------------------------------------------------------------------------------- cards
// settingCard (app.js) at (x, y) in the panel; (ox, oy) is the panel's place in its section.
// craft: in the Crafting page's content (#craft-category-content): no icon, no description
// min-height; width: the card's (the only card of a panel spans the grid).
float BuildCard(Section& s, void* panel, float ox, float oy, float x, float y, const Control& c, bool automatic, const JValue* override_, float width = kCardW,
                bool craft = false) {
    stage = "card " + c.id;
    const float cardW = width, cardInner = width - 2 * kCardPad;  // this card's own width
    Card k;
    k.c = &c;
    k.automatic = automatic;
    k.section = s.id;
    k.x = ox + x;
    k.y = oy + y;
    k.w = width;
    const int index = static_cast<int>(win.cards.size());
    Node card = New(c.id.c_str(), panel, x, y, cardW, 10);
    k.node.Set(card.rt);
    k.grad.Set(TransformOf(Picture(card.rt, "Gradient", 0, 0, cardW, 10, "grad-card.png", false, kWhite)));
    k.frame.Set(TransformOf(Outline(card.rt, "Border", 0, 0, cardW, 10, Hex("#5e644e77"), 4)));
    const std::string label = Tr(override_ ? (*override_)["label"].Str(c.label) : c.label);
    const std::string description = Tr(override_ ? (*override_)["description"].Str(c.description) : c.description);
    const bool choice = c.widget == "choice";
    // .control-top: icon, title, badge (margin-left auto); aligned to the top
    float badgeH = 0;
    const float badgeW = Badge(card.rt, cardW - kCardPad, kCardPad, LifetimeLabel(c.lifetime), &badgeH);
    const float titleRight = cardW - kCardPad - badgeW - 14;
    float topH = badgeH;
    if (choice) {
        Style h3{Face::Serif, 20, C("ink")};
        h3.wrap = true;
        topH = std::max(topH, Paragraph(card.rt, "Title", kCardPad, kCardPad, titleRight - kCardPad, h3, label) + 12);
    } else {
        if (!craft) Picture(card.rt, "Icon", kCardPad, kCardPad, 55, 61, "icon-" + c.icon + ".png", false, kWhite);
        Style ts{Face::BodySemi, 15, C("ink")};
        ts.line = 1.45f;
        ts.wrap = true;
        const float tx = craft ? kCardPad : kCardPad + 55 + 14;
        topH = std::max({topH, craft ? 0.f : 61.f, Paragraph(card.rt, "Title", tx, kCardPad, titleRight - tx, ts, label) + 8});
    }
    // .control-description: margin 6px 0 14px, min-height 40px
    Style ds{Face::Body, 12, C("muted")};
    ds.line = 1.65f;
    ds.wrap = true;
    float cy = kCardPad + topH + 6;
    cy += std::max(craft ? 0.f : 40.f, Paragraph(card.rt, "Description", kCardPad, cy, cardInner, ds, description)) + 14;
    k.toolsY = cy;
    Node tools = New("Tools", card.rt, 0, cy, cardW, 10);
    k.tools.Set(tools.rt);
    float toolsH = 0;
    Hit def;
    def.kind = HitKind::Default;
    def.area = Area::Main;
    def.a = index;
    def.name = c.id;
    // tools coordinates: from the top-left of the card at the tools row
    const auto place = [&](Hit& h, float hx, float hy) { h.x = k.x + hx; h.y = k.y + cy + hy; };
    const auto push = [&](Hit h) { s.hits.push_back(std::move(h)); k.hits.push_back(static_cast<int>(s.hits.size()) - 1); };
    if (c.widget == "number") {
        toolsH = 38;
        Button d = MakeButton(tools.rt, Tr("Default"), Look::Quiet, def);
        const float dx = cardW - kCardPad - d.w, dy = (toolsH - d.h) / 2;
        Move(d.node.rt, dx, dy);
        place(def, dx, dy);
        // .unit (an empty one still takes its gaps)
        Style us{Face::Body, 13, C("gold")};
        us.align = kLeft;
        const std::string unitText = Tr(Trim(c.unit));
        void* unit = TextBox(tools.rt, "Unit", 0, 0, 120, toolsH, us, unitText);
        const float unitX = dx - 10 - (unitText.empty() ? 0 : std::ceil(TextWidth(unit)));
        Move(TransformOf(unit), unitX, 0);
        // .stepper: 144x38 with a line border; buttons 33x36 with the button gradient; value 74 wide
        const float stepX = unitX - 10 - 144;
        Outline(tools.rt, "Stepper", stepX, 0, 144, 38, C("line"), 3);
        for (int side = 0; side < 2; ++side) {
            Hit h;
            h.kind = side ? HitKind::Plus : HitKind::Minus;
            h.area = Area::Main;
            h.a = index;
            h.name = c.id;
            const float bx = side ? stepX + 1 + 33 + 74 : stepX + 1;
            void* soft = Fill(tools.rt, "Step hover", bx, 1, 33, 36, kWhite);
            Picture(tools.rt, "Step gradient", bx, 1, 33, 36, "grad-button.png", false, kWhite);
            Style ss{Face::Body, 20, C("ink")};
            ss.align = kCenter;
            TextBox(tools.rt, side ? "Plus" : "Minus", bx, 1, 33, 36, ss, side ? "+" : "\xE2\x88\x92");
            AddHover(h, soft, Alpha(C("soft"), 0), C("soft"));
            h.w = 33;
            h.h = 36;
            place(h, bx, 1);
            push(std::move(h));
        }
        Style vs{Face::Body, 15, C("ink")};
        vs.align = kCenter;
        k.value.Set(TextBox(tools.rt, "Value", stepX + 34, 1, 74, 36, vs, ""));
        // input[type=range]: margin 2px, 8px tall, centred; takes the rest of the row
        k.trackX = kCardPad + 2;
        k.trackW = stepX - 10 - 2 - k.trackX;
        Picture(tools.rt, "Track", k.trackX, 15, k.trackW, 8, "track.png", false, kWhite);
        k.thumb.Set(TransformOf(Picture(tools.rt, "Thumb", 0, 0, 44, 44, "thumb.png", false, kWhite)));
        Hit track;
        track.kind = HitKind::Track;
        track.area = Area::Main;
        track.a = index;
        track.name = c.id;
        track.w = k.trackW + 4;
        track.h = toolsH;
        place(track, k.trackX - 2, 0);
        push(std::move(track));
    } else if (c.widget == "toggle") {
        // label.toggle (margin-left auto): "On"/"Off", gap 10, the 48x25 switch; gap 10, Default
        Button d = MakeButton(tools.rt, Tr("Default"), Look::Quiet, def);
        toolsH = d.h;
        const float dx = cardW - kCardPad - d.w;
        Move(d.node.rt, dx, 0);
        place(def, dx, 0);
        const float sx = dx - 10 - 48, sy = (toolsH - 25) / 2;
        Node sw = New("Switch", tools.rt, sx, sy, 48, 25);
        k.switchFill.Set(Fill(sw.rt, "Background", 0, 0, 48, 25, kWhite, 12));
        k.switchFrame.Set(Outline(sw.rt, "Border", 0, 0, 48, 25, kWhite, 12));
        Node knob = New("Knob", sw.rt, 4, 4, 17, 17);
        k.knob.Set(knob.rt);
        k.knobImage.Set(Picture(knob.rt, "Knob", 0, 0, 17, 17, "circle-17.png", false, kWhite));
        Style os{Face::Body, 14, C("ink")};
        os.align = kRight;
        k.onOff.Set(TextBox(tools.rt, "On/Off", sx - 10 - 120, 0, 120, toolsH, os, ""));
        Hit toggle;  // the whole label: its text and the switch
        toggle.kind = HitKind::Toggle;
        toggle.area = Area::Main;
        toggle.a = index;
        toggle.name = c.id;
        toggle.w = 48 + 10 + 34;
        toggle.h = toolsH;
        place(toggle, sx - 10 - 34, 0);
        push(std::move(toggle));
    } else if (choice) {
        // select (width 100%), then Default wrapped onto the next line (row gap 10)
        Style cs{Face::Body, 14, C("ink")};
        cs.align = kLeft;
        const float selH = SelectHeight(cs);
        Fill(tools.rt, "Select", kCardPad, 0, cardInner, selH, Hex("#080e0e"), 3);
        Outline(tools.rt, "Select border", kCardPad, 0, cardInner, selH, Hex("#7b745a99"), 3);
        k.choiceText.Set(TextBox(tools.rt, "Choice", kCardPad + 11, 0, cardInner - 40, selH, cs, ""));
        Style ar{Face::Body, 12, C("ink")};
        ar.align = kCenter;
        TextBox(tools.rt, "Arrow", kCardPad + cardInner - 26, 0, 16, selH, ar, "\xE2\x8C\x84");
        Hit sel;
        sel.kind = HitKind::Choice;
        sel.area = Area::Main;
        sel.a = index;
        sel.name = c.id;
        sel.w = cardInner;
        sel.h = selH;
        place(sel, kCardPad, 0);
        push(std::move(sel));
        Button d = MakeButton(tools.rt, Tr("Default"), Look::Quiet, def);
        Move(d.node.rt, kCardPad, selH + 10);
        place(def, kCardPad, selH + 10);
        toolsH = selH + 10 + d.h;
    }
    push(std::move(def));
    // .limits (margin-top 10px): the default text, a space, then the feedback (margin-left .6rem)
    Style ls{Face::Body, 10, C("muted")};
    const float ly = toolsH + 10;
    float limitsH = 0, fx = kCardPad;
    if (!choice) {
        void* lt = TextBox(tools.rt, "Limits", kCardPad, ly, cardInner, LineHeight(ls), ls, Tr(c.defaultText));
        limitsH = LineHeight(ls);
        fx += std::ceil(TextWidth(lt)) + 2.8f + 8.4f;
    }
    Style fs{Face::Body, 10, C("accent")};
    k.feedback.Set(TextBox(tools.rt, "Feedback", fx, ly - 0.5f, cardW - kCardPad - fx, 16, fs, ""));
    k.h = cy + ly + limitsH + kCardPad;
    Resize(tools.rt, cardW, ly + limitsH);
    Resize(card.rt, cardW, k.h);
    Resize(k.frame.Get(), cardW, k.h);
    Resize(k.grad.Get(), cardW, k.h);
    win.cards.push_back(std::move(k));
    Paint(win.cards.back(), false);
    if (!c.read.empty()) s.polls.insert(c.read);
    if (c.id == "speed") s.polls.insert("statraw 9 0 0 0");
    return win.cards.back().h;
}
// CSS grid rows stretch their cards, and .control-tools (margin-top auto) goes to the bottom.
void Stretch(int cardIndex, float rowH) {
    Card& k = win.cards[cardIndex];
    const float extra = rowH - k.h;
    if (extra <= 0.01f) return;
    Resize(k.node.Get(), k.w, rowH);
    Resize(k.frame.Get(), k.w, rowH);
    Resize(k.grad.Get(), k.w, rowH);
    Move(k.tools.Get(), 0, k.toolsY + extra);
    for (int h : k.hits) win.sections[k.section]->hits[h].y += extra;
}

// ---------------------------------------------------------------------------------- sections
Section& Sec(int id) { return *win.sections[id]; }
void NewBody(Section& s) {
    Node body = New("Body", s.root.Get(), 0, 0, kContentW, 10);
    s.body.Set(body.rt);
}
// settingPanel (app.js): the heading with its badge (or the Weaver's Will Refresh button), an
// optional muted paragraph and facts note, the 2-column grid of cards; an optional note after the
// panel. One job per row of cards, so building never holds a frame for long.
struct SettingsBuild {
    int section = 0;
    view::Panel panel;
    float y = 0, top = kPanelPad;  // top: the heading row (below the summary of a <details> panel)
    bool craft = false;            // ui.json look "craft": the Crafting page's cards
    std::vector<const Control*> list;
    std::vector<bool> automatic;
};
// <details> panels open or closed (app.js craftDisclosure), while the game runs.
std::map<std::string, bool>& DetailsOpen() {
    static std::map<std::string, bool> open;
    return open;
}
void ToggleDetails(int sectionId);
}  // namespace
namespace view {
// A setting panel kept after it is built, for its parts that change in place: the facts note
// (rewritten when its read arrives) and the tail after the cards (Weaver's Will affixes).
struct PanelState {
    Panel panel;
    float panelH = 0;
    std::string factsKind, factsRead;
    NoteParts facts;
    float factsY = 0;
    std::string tailKind;
    Pin tailNode;
    float tailY = 0, tailH = 0;
    size_t tailHits = 0;  // the section's hits from here on are the tail's
    Pin afterNote;        // p.note after the panel
    float afterNoteH = 0;
    // a <details class="panel"> (ui.json "details"): its summary, and the setting panel inside it
    bool details = false, open = true;
    std::string detailsKey;
    Pin content, marker, ring;
    float closedH = 0;
    size_t detailsHits = 0;  // the section's hits from here on are the content's
};
// Where a setting panel's cards and tail are drawn: the panel, or the content of its <details>.
void* PanelParent(const PanelState& ps) { return ps.details ? ps.content.Get() : ps.panel.rt.Get(); }
}  // namespace view
namespace {
std::map<std::string, Tail>& Tails() {
    static std::map<std::string, Tail> tails;
    return tails;
}
// The panel's shown height (a closed <details>: its summary), its note after it, the page below.
void FitPanel(Section& s, bool relayout = true) {
    PanelState& ps = *s.panelState;
    const float shown = ps.details && !ps.open ? ps.closedH : ps.panelH;
    ClosePanel(ps.panel, kPanelW, shown);
    if (ps.afterNote) Move(ps.afterNote.Get(), kPadX, shown + 20);
    s.h = shown + (ps.afterNote ? 20 + ps.afterNoteH : 0);
    if (relayout) Relayout(s.page);
}
// Everything of a setting panel from fromY down moves by dy; the panel and the page follow.
void ShiftBelow(Section& s, float fromY, float dy) {
    if (std::fabs(dy) < 0.01f || !s.panelState) return;
    PanelState& ps = *s.panelState;
    for (auto& k : win.cards) {
        if (k.section != s.id || k.y < fromY) continue;
        k.y += dy;
        Move(k.node.Get(), k.x - kPadX, k.y);
    }
    for (auto& h : s.hits) if (h.y >= fromY) h.y += dy;
    for (auto& w : s.wheels) if (w.y >= fromY) w.y += dy;
    if (ps.tailNode && ps.tailY >= fromY) {
        ps.tailY += dy;
        Move(ps.tailNode.Get(), 0, ps.tailY);
    }
    ps.panelH += dy;
    FitPanel(s);
}
std::string FactsText(const std::string& kind);
void UpdateFacts(Section& s) {
    PanelState& ps = *s.panelState;
    const std::string text = FactsText(ps.factsKind);
    if (text == s.state["facts"]) return;
    s.state["facts"] = text;
    const float before = ps.facts.h;
    ShiftBelow(s, ps.factsY + before, SetNote(ps.facts, text) - before);
}
void SettingsData(Section& s, const std::string& read) {
    if (!s.panelState) return;
    PanelState& ps = *s.panelState;
    if (!ps.factsKind.empty() && read == ps.factsRead) UpdateFacts(s);
    if (ps.tailKind.empty()) return;
    auto it = Tails().find(ps.tailKind);
    if (it == Tails().end() || !it->second.reads.count(read)) return;
    if (it->second.onData) it->second.onData(s, read);
    else RebuildTail(s.id);
}
void QueueSettings(std::vector<std::function<void()>>& out, int sectionId) {
    auto b = std::make_shared<SettingsBuild>();
    b->section = sectionId;
    out.push_back([b] {
        Section& s = Sec(b->section);
        const JValue& spec = *s.spec;
        const auto& ids = spec["controls"].items;
        for (size_t i = 0; i < ids.size(); ++i) {
            auto it = controls.find(ids[i].Str());
            if (it == controls.end()) continue;
            b->list.push_back(&it->second);
            b->automatic.push_back(i < spec["automatic"].items.size() ? spec["automatic"].items[i].boolean : true);
        }
        const bool allAutomatic = std::all_of(b->automatic.begin(), b->automatic.end(), [](bool v) { return v; });
        s.panelState = std::make_shared<PanelState>();
        PanelState& ps = *s.panelState;
        NewBody(s);
        b->panel = OpenPanel(s.body.Get(), kPadX, 0, kPanelW);
        b->craft = spec["look"].Str() == "craft";
        void* rt = b->panel.rt.Get();
        if (const JValue* details = spec.Find("details")) {
            // <details class="panel craft-advanced">: the summary (15px), then the setting panel as
            // its content (margin 20px 0; no border, padding or background; its inset shadow and
            // corners stay: .craft-advanced>.panel)
            ps.details = true;
            ps.detailsKey = (*details)["key"].Str();
            ps.open = DetailsOpen()[ps.detailsKey];
            const int id = s.id;
            void* marker = nullptr;
            const float summaryH = Details(Ctx{&s.hits, Area::Main, rt, kPadX, 0, s.id}, kPanelPad, kPanelPad, Tr((*details)["summary"].Str()), ps.open,
                                           [id] { ToggleDetails(id); }, 15, &marker);
            ps.marker.Set(marker);
            ps.closedH = kPanelPad + summaryH + kPanelPad;
            ps.detailsHits = s.hits.size();
            Node content = New("Details", rt, 0, 0, kPanelW, 10);
            ps.content.Set(content.rt);
            rt = content.rt;
            b->top = kPanelPad + summaryH + 20;
            ps.ring.Set(TransformOf(Picture(rt, "Inset", kPanelPad, b->top, kPanelInner, 8, "band-4.png", true, Hex("#02070466"), true)));
        }
        // .section-head: h2 (Georgia 27) and, centred beside it, the badge; margin-bottom 16
        Style h2{Face::Serif, 27, C("ink")};
        const float lh = LineHeight(h2);
        // the head's buttons first: a row as tall as the tallest, the h2 and buttons centred in it
        struct Placed { void* rt; float x, h; int hit; };
        std::vector<Placed> placed;
        float row = lh;
        if (spec["head"].Str() == "refresh") {
            Hit h;
            h.kind = HitKind::Refresh;
            h.area = Area::Main;
            h.name = "Refresh";
            Button btn = MakeButton(rt, Tr("Refresh"), Look::Normal, h);
            const float bx = kPanelW - kPanelPad - btn.w;
            h.x = kPadX + bx;
            s.hits.push_back(std::move(h));
            placed.push_back({btn.node.rt, bx, btn.h, static_cast<int>(s.hits.size()) - 1});
            row = std::max(row, btn.h);
        } else if (auto tail = Tails().find(spec["tail"].Str()); spec["head"].Str() == "tail" && tail != Tails().end() && tail->second.head) {
            // .section-head .actions: the tail's buttons, right to left, gap 10
            const Ctx hc{&s.hits, Area::Main, rt, kPadX, 0, s.id};
            const auto buttons = tail->second.head(s);
            float right = kPanelW - kPanelPad;
            for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
                Button btn = AddButton(hc, right, b->top, it->label, it->primary ? Look::Primary : Look::Normal, it->action, true, it->name);
                placed.push_back({btn.node.rt, right - btn.w, btn.h, btn.hit});
                row = std::max(row, btn.h);
                right -= btn.w + 10;
            }
        } else if (spec["head"].Str() != "none") {
            const float badgeY = b->top + (lh - (LineHeight(Style{Face::Body, 10}) + 12)) / 2;
            Badge(rt, kPanelW - kPanelPad, badgeY, Tr(allAutomatic ? "Changes apply automatically" : "Use Apply changes below"));
        }
        for (const auto& b2 : placed) {
            const float by = b->top + (row - b2.h) / 2;
            Move(b2.rt, b2.x, by);
            s.hits[b2.hit].y = by;
        }
        TextBox(rt, "Title", kPanelPad, b->top + (row - lh) / 2, kPanelInner - 300, lh, h2, Tr(spec["title"].Str()));
        b->y = b->top + row + 16;
        if (spec["text"].IsString()) {
            // p.muted: its 10px top margin collapses into the heading's 16
            b->y += Muted(rt, kPanelPad, b->y, kPanelInner, Tr(spec["text"].Str())) + 10;
        }
        if (spec["facts"].IsString()) {
            // p.note#rage-facts (margin-bottom 18), rewritten in place when its read arrives
            ps.factsKind = spec["facts"].Str();
            ps.factsRead = ps.factsKind == "rage" ? "rageread" : ps.factsKind;
            s.reads.insert(ps.factsRead);
            s.state["facts"] = FactsText(ps.factsKind);
            ps.factsY = b->y;
            b->y += NoteBox(rt, kPanelPad, b->y, kPanelInner, s.state["facts"], &ps.facts) + 18;
        }
        if (spec["tail"].IsString())
            if (auto it = Tails().find(spec["tail"].Str()); it != Tails().end()) s.reads.insert(it->second.reads.begin(), it->second.reads.end());
        if (spec["facts"].IsString() || spec["tail"].IsString()) s.onData = SettingsData;
    });
    const size_t count = (*Sec(sectionId).spec)["controls"].items.size();
    for (size_t row = 0; row < (count + 1) / 2; ++row) {
        out.push_back([b, row] {
            const size_t first = row * 2;
            if (first >= b->list.size()) return;
            Section& s = Sec(b->section);
            const JValue* overrides = s.spec->Find("overrides");
            const auto over = [&](const Control* c) { return overrides ? overrides->Find(c->id) : nullptr; };
            const int before = static_cast<int>(win.cards.size());
            // the panel is the build's until its last job (ps.panel then)
            void* parent = s.panelState->details ? s.panelState->content.Get() : b->panel.rt.Get();
            // #craft-category-content .control-grid:has(>article:only-child): one column
            const float w = b->craft && b->list.size() == 1 ? kPanelInner : kCardW;
            float rowH = BuildCard(s, parent, kPadX, 0, kPanelPad, b->y, *b->list[first], b->automatic[first], over(b->list[first]), w, b->craft);
            if (first + 1 < b->list.size())
                rowH = std::max(rowH, BuildCard(s, parent, kPadX, 0, kPanelPad + kCardW + kGap, b->y, *b->list[first + 1], b->automatic[first + 1],
                                                over(b->list[first + 1]), w, b->craft));
            for (int i = before; i < static_cast<int>(win.cards.size()); ++i) Stretch(i, rowH);
            b->y += rowH + kGap;
        });
    }
    // the tail: a second grid right after the cards' (no gap between the two grids)
    if ((*Sec(sectionId).spec)["tail"].IsString()) {
        out.push_back([b] {
            Section& s = Sec(b->section);
            auto it = Tails().find((*s.spec)["tail"].Str());
            if (it == Tails().end()) return;
            PanelState& ps = *s.panelState;
            ps.tailKind = it->first;
            // in a <details>: after the inner panel's 20px margin
            ps.tailY = (b->list.empty() ? b->y : b->y - kGap) + (ps.details ? 20 : 0);
            Node node = New("Tail", ps.details ? ps.content.Get() : b->panel.rt.Get(), 0, ps.tailY, kPanelW, 10);
            ps.tailNode.Set(node.rt);
            ps.tailHits = s.hits.size();
            ps.tailH = it->second.build(s, Ctx{&s.hits, Area::Main, node.rt, kPadX, ps.tailY, s.id});
        });
    }
    out.push_back([b] {
        Section& s = Sec(b->section);
        PanelState& ps = *s.panelState;
        const float cardsEnd = b->list.empty() ? b->y : b->y - kGap;
        if (ps.details) {
            // the inner panel: its inset shadow down to the cards' end, its corners over its content
            const float innerH = cardsEnd - b->top;
            Resize(ps.ring.Get(), kPanelInner, innerH);
            void* content = ps.content.Get();
            Picture(content, "Corner", kPanelPad + 4, b->top + 4, 13, 13, "corner.png", false, C("gold"));
            void* br = Picture(content, "Corner", kPanelPad + kPanelInner - 4, b->top + innerH - 4, 13, 13, "corner.png", false, C("gold"));
            Turn(TransformOf(br), 180);
        }
        const float end = ps.tailNode ? ps.tailY + ps.tailH : ps.details ? cardsEnd + 20 : cardsEnd;
        ps.panel = std::move(b->panel);
        ps.panelH = end + kPanelPad;
        s.after = 20;
        if ((*s.spec)["note"].IsString()) {
            // p.note after the panel: its margin-top collapses into the panel's 20; margin-bottom 18;
            // a tail as written (the rule name examples)
            NoteParts note;
            ps.afterNoteH = NoteBox(s.body.Get(), kPadX, ps.panelH + 20, kPanelW, Tr((*s.spec)["note"].Str()) + (*s.spec)["noteTail"].Str(), &note);
            ps.afterNote.Set(note.root.Get());
            s.after = 18;
        }
        if (ps.details && !ps.open) {
            SetActive(GameObjectOf(ps.content.Get()), false);
            for (size_t i = ps.detailsHits; i < s.hits.size(); ++i) s.hits[i].visible = false;
        }
        FitPanel(s, false);
        s.built = true;
    });
}
// A desktop section the in-game window does not have yet: its heading and a pointer to the app.
float BuildPending(Section& s, const Ctx& c) {
    view::Panel panel = OpenPanel(c.parent, kPadX, 0, kPanelW);
    float y = kPanelPad;
    y += H2(panel.rt.Get(), kPanelPad, y, kPanelInner, Tr((*s.spec)["title"].Str())) + 14;
    y += Muted(panel.rt.Get(), kPanelPad, y, kPanelInner, Tr("This section is not in the in-game window yet. Use the EpochPact app for it."));
    y += kPanelPad;
    ClosePanel(panel, kPanelW, y);
    return y;
}
// .summary-grid: four tiles, gap 14, margin-bottom 22.
float BuildSummary(Section& s, const Ctx& c) {
    std::vector<std::pair<std::string, std::string>> tiles;
    for (const auto& item : (*s.spec)["items"].items)
        tiles.emplace_back(item.items.size() > 0 ? item.items[0].Str() : "", Tr(item.items.size() > 1 ? item.items[1].Str() : ""));
    s.after = 22;
    return SummaryTiles(c.parent, kPadX, 0, kPanelW, tiles, 4);
}
// The overview's "Explore your workshop": a panel of button.settings-card links.
float BuildLinks(Section& s, const Ctx& c) {
    view::Panel panel = OpenPanel(c.parent, kPadX, 0, kPanelW);
    Style h3{Face::Serif, 20, C("ink")}, ms{Face::Body, 14, C("muted")};
    h3.wrap = true;
    ms.line = 1.65f;
    ms.wrap = true;
    float y = kPanelPad;
    y += H2(panel.rt.Get(), kPanelPad, y, kPanelInner, Tr((*s.spec)["title"].Str())) + 14;
    const auto& links = (*s.spec)["pages"].items;
    for (size_t i = 0; i < links.size(); i += 2) {
        struct Made { Node node; void* fill; void* frame; Node inner; float h; int hit; };
        std::vector<Made> row;
        for (size_t j = i; j < i + 2 && j < links.size(); ++j) {
            int target = -1;
            for (size_t q = 0; q < win.pages.size(); ++q) if (win.pages[q].id == links[j].Str()) target = static_cast<int>(q);
            if (target < 0) continue;
            const Page& t = win.pages[target];
            const float x = kPanelPad + (j - i) * (kCardW + kGap);
            Made m;
            m.node = New("Link", panel.rt.Get(), x, y, kCardW, 10);
            m.fill = Fill(m.node.rt, "Background", 0, 0, kCardW, 10, kWhite);
            m.frame = Outline(m.node.rt, "Border", 0, 0, kCardW, 10, kWhite);
            m.inner = New("Content", m.node.rt, 0, 0, kCardW, 10);
            Hit hit;
            hit.kind = HitKind::Link;
            hit.a = target;
            hit.name = t.id;
            AddHover(hit, m.fill, C("panel"), C("soft"));
            AddHover(hit, m.frame, C("line"), C("accent"));
            // padding 22, h3 margin 4px 0 10px, p.muted margin 10px 0 (collapsing with the h3's)
            float cy = 23 + 4;
            cy += Paragraph(m.inner.rt, "Title", 23, cy, kCardW - 46, h3, Tr(t.symbol + " " + t.nav)) + 10;
            cy += Paragraph(m.inner.rt, "Text", 23, cy, kCardW - 46, ms, Tr(t.subtitle)) + 10;
            m.h = cy + 23;
            hit.x = kPadX + x;
            hit.y = y;
            hit.w = kCardW;
            hit.h = m.h;
            m.hit = c.Add(std::move(hit));
            row.push_back(m);
        }
        float rowH = 0;
        for (const auto& m : row) rowH = std::max(rowH, m.h);
        for (auto& m : row) {
            // a button centres its content in the stretched row
            Resize(m.node.rt, kCardW, rowH);
            Resize(TransformOf(m.fill), kCardW, rowH);
            Resize(TransformOf(m.frame), kCardW, rowH);
            Move(m.inner.rt, 0, (rowH - m.h) / 2);
            (*c.hits)[m.hit].h = rowH;
        }
        y += rowH + kGap;
    }
    y += kPanelPad - kGap;
    ClosePanel(panel, kPanelW, y);
    return y;
}

// A page's content below the hero: a job per section (for settings, per row of cards).
void QueuePage(int index, bool front = false) {
    Page& p = win.pages[index];
    if (p.built || p.queued) return;
    p.queued = true;
    std::vector<std::function<void()>> list;
    list.push_back([index] {
        Page& q = win.pages[index];
        Node root = New(q.id.c_str(), win.mainContent.Get(), 0, 0, kContentW, 10);
        q.root.Set(root.rt);
        q.canvas.Set(AddCanvas(root.go));
    });
    const JValue& spec = D()["pages"].items[index];
    for (const JValue& sectionSpec : spec["sections"].items) {
        const std::string kind = sectionSpec["kind"].Str();
        const std::string native = sectionSpec["native"].Str();
        const JValue* specPtr = &sectionSpec;
        const int id = static_cast<int>(win.sections.size());
        auto s = std::make_unique<Section>();
        s->id = id;
        s->page = index;
        s->kind = kind == "native" ? native : kind;
        s->spec = specPtr;
        win.sections.push_back(std::move(s));
        win.pages[index].sections.push_back(id);
        list.push_back([id, index] {
            Section& sec = Sec(id);
            Node root = New(sec.kind.c_str(), win.pages[index].root.Get(), 0, 0, kContentW, 10);
            sec.root.Set(root.rt);
        });
        if (kind == "settings") {
            QueueSettings(list, id);
            continue;
        }
        list.push_back([id] {
            Section& sec = Sec(id);
            stage = "section " + sec.kind;
            if (sec.kind == "summary") sec.build = BuildSummary;
            else if (sec.kind == "links") sec.build = BuildLinks;
            else if (auto it = Makers().find(sec.kind); it != Makers().end()) it->second(sec);
            else sec.build = BuildPending;
            NewBody(sec);
            sec.h = sec.build(sec, sec.Body(sec.body.Get()));
            sec.built = true;
        });
    }
    list.push_back([index] {
        Page& q = win.pages[index];
        q.built = true;
        Relayout(index);
        // hidden until shown: a page whose Canvas is off costs nothing
        if (index != win.page) Enable(q.canvas.Get(), false);
    });
    if (front) for (auto it = list.rbegin(); it != list.rend(); ++it) jobs.push_front(std::move(*it));
    else for (auto& j : list) jobs.push_back(std::move(j));
}

// ------------------------------------------------------------------------------- the shell
// The hero's text styles (BuildBase), the h1 at a size: 3vw, or clamp(28px, 2.5vw, 38px) on a page
// with tabs.
Style HeroTitleStyle(float size) {
    Style h1{Face::Serif, size, C("ink")};
    h1.spacing = -0.5f;
    h1.wrap = true;
    return h1;
}
Style HeroSubStyle() {
    Style sub{Face::Body, 15, Hex("#c6d1c8")};
    sub.line = 1.65f;
    sub.wrap = true;
    return sub;
}
void BuildBase() {
    Node rootGo = New("EpochPact In-game Window", nullptr, 0, 0, 0, 0);
    win.rootGo.Set(rootGo.go);
    fonts::SetWindowRoot(rootGo.rt);  // the interface font option leaves these texts alone
    void* canvas = AddCanvas(rootGo.go);
    win.rootCanvas.Set(canvas);
    int overlay = 0, order = 30000;
    void* ma[]{&overlay};
    void* oa[]{&order};
    void* sa[]{&win.scale};
    Invoke(Find("UnityEngine.UIModule.dll", "UnityEngine", "Canvas", "set_renderMode", 1), canvas, ma);
    Invoke(Find("UnityEngine.UIModule.dll", "UnityEngine", "Canvas", "set_sortingOrder", 1), canvas, oa);
    Invoke(Find("UnityEngine.UIModule.dll", "UnityEngine", "Canvas", "set_scaleFactor", 1), canvas, sa);
    void* keep[]{rootGo.go};
    Invoke(U("Object", "DontDestroyOnLoad", 1), nullptr, keep);
    Enable(canvas, false);
    // The window, centred on the screen. Everything but the pages sits on its canvas, which stays
    // on: texts get their meshes while the window is closed, and only the root canvas switches.
    Node w = New("Window", rootGo.rt, 0, 0, kW, kH);
    V2 mid{0.5f, 0.5f}, zero{0, 0};
    void* mida[]{&mid};
    void* za[]{&zero};
    for (const char* field : {"set_anchorMin", "set_anchorMax", "set_pivot"}) Invoke(U("RectTransform", field, 1), w.rt, mida);
    Invoke(U("RectTransform", "set_anchoredPosition", 1), w.rt, za);
    win.windowRt.Set(w.rt);
    AddCanvas(w.go);
    win.raycaster.Set(Add(w.go, "UnityEngine.UI.dll", "UnityEngine.UI", "GraphicRaycaster"));
    // The page colour on the window itself catches the pointer, so the game treats the window like
    // one of its own panels: no clicks go through to the world, also over a text field.
    void* catcher = Add(w.go, "UnityEngine.UI.dll", "UnityEngine.UI", "Image");
    Tint(catcher, C("bg"));
    Raycast(catcher, true);
    win.listener.Set(Add(w.go, "LE.dll", "", "UIMouseListener"));
    Enable(win.listener.Get(), false);
    // main: the radial background, the clipped scrolling content with the hero
    Picture(w.rt, "Main background", kMainX, kMainY, kMainW, kMainH, "main.png", false, kWhite);
    Node viewport = New("Main viewport", w.rt, kMainX, kMainY, kContentW, kMainH);
    Clip(viewport);
    Node content = New("Main content", viewport.rt, 0, 0, kContentW, kMainH);
    win.mainContent.Set(content.rt);
    // .hero-art (inside the bottom border); a page with tabs has its own, 156px hero
    win.heroArt.Set(TransformOf(Picture(content.rt, "Hero", 0, 0, kContentW, kHeroH - 1, "hero.jpg", false, kWhite)));
    win.heroArtCompact.Set(TransformOf(Picture(content.rt, "Hero compact", 0, 0, kContentW, kHeroCompactH - 1, "hero-compact.jpg", false, kWhite)));
    SetActive(GameObjectOf(win.heroArtCompact.Get()), false);
    win.heroBorder.Set(TransformOf(Solid(content.rt, "Hero border", 0, kHeroH - 1, kContentW, 1, C("line"))));
    Style eyebrow{Face::Body, 10, C("accent")};
    eyebrow.spacing = 2;
    const Style h1 = HeroTitleStyle(43.2f), sub = HeroSubStyle();
    win.heroEyebrow.Set(TextBox(content.rt, "Eyebrow", 32, 0, kHeroTextW, LineHeight(eyebrow), eyebrow, Tr("CHRONOFORGE")));
    win.heroTitle.Set(TextBox(content.rt, "Title", 32, 0, kHeroTextW, LineHeight(h1), h1, ""));
    win.heroSub.Set(TextBox(content.rt, "Subtitle", 32, 0, kHeroTextW, LineHeight(sub), sub, ""));
    // the main scrollbar: scrollbar-color gold on the page colour
    Solid(w.rt, "Scrollbar", kMainX + kContentW, kMainY, kBar, kMainH, C("bg"));
    Style arrow{Face::Body, 7, C("gold")};
    arrow.align = kCenter;
    TextBox(w.rt, "Scroll up", kMainX + kContentW, kMainY, kBar, kBar, arrow, "\xE2\x96\xB2");
    TextBox(w.rt, "Scroll down", kMainX + kContentW, kMainY + kMainH - kBar, kBar, kBar, arrow, "\xE2\x96\xBC");
    win.mainThumb.Set(TransformOf(Fill(w.rt, "Scroll thumb", kMainX + kContentW + 3, kMainY + kBar, kBar - 6, 40, C("gold"), 4)));
}
Ctx FixedCtx(void* parent = nullptr) { return {&win.hits, Area::Fixed, parent ? parent : win.windowRt.Get(), 0, 0, -1}; }
void RequestValues(bool all);
void BuildTopbar() {
    void* w = win.windowRt.Get();
    Solid(w, "Topbar", kMainX, 1, kMainW, kTopH, Hex("#090e0eef"));
    Solid(w, "Topbar border", kMainX, kTopH, kMainW, 1, C("line"));
    Style s{Face::Body, 12, C("muted")};
    s.align = kLeft;
    win.crumb.Set(TextBox(w, "Breadcrumb", kMainX + 20, 1, 600, kTopH - 1, s, ""));
    // .top-search: 250 wide at the right edge: the ⌕ (27px gold), gap 8, the input
    const float searchX = kMainX + kMainW - 20 - 250;
    Style ss{Face::Symbol, 27, C("gold")};  // Inter has no ⌕
    ss.align = kLeft;
    TextBox(w, "Search icon", searchX, 1, 20, kTopH - 1, ss, "\xE2\x8C\x95");
    const float inputX = searchX + 23.6f, inputW = 250 - 23.6f, inputH = 35.3f, inputY = 1 + (kTopH - 1 - inputH) / 2;
    Fill(w, "Search", inputX, inputY, inputW, inputH, Hex("#080e0e"), 3);
    Outline(w, "Search border", inputX, inputY, inputW, inputH, Hex("#7b745a99"), 3);
    Style ps{Face::Body, 12, Hex("#8b8f8c")};
    ps.align = kLeft;
    TextBox(w, "Search placeholder", inputX + 11, inputY, inputW - 22, inputH, ps, Tr("Find a setting…"));
    // ↻ Refresh (quiet), 16px left of the search; the connection text 16px left of it
    Button b = AddButton(FixedCtx(), searchX - 16, 0, Tr("↻ Refresh"), Look::Quiet, [] { RequestValues(true); }, true, "refresh");
    const float ry = 1 + (kTopH - 1 - b.h) / 2, rx = win.hits[b.hit].x;
    Move(b.node.rt, rx, ry);
    win.hits[b.hit].y = ry;
    win.hits[b.hit].kind = HitKind::Refresh;
    Style cs{Face::Body, 12, C("muted")};
    cs.align = kRight;
    cs.line = 1.3f;
    win.connection.Set(TextBox(w, "Connection", rx - 16 - 380, 1, 380, kTopH - 1, cs, Tr("Checking connection")));
}
void ApplyDrafts();
void DiscardDrafts();
void BuildFooter() {
    void* w = win.windowRt.Get();
    Solid(w, "Footer", kMainX, kFootY, kMainW, kFootH, C("panel"));
    Solid(w, "Footer border", kMainX, kFootY, kMainW, 1, C("gold"));
    const float innerY = kFootY + 1 + 13, innerH = kFootH - 1 - 26;
    const float sealY = innerY + (innerH - 34) / 2;
    Picture(w, "Seal", kMainX + 23, sealY, 34, 34, "ring-34.png", false, C("accent"));
    Style check{Face::Body, 20, C("accent")};
    check.align = kCenter;
    TextBox(w, "Seal mark", kMainX + 23, sealY, 34, 34, check, "\xE2\x9C\x93");
    Style label{Face::Body, 14, C("ink")}, small{Face::Body, 10, C("muted")};
    const float blockH = LineHeight(label) + 5 + LineHeight(small), by = innerY + (innerH - blockH) / 2;
    win.draftLabel.Set(TextBox(w, "Status", kMainX + 23 + 34 + 15, by, 600, LineHeight(label), label, Tr("Ready")));
    TextBox(w, "Status detail", kMainX + 23 + 34 + 15, by + LineHeight(label) + 5, 600, LineHeight(small), small, Tr("Save actions use their own buttons."));
    Hit apply, discard;
    apply.kind = HitKind::Apply;
    apply.name = "apply";
    discard.kind = HitKind::Discard;
    discard.name = "discard";
    Button a = MakeButton(w, Tr("Apply changes") + " \xE2\x97\x87", Look::Primary, apply);
    Button d = MakeButton(w, Tr("Discard draft"), Look::Normal, discard);
    const float ax = kMainX + kMainW - 23 - a.w, dx = ax - 12 - d.w;
    apply.x = ax;
    apply.y = innerY + (innerH - a.h) / 2;
    discard.x = dx;
    discard.y = innerY + (innerH - d.h) / 2;
    Move(a.node.rt, apply.x, apply.y);
    Move(d.node.rt, discard.x, discard.y);
    // disabled (opacity .45) while there is no draft to apply
    win.applyGroup.Set(Add(a.node.go, "UnityEngine.UIModule.dll", "UnityEngine", "CanvasGroup"));
    win.applyLabel.Set(a.label.Get());
    win.applyGo.Set(a.node.go);
    win.discardGo.Set(d.node.go);
    win.hits.push_back(std::move(apply));
    win.applyHit = static_cast<int>(win.hits.size()) - 1;
    win.hits.push_back(std::move(discard));
    win.discardHit = static_cast<int>(win.hits.size()) - 1;
}
void BuildSidebarTop() {
    void* w = win.windowRt.Get();
    Picture(w, "Sidebar", 1, 1, kSideW, kH - 2, "sidebar.jpg", false, kWhite);
    Node side = New("Sidebar viewport", w, 1, 1, kSideInner, kH - 2);
    Clip(side);
    Node sc = New("Sidebar content", side.rt, 0, 0, kSideInner, 10);
    win.sideContent.Set(sc.rt);
    // .brand: padding 22px 10px 25px; the crest, EpochPact (Georgia 33), the small line
    Picture(sc.rt, "Crest", (kSideInner - 135) / 2, 22, 135, 120, "crest.png", false, kWhite);
    Style brand{Face::Serif, 33, Hex("#dfc79c")};
    brand.spacing = 0.5f;
    brand.align = kTop;
    TextBox(sc.rt, "Brand", 10, 142, 202, LineHeight(brand), brand, "EpochPact");
    Style small{Face::Body, 9, C("ink")};
    small.spacing = 2;
    small.align = kTop;
    const float smallY = 142 + LineHeight(brand) + 7;
    TextBox(sc.rt, "Brand detail", 10, smallY, 202, LineHeight(small), small, Tr("LAST EPOCH · OFFLINE TOOLS"));
    win.sideHeight = smallY + LineHeight(small) + 25;
    Solid(sc.rt, "Brand border", 0, win.sideHeight, kSideInner, 1, C("line"));
    win.sideHeight += 1;
}
void BuildNav() {
    void* sc = win.sideContent.Get();
    // #nav: padding 15px 8px, gap 5; .nav-item: padding 12px 11px, 1px border, symbol 25 wide, gap
    // 15. Heights follow each symbol's fallback font, as measured in the desktop app.
    static const std::map<std::string, float> heights{{"\xE2\x97\x88", 54.7f}, {"\xE2\x8C\x81", 54.7f}, {"\xE2\x96\xA3", 54.7f}, {"\xE2\x97\x89", 54.7f},
                                                      {"\xE2\x8C\x96", 54.7f}, {"\xE2\x96\xA4", 54.7f}, {"\xE2\x8C\x98", 52.7f}, {"\xE2\x97\x87", 52.7f}};
    float y = win.sideHeight + 15;
    for (size_t i = 0; i < win.pages.size(); ++i) {
        const Page& p = win.pages[i];
        const auto hh = heights.find(p.symbol);
        const float h = hh != heights.end() ? hh->second : 56.f;
        Hit hit;
        hit.kind = HitKind::Nav;
        hit.a = static_cast<int>(i);
        hit.area = Area::Sidebar;
        hit.name = p.id;
        hit.x = 8;
        hit.y = y;
        hit.w = 206;
        hit.h = h;
        // the current page: gradient, gold border, 3px accent bar; hover: soft (over the gradient)
        win.navFill.emplace_back(Picture(sc, "Nav selected", 8, y, 206, h, "grad-nav.png", false, kWhite));
        void* hover = Fill(sc, "Nav hover", 8, y, 206, h, kWhite, 2);
        AddHover(hit, hover, Alpha(C("soft"), 0), C("soft"), 0);
        win.navFrame.emplace_back(Outline(sc, "Nav border", 8, y, 206, h, C("gold"), 2));
        win.navBar.emplace_back(Solid(sc, "Nav bar", 9, y + 1, 3, h - 2, C("accent")));
        Style sym{p.symbolFace == "body" ? Face::Body : Face::Symbol, 23, C("gold")};
        sym.align = kCenter;
        TextBox(sc, "Nav symbol", 8 + 12, y, 25, h, sym, p.symbol);
        Style label{Face::Body, 14, C("ink")};
        label.align = kLeft;
        TextBox(sc, "Nav label", 8 + 12 + 25 + 15, y, 150, h, label, Tr(p.nav));
        win.hits.push_back(std::move(hit));
        y += h + 5;
    }
    // .nav-customize: block, padding 12px 22px, 11.2px muted
    Style custom{Face::Body, 11.2f, C("muted")};
    TextBox(sc, "Customize", 8 + 22, y + 12, 170, LineHeight(custom), custom, Tr("Customize navigation"));
    win.sideHeight = y + LineHeight(custom) + 24 + 15;
}
void BuildCaption() {
    void* sc = win.sideContent.Get();
    // .sidebar-caption: margin 0 20px 20px, padding-top 25, centred, line-height 2, letter-spacing 2
    Style cap{Face::Body, 10, Hex("#b9ad96")};
    cap.line = 2;
    cap.spacing = 2;
    cap.align = kTop;
    float y = win.sideHeight + 25;
    TextBox(sc, "Caption", 20, y, 182, LineHeight(cap), cap, Tr("Rewrite the past."));
    TextBox(sc, "Caption", 20, y + LineHeight(cap), 182, LineHeight(cap), cap, Tr("Shape your journey."));
    y += 2 * LineHeight(cap);
    // each span: margin-top 20, a line border on top, padding-top 14, letter-spacing 1
    const auto span = [&](const char* name, const Style& s, const std::string& text, float lineH) {
        y += 20;
        Solid(sc, "Caption border", 20, y, 182, 1, C("line"));
        y += 1 + 14;
        if (!text.empty()) TextBox(sc, name, 20, y, 182, lineH, s, text);
        y += lineH;
    };
    Style credit = cap;
    credit.spacing = 1;
    span("Credit", credit, Tr("Created by Falor"), 20);
    Style version = credit;
    version.size = 12;
    version.color = Alpha(Hex("#b9ad96"), 0.7f);
    span("Version", version, "v" + D()["appVersion"].Str(""), 24);
    span("Update notice", credit, "", 0);  // empty in the app too, with its border
    // .support-link: margin-top .45rem, .78rem gold, letter-spacing .04em
    Style support{Face::Body, 10.92f, C("gold")};
    support.line = 2;
    support.spacing = 0.4368f;
    support.align = kTop;
    y += 6.3f;
    TextBox(sc, "Support", 20, y, 182, LineHeight(support), support, Tr("Support · Discord"));
    Hit link;
    link.kind = HitKind::Support;
    link.area = Area::Sidebar;
    link.name = "support";
    link.x = 20;
    link.y = y;
    link.w = 182;
    link.h = LineHeight(support);
    win.hits.push_back(std::move(link));
    y += LineHeight(support) + 20;
    win.sideHeight = y;
    Resize(sc, kSideInner, y);
}
void BuildFrame() {
    void* w = win.windowRt.Get();
    // the sidebar's scrollbar (the dark theme's own), the gold border right of it
    Solid(w, "Sidebar scrollbar", 1 + kSideInner, 1, kBar, kH - 2, Hex("#171a19"));
    win.sideThumb.Set(TransformOf(Fill(w, "Sidebar thumb", 1 + kSideInner + 4, 1 + kBar, kBar - 8, 40, Hex("#4d524f"), 4)));
    Style arrow{Face::Body, 7, Hex("#8a8f8c")};
    arrow.align = kCenter;
    TextBox(w, "Sidebar up", 1 + kSideInner, 1, kBar, kBar, arrow, "\xE2\x96\xB2");
    TextBox(w, "Sidebar down", 1 + kSideInner, kH - 1 - kBar, kBar, kBar, arrow, "\xE2\x96\xBC");
    Solid(w, "Sidebar border", kSideW, 1, 1, kH - 2, C("gold"));
    Outline(w, "Shell border", 0, 0, kW, kH, C("gold"));
    // overlays, top to bottom: the open list, the notification, the dialog
    for (Pin* layer : {&win.popup, &win.toast, &win.dialog}) {
        Node n = New("Overlay", w, 0, 0, kW, kH);
        layer->Set(n.rt);
        SetActive(n.go, false);
    }
}

// ------------------------------------------------------------------------------ show a page
void PlaceHero() {
    const Page& p = win.pages[win.page];
    // .hero: padding 35px 32px (a page with tabs: 156px tall, padding 20px 28px, h1 36px with
    // margins 8/10), its text block centred in the box less the 1px bottom border. The eyebrow
    // sits on the baseline of the 14px strut's line.
    const bool compact = p.compact;
    const float padY = compact ? 20.f : 35.f, padX = compact ? 28.f : 32.f, m1 = compact ? 8.f : 12.f, m2 = compact ? 10.f : 14.f;
    const Style h1 = HeroTitleStyle(compact ? 36.f : 43.2f), sub = HeroSubStyle();
    if (compact != win.heroCompact) {
        win.heroCompact = compact;
        SetActive(GameObjectOf(win.heroArt.Get()), !compact);
        SetActive(GameObjectOf(win.heroArtCompact.Get()), compact);
        Move(win.heroBorder.Get(), 0, p.heroH - 1);
        // the title's size and its line spacing for that size (TextBox's)
        const auto m = assets::FontMetrics(Face::Serif);
        float size = h1.size, spacing = h1.spacing / h1.size * 100.f, lineSpacing = (LineHeight(h1) / h1.size - m.line) * 100.f;
        void* za[]{&size};
        void* ca[]{&spacing};
        void* la[]{&lineSpacing};
        Invoke(T("set_fontSize", 1), win.heroTitle.Get(), za);
        Invoke(T("set_characterSpacing", 1), win.heroTitle.Get(), ca);
        Invoke(T("set_lineSpacing", 1), win.heroTitle.Get(), la);
        // .hero>div max-width 70% of the content box
        const float textW = (kContentW - 2 * padX) * 0.7f;
        for (void* t : {win.heroEyebrow.Get(), win.heroTitle.Get(), win.heroSub.Get()}) Resize(TransformOf(t), textW, 100);
    }
    SetText(win.heroTitle.Get(), Tr(p.title));
    SetText(win.heroSub.Get(), Tr(p.subtitle));
    const Style strutStyle{Face::Body, 14};
    const float strut = LineHeight(strutStyle), h1Line = LineHeight(h1), subLine = LineHeight(sub);
    const int titleLines = TextLines(win.heroTitle.Get()), subLines = TextLines(win.heroSub.Get());
    const float block = strut + m1 + titleLines * h1Line + m2 + subLines * subLine;
    const float top = padY + (p.heroH - 2 * padY - 1 - block) / 2;
    // the strut's baseline: its rounded ascent below the line's top; TMP puts a text's baseline its
    // face's ascent below the text's top
    const auto body = assets::FontMetrics(Face::Body);
    const float baseline = top + std::round(body.ascent * 14);
    Move(TransformOf(win.heroEyebrow.Get()), padX, baseline - body.ascent * 10);
    Move(TransformOf(win.heroTitle.Get()), padX, top + strut + m1 + TopOffset(h1));
    Move(TransformOf(win.heroSub.Get()), padX, top + strut + m1 + titleLines * h1Line + m2 + TopOffset(sub));
}
void UpdateFooter() {
    if (!win.built) return;
    const std::string& id = win.pages[win.page].id;
    const bool autoPage = id == "general" || id == "overview";
    std::string label;
    if (!errors.empty()) label = Tr("A setting needs attention");
    else if (!pending.empty() || !sendingId.empty()) label = Tr("Applying changes…");
    else if (autoPage) label = Tr("Changes apply automatically");
    else if (!drafts.empty()) label = Fmt("{0} settings in draft", {std::to_string(drafts.size())});
    else label = Tr("Ready");
    SetText(win.draftLabel.Get(), label);
    const bool canApply = !drafts.empty() && offlinePlay;
    float alpha = canApply ? 1.f : 0.45f;
    void* a[]{&alpha};
    Invoke(Find("UnityEngine.UIModule.dll", "UnityEngine", "CanvasGroup", "set_alpha", 1), win.applyGroup.Get(), a);
    InkRaw(win.applyLabel.Get(), Alpha(Hex("#fff0d3"), canApply ? 1.f : kDisabledTextAlpha));
    win.hits[win.applyHit].enabled = canApply;
    // hidden on the pages whose settings all apply automatically (and on Game setup), and on a tab
    // other than the one with the drafts (Crafting: Materials, Custom Items)
    const Page& shown = win.pages[win.page];
    const bool buttons = !autoPage && id != "game_setup" && (shown.draftTab.empty() || shown.tab == shown.draftTab);
    if (buttons != win.hits[win.discardHit].visible) {
        SetActive(win.applyGo.Get(), buttons);
        SetActive(win.discardGo.Get(), buttons);
        win.hits[win.applyHit].visible = win.hits[win.discardHit].visible = buttons;
    }
}
float MaxMain() { return std::max(0.f, win.pages[win.page].height - kMainH); }
float MaxSide() { return std::max(0.f, win.sideHeight - (kH - 2)); }
void ReleaseKeyboard(int section = -2);  // every field, or one section's
// A <details> setting panel opens or closes in place: its content on or off, the page moves.
void ToggleDetails(int sectionId) {
    Section& s = Sec(sectionId);
    if (!s.panelState || !s.panelState->details) return;
    PanelState& ps = *s.panelState;
    ps.open = !ps.open;
    DetailsOpen()[ps.detailsKey] = ps.open;
    ReleaseKeyboard(sectionId);
    SetActive(GameObjectOf(ps.content.Get()), ps.open);
    SetText(ps.marker.Get(), ps.open ? "\xE2\x96\xBC" : "\xE2\x96\xB6");
    for (size_t i = ps.detailsHits; i < s.hits.size(); ++i) s.hits[i].visible = ps.open;
    FitPanel(s);
}
void ShowPage(int index) {
    if (index < 0 || index >= static_cast<int>(win.pages.size())) return;
    const double start = Ms();
    if (index != win.page && win.built) ReleaseKeyboard();
    const int previous = win.page;
    Page& p = win.pages[index];
    if (!p.built) {
        QueuePage(index, true);
        while (!p.built && !jobs.empty()) { auto j = std::move(jobs.front()); jobs.pop_front(); j(); }
    }
    CloseList();
    if (previous != index && win.pages[previous].canvas) Enable(win.pages[previous].canvas.Get(), false);
    win.page = index;
    Enable(p.canvas.Get(), true);
    win.mainScroll = win.mainTarget = 0;
    Move(win.mainContent.Get(), 0, 0);
    for (size_t i = 0; i < win.pages.size(); ++i) {
        const bool on = static_cast<int>(i) == index;
        SetActive(GameObjectOf(win.navFill[i].Get()), on);
        SetActive(GameObjectOf(win.navFrame[i].Get()), on);
        SetActive(GameObjectOf(win.navBar[i].Get()), on);
    }
    SetText(win.crumb.Get(), "EpochPact / " + Tr(p.nav));
    PlaceHero();
    UpdateFooter();
    RequestValues(true);
    win.thumbsAt[0] = -1;
    timing.pageMs = Ms() - start;
}

// ------------------------------------------------------------------------------ the values
std::string IdText(const JValue* v) { return !v ? "" : v->IsString() ? v->text : v->IsNumber() ? Num(v->number) : ""; }
void Notify(const std::string& read) {
    for (auto& s : win.sections) {
        if (!s->built || (!s->reads.count(read) && !s->polls.count(read))) continue;
        if (s->onData) s->onData(*s, read);
        else if (s->build) Rebuild(s->id);
    }
}
void ReadInto(const std::string& read, const std::string& reply) {
    if (read == "status") {
        statusKnown = true;
        offlinePlay = reply.find("gate: offline play") != std::string::npos;
        // ui_bridge: "^name: x<number>", "^weaversdrop: <n>%", "^autopickup: on|off"
        std::map<std::string, double> found;
        std::istringstream lines(reply);
        std::string line;
        while (std::getline(lines, line)) {
            const size_t colon = line.find(": ");
            if (colon == std::string::npos) continue;
            const std::string name = line.substr(0, colon), rest = line.substr(colon + 2);
            char* end = nullptr;
            if (rest.size() > 1 && rest[0] == 'x') {
                const double v = std::strtod(rest.c_str() + 1, &end);
                if (end != rest.c_str() + 1 && std::isfinite(v)) found.emplace(name, v);
            } else if (name == "weaversdrop") {
                const double v = std::strtod(rest.c_str(), &end);
                if (end != rest.c_str() && *end == '%') found.emplace(name, v);
            } else if (name == "autopickup" && (rest.rfind("on", 0) == 0 || rest.rfind("off", 0) == 0)) {
                found.emplace(name, rest[1] == 'n' ? 1 : 0);
            }
        }
        for (auto& [id, c] : controls) {
            if (c.read != "status" || c.id == "speed" || !found.count(c.path)) continue;
            if (pending.count(id) || drafts.count(id) || sendingId == id) continue;
            values[id] = appliedValues[id] = found[c.path];
        }
    } else if (read == "sessionread") {
        JValue v;
        if (json::Parse(reply, v)) {
            sessionState = v["state"].Str(sessionState);
            transitioning = v["transitioning"].IsBool() && v["transitioning"].boolean;
        }
    } else if (read == "statraw 9 0 0 0") {
        // ui_bridge: the movement speed bonus EpochPact attached; "increased" is x-1
        double speed = 1;
        const size_t at = reply.find("EpochPact added=");
        if (at != std::string::npos) {
            const size_t inc = reply.find(" increased=", at), att = reply.find(" attached=1", at);
            if (inc != std::string::npos && att != std::string::npos && inc < att) speed = 1 + std::strtod(reply.c_str() + inc + 11, nullptr);
        }
        if (!pending.count("speed") && !drafts.count("speed") && sendingId != "speed") values["speed"] = appliedValues["speed"] = speed;
    } else {
        // a JSON read: the setting values it carries, and kept for the sections
        auto v = std::make_shared<JValue>();
        const bool parsed = json::Parse(reply, *v);
        if (parsed && !(v->Find("ok") && (*v)["ok"].IsBool() && !(*v)["ok"].boolean)) {
            for (auto& [id, c] : controls) {
                if (c.read != read || pending.count(id) || drafts.count(id) || sendingId == id) continue;
                const JValue* x = v->Path(c.path);
                if (x && (x->IsNumber() || x->IsBool())) values[id] = appliedValues[id] = x->Num();
                // a chance left to the game (hopePercent null): NaN, sent as "reset"
                else if (x && x->IsNull() && c.widget == "selector") values[id] = appliedValues[id] = std::nan("");
            }
        }
        Kept& kept = cache[read];
        const bool changed = kept.text != reply;
        kept.text = reply;
        kept.json = parsed ? v : nullptr;
        if (changed) Notify(read);
    }
    if (read == "status" || read == "sessionread") {
        const std::string text = !statusKnown || sessionState.empty() ? Tr("Checking connection")
                                 : Tr((offlinePlay ? "\xE2\x97\x8F Offline \xC2\xB7 " : "\xE2\x97\x8F ") + sessionState);
        SetText(win.connection.Get(), text);
        Ink(win.connection.Get(), statusKnown && !sessionState.empty() ? C("accent") : C("muted"));
        UpdateFooter();
    }
    for (auto& k : win.cards)
        if (k.c->read == read || (read == "statraw 9 0 0 0" && k.c->id == "speed")) Paint(k, true);
}
uint64_t nextRead = 0;
// all: when a page opens or Refresh is pressed: also the sections' one-time reads.
void RequestValues(bool all) {
    if (!win.built) return;
    std::set<std::string> reads{"status", "sessionread"};
    static std::map<std::string, uint64_t> lastPolled;
    const uint64_t now = Now();
    for (int id : win.pages[win.page].sections) {
        const Section& s = Sec(id);
        for (const auto& r : s.polls) {
            // a poll with its own interval (the loot log every 2 s) waits for its turn
            const auto every = s.every.find(r);
            if (!all && every != s.every.end() && now - lastPolled[r] < every->second) continue;
            reads.insert(r);
        }
        if (all) for (const auto& r : s.reads) if (!cache.count(r) || win.shown) reads.insert(r);
    }
    for (const auto& r : reads) lastPolled[r] = now;
    for (const auto& r : reads) {
        bool queued = false;
        for (const auto& j : commandQueue) queued |= j.line == r;
        if (!queued) Send(r, [r](const std::string& reply) { ReadInto(r, reply); });
    }
    nextRead = Now() + 1000;  // the desktop app polls every second
}

// ----------------------------------------------------------------------------- the changes
void Change(const Control& c, double v, unsigned delay = 250) {
    if (c.widget == "number") {
        v = std::clamp(v, c.min, c.max);
        if (c.integer) v = std::round(v);
    }
    values[c.id] = v;
    errors.erase(c.id);
    applied.erase(c.id);
    bool automatic = true;
    for (const auto& k : win.cards) if (k.c == &c) automatic = k.automatic;
    if (automatic) pending[c.id] = {v, Now() + delay};
    else drafts.insert(c.id);
    PaintId(c.id);
}
std::string CommandFor(const Control& c, double v) {
    std::string line = c.command;
    // app.js applyDraft: value ?? "reset"
    for (size_t at = line.find("{value}"); at != std::string::npos; at = line.find("{value}")) line.replace(at, 7, std::isnan(v) ? "reset" : Num(v));
    return line;
}
// One setting to the game. A draft (Apply changes) that is refused stays a draft with its value,
// as app.js keeps it; an automatic one shows the error and goes back to the applied value. after
// gets whether the game took it; the setting's read follows (or, in Apply changes, after the last).
std::set<std::string>* applyReads = nullptr;
void SendSetting(const std::string& id, double v, std::function<void(bool)> after = {}, bool draft = false) {
    const Control& c = controls.at(id);
    sendingId = id;
    Send(CommandFor(c, v), [id, v, after, draft](const std::string& reply) {
        if (sendingId == id) sendingId.clear();
        std::string why;
        const bool ok = !Refused(reply, &why);
        if (!ok && draft) drafts.insert(id);
        else if (!ok) {
            errors[id] = why;
            if (appliedValues.count(id)) values[id] = appliedValues[id];
        } else {
            appliedValues[id] = v;
            applied.insert(id);
        }
        PaintId(id);
        const Control& sent = controls.at(id);
        if (!sent.read.empty() && sent.read != "status") {
            if (applyReads) applyReads->insert(sent.read);
            else Read(sent.read);
        }
        if (id == "speed") Send("statraw 9 0 0 0", [](const std::string& text) { ReadInto("statraw 9 0 0 0", text); });
        if (after) after(ok);
    });
    PaintId(id);
}
void FlushPending() {
    if (!sendingId.empty()) return;
    const uint64_t now = Now();
    for (auto it = pending.begin(); it != pending.end(); ++it) {
        if (it->second.at > now) continue;
        const std::string id = it->first;
        const double v = it->second.value;
        pending.erase(it);
        SendSetting(id, v);
        return;  // one at a time, as app.js does
    }
}
// Apply changes (app.js applyDraft): the drafts one after another; a refused one stops the rest,
// which stay drafts. The settings' reads once at the end ("craft_read is read once after this batch").
int applyCount = -1;  // settings applied in the running Apply changes; -1 when none runs
std::set<std::string> batchReads;
void ApplyNext() {
    if (drafts.empty()) {
        applyReads = nullptr;
        for (const auto& r : batchReads) Read(r);
        Toast(Fmt("{0} settings applied in order.", {std::to_string(applyCount)}));
        applyCount = -1;
        UpdateFooter();
        return;
    }
    const std::string id = *drafts.begin();
    drafts.erase(drafts.begin());
    SendSetting(id, values[id], [](bool ok) {
        if (!ok) {
            applyReads = nullptr;
            for (const auto& r : batchReads) Read(r);
            Toast(Fmt("{0} settings applied; stopped after an error. Remaining drafts are kept.", {std::to_string(applyCount)}), true);
            applyCount = -1;
            UpdateFooter();
            return;
        }
        ++applyCount;
        ApplyNext();
    }, true);
}
void ApplyDrafts() {
    if (drafts.empty() || !sendingId.empty() || applyCount >= 0) return;
    applyCount = 0;
    batchReads.clear();
    applyReads = &batchReads;
    ApplyNext();
}
void DiscardDrafts() {
    const auto ids = drafts;
    drafts.clear();
    for (const auto& id : ids) {
        if (appliedValues.count(id)) values[id] = appliedValues[id];
        else values.erase(id);
        PaintId(id);
    }
    UpdateFooter();
}

// -------------------------------------------------------------------------------- the input
bool GameInFront(HWND* window = nullptr) {
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    if (!w) return false;
    GetWindowThreadProcessId(w, &pid);
    if (window) *window = w;
    return pid == GetCurrentProcessId();
}
int screenW = 0, screenH = 0;
// The cursor in window css px; false outside the game's window.
bool Cursor(float* x, float* y) {
    HWND window = nullptr;
    if (!GameInFront(&window)) return false;
    POINT p{};
    RECT client{};
    if (!GetCursorPos(&p) || !ScreenToClient(window, &p) || !GetClientRect(window, &client)) return false;
    const int cw = client.right - client.left, ch = client.bottom - client.top;
    if (cw <= 0 || ch <= 0 || p.x < 0 || p.y < 0 || p.x >= cw || p.y >= ch) return false;
    const float ux = p.x * static_cast<float>(screenW) / cw, uy = p.y * static_cast<float>(screenH) / ch;
    *x = (ux - (screenW - kW * win.scale) / 2) / win.scale;
    *y = (uy - (screenH - kH * win.scale) / 2) / win.scale;
    return true;
}
// A hit list: -1 the window's own, -2 the open list, -3 the dialog, 0.. a section.
constexpr int kFixed = -1, kPopup = -2, kDialog = -3;
struct HitRef {
    int list = kFixed, index = -1;
    bool operator==(const HitRef& o) const { return list == o.list && index == o.index; }
};
std::vector<Hit>* ListOf(int list) {
    if (list == kFixed) return &win.hits;
    if (list == kPopup) return &win.popupHits;
    if (list == kDialog) return &win.dialogHits;
    return list >= 0 && list < static_cast<int>(win.sections.size()) ? &win.sections[list]->hits : nullptr;
}
Hit* HitAt(HitRef r) {
    auto* list = ListOf(r.list);
    return list && r.index >= 0 && r.index < static_cast<int>(list->size()) ? &(*list)[r.index] : nullptr;
}
// A hit's place in window coords, after scrolling; false when it is not on the shown page.
bool Placed(int list, const Hit& h, float* x, float* y) {
    *x = h.x;
    *y = h.y;
    if (list >= 0) {
        const Section& s = Sec(list);
        if (s.page != win.page || s.hidden) return false;
        *x += kMainX;
        *y += kMainY - win.mainScroll + s.y;
    } else if (h.area == Area::Sidebar) {
        *x += 1;
        *y += 1 - win.sideScroll;
    }
    return true;
}
bool Inside(int list, const Hit& h, float px, float py) {
    float x, y;
    if (!h.visible || !Placed(list, h, &x, &y) || px < x || px >= x + h.w || py < y || py >= y + h.h) return false;
    if (list >= 0) return px >= kMainX && px < kMainX + kContentW && py >= kMainY && py < kMainY + kMainH;
    if (h.area == Area::Sidebar) return px >= 1 && px < 1 + kSideInner && py >= 1 && py < kH - 1;
    return true;
}

HitRef hovered{kFixed, -1}, pressed{kFixed, -1}, dragHit{kFixed, -1};
int dragCard = -1;
bool mouseDown = false;
enum class Drag { None, Main, Side } scrollDrag = Drag::None;
float dragStartY = 0, dragStartScroll = 0;

void SetHover(HitRef r) {
    if (r == hovered) return;
    if (Hit* old = HitAt(hovered)) for (auto& part : old->hover) Fade(part.graphic, part.normal, part.seconds);
    hovered = r;
    if (Hit* now = HitAt(hovered)) for (auto& part : now->hover) Fade(part.graphic, part.hover, part.seconds);
}
void ClearHover() { SetHover({kFixed, -1}); }
void DragSlider(int cardIndex, float px) {
    const Card& k = win.cards[cardIndex];
    const Control& c = *k.c;
    const float left = kMainX + k.x + k.trackX + 1 + 9.5f, width = k.trackW - 2 - 19;
    const double t = std::clamp((px - left) / width, 0.f, 1.f), step = c.integer ? 1 : 0.1;
    double v = c.min + std::round(t * (c.max - c.min) / step) * step;
    v = std::round(std::clamp(v, c.min, c.max) * 1e6) / 1e6;
    if (values.count(c.id) && values[c.id] == v) return;
    Change(c, v);
}
// A native section's slider: its drag gets the pointer's x from the hit's left edge.
void DragHit(float px) {
    Hit* h = HitAt(dragHit);
    float x = 0, y = 0;
    if (!h || !h->drag || !Placed(dragHit.list, *h, &x, &y)) return;
    const auto drag = h->drag;  // a copy: the drag may rebuild its section
    drag(px - x);
}
void ClearLayer(void* rt) {
    const int n = Value<int>(U("Transform", "get_childCount", 0), rt);
    for (int i = n - 1; i >= 0; --i) {
        void* a[]{&i};
        void* child = GameObjectOf(Invoke(U("Transform", "GetChild", 1), rt, a));
        SetActive(child, false);
        void* d[]{child};
        Invoke(U("Object", "Destroy", 1), nullptr, d);
    }
}
// An overlay is switched on before its content is made: TMP measures only a text on an active
// object, and nothing is drawn before the frame ends.
void* OpenLayer(const Pin& layer) {
    void* rt = layer.Get();
    ClearLayer(rt);
    SetActive(GameObjectOf(rt), true);
    return rt;
}
void OpenCardChoice(const Hit& select) {
    const Card& k = win.cards[select.a];
    const Control& c = *k.c;
    float x, y;
    Placed(k.section, select, &x, &y);
    std::vector<std::string> labels;
    int current = -1;
    for (size_t i = 0; i < c.choices.size(); ++i) {
        labels.push_back(ChoiceLabel(c, i));
        if (values.count(c.id) && values[c.id] == c.choices[i]) current = static_cast<int>(i);
    }
    const Control* control = &c;
    OpenList(x, y, select.w, select.h, labels, current, [control](int i) { Change(*control, control->choices[i], 0); });
}
void Click(int list, const Hit& h) {
    if (!h.enabled) return;
    switch (h.kind) {
    case HitKind::Nav:
    case HitKind::Link: ShowPage(h.a); break;
    case HitKind::Refresh: if (h.action) h.action(); else RequestValues(true); break;
    case HitKind::Apply: ApplyDrafts(); break;
    case HitKind::Discard: DiscardDrafts(); break;
    case HitKind::Support: ShellExecuteW(nullptr, L"open", L"https://discord.gg/q6aexZZSAf", nullptr, nullptr, SW_SHOWNORMAL); break;
    case HitKind::Minus:
    case HitKind::Plus: {
        // data-step: ±1 (integer) or ±0.1, toFixed(2), within the limits
        const Control& c = *win.cards[h.a].c;
        const double step = (h.kind == HitKind::Plus ? 1 : -1) * (c.integer ? 1 : 0.1);
        Change(c, std::round(std::clamp(Shown(c) + step, c.min, c.max) * 100) / 100);
        break;
    }
    case HitKind::Default: {
        const Control& c = *win.cards[h.a].c;
        if (c.hasDefault) Change(c, c.def);
        break;
    }
    case HitKind::Toggle: {
        const Control& c = *win.cards[h.a].c;
        Change(c, Shown(c) != 0 ? 0 : 1);
        break;
    }
    case HitKind::Choice: OpenCardChoice(h); break;
    case HitKind::Action:
    case HitKind::Input: if (h.action) h.action(); break;
    case HitKind::Track: break;
    }
    (void)list;
}
float WheelDelta() {
    static bool unavailable = false;
    static game::MethodRef method;
    if (unavailable) return 0;
    try {
        if (!method) method = game::FindMethod("UnityEngine.InputLegacyModule.dll", "UnityEngine", "Input", "get_mouseScrollDelta", 0);
        if (!method) { unavailable = true; return 0; }
        return reinterpret_cast<V2 (*)(const il2cpp::Method*)>(method.code)(method.info).y;
    } catch (...) {
        unavailable = true;  // legacy input switched off in this build of the game
        return 0;
    }
}
void Animate() {
    const double now = Ms();
    for (auto& k : win.cards) {
        if (k.knobStart < 0) continue;
        const double t = std::min(1.0, (now - k.knobStart) / 150.0), e = t * (2 - t);  // transform .15s ease
        Move(k.knob.Get(), static_cast<float>(k.knobFrom + (k.knobTo - k.knobFrom) * e), 4);
        if (t >= 1) k.knobStart = -1;
    }
}
// The hit under the pointer: the dialog when one is open; else the open list, the window's own,
// then the shown page's sections.
HitRef Under(float px, float py) {
    const auto search = [&](int list) -> HitRef {
        auto* hits = ListOf(list);
        if (!hits) return {kFixed, -1};
        for (size_t i = 0; i < hits->size(); ++i)
            if (Inside(list, (*hits)[i], px, py)) return {list, static_cast<int>(i)};
        return {kFixed, -1};
    };
    if (win.dialogOpen) return search(kDialog);
    for (int list : {kPopup, kFixed}) {
        const HitRef r = search(list);
        if (r.index >= 0) return r;
    }
    for (int id : win.pages[win.page].sections) {
        const HitRef r = search(id);
        if (r.index >= 0) return r;
    }
    return {kFixed, -1};
}
void Input(float dt) {
    float px = -1, py = -1;
    const bool inside = Cursor(&px, &py);
    if (!mouseDown) SetHover(inside ? Under(px, py) : HitRef{kFixed, -1});
    const int vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    const bool down = inside && (GetAsyncKeyState(vk) & 0x8000) != 0;
    if (down && !mouseDown) {
        pressed = hovered;
        const Hit* p = HitAt(pressed);
        if (win.listOpen && pressed.list != kPopup) { CloseList(); pressed = {kFixed, -1}; }
        else if (p && p->kind == HitKind::Track && p->drag) { dragHit = pressed; DragHit(px); }
        else if (p && p->kind == HitKind::Track) { dragCard = p->a; DragSlider(dragCard, px); }
        else if (!win.dialogOpen && px >= kMainX + kContentW && px < kMainX + kMainW && py >= kMainY && py < kMainY + kMainH && MaxMain() > 0) {
            scrollDrag = Drag::Main;
            dragStartY = py;
            dragStartScroll = win.mainTarget;
        } else if (!win.dialogOpen && px >= 1 + kSideInner && px < kSideW && py >= 1 && py < kH - 1 && MaxSide() > 0) {
            scrollDrag = Drag::Side;
            dragStartY = py;
            dragStartScroll = win.sideTarget;
        }
    } else if (down && mouseDown) {
        if (dragCard >= 0) DragSlider(dragCard, px);
        if (dragHit.index >= 0) DragHit(px);
        if (scrollDrag == Drag::Main)
            win.mainTarget = win.mainScroll = std::clamp(dragStartScroll + (py - dragStartY) * win.pages[win.page].height / (kMainH - 2 * kBar), 0.f, MaxMain());
        else if (scrollDrag == Drag::Side)
            win.sideTarget = win.sideScroll = std::clamp(dragStartScroll + (py - dragStartY) * win.sideHeight / (kH - 2 - 2 * kBar), 0.f, MaxSide());
    } else if (!down && mouseDown) {
        const Hit* p = HitAt(pressed);
        if (dragHit.index >= 0) {
            const Hit* d = HitAt(dragHit);
            const Action release = d ? d->release : Action{};  // a copy: it may rebuild the section
            dragHit = {kFixed, -1};
            pressed = {kFixed, -1};
            p = nullptr;
            if (release) release();
        }
        if (dragCard < 0 && scrollDrag == Drag::None && p && Inside(pressed.list, *p, px, py)) {
            const Hit h = *p;  // a copy: a click may rebuild sections and their hits
            const int list = pressed.list;
            pressed = {kFixed, -1};
            Click(list, h);
        }
        dragCard = -1;
        dragHit = {kFixed, -1};
        pressed = {kFixed, -1};
        scrollDrag = Drag::None;
    }
    mouseDown = down;
    // the wheel: 100 px a notch, eased like the browser's smooth scrolling
    const float wheel = inside && !win.dialogOpen ? WheelDelta() : 0;
    if (wheel != 0 && win.listOpen && win.popupWheel) win.popupWheel(wheel);
    if (wheel != 0 && !win.listOpen) {
        // a list box under the pointer scrolls itself, not the page
        bool used = false;
        if (px >= kMainX && px < kMainX + kContentW && py >= kMainY && py < kMainY + kMainH)
            for (int id : win.pages[win.page].sections) {
                if (Sec(id).hidden) continue;
                for (auto& w : Sec(id).wheels) {
                    const float wx = kMainX + w.x, wy = kMainY - win.mainScroll + Sec(id).y + w.y;
                    if (px >= wx && px < wx + w.w && py >= wy && py < wy + w.h) { if (w.scroll) w.scroll(wheel); used = true; break; }
                }
                if (used) break;
            }
        if (used) {}
        else if (px >= 0 && px < kSideW) win.sideTarget = std::clamp(win.sideTarget - wheel * 100, 0.f, MaxSide());
        else if (px >= kMainX && py >= kMainY && py < kMainY + kMainH) win.mainTarget = std::clamp(win.mainTarget - wheel * 100, 0.f, MaxMain());
    }
    const float k = std::min(1.f, dt * 14);
    const auto approach = [&](float& value, float target) {
        if (std::fabs(target - value) < 0.5f) value = target;
        else value += (target - value) * k;
    };
    const float mainBefore = win.mainScroll, sideBefore = win.sideScroll;
    approach(win.mainScroll, win.mainTarget);
    approach(win.sideScroll, win.sideTarget);
    if (win.mainScroll != mainBefore) { Move(win.mainContent.Get(), 0, -win.mainScroll); CloseList(); }
    if (win.sideScroll != sideBefore) Move(win.sideContent.Get(), 0, -win.sideScroll);
}
// Text fields the player is typing in: their text, each time it changes.
// EpochInputManager.instance.isInputFieldActive: the game's own switch for "typing in a text
// field" (its BlockPlayerInputField sets it for the game's fields). On while a field of the window
// has the keyboard, so typed keys do not drink a potion or cast a skill.
bool gameKeysBlocked = false;
void BlockGameKeys(bool on) {
    if (on == gameKeysBlocked) return;
    static const il2cpp::Field* instance = game::FindStaticField("LE.dll", "", "EpochInputManager", "instance");
    static const size_t flag = game::FieldOffset("LE.dll", "", "EpochInputManager", "isInputFieldActive");
    void* manager = nullptr;
    if (instance && flag) il2cpp::api().field_static_get_value(instance, &manager);
    if (manager) *reinterpret_cast<bool*>(static_cast<char*>(manager) + flag) = on;
    gameKeysBlocked = on;
}
// A text field of the window gives the keyboard back: while one is selected the game ignores its
// own input (click to move, skills). On closing, on another page, before a section is built again.
void ReleaseKeyboard(int section) {
    static game::MethodRef focusedMethod;
    if (!focusedMethod) focusedMethod = game::FindMethod("Unity.TextMeshPro.dll", "TMPro", "TMP_InputField", "get_isFocused", 0);
    void* system = Invoke(Find("UnityEngine.UI.dll", "UnityEngine.EventSystems", "EventSystem", "get_current", 0));
    void* selected = system ? Invoke(Find("UnityEngine.UI.dll", "UnityEngine.EventSystems", "EventSystem", "get_currentSelectedGameObject", 0), system) : nullptr;
    for (const auto& w : win.inputs) {
        if (section != -2 && w.section != section) continue;
        void* field = w.field.Get();
        if (!field) continue;
        if (focusedMethod && reinterpret_cast<bool (*)(void*, const il2cpp::Method*)>(focusedMethod.code)(field, focusedMethod.info)) {
            if (const auto* deactivate = Find("Unity.TextMeshPro.dll", "TMPro", "TMP_InputField", "DeactivateInputField", 1, false)) {
                bool clear = false;
                void* a[]{&clear};
                Invoke(deactivate, field, a);
            } else Invoke(Find("Unity.TextMeshPro.dll", "TMPro", "TMP_InputField", "DeactivateInputField", 0), field);
        }
        if (selected && selected == GameObjectOf(field)) {
            void* none[]{nullptr};
            Invoke(Sig("UnityEngine.UI.dll", "UnityEngine.EventSystems", "EventSystem", "SetSelectedGameObject", {"UnityEngine.GameObject"}), system, none);
            selected = nullptr;
        }
    }
    if (section == -2) BlockGameKeys(false);
}
bool FieldFocused(void* field) {
    static game::MethodRef focusedMethod;
    if (!focusedMethod) focusedMethod = game::FindMethod("Unity.TextMeshPro.dll", "TMPro", "TMP_InputField", "get_isFocused", 0);
    return field && focusedMethod && reinterpret_cast<bool (*)(void*, const il2cpp::Method*)>(focusedMethod.code)(field, focusedMethod.info);
}
void PollInputs() {
    static game::MethodRef focusedMethod, textMethod;
    if (!focusedMethod) focusedMethod = game::FindMethod("Unity.TextMeshPro.dll", "TMPro", "TMP_InputField", "get_isFocused", 0);
    if (!textMethod) textMethod = game::FindMethod("Unity.TextMeshPro.dll", "TMPro", "TMP_InputField", "get_text", 0);
    if (!focusedMethod || !textMethod) return;
    bool typing = false;
    for (const Watched& w : win.inputs) {
        if (w.section >= 0 && Sec(w.section).page != win.page) continue;
        if (reinterpret_cast<bool (*)(void*, const il2cpp::Method*)>(focusedMethod.code)(w.field.Get(), focusedMethod.info)) { typing = true; break; }
    }
    BlockGameKeys(typing);
    for (size_t i = 0; i < win.inputs.size(); ++i) {
        Watched& w = win.inputs[i];
        if (w.section >= 0 && Sec(w.section).page != win.page) continue;
        if (!reinterpret_cast<bool (*)(void*, const il2cpp::Method*)>(focusedMethod.code)(w.field.Get(), focusedMethod.info)) continue;
        const std::string text = Text(reinterpret_cast<void* (*)(void*, const il2cpp::Method*)>(textMethod.code)(w.field.Get(), textMethod.info));
        if (text == w.last) continue;
        w.last = text;
        auto onChange = w.onChange;  // may rebuild the section (and drop this entry)
        if (onChange) onChange(text);
        break;
    }
}
// The scrollbar thumbs, between the arrow buttons; moved only when the scroll changes.
void PlaceThumbs() {
    if (win.thumbsAt[0] == win.mainScroll && win.thumbsAt[1] == win.sideScroll) return;
    win.thumbsAt[0] = win.mainScroll;
    win.thumbsAt[1] = win.sideScroll;
    const float track = kMainH - 2 * kBar, pageH = std::max(win.pages[win.page].height, kMainH);
    const float h = std::max(20.f, track * kMainH / pageH);
    Move(win.mainThumb.Get(), kMainX + kContentW + 3, kMainY + kBar + (track - h) * (MaxMain() > 0 ? win.mainScroll / MaxMain() : 0));
    Resize(win.mainThumb.Get(), kBar - 6, h);
    const float sideTrack = kH - 2 - 2 * kBar, sh = std::max(20.f, sideTrack * (kH - 2) / std::max(win.sideHeight, kH - 2));
    Move(win.sideThumb.Get(), 1 + kSideInner + 4, 1 + kBar + (sideTrack - sh) * (MaxSide() > 0 ? win.sideScroll / MaxSide() : 0));
    Resize(win.sideThumb.Get(), kBar - 8, sh);
}
void HideToast() {
    if (!win.toastUntil) return;
    win.toastUntil = 0;
    ClearLayer(win.toast.Get());
    SetActive(GameObjectOf(win.toast.Get()), false);
}

// --------------------------------------------------------------------------- open and close
float ScaleFor() { return std::max(0.4f, std::min(screenW / kW, screenH / kH) * 0.94f); }
// The desktop app's stored choices (%LOCALAPPDATA%\EpochPact\preferences.json: {"values": {key: JSON
// text}}), read when the window is built or opened; the window never writes them.
JValue preferences;
// EPOCHPACT_PREFERENCES names another file (tests keep the player's own choices untouched).
std::filesystem::path PreferencesPath() {
    wchar_t buffer[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"EPOCHPACT_PREFERENCES", buffer, MAX_PATH);
    if (n && n < MAX_PATH) return std::filesystem::path(buffer);
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    return std::filesystem::path(buffer) / L"EpochPact" / L"preferences.json";
}
JValue ReadPreferencesFile() {
    JValue out;
    const auto path = PreferencesPath();
    if (path.empty()) return out;
    std::ifstream in(path, std::ios::binary);
    if (!in) return out;
    std::stringstream text;
    text << in.rdbuf();
    if (!json::Parse(text.str(), out)) out = JValue{};
    return out;
}
void LoadPreferences() { preferences = ReadPreferencesFile(); }
// The app's language ("epochpact.language").
std::string ReadLanguage() {
    LoadPreferences();
    const JValue v = Preference("epochpact.language");
    return v.IsString() && (v.text == "tr" || v.text == "ru") ? v.text : "en";
}
void CloseDialog();
// Closed, the window's canvas renders to display 8, which the game never uses: switching a canvas
// off and on makes Unity batch every graphic again (a 15-30 ms frame), a display change does not.
// Its raycaster is off meanwhile, so it never takes a click.
bool Park(bool closed) {
    static const il2cpp::Method* display = Find("UnityEngine.UIModule.dll", "UnityEngine", "Canvas", "set_targetDisplay", 1, false);
    if (!display) return false;
    int index = closed ? 7 : 0;
    void* a[]{&index};
    Invoke(display, win.rootCanvas.Get(), a);
    return true;
}
void Show(bool open) {
    const double start = Ms();
    if (open) {
        Enable(win.listener.Get(), true);
        if (win.parked) {
            Park(false);
            Enable(win.raycaster.Get(), true);
        } else Enable(win.rootCanvas.Get(), true);
        win.shown = true;
        ++opens;
        timing.framesSinceOpen = 0;
        timing.spikeAfterOpen = 0;
        RequestValues(true);
        timing.openMs = Ms() - start;
    } else {
        CloseList();
        ClearHover();
        ReleaseKeyboard();
        if (win.parked) {
            Enable(win.raycaster.Get(), false);
            Park(true);
        } else Enable(win.rootCanvas.Get(), false);
        // A disabled canvas never tells the listener the pointer left: give the world its clicks back.
        bool no = false;
        void* a[]{&no};
        Invoke(Find("LE.dll", "", "UIMouseListener", "set_underMouse", 1), win.listener.Get(), a);
        Enable(win.listener.Get(), false);
        win.shown = false;
        mouseDown = false;
        dragCard = -1;
        dragHit = {kFixed, -1};
        pressed = {kFixed, -1};
        scrollDrag = Drag::None;
        ++closes;
        timing.framesSinceClose = 0;
        timing.spikeAfterClose = 0;
        timing.closeMs = Ms() - start;
    }
}
void Destroy() {
    BlockGameKeys(false);  // never leave the game's keys switched off
    timers.clear();
    if (void* go = win.rootGo.Get()) { void* a[]{go}; Invoke(U("Object", "Destroy", 1), nullptr, a); }
    fonts::SetWindowRoot(nullptr);
    win = Window{};
    ForgetMeasures();
    jobs.clear();
    hovered = pressed = dragHit = {kFixed, -1};
    dragCard = -1;
    mouseDown = false;
}
// Build everything ahead of time: the shell, the page shown first, then every other page.
void QueueAll() {
    win = Window{};
    eligibleBuilt = Eligible();
    nextEligibleCheck = Now() + 1000;
    win.lang = language;
    win.scale = ScaleFor();
    LoadControls();
    for (const auto& spec : D()["pages"].items) {
        Page p;
        p.id = spec["id"].Str();
        p.nav = spec["nav"].Str();
        p.symbol = spec["symbol"].Str();
        p.symbolFace = spec["symbolFace"].Str("symbol");
        p.title = spec["title"].Str();
        p.subtitle = spec["subtitle"].Str();
        p.tab = spec["tab"].Str();  // the first tab; the one shown last while the game runs
        if (auto t = RememberedTabs().find(p.id); t != RememberedTabs().end()) p.tab = t->second;
        p.draftTab = spec["draftTab"].Str();
        p.compact = spec["hero"].Str() == "compact";
        p.heroH = p.compact ? kHeroCompactH : kHeroH;
        win.pages.push_back(std::move(p));
    }
    for (size_t i = 0; i < win.pages.size(); ++i) if (win.pages[i].id == "general") win.page = static_cast<int>(i);
    for (auto step : {BuildBase, BuildTopbar, BuildFooter, BuildSidebarTop, BuildNav, BuildCaption, BuildFrame}) jobs.push_back(step);
    // every glyph the window's texts use, into the fonts' atlases a few at a time
    {
        const std::string chars = D()["charset"][language].Str(D()["charset"]["en"].Str());
        std::vector<std::string> chunks(1);
        int count = 0;
        for (size_t i = 0; i < chars.size();) {
            const unsigned char c = chars[i];
            const size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
            if (count == 4) { chunks.emplace_back(); count = 0; }  // about 4 ms of glyph drawing a job
            chunks.back() += chars.substr(i, n);
            ++count;
            i += n;
        }
        for (Face face : {Face::Body, Face::BodySemi, Face::BodyBold, Face::Serif, Face::SerifBold})
            for (const auto& chunk : chunks)
                jobs.push_back([face, chunk] {
                    stage = "fonts: glyphs";
                    assets::Warm(face, chunk);
                });
    }
    jobs.push_back([] {
        QueuePage(win.page);
        for (size_t i = 0; i < win.pages.size(); ++i) if (static_cast<int>(i) != win.page) QueuePage(static_cast<int>(i));
        jobs.push_back([] {
            win.built = true;
            ShowPage(win.page);
            PlaceThumbs();
            timing.firstBuildMs = timing.buildMs;
            // built: the canvas goes on, parked (it was off while thousands of parts were added)
            if (!win.shown && Park(true)) {
                Enable(win.raycaster.Get(), false);
                Enable(win.rootCanvas.Get(), true);
                win.parked = true;
            }
        });
    });
}
void RunJobs(double budgetMs) {
    const double start = Ms();
    while (!jobs.empty() && Ms() - start < budgetMs) {
        auto job = std::move(jobs.front());
        jobs.pop_front();
        const double t = Ms();
        stage = "build job (" + std::to_string(jobs.size()) + " left)";
        job();
        const double took = Ms() - t;
        timing.buildMs += took;
        if (took > 4) Record("job " + stage, took);
    }
}
bool InGameNow() {
    if (!game::IsOfflinePlay()) return false;
    const auto s = SessionState();
    return s.state == 4 && !s.transitioning;
}

uint64_t nextBuildCheck = 0, inGameSince = 0;
// A character loaded or an area change began or ended: the sections that show why an action is
// unavailable are built again in the background (app.js updateDisabled after every change).
void CheckEligibility() {
    nextEligibleCheck = Now() + 1000;
    const bool now = Eligible();
    if (now == eligibleBuilt) return;
    eligibleBuilt = now;
    for (const auto& s : win.sections) {
        const int id = s->id;
        if (s->build) jobs.push_back([id] { Rebuild(id); });
        else if (s->panelState && s->panelState->tailNode) jobs.push_back([id] { RebuildTail(id); });
    }
}
double lastStep = 0;
void Step() {
    bool ok = false;
    if (!assets::Prepared(&ok) || !ok) { toggleRequested = false; return; }
    const double now = Ms();
    const float dt = lastStep > 0 ? static_cast<float>(std::min(0.1, (now - lastStep) / 1000.0)) : 0.016f;
    lastStep = now;
    const bool toggle = toggleRequested.exchange(false);
    // the screen size: on opening and once a second while open (a resolution change)
    static uint64_t nextScreen = 0;
    if (toggle || !screenW || Now() >= nextScreen) {
        static game::MethodRef width, height;
        if (!width) width = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Screen", "get_width", 0);
        if (!height) height = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Screen", "get_height", 0);
        if (!width || !height) throw std::runtime_error("Screen size unavailable");
        screenW = reinterpret_cast<int (*)(const il2cpp::Method*)>(width.code)(width.info);
        screenH = reinterpret_cast<int (*)(const il2cpp::Method*)>(height.code)(height.info);
        nextScreen = Now() + 1000;
    }
    if (toggle && !win.shown) {
        broken = false;
        // a new language or screen size builds the window again (rare; nothing else is rebuilt)
        language = ReadLanguage();
        if (win.rootGo && (language != win.lang || std::fabs(ScaleFor() - win.scale) > 0.001f)) Destroy();
    }
    if (!win.rootGo && jobs.empty()) {
        bool start = toggle;
        if (!start && Now() >= nextBuildCheck) {
            nextBuildCheck = Now() + 1000;
            if (!InGameNow()) inGameSince = 0;
            else if (!inGameSince) inGameSince = Now();
            start = inGameSince && Now() - inGameSince >= 5000;
        }
        if (start) {
            std::string why;
            stage = "fonts";
            if (!assets::FontsReady(&why)) {
                lastError = why;
                nextBuildCheck = Now() + 3000;
                return;
            }
            if (!toggle) language = ReadLanguage();
            QueueAll();
        }
    }
    if (toggle) {
        if (!win.shown) {
            RunJobs(1e9);  // whatever is not built yet is finished now
            if (!win.built) return;
            Show(true);
        } else Show(false);
    } else if (!jobs.empty()) RunJobs(win.shown ? 6 : 2.5);
    if (!win.built) return;
    if (Now() >= nextEligibleCheck) CheckEligibility();
    if (!timers.empty()) {
        // due work runs once (it may add new timers)
        std::vector<std::function<void()>> due;
        const uint64_t nowMs = Now();
        for (auto it = timers.begin(); it != timers.end();) {
            if (it->first <= nowMs) { due.push_back(std::move(it->second)); it = timers.erase(it); }
            else ++it;
        }
        for (auto& f : due) { stage = "timer"; f(); }
    }
    double t0 = Ms();
    stage = "commands";
    PumpCommands();
    Record("commands", Ms() - t0);
    if (win.toastUntil && Now() >= win.toastUntil) HideToast();
    if (!win.shown) return;
    t0 = Ms();
    FlushPending();
    Record("flush", Ms() - t0);
    t0 = Ms();
    stage = "input";
    Input(dt);
    PollInputs();
    Record("input", Ms() - t0);
    t0 = Ms();
    stage = "animate";
    Animate();
    Record("animate", Ms() - t0);
    for (int id : win.pages[win.page].sections)
        if (Sec(id).tick && Sec(id).built && !Sec(id).hidden) {
            t0 = Ms();
            stage = "tick " + Sec(id).kind;
            Sec(id).tick(Sec(id));
            Record(stage, Ms() - t0);
        }
    t0 = Ms();
    PlaceThumbs();
    Record("thumbs", Ms() - t0);
    if (Now() >= nextRead && commandQueue.empty() && !commandInFlight) RequestValues(false);
}

// ---------------------------------------------------------------------------------- hotkey
void Watch() {
    bool down = false;
    while (!lifecycle::Stopping()) {
        Sleep(15);
        const int vk = hotkey.load();
        if (!vk) { down = false; continue; }
        const bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (now && !down && GameInFront()) toggleRequested = true;
        down = now;
    }
}
std::filesystem::path KeyFile() { return std::filesystem::path(PluginDir()) / L"panel-key.txt"; }
std::string KeyName(int vk) { return vk >= VK_F5 && vk <= VK_F12 ? "F" + std::to_string(vk - VK_F1 + 1) : "off"; }
int KeyCode(const std::string& name) {
    if (name == "off") return 0;
    if (name.size() >= 2 && (name[0] == 'F' || name[0] == 'f')) {
        const int n = std::atoi(name.c_str() + 1);
        if (n >= 5 && n <= 12) return VK_F1 + n - 1;
    }
    return -1;
}
const char* KindName(HitKind k) {
    switch (k) {
    case HitKind::Nav: return "nav";
    case HitKind::Link: return "link";
    case HitKind::Minus: return "minus";
    case HitKind::Plus: return "plus";
    case HitKind::Track: return "track";
    case HitKind::Default: return "default";
    case HitKind::Toggle: return "toggle";
    case HitKind::Choice: return "choice";
    case HitKind::Refresh: return "refresh";
    case HitKind::Apply: return "apply";
    case HitKind::Discard: return "discard";
    case HitKind::Support: return "support";
    case HitKind::Action: return "action";
    case HitKind::Input: return "input";
    }
    return "?";
}
// "panel measure": every text of the built pages whose line count now differs from the one its
// section was laid out with (a test finds text that wraps over the next line or leaves a gap).
void TextsUnder(void* rt, void* type, std::vector<void*>& out) {
    void* a[]{type};
    if (void* text = Invoke(Sig("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "GetComponent", {"System.Type"}), rt, a)) out.push_back(text);
    const int n = Value<int>(U("Transform", "get_childCount", 0), rt);
    for (int i = 0; i < n; ++i) {
        void* ia[]{&i};
        TextsUnder(Invoke(U("Transform", "GetChild", 1), rt, ia), type, out);
    }
}
std::string Measure() {
    void* type = Type("Unity.TextMeshPro.dll", "TMPro", "TextMeshProUGUI");
    std::ostringstream out;
    out << "{\"ok\":true,\"texts\":[";
    int total = 0, drift = 0, firstPassWrong = 0;
    for (const auto& p : win.pages) {
        if (!p.root) continue;
        std::vector<void*> texts;
        TextsUnder(p.root.Get(), type, texts);
        for (void* text : texts) {
            const int measured = MeasuredLines(text);
            if (measured < 0) continue;
            ++total;
            if (FirstPassLines(text) != measured) ++firstPassWrong;
            const int drawn = DrawnLines(text);
            Layout(text);
            const int now = DrawnLines(text);
            if (drawn == measured && now == measured) continue;
            if (++drift > 80) continue;
            std::string words = Text(Invoke(T("get_text", 0), text));
            if (words.size() > 60) {
                size_t cut = 60;
                while (cut && (static_cast<unsigned char>(words[cut]) & 0xC0) == 0x80) --cut;
                words.resize(cut);
            }
            const V2 box = Value<V2>(U("RectTransform", "get_sizeDelta", 0), TransformOf(text));
            out << (drift > 1 ? "," : "") << "{\"page\":" << Json(p.id) << ",\"name\":" << Json(Text(Invoke(U("Object", "get_name", 0), GameObjectOf(text))))
                << ",\"measured\":" << measured << ",\"drawn\":" << drawn << ",\"now\":" << now << ",\"size\":" << Value<float>(T("get_fontSize", 0), text)
                << ",\"width\":" << box.x << ",\"detail\":" << MeasureDetail(text) << ",\"text\":" << Json(words) << '}';
        }
    }
    out << "],\"total\":" << total << ",\"drift\":" << drift << ",\"firstPassWrong\":" << firstPassWrong << '}';
    return out.str();
}
// "panel read": the state, timings and every clickable place in window css px (a test turns
// them into screen pixels with scale and screen).
std::string State() {
    std::ostringstream out;
    bool ok = false;
    const bool prepared = assets::Prepared(&ok);
    out << "{\"ok\":true,\"key\":" << Json(KeyName(hotkey.load())) << ",\"prepared\":" << Boolean(prepared && ok) << ",\"built\":" << Boolean(win.built)
        << ",\"shown\":" << Boolean(win.shown) << ",\"parked\":" << Boolean(win.parked) << ",\"gameKeysBlocked\":" << Boolean(gameKeysBlocked) << ",\"language\":" << Json(language) << ",\"page\":" << Json(win.built ? win.pages[win.page].id : "") << ",\"tab\":" << Json(win.built ? win.pages[win.page].tab : "")
        << ",\"jobs\":" << jobs.size() << ",\"cards\":" << win.cards.size() << ",\"sections\":" << win.sections.size() << ",\"scale\":" << win.scale
        << ",\"screen\":[" << screenW << ',' << screenH << ']' << ",\"scaleStatic\":" << Boolean(scaleStatic) << ",\"opens\":" << opens.load()
        << ",\"closes\":" << closes.load() << ",\"faults\":" << faults.load() << ",\"broken\":" << Boolean(broken) << ",\"error\":" << Json(lastError)
        << ",\"scroll\":" << win.mainScroll << ",\"sideScroll\":" << win.sideScroll << ",\"pending\":" << pending.size() << ",\"drafts\":" << drafts.size()
        << ",\"errors\":" << errors.size() << ",\"sending\":" << Json(sendingId) << ",\"dialog\":" << Boolean(win.dialogOpen) << ",\"list\":" << Boolean(win.listOpen)
        << ",\"toast\":" << Boolean(win.toastUntil != 0) << ",\"connection\":" << Json((offlinePlay ? "offline " : "online ") + sessionState)
        << ",\"fonts\":" << assets::FontReport() << ",\"timing\":{\"openMs\":" << timing.openMs << ",\"closeMs\":" << timing.closeMs
        << ",\"pageMs\":" << timing.pageMs << ",\"buildMs\":" << timing.buildMs << ",\"rebuildMs\":" << timing.rebuildMs << ",\"firstBuildMs\":" << timing.firstBuildMs
        << ",\"frameAvgClosed\":" << timing.frameAvgClosed << ",\"frameAvgOpen\":" << timing.frameAvgOpen << ",\"stepAvgOpen\":" << timing.stepAvgOpen
        << ",\"stepMaxOpen\":" << timing.stepMaxOpen << ",\"idleCostMax\":" << timing.idleCostMax << ",\"spikeAfterOpen\":" << timing.spikeAfterOpen
        << ",\"spikeAfterClose\":" << timing.spikeAfterClose << ",\"framesOpen\":" << timing.framesOpen << ",\"slowOpen\":" << timing.slowOpen
        << ",\"framesClosed\":" << timing.framesClosed << ",\"slowClosed\":" << timing.slowClosed
        << ",\"slowestStage\":" << Json(timing.slowestStage) << ",\"slowestIdleStage\":" << Json(timing.slowestIdleStage) << ",\"phases\":{";
    {
        bool comma = false;
        for (const auto& [name, ms] : timing.phases) {
            if (ms < 1) continue;
            out << (comma ? "," : "") << Json(name) << ':' << ms;
            comma = true;
        }
    }
    out << "}}";
    // what Unity's event system has selected, and the window's mouse listener (a test checks the game
    // gets its input back when the window closes)
    try {
        void* system = Invoke(Find("UnityEngine.UI.dll", "UnityEngine.EventSystems", "EventSystem", "get_current", 0));
        void* selected = system ? Invoke(Find("UnityEngine.UI.dll", "UnityEngine.EventSystems", "EventSystem", "get_currentSelectedGameObject", 0), system) : nullptr;
        out << ",\"selected\":" << Json(selected ? Text(Invoke(U("Object", "get_name", 0), selected)) : "");
        if (void* l = win.listener.Get()) {
            out << ",\"listenerEnabled\":" << Boolean(Value<bool>(U("Behaviour", "get_enabled", 0), l));
            if (const auto* under = Find("LE.dll", "", "UIMouseListener", "get_underMouse", 0, false)) out << ",\"underMouse\":" << Boolean(Value<bool>(under, l));
        }
    } catch (const std::exception&) {
    }
    // where the character stands: a test checks that clicks on the window do not move it
    try {
        void* actor = Invoke(Find("LE.dll", "", "PlayerFinder", "getPlayerActor", 0));
        if (actor && game::IsAlive(actor)) {
            const V3 p = Value<V3>(U("Transform", "get_position", 0), TransformOf(actor));
            out << ",\"player\":[" << p.x << ',' << p.y << ',' << p.z << ']';
        }
    } catch (const std::exception&) {
    }
    out << ",\"values\":{";
    bool comma = false;
    for (const auto& [id, v] : values) {
        out << (comma ? "," : "") << Json(id) << ':';
        if (std::isnan(v)) out << "null";  // a chance left to the game
        else out << v;
        comma = true;
    }
    out << "},\"feedback\":{";
    comma = false;
    for (const auto& k : win.cards) {
        if (k.feedbackShown.empty() || k.feedbackShown == "\x01") continue;
        out << (comma ? "," : "") << Json(k.c->id) << ':' << Json(k.feedbackShown);
        comma = true;
    }
    out << "},\"sectionsState\":" << SectionsState() << ",\"heights\":{";
    comma = false;
    for (const auto& p : win.pages) { out << (comma ? "," : "") << Json(p.id) << ':' << p.height; comma = true; }
    out << "},\"sectionList\":[";
    comma = false;
    for (const auto& s : win.sections) {
        out << (comma ? "," : "") << "{\"id\":" << s->id << ",\"page\":" << Json(win.pages[s->page].id) << ",\"kind\":" << Json(s->kind)
            << ",\"y\":" << s->y << ",\"h\":" << s->h << ",\"built\":" << Boolean(s->built) << ",\"hidden\":" << Boolean(s->hidden) << '}';
        comma = true;
    }
    out << "],\"inputs\":[";
    comma = false;
    for (const auto& w : win.inputs) {
        const bool focused = Value<bool>(Find("Unity.TextMeshPro.dll", "TMPro", "TMP_InputField", "get_isFocused", 0), w.field.Get());
        const std::string text = Text(Invoke(Find("Unity.TextMeshPro.dll", "TMPro", "TMP_InputField", "get_text", 0), w.field.Get()));
        out << (comma ? "," : "") << "{\"name\":" << Json(w.name) << ",\"section\":" << w.section << ",\"text\":" << Json(text)
            << ",\"focused\":" << Boolean(focused) << '}';
        comma = true;
    }
    out << "],\"cached\":{";
    comma = false;
    for (const auto& [read, kept] : cache) { out << (comma ? "," : "") << Json(read) << ':' << kept.text.size(); comma = true; }
    out << "},\"hits\":[";
    comma = false;
    const auto emit = [&](int list) {
        auto* hits = ListOf(list);
        if (!hits) return;
        for (const auto& h : *hits) {
            float x, y;
            if (!h.visible || !Placed(list, h, &x, &y)) continue;
            const bool cardHit = h.a >= 0 && h.a < static_cast<int>(win.cards.size()) && h.kind != HitKind::Nav && h.kind != HitKind::Link;
            out << (comma ? "," : "") << "{\"kind\":\"" << KindName(h.kind) << "\",\"a\":" << h.a << ",\"b\":" << h.b << ",\"list\":" << list
                << ",\"id\":" << Json(cardHit ? win.cards[h.a].c->id : "") << ",\"name\":" << Json(h.name) << ",\"enabled\":" << Boolean(h.enabled)
                << ",\"x\":" << x << ",\"y\":" << y << ",\"w\":" << h.w << ",\"h\":" << h.h << '}';
            comma = true;
        }
    };
    if (win.built) {
        emit(kDialog);
        emit(kPopup);
        emit(kFixed);
        for (int id : win.pages[win.page].sections) emit(id);
    }
    out << "]}";
    return out.str();
}
}  // namespace

// ------------------------------------------------------------------ the services for sections
namespace view {
const Control* ControlById(const std::string& id) {
    auto it = controls.find(id);
    return it == controls.end() ? nullptr : &it->second;
}
// tools/preferences_store.py update(): the whole file again, {"version": 1, "values": {...}} with
// indent 1 and the texts as written, through a temporary file replaced in one step.
bool StorePreference(const std::string& key, const std::string* value) {
    const auto path = PreferencesPath();
    if (path.empty() || key.rfind("epochpact.", 0) != 0) return false;
    JValue file = ReadPreferencesFile();
    // the other values as they are, in their order; this one in its place, or last when new
    std::vector<std::pair<std::string, std::string>> stored;
    bool placed = false;
    for (const auto& [k, v] : file["values"].members) {
        if (!v.IsString()) continue;
        if (k != key) stored.emplace_back(k, v.text);
        else if (value) { stored.emplace_back(k, *value); placed = true; }
    }
    if (value && !placed) stored.emplace_back(key, *value);
    const auto quote = [](const std::string& text) {
        std::string out = "\"";
        for (unsigned char c : text) {
            if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else if (c == '\b') out += "\\b";
            else if (c == '\f') out += "\\f";
            else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
            else out += static_cast<char>(c);
        }
        return out + "\"";
    };
    std::string text = "{\n \"version\": 1,\n \"values\": {";
    for (size_t i = 0; i < stored.size(); ++i) text += (i ? ",\n  " : "\n  ") + quote(stored[i].first) + ": " + quote(stored[i].second);
    text += stored.empty() ? "}\n}" : "\n }\n}";
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    const auto temporary = std::filesystem::path(path.wstring() + L".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << text;
        if (!out) return false;
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return false;
    LoadPreferences();
    return true;
}
JValue Preference(const std::string& key) {
    JValue inner;
    const JValue* stored = preferences["values"].Find(key);
    if (!stored || !stored->IsString() || !json::Parse(stored->text, inner)) return JValue{};
    return inner;
}
std::string PageTab(int page) { return page >= 0 && page < static_cast<int>(win.pages.size()) ? win.pages[page].tab : ""; }
void ShowTab(int page, const std::string& tab) {
    if (page < 0 || page >= static_cast<int>(win.pages.size())) return;
    Page& p = win.pages[page];
    if (p.tab == tab) return;
    if (page == win.page) {
        ReleaseKeyboard();
        CloseList();
        ClearHover();
    }
    p.tab = tab;
    RememberedTabs()[p.id] = tab;
    for (int id : p.sections)
        if (Sec(id).built && Sec(id).onTab) Sec(id).onTab(Sec(id));
    Relayout(page);
    UpdateFooter();
}
bool Typing(int sectionId) {
    for (const auto& w : win.inputs)
        if (w.section == sectionId && FieldFocused(w.field.Get())) return true;
    return false;
}
bool& Disclosure(const std::string& key) { return DetailsOpen()[key]; }
bool SettingValue(const std::string& id, double* v) {
    const auto it = values.find(id);
    if (it == values.end()) return false;
    *v = it->second;
    return true;
}
void SetDraft(const std::string& id, double v) {
    values[id] = v;
    errors.erase(id);
    applied.erase(id);
    drafts.insert(id);
    PaintId(id);
}
std::string SettingFeedback(const std::string& id, Color* color, float* size) {
    *color = C("accent");
    *size = 10;
    if (auto e = errors.find(id); e != errors.end()) { *color = Hex("#f2a594"); return e->second.substr(0, e->second.find('\n')); }
    if (pending.count(id) || sendingId == id) { *size = 11; return Tr("Applying\xE2\x80\xA6"); }
    if (drafts.count(id)) { *size = 11; return Tr("Draft"); }
    if (applied.count(id)) return Tr("Applied");
    return "";
}
int DraftCount(const std::string& prefix) {
    int n = 0;
    for (const auto& id : drafts) n += id.rfind(prefix, 0) == 0;
    return n;
}
void ClearDrafts(const std::string& prefix) {
    std::vector<std::string> ids;
    for (const auto& id : drafts) if (id.rfind(prefix, 0) == 0) ids.push_back(id);
    for (const auto& id : ids) {
        drafts.erase(id);
        if (appliedValues.count(id)) values[id] = appliedValues[id];
        else values.erase(id);
        PaintId(id);
    }
    UpdateFooter();
}
void RegisterSection(const std::string& kind, SectionMaker make) { Makers()[kind] = std::move(make); }
void RegisterTail(const std::string& kind, Tail tail) { Tails()[kind] = std::move(tail); }
void RebuildTail(int sectionId) {
    Section* s = FindSection(sectionId);
    if (!s || !s->built || !s->panelState || !s->panelState->tailNode) return;
    PanelState& ps = *s->panelState;
    auto it = Tails().find(ps.tailKind);
    if (it == Tails().end()) return;
    const double start = Ms();
    if (hovered.list == sectionId) ClearHover();
    if (pressed.list == sectionId) pressed = {kFixed, -1};
    ReleaseKeyboard(sectionId);
    win.inputs.erase(std::remove_if(win.inputs.begin(), win.inputs.end(), [&](const Watched& w) { return w.section == sectionId; }), win.inputs.end());
    s->hits.erase(s->hits.begin() + static_cast<std::ptrdiff_t>(std::min(ps.tailHits, s->hits.size())), s->hits.end());
    s->wheels.clear();  // a setting panel's wheels are its tail's
    void* go = GameObjectOf(ps.tailNode.Get());
    SetActive(go, false);
    void* d[]{go};
    Invoke(U("Object", "Destroy", 1), nullptr, d);
    Node node = New("Tail", PanelParent(ps), 0, ps.tailY, kPanelW, 10);
    ps.tailNode.Set(node.rt);
    stage = "tail " + ps.tailKind;
    // TMP measures only texts that are switched on: a closed <details> or a section on another tab
    // is on while its new tail is made
    void* asleep = s->hidden ? GameObjectOf(s->root.Get()) : nullptr;
    void* closed = ps.details && !ps.open ? GameObjectOf(ps.content.Get()) : nullptr;
    if (asleep) SetActive(asleep, true);
    if (closed) SetActive(closed, true);
    const float h = it->second.build(*s, Ctx{&s->hits, Area::Main, node.rt, kPadX, ps.tailY, s->id});
    if (closed) {
        SetActive(closed, false);
        for (size_t i = ps.tailHits; i < s->hits.size(); ++i) s->hits[i].visible = false;
    }
    if (asleep) SetActive(asleep, false);
    const float dy = h - ps.tailH;
    ps.tailH = h;
    if (std::fabs(dy) >= 0.01f) {
        ps.panelH += dy;
        FitPanel(*s);
    }
    timing.rebuildMs = Ms() - start;
    Record("rebuild tail " + ps.tailKind, timing.rebuildMs);
}
Section* FindSection(int id) { return id >= 0 && id < static_cast<int>(win.sections.size()) ? win.sections[id].get() : nullptr; }
void Relayout(int page) {
    if (page < 0 || page >= static_cast<int>(win.pages.size()) || !win.pages[page].root) return;
    Page& p = win.pages[page];
    float y = p.heroH + kPadTop;
    // a panel group: one panel from its first section's top to its last section's bottom
    Section* groupFirst = nullptr;
    std::string group;
    float bottom = y;
    const auto closeGroup = [&] {
        if (groupFirst && groupFirst->frame) ClosePanel(*groupFirst->frame, kPanelW, bottom - groupFirst->y);
        groupFirst = nullptr;
        group.clear();
    };
    for (int id : p.sections) {
        Section& s = Sec(id);
        if (!s.built || !s.root) continue;
        // a section of another tab: switched off, no place on the page
        const std::string tab = (*s.spec)["tab"].Str();
        const bool hidden = !tab.empty() && tab != p.tab;
        if (hidden != s.hidden) {
            SetActive(GameObjectOf(s.root.Get()), !hidden);
            s.hidden = hidden;
        }
        if (hidden) continue;
        const std::string g = (*s.spec)["group"].Str();
        if (g != group) {
            closeGroup();
            if (!g.empty()) { groupFirst = &s; group = g; }
        }
        s.y = y;
        Move(s.root.Get(), 0, y);
        bottom = y + s.h;
        y += s.h + s.after;
    }
    closeGroup();
    p.height = y + kPadBottom;
    Resize(p.root.Get(), kContentW, p.height);
    if (page == win.page && win.built) {
        win.mainTarget = std::min(win.mainTarget, MaxMain());
        win.mainScroll = std::min(win.mainScroll, MaxMain());
        Move(win.mainContent.Get(), 0, -win.mainScroll);
        win.thumbsAt[0] = -1;
    }
}
void Rebuild(int sectionId) {
    Section* s = FindSection(sectionId);
    if (!s || !s->build || !s->root) return;
    const double start = Ms();
    if (hovered.list == sectionId) ClearHover();
    if (pressed.list == sectionId) pressed = {kFixed, -1};
    ReleaseKeyboard(sectionId);
    win.inputs.erase(std::remove_if(win.inputs.begin(), win.inputs.end(), [&](const Watched& w) { return w.section == sectionId; }), win.inputs.end());
    if (void* old = s->body.Get()) {
        void* go = GameObjectOf(old);
        SetActive(go, false);
        void* d[]{go};
        Invoke(U("Object", "Destroy", 1), nullptr, d);
    }
    s->hits.clear();
    s->wheels.clear();
    s->live.reset();  // its widgets were in the old body
    NewBody(*s);
    stage = "rebuild " + s->kind;
    // TMP measures only texts that are switched on: a section on another tab is on while it is made
    void* asleep = s->hidden ? GameObjectOf(s->root.Get()) : nullptr;
    if (asleep) SetActive(asleep, true);
    s->h = s->build(*s, s->Body(s->body.Get()));
    if (asleep) SetActive(asleep, false);
    Relayout(s->page);
    timing.rebuildMs = Ms() - start;
    Record("rebuild " + s->kind, timing.rebuildMs);
}
void Send(std::string line, Reply done) { commandQueue.push_back({std::move(line), std::move(done)}); }
void Keep(const std::string& read, const std::string& reply) { ReadInto(read, reply); }
void After(uint64_t ms, Action work) { timers.emplace_back(Now() + ms, std::move(work)); }
// The desktop app's test (ui_bridge.text_ok): a "...: refused:" / "error:" / "failed:" line, or a
// JSON reply with ok false.
bool Refused(const std::string& reply, std::string* why) {
    if (reply.empty()) { *why = "The game did not respond. The action was not automatically retried."; return true; }
    if (reply[0] == '{') {
        JValue v;
        if (json::Parse(reply, v) && v["ok"].IsBool() && !v["ok"].boolean) { *why = v["error"].Str(reply); return true; }
        return false;
    }
    std::istringstream lines(reply);
    std::string line;
    while (std::getline(lines, line)) {
        std::string lower(line);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        for (const char* word : {"refused:", "error:", "failed:"}) {
            for (size_t at = lower.find(word); at != std::string::npos; at = lower.find(word, at + 1)) {
                if (!at) continue;
                const size_t colon = lower.rfind(':', at - 1);
                if (colon != std::string::npos && lower.find_first_not_of(" \t", colon + 1) == at) { *why = reply; return true; }
            }
        }
    }
    return false;
}
void Read(const std::string& read) { Send(read, [read](const std::string& reply) { ReadInto(read, reply); }); }
const JValue* Cached(const std::string& read) {
    auto it = cache.find(read);
    return it == cache.end() || !it->second.json ? nullptr : it->second.json.get();
}
void Forget(const std::string& read) { cache.erase(read); }
bool Eligible(std::string* reason) {
    // the game's own state: the window runs in the game, on its main thread
    const bool ok = InGameNow();
    if (!ok && reason) *reason = Tr("Load an offline character and wait for the area to finish loading.");
    return ok;
}
std::string PlayerError(const std::string& error) {
    std::string lower(error);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (lower.find("main thread") != std::string::npos || lower.find("pick the job") != std::string::npos || lower.find("paused or minimi") != std::string::npos)
        return Tr("The game did not respond. Return to the game, then refresh and try again.");
    if (error.find("Traceback") != std::string::npos || error.find('{') != std::string::npos || error.find(":\\") != std::string::npos)
        return Tr("The game could not complete this action. Refresh the connection and try again. Technical details were saved in the local log.");
    return error;
}
void RunAction(ActionRun run) {
    auto go = [run]() {
        auto proceed = [run](const std::string& liveId) {
            std::string line = run.command;
            for (size_t at = line.find("{save_id}"); at != std::string::npos; at = line.find("{save_id}")) line.replace(at, 9, liveId);
            Send(line, [run](const std::string& reply) {
                std::string why;
                if (Refused(reply, &why)) { Toast(PlayerError(why), true); return; }
                JValue v;
                json::Parse(reply, v);
                std::string text = run.success ? run.success(v) : "";
                if (!text.empty() && v["backup"].IsString() && !v["backup"].text.empty()) text += " " + Tr("A recovery backup is available in Settings.");
                if (!text.empty()) Toast(text);
                for (const auto& r : run.reread) { Forget(r); Read(r); }
                if (run.done) run.done(v);
            });
        };
        if (run.command.find("{save_id}") == std::string::npos && run.snapshot.empty() && run.expectId.empty()) { proceed(""); return; }
        // the loaded character must be the one the section's data was read for
        Send("identityread", [run, proceed](const std::string& reply) {
            JValue id;
            if (!json::Parse(reply, id) || !(id["offline"].IsBool() && id["offline"].boolean)) {
                Toast(Tr("Load an offline character and wait for the area to finish loading."), true);
                return;
            }
            const std::string live = IdText(id.Path("player.id"));
            if (live.empty()) { Toast(Tr("The offline character is not ready yet."), true); return; }
            if (!run.expectId.empty() && run.expectId != live) {
                Toast(Tr("The loaded character has changed. Refresh this section and try again."), true);
                return;
            }
            if (!run.snapshot.empty()) {
                const JValue* snap = Cached(run.snapshot);
                if (!snap || IdText(snap->Path("player.id")) != live) {
                    Toast(Tr("The loaded character has changed. Refresh this section and try again."), true);
                    return;
                }
            }
            proceed(live);
        });
    };
    if (!run.confirmTitle.empty()) Confirm(run.confirmTitle, run.confirmMessage, go);
    else go();
}

// .notification: right 25, bottom 100, max-width 600, padding 16px 20px, panel colour, accent
// border (error: #ee8070, text #ffc6b9), 13px, line-height 1.6; hides after 6.5 s (errors 12 s).
void Toast(const std::string& text, bool error) {
    if (!win.toast) return;
    void* layer = OpenLayer(win.toast);
    Style s{Face::Body, 13, error ? Hex("#ffc6b9") : C("ink")};
    s.line = 1.6f;
    s.wrap = true;
    Node box = New("Notification", layer, 0, 0, 600, 10);
    void* bg = Fill(box.rt, "Background", 0, 0, 600, 10, C("panel"));
    void* frame = Outline(box.rt, "Border", 0, 0, 600, 10, error ? Hex("#ee8070") : C("accent"));
    void* probe = TextBox(box.rt, "Text", 21, 17, 558, LineHeight(s), s, text);
    const float textW = std::min(558.f, std::ceil(TextWidth(probe)) + 1);
    Resize(TransformOf(probe), textW, LineHeight(s));
    const float h = 17 + TextLines(probe) * LineHeight(s) + 17, w = textW + 42;
    Resize(box.rt, w, h);
    Resize(TransformOf(bg), w, h);
    Resize(TransformOf(frame), w, h);
    Move(box.rt, kW - 25 - w, kH - 100 - h);
    win.toastUntil = Now() + (error ? 12000 : 6500);
}
void CloseDialog() {
    if (!win.dialogOpen) return;
    win.dialogOpen = false;
    if (hovered.list == kDialog) ClearHover();
    win.dialogHits.clear();
    ClearLayer(win.dialog.Get());
    SetActive(GameObjectOf(win.dialog.Get()), false);
}
// #confirm-dialog: a 690px dialog (gold border, panel colour, padding 27) over a #000a backdrop;
// the title, ×, the message (line breaks kept), Cancel and the primary "Apply action".
void Confirm(const std::string& title, const std::string& message, Action yes) {
    CloseList();
    CloseDialog();
    void* layer = OpenLayer(win.dialog);
    Solid(layer, "Backdrop", 0, 0, kW, kH, Hex("#000000aa"));
    const float w = 690, pad = 27, inner = w - 2 * pad;
    Node box = New("Dialog", layer, (kW - w) / 2, 0, w, 10);
    void* bg = Fill(box.rt, "Background", 0, 0, w, 10, C("panel"));
    void* frame = Outline(box.rt, "Border", 0, 0, w, 10, C("gold"));
    Ctx c{&win.dialogHits, Area::Dialog, box.rt, 0, 0, -1};
    // .dialog-head: the h2 (margin-bottom 14) and the × button (21px, padding 4px 11px)
    Hit closeHit;
    closeHit.kind = HitKind::Action;
    closeHit.name = "close";
    closeHit.action = [] { CloseDialog(); };
    Button x = MakeButton(box.rt, "\xC3\x97", Look::Normal, closeHit);
    const float xw = x.w, headH = H2(box.rt, pad, pad, inner - xw - 15, title) + 14;
    Move(x.node.rt, w - pad - xw, pad);
    closeHit.x = w - pad - xw;
    closeHit.y = pad;
    float y = pad + std::max(headH, x.h);
    // p: margin 10px 0, white-space pre-line
    Style ps{Face::Body, 14, C("ink")};
    ps.line = 1.65f;
    ps.wrap = true;
    y += 10 + Paragraph(box.rt, "Message", pad, y + 10, inner, ps, message) + 10;
    // .actions: margin-top 18; Cancel, then the primary at the right
    y += 18;
    Button cancel = AddButton(c, pad, y, Tr("Cancel"), Look::Normal, [] { CloseDialog(); }, false, "cancel");
    Button apply = AddButton(c, w - pad, y, Tr("Apply action"), Look::Primary, [yes] { CloseDialog(); if (yes) yes(); }, true, "confirm");
    y += std::max(cancel.h, apply.h) + pad;
    const float top = std::max(20.f, (kH - y) / 2);
    Resize(box.rt, w, y);
    Resize(TransformOf(bg), w, y);
    Resize(TransformOf(frame), w, y);
    Move(box.rt, (kW - w) / 2, top);
    // the dialog's hits in window coords
    for (auto& h : win.dialogHits) { h.x += (kW - w) / 2; h.y += top; }
    closeHit.x += (kW - w) / 2;
    closeHit.y += top;
    closeHit.area = Area::Dialog;
    win.dialogHits.push_back(std::move(closeHit));
    win.dialogOpen = true;
}
// #result-dialog (players see it for the prophecy preview): the head, p.note, then
// dl.result-summary (grid: minmax(80px, .35fr) 1fr, gap 1rem, margin 1.5rem 0; dt #aebdb7).
void ShowResult(const std::string& title, const std::string& note, const std::vector<std::pair<std::string, std::string>>& rows) {
    CloseList();
    CloseDialog();
    void* layer = OpenLayer(win.dialog);
    Solid(layer, "Backdrop", 0, 0, kW, kH, Hex("#000000aa"));
    const float w = 690, pad = 27, inner = w - 2 * pad;
    Node box = New("Dialog", layer, (kW - w) / 2, 0, w, 10);
    void* bg = Fill(box.rt, "Background", 0, 0, w, 10, C("panel"));
    void* frame = Outline(box.rt, "Border", 0, 0, w, 10, C("gold"));
    Hit closeHit;
    closeHit.kind = HitKind::Action;
    closeHit.name = "close";
    closeHit.action = [] { CloseDialog(); };
    Button x = MakeButton(box.rt, "\xC3\x97", Look::Normal, closeHit);
    const float xw = x.w, headH = H2(box.rt, pad, pad, inner - xw - 15, title) + 14;
    Move(x.node.rt, w - pad - xw, pad);
    closeHit.x = w - pad - xw;
    closeHit.y = pad;
    float y = pad + std::max(headH, x.h);
    y += NoteBox(box.rt, pad, y, inner, note) + 18;
    y += 3;  // the dl's margin 1.5rem (21px) collapses with the note's 18
    const float dtW = std::max(80.f, (inner - 14) * 0.35f / 1.35f), ddW = inner - 14 - dtW;
    Style dt{Face::Body, 14, Hex("#aebdb7")}, dd{Face::Body, 14, C("ink")};
    dt.wrap = dd.wrap = true;
    for (size_t i = 0; i < rows.size(); ++i) {
        const float a = Paragraph(box.rt, "Term", pad, y, dtW, dt, rows[i].first), b = Paragraph(box.rt, "Detail", pad + dtW + 14, y, ddW, dd, rows[i].second);
        y += std::max(a, b) + (i + 1 < rows.size() ? 14 : 0);
    }
    y += 21 + pad;
    const float top = std::max(20.f, (kH - y) / 2);
    Resize(box.rt, w, y);
    Resize(TransformOf(bg), w, y);
    Resize(TransformOf(frame), w, y);
    Move(box.rt, (kW - w) / 2, top);
    closeHit.x += (kW - w) / 2;
    closeHit.y += top;
    closeHit.area = Area::Dialog;
    win.dialogHits.push_back(std::move(closeHit));
    win.dialogOpen = true;
}
void CloseList() {
    if (!win.listOpen) return;
    win.listOpen = false;
    win.popupWheel = nullptr;
    if (hovered.list == kPopup) ClearHover();
    win.popupHits.clear();
    ClearLayer(win.popup.Get());
    SetActive(GameObjectOf(win.popup.Get()), false);
}
// The choice list, below the select (above it when there is no room), like the browser's: up to
// 20 rows at a time; a longer list scrolls with the wheel and opens at the current choice.
void OpenList(float x, float y, float w, float h, const std::vector<std::string>& labels, int current, std::function<void(int)> chosen) {
    CloseList();
    void* rt = OpenLayer(win.popup);
    const float rowH = 28;
    const int total = static_cast<int>(labels.size());
    const int room = std::max(1, static_cast<int>((kH - 4 - 2) / rowH));
    const int visible = std::min({total, 20, room});
    const float listH = rowH * visible + 2;
    float top = y + h;
    if (top + listH > kH - 2) top = y - listH;
    if (top < 2) top = std::max(2.f, kH - 2 - listH);
    struct ListState {
        std::vector<std::string> labels;
        int offset = 0, current = -1, visible = 0;
        std::vector<Pin> texts, fills;
        Pin thumb;
        float x = 0, top = 0, w = 0, rowH = 28;
    };
    auto st = std::make_shared<ListState>();
    st->labels = labels;
    st->current = current;
    st->visible = visible;
    st->x = x;
    st->top = top;
    st->w = w;
    st->rowH = rowH;
    if (current >= visible) st->offset = std::min(current - visible / 2, total - visible);
    st->offset = std::max(0, st->offset);
    Fill(rt, "List", x, top, w, listH, Hex("#080e0e"));
    Outline(rt, "List border", x, top, w, listH, C("gold"));
    const bool scrolls = total > visible;
    const float rowW = w - 2 - (scrolls ? 10 : 0);
    for (int i = 0; i < visible; ++i) {
        Hit option;
        option.kind = HitKind::Action;
        option.area = Area::Popup;
        option.b = i;
        option.name = labels[st->offset + i];
        option.x = x + 1;
        option.y = top + 1 + i * rowH;
        option.w = rowW;
        option.h = rowH;
        void* fill = Fill(rt, "Option", option.x, option.y, option.w, rowH, kWhite);
        st->fills.emplace_back(fill);
        AddHover(option, fill, st->offset + i == current ? C("soft") : Alpha(C("soft"), 0), Alpha(C("accent"), 0.3f), 0);
        Style s{Face::Body, 14, C("ink")};
        s.align = kLeft;
        void* text = TextBox(rt, "Option text", option.x + 10, option.y, option.w - 20, rowH, s, labels[st->offset + i]);
        int ellipsis = 1;
        void* ea[]{&ellipsis};
        Invoke(T("set_overflowMode", 1), text, ea);
        st->texts.emplace_back(text);
        option.action = [st, chosen, i] {
            const int index = st->offset + i;
            CloseList();
            if (chosen) chosen(index);
        };
        win.popupHits.push_back(std::move(option));
    }
    if (scrolls) {
        const float track = listH - 4, thumbH = std::max(16.f, track * visible / total);
        st->thumb.Set(TransformOf(Fill(rt, "Thumb", x + w - 9, top + 2, 6, thumbH, Alpha(C("gold"), 0.7f), 3)));
        win.popupWheel = [st](float notches) {
            const int total = static_cast<int>(st->labels.size());
            const int offset = std::clamp(st->offset - static_cast<int>(std::lround(notches * 3)), 0, total - st->visible);
            if (offset == st->offset) return;
            st->offset = offset;
            ClearHover();
            for (int i = 0; i < st->visible && i < static_cast<int>(win.popupHits.size()); ++i) {
                const int index = offset + i;
                SetText(st->texts[i].Get(), st->labels[index]);
                Hit& hit = win.popupHits[i];
                hit.name = st->labels[index];
                const Color normal = index == st->current ? C("soft") : Alpha(C("soft"), 0);
                if (!hit.hover.empty()) hit.hover[0].normal = normal;
                Fade(st->fills[i].Get(), normal, 0);  // the renderer's colour (AddHover)
            }
            const float track = st->rowH * st->visible + 2 - 4, thumbH = std::max(16.f, track * st->visible / total);
            Move(st->thumb.Get(), st->x + st->w - 9, st->top + 2 + (track - thumbH) * offset / std::max(1, total - st->visible));
        };
    }
    win.listOpen = true;
}
// input type=color: the browser opens the system picker; here a popup with the theme's and the
// usual colours, the current colour and a hex field (six digits), like the app's own hints.
void OpenColorPicker(float x, float y, const std::string& hex, std::function<void(const std::string&)> chosen) {
    CloseList();
    void* rt = OpenLayer(win.popup);
    static const char* swatches[] = {"FFFFFF", "2E8BFF", "FFD700", "AA55FF", "FF5500", "00FF00", "FF0044", "00FFFF", "3FD0FF", "FFD27F", "80E2B1", "AC8950",
                                     "E8C690", "FF4040", "FFFF00", "7FB0FF", "C79BFF", "F2DC6B", "E8A35A", "7EE08A", "9FD6D0", "F0A64A", "B3BEB8", "000000"};
    const float cell = 26, gap = 6, pad = 12, cols = 8, w = pad * 2 + cols * cell + (cols - 1) * gap;
    const float gridH = 3 * cell + 2 * gap;
    const float h = pad + gridH + 12 + 36 + 10 + 34 + pad;
    float top = y;
    if (top + h > kH - 2) top = std::max(2.f, kH - 2 - h);
    float left = std::min(x, kW - 2 - w);
    Fill(rt, "Picker", left, top, w, h, C("panel"));
    Outline(rt, "Picker border", left, top, w, h, C("gold"));
    auto value = std::make_shared<std::string>(hex);
    for (int i = 0; i < 24; ++i) {
        const float sx = left + pad + (i % 8) * (cell + gap), sy = top + pad + (i / 8) * (cell + gap);
        const std::string code = swatches[i];
        Fill(rt, "Swatch", sx, sy, cell, cell, Hex("#" + code), 2);
        void* frame = Outline(rt, "Swatch border", sx, sy, cell, cell, Hex("#7b745a99"), 2);
        Hit hit;
        hit.kind = HitKind::Action;
        hit.area = Area::Popup;
        hit.name = code;
        hit.x = sx;
        hit.y = sy;
        hit.w = hit.h = cell;
        AddHover(hit, frame, Hex("#7b745a99"), C("accent"), 0);
        hit.action = [chosen, code] { CloseList(); if (chosen) chosen(code); };
        win.popupHits.push_back(std::move(hit));
    }
    // the current colour and its hex code
    const float fy = top + pad + gridH + 12;
    Fill(rt, "Current", left + pad, fy + 3, 30, 30, Hex("#" + hex), 3);
    Outline(rt, "Current border", left + pad, fy + 3, 30, 30, C("line"), 3);
    Ctx c{&win.popupHits, Area::Popup, rt, 0, 0, -1};
    InputField(c, left + pad + 40, fy, w - 2 * pad - 40, "", hex, "3FD0FF", false, [value](const std::string& text) { *value = text; }, "color-hex");
    const float by = fy + 36 + 10;
    AddButton(c, left + w - pad, by, Tr("Apply"), Look::Normal, [value, chosen] {
        std::string code = *value;
        code.erase(std::remove(code.begin(), code.end(), '#'), code.end());
        const bool valid = code.size() == 6 && std::all_of(code.begin(), code.end(), [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)) != 0; });
        if (!valid) { Toast(Tr("Colour of the drawn areas (six hexadecimal digits, for example 3FD0FF)."), true); return; }
        std::transform(code.begin(), code.end(), code.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        CloseList();
        if (chosen) chosen(code);
    }, true, "color-apply");
    win.listOpen = true;
}
bool CopyText(const std::string& text) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (static_cast<size_t>(n) + 1) * sizeof(wchar_t));
    bool ok = false;
    if (memory) {
        auto* data = static_cast<wchar_t*>(GlobalLock(memory));
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), data, n);
        data[n] = 0;
        GlobalUnlock(memory);
        ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        if (!ok) GlobalFree(memory);
    }
    CloseClipboard();
    return ok;
}
void Resized(Section& s, float h) {
    if (std::fabs(s.h - h) < 0.01f) return;
    s.h = h;
    Relayout(s.page);
}
bool WindowPoint(const Ctx& c, float x, float y, float* wx, float* wy) {
    *wx = c.ox + x;
    *wy = c.oy + y;
    if (c.section >= 0) {
        const Section& s = Sec(c.section);
        if (s.page != win.page) return false;
        *wx += kMainX;
        *wy += kMainY - win.mainScroll + s.y;
    } else if (c.area == Area::Sidebar) {
        *wx += 1;
        *wy += 1 - win.sideScroll;
    }
    return true;
}
void WatchInput(const Ctx& c, void* field, const std::string& value, std::function<void(const std::string&)> onChange, const std::string& name) {
    Watched w;
    w.section = c.section;
    w.field.Set(field);
    w.last = value;
    w.name = name;
    w.onChange = std::move(onChange);
    win.inputs.push_back(std::move(w));
}
}  // namespace view

namespace {
struct Fault { DWORD code = 0; void* address = nullptr; ULONG_PTR target = 0; };
int Filter(EXCEPTION_POINTERS* e, Fault* f) {
    f->code = e->ExceptionRecord->ExceptionCode;
    f->address = e->ExceptionRecord->ExceptionAddress;
    if (f->code == EXCEPTION_ACCESS_VIOLATION && e->ExceptionRecord->NumberParameters >= 2) f->target = e->ExceptionRecord->ExceptionInformation[1];
    return EXCEPTION_EXECUTE_HANDLER;
}
// No C++ objects in this frame (SEH); fn catches its own C++ exceptions.
template <typename F> bool Seh(F& fn, Fault* f) {
    __try { fn(); return true; } __except (Filter(GetExceptionInformation(), f)) { return false; }
}
std::string Describe(const Fault& f) {
    char text[400];
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(f.address), &module))
        GetModuleFileNameW(module, path, MAX_PATH);
    const std::wstring file = std::filesystem::path(path).filename().wstring();
    std::snprintf(text, sizeof text, "fault 0x%08X at %ls+0x%llX (reading 0x%llX) during %s", static_cast<unsigned>(f.code), file.c_str(),
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(f.address) - reinterpret_cast<uintptr_t>(module)),
                  static_cast<unsigned long long>(f.target), stage.c_str());
    return text;
}
// "Rage of Morditas" facts note (app.js rageFacts): the game's own hunt time, charge and buff kills.
std::string Joined(const JValue& list) {
    std::string out;
    for (const auto& v : list.items) out += (out.empty() ? "" : ", ") + Num(v.Num());
    return out;
}
}  // namespace

namespace {
std::string FactsText(const std::string& kind) {
    if (kind != "rage") return "";
    const JValue* r = Cached("rageread");
    if (!r || !r->Find("game")) return "";
    const JValue& game = (*r)["game"];
    const JValue* hunt = game["configs"].items.empty() ? nullptr : game["configs"].items[0].Find("huntSeconds");
    return Fmt("Now: hunt {0} s · Bloodrage charges at {1} kills · hunt buffs at {2} kills.",
               {hunt && hunt->IsNumber() ? Num(hunt->number) : "\xE2\x80\x94", Joined(game["chargeKills"]), Joined(game["buffKills"])});
}
}  // namespace

bool Init() {
    std::ifstream in(KeyFile());
    std::string saved;
    if (in >> saved) { const int code = KeyCode(saved); if (code >= 0) hotkey = code; }
    std::string why;
    if (!assets::Start(&why)) Log("in-game window unavailable: %s", why.c_str());
    view::RegisterNativeSections();
    std::thread(Watch).detach();
    Log("in-game window: hotkey %s", KeyName(hotkey.load()).c_str());
    return true;
}

void Frame() {
    if (lifecycle::Stopping()) return;
    // the game's frame time: closed, open, and the frames right after opening and closing
    const double now = Ms(), frame = timing.lastFrame > 0 ? now - timing.lastFrame : 0;
    timing.lastFrame = now;
    if (frame > 0 && frame < 1000) {
        double& avg = win.shown ? timing.frameAvgOpen : timing.frameAvgClosed;
        avg = avg > 0 ? avg * 0.98 + frame * 0.02 : frame;
        if (timing.framesSinceOpen < 10) timing.spikeAfterOpen = std::max(timing.spikeAfterOpen, frame);
        if (timing.framesSinceClose < 10) timing.spikeAfterClose = std::max(timing.spikeAfterClose, frame);
        if (timing.framesSinceOpen >= 10 && timing.framesSinceClose >= 10 && win.built) {
            ++(win.shown ? timing.framesOpen : timing.framesClosed);
            if (frame > 50) ++(win.shown ? timing.slowOpen : timing.slowClosed);
        }
    }
    ++timing.framesSinceOpen;
    ++timing.framesSinceClose;
    const bool toggle = toggleRequested.load(std::memory_order_relaxed);
    if (broken && !toggle) return;
    // closed and idle, or not in game yet and not time to look again: nothing to do this frame
    if (!win.shown && win.built && jobs.empty() && !toggle && commandQueue.empty() && !commandInFlight && !win.toastUntil && Now() < nextEligibleCheck && timers.empty()) return;
    if (!win.rootGo && jobs.empty() && !toggle && Now() < nextBuildCheck) return;
    std::string why, ours;
    const double start = Ms();
    Fault fault;
    auto run = [&] {
        try { Step(); }
        catch (const std::exception& e) { ours = e.what(); }
        catch (...) { ours = "managed exception inside game code during " + stage; }
    };
    const bool safe = Seh(run, &fault);
    if (!safe) why = Describe(fault);
    const double cost = Ms() - start;
    if (win.shown) {
        timing.stepAvgOpen = timing.stepAvgOpen > 0 ? timing.stepAvgOpen * 0.98 + cost * 0.02 : cost;
        if (timing.framesSinceOpen > 2 && cost > timing.stepMaxOpen) { timing.stepMaxOpen = cost; timing.slowestStage = stage; }
    } else if (win.built && jobs.empty() && cost > timing.idleCostMax) { timing.idleCostMax = cost; timing.slowestIdleStage = stage; }
    if (!safe || !ours.empty()) {
        if (!ours.empty()) why = ours;
        ++faults;
        lastError = why;
        Log("in-game window fault: %s", why.c_str());
        // start over from nothing: a half-built window is never shown
        game::Guarded([] { if (win.shown && win.rootCanvas.Get()) Show(false); }, nullptr);
        game::Guarded([] { Destroy(); }, nullptr);
        nextBuildCheck = Now() + 10000;
        if (faults.load() >= 3) broken = true;
    }
}

// "panel kern": does TMP kern this game's texts? A line of kerning pairs: TMP's width, the faces'
// advances without kerning, the switch and the font assets' pair records (the browser kerns).
std::string Kerning() {
    if (!win.built) return "{\"ok\":false,\"error\":\"the window is not built yet\"}";
    const std::string sample = "AVAVAVAVAV To To Ty Wa Yo LT";
    std::ostringstream out;
    out << "{\"ok\":true,\"sample\":" << Json(sample);
    for (Face face : {Face::Body, Face::Serif}) {
        Style s{face, 100, C("ink")};
        void* t = TextBox(win.windowRt.Get(), "Kern probe", 0, 0, 4000, 200, s, sample);
        out << ",\"" << (face == Face::Body ? "body" : "serif") << "\":{\"tmp\":" << TextWidth(t) << ",\"advances\":" << TextAdvance(face, 100, sample);
        if (const auto* get = Find("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_enableKerning", 0, false)) {
            out << ",\"enableKerning\":" << Boolean(Value<bool>(get, t));
            if (const auto* set = Find("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_enableKerning", 1, false)) {
                bool on = true;
                void* a[]{&on};
                Invoke(set, t, a);
                Layout(t);
                out << ",\"tmpKerningOn\":" << TextWidth(t);
            }
        }
        if (const auto* features = Find("Unity.TextMeshPro.dll", "TMPro", "TMP_FontAsset", "get_fontFeatureTable", 0, false)) {
            void* asset = assets::Font(face);
            void* table = asset ? Invoke(features, asset) : nullptr;
            if (table) {
                if (const auto* records = Find("Unity.TextMeshPro.dll", "TMPro", "TMP_FontFeatureTable", "get_glyphPairAdjustmentRecords", 0, false)) {
                    void* list = Invoke(records, table);
                    if (list) out << ",\"pairRecords\":" << Value<int>(Method(list, "get_Count", 0), list);
                }
            }
        }
        out << '}';
        void* go = GameObjectOf(t);
        SetActive(go, false);
        void* d[]{go};
        Invoke(U("Object", "Destroy", 1), nullptr, d);
    }
    return out.str() + "}";
}
std::string Panel(const std::string& action) {
    if (action == "read") return Run([] { return State(); });
    if (action == "measure") return Run([] { return Measure(); });
    if (action == "kern") return Run([] { return Kerning(); });
    if (action != "0" && action != "1") return "{\"ok\":false,\"error\":\"use panel read|measure|kern|0|1\"}";
    const bool open = action == "1";
    return Run([open] {
        if (open != win.shown) toggleRequested = true;
        Step();
        return State();
    });
}

std::string SetKey(const std::string& key) {
    const int code = KeyCode(key);
    if (code < 0) return "{\"ok\":false,\"error\":\"use panelkey F5-F12|off\"}";
    hotkey = code;
    std::error_code ignored;
    std::filesystem::create_directories(KeyFile().parent_path(), ignored);
    std::ofstream(KeyFile(), std::ios::binary | std::ios::trunc) << KeyName(code) << "\n";
    return Run([] { return State(); });
}

// Runtime shutdown has already stopped frames. Do not call Unity Destroy here.
void ReleaseRoots() {
    win = Window{};
    cache.clear();
    ui::ReleaseTypes();
    assets::Release();
}

}  // namespace ep::ingame
