// The Crafting page (app.js renderCrafting): its four tabs, the Forge Settings tab's native parts
// (the note, Glyph chances, Craft from EpochPact inside Advanced forge options, Restore game
// defaults) and the Materials tab. The Forge Settings panels are setting panels (ingame_panel.cpp);
// the Custom Items workshop and Spawn Items are ingame_workshop.cpp.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace ep::ingame::view {
namespace {
using JValue = json::Value;
using assets::Face;

const Control& Ctl(const std::string& id) {
    static const Control missing;
    const Control* c = ControlById(id);
    return c ? *c : missing;
}
std::string IdOf(const JValue* v) { return !v ? "" : v->IsString() ? v->text : v->IsNumber() ? Num(v->number) : ""; }
std::string Grouped(double v) {  // toLocaleString('en-US')
    const long long n = std::llround(v);
    const std::string digits = std::to_string(n < 0 ? -n : n);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return (n < 0 ? "-" : "") + out;
}
std::string SaveMessage(const Control& c) { return Tr(c.description) + " " + Tr("This action changes the game save. The backend creates a backup before applying it."); }
// .section-head: the h2 and one button (centred on the h2's line); returns its height
float HeadWithButton(const Ctx& c, float y, const std::string& title, const std::string& label, Action click, const std::string& name) {
    Style h2{Face::Serif, 27, C("ink")};
    const float lh = LineHeight(h2);
    Button b = AddButton(c, kPanelPad + kPanelInner, y, label, Look::Normal, std::move(click), true, name);
    const float row = std::max(lh, b.h);
    Move(b.node.rt, (*c.hits)[b.hit].x - c.ox, y + (row - b.h) / 2);
    (*c.hits)[b.hit].y = c.oy + y + (row - b.h) / 2;
    TextBox(c.parent, "Title", kPanelPad, y + (row - lh) / 2, kPanelInner - b.w - 15, lh, h2, title);
    return row;
}
// The heading row of a settingPanel: the h2 and its badge.
float HeadWithBadge(void* parent, float y, const std::string& title, const std::string& badge) {
    Style h2{Face::Serif, 27, C("ink")};
    const float lh = LineHeight(h2);
    TextBox(parent, "Title", kPanelPad, y, kPanelInner - 300, lh, h2, title);
    Badge(parent, kPanelPad + kPanelInner, y + (lh - (LineHeight(Style{Face::Body, 10}) + 12)) / 2, badge);
    return lh;
}
// craftread's forge state: "preview" (the desktop bridge renames it forgePreview)
const JValue* Forge() {
    const JValue* r = Cached("craftread");
    if (!r) return nullptr;
    if (const JValue* f = r->Find("forgePreview")) return f;
    return r->Find("preview");
}
bool True(const JValue* v) { return v && v->IsBool() && v->boolean; }

// ---------------------------------------------------------------------------------------- tabs
// .craft-categories: four buttons (strong 15px, span 11px muted), the pressed one with the accent
// border, an inset accent bar and the soft colour; then, away from Forge Settings, the note about
// Forge settings in draft with its link back.
void MakeTabs(Section& s) {
    s.build = [](Section& sec, const Ctx& c) {
        const std::string current = PageTab(sec.page);
        const std::array<std::array<std::string, 3>, 4> tabs{{{"forge", Tr("Forge Settings"), Tr("Costs, materials and glyph chances")},
                                                              {"materials", Tr("Materials"), Tr("Stock your crafting stash")},
                                                              {"custom", Tr("Custom Items"), Tr("Build an item step by step")},
                                                              {"spawn", Tr("Spawn Items"), Tr("Drop any item at your feet")}}};
        const float gap = 12, w = (kPanelW - 3 * gap) / 4;
        Style strong{Face::BodyBold, 15, C("ink")}, hint{Face::Body, 11, C("muted")};
        strong.wrap = hint.wrap = true;
        hint.line = 1.5f;
        struct Made { Node node; void* fill; void* frame; Node inner; float h; int hit; bool on; };
        std::vector<Made> made;
        float rowH = 0;
        const int page = sec.page;
        for (size_t i = 0; i < tabs.size(); ++i) {
            const auto& t = tabs[i];
            Made m;
            m.on = current == t[0];
            const float x = kPadX + i * (w + gap);
            m.node = New("Tab", c.parent, x, 0, w, 10);
            m.fill = Fill(m.node.rt, "Background", 0, 0, w, 10, m.on ? C("soft") : C("panel"), 3);
            m.frame = Outline(m.node.rt, "Border", 0, 0, w, 10, m.on ? C("accent") : C("line"), 3);
            m.inner = New("Content", m.node.rt, 0, 0, w, 10);
            // padding 16px 18px inside the 1px border; the span: margin-top 8
            float cy = 17;
            cy += Paragraph(m.inner.rt, "Label", 19, cy, w - 38, strong, t[1]);
            cy += 8 + Paragraph(m.inner.rt, "Hint", 19, cy + 8, w - 38, hint, t[2]);
            m.h = cy + 17;
            Hit hit;
            hit.kind = HitKind::Action;
            hit.name = "craft-tab-" + t[0];
            hit.x = x;
            hit.y = 0;
            hit.w = w;
            if (!m.on) {
                AddHover(hit, m.fill, C("panel"), C("soft"));
                AddHover(hit, m.frame, C("line"), C("accent"));
            }
            const std::string id = t[0];
            hit.action = [page, id] { ShowTab(page, id); };
            m.hit = c.Add(std::move(hit));
            rowH = std::max(rowH, m.h);
            made.push_back(m);
        }
        for (auto& m : made) {
            // the grid row stretches the buttons; a button centres its content
            Resize(m.node.rt, w, rowH);
            Resize(TransformOf(m.fill), w, rowH);
            Resize(TransformOf(m.frame), w, rowH);
            Move(m.inner.rt, 0, (rowH - m.h) / 2);
            (*c.hits)[m.hit].h = rowH;
            if (m.on) Solid(m.node.rt, "Pressed", 1, rowH - 4, w - 2, 3, C("accent"));  // box-shadow: inset 0 -3px
        }
        sec.after = 22;
        const int drafts = current == "forge" ? 0 : DraftCount("craft_");
        if (!drafts) return rowH;
        // div.note (margin-top: the tabs' 22): "{n} Forge settings have unapplied changes." and a link-button
        const float y = rowH + 22;
        Node note = New("Note", c.parent, kPadX, y, kPanelW, 10);
        Style ns{Face::Body, 12, C("muted")};
        ns.line = 1.7f;
        const float lh = LineHeight(ns), h = 12 + lh + 12;
        Fill(note.rt, "Background", 0, 0, kPanelW, h, C("soft"));
        Fill(note.rt, "Bar", 0, 0, 2, h, C("accent"));
        void* text = TextBox(note.rt, "Text", 18, 12, kPanelW - 34, lh, ns, Fmt("{0} Forge settings have unapplied changes.", {std::to_string(drafts)}));
        const float lx = 18 + TextWidth(text) + TextAdvance(Face::Body, 12, " ");
        Style ls = ns;
        ls.color = C("accent");
        void* link = TextBox(note.rt, "Review", lx, 12, kPanelW - 18 - lx, lh, ls, Tr("Review Forge Settings"));
        Hit hit;
        hit.kind = HitKind::Action;
        hit.name = "craft-review-forge";
        hit.x = kPadX + lx;
        hit.y = y + 12;
        hit.w = std::ceil(TextWidth(link));
        hit.h = lh;
        hit.action = [page] { ShowTab(page, "forge"); };
        c.Add(std::move(hit));
        sec.after = 18;
        return y + h;
    };
    s.onTab = [](Section& sec) { Rebuild(sec.id); };
}

// ---------------------------------------------------------------------------- Forge Settings
// p.note: "… then use <strong>Apply changes</strong> below. …" (margin-bottom 18)
void MakeForgeNote(Section& s) {
    s.build = [](Section& sec, const Ctx& c) {
        Node note = New("Note", c.parent, kPadX, 0, kPanelW, 10);
        Style ns{Face::Body, 12, C("muted")};
        ns.line = 1.7f;
        ns.wrap = true;
        ns.rich = true;
        const std::string text = "<noparse>" + Tr("These settings affect the game\xE2\x80\x99s normal forge. Edit them here, then use") + "</noparse> <b>" +
                                 Tr("Apply changes") + "</b> <noparse>" + Tr("below. No item is crafted until you craft in the game or press Craft once.") +
                                 "</noparse>";
        void* bg = Fill(note.rt, "Background", 0, 0, kPanelW, 10, C("soft"));
        void* bar = Fill(note.rt, "Bar", 0, 0, 2, 10, C("accent"));
        const float h = 12 + Paragraph(note.rt, "Text", 18, 12, kPanelW - 34, ns, text) + 12;
        Resize(TransformOf(bg), kPanelW, h);
        Resize(TransformOf(bar), 2, h);
        sec.after = 18;
        return h;
    };
}

// Glyph chances (glyphChanceCard): Game default or a custom percentage, a draft for Apply changes.
struct GlyphCard {
    std::string id;
    bool custom = false;
    double value = 0;
    std::string feedback;
    Pin thumb;
    float trackX = 0, trackW = 0, thumbY = 0;
};
struct GlyphView {
    GlyphCard cards[2];
    bool stale = false;  // a draft changed while its field had the keyboard: built again afterwards
};
struct GlyphState { bool custom; double value; std::string feedback; };
GlyphState GlyphNow(const std::string& id) {
    double v = 0;
    const bool known = SettingValue(id, &v);
    Color color;
    float size = 10;
    const bool custom = known && !std::isnan(v);
    return {custom, custom ? v : 0, SettingFeedback(id, &color, &size)};
}
bool GlyphChanged(Section& s) {
    auto view = std::static_pointer_cast<GlyphView>(s.live);
    if (!view) return true;
    for (const auto& k : view->cards) {
        const GlyphState now = GlyphNow(k.id);
        if (now.custom != k.custom || (now.custom && now.value != k.value) || now.feedback != k.feedback) return true;
    }
    return false;
}
void PlaceThumb(GlyphCard& k, double v) {
    const double t = std::clamp(v / 100.0, 0.0, 1.0);
    Move(k.thumb.Get(), static_cast<float>(k.trackX + 1 + 9.5 + t * (k.trackW - 2 - 19)) - 22, k.thumbY);
}
void MakeGlyphChances(Section& s) {
    s.reads.insert("craftread");
    s.settings = {"craft_hope", "craft_despair"};
    s.build = [](Section& sec, const Ctx& c) {
        auto view = std::make_shared<GlyphView>();
        sec.live = view;
        Panel panel = OpenPanel(c.parent, kPadX, 0, kPanelW);
        void* rt = panel.rt.Get();
        const Ctx in = c.At(rt, kPadX, 0);
        const float top = kPanelPad + HeadWithBadge(rt, kPanelPad, Tr("Glyph chances"), Tr("Use Apply changes below")) + 16;
        const std::array<std::array<std::string, 3>, 2> glyphs{{
            {"craft_hope", Tr("Glyph of Hope"), Tr("Chance to prevent FP loss. Requires Basic Glyph of Hope.")},
            {"craft_despair", Tr("Glyph of Despair"), Tr("Chance to seal an eligible T1\xE2\x80\x93T4 affix. Other game requirements still apply.")}}};
        struct Made { CardBox box; float natural = 0, fbH = 0; Color fbColor; float fbSize = 10; };
        Made made[2];
        const int sid = sec.id;
        for (int i = 0; i < 2; ++i) {
            GlyphCard& k = view->cards[i];
            k.id = glyphs[i][0];
            const GlyphState now = GlyphNow(k.id);
            k.custom = now.custom;
            k.value = now.value;
            k.feedback = now.feedback;
            Made& m = made[i];
            SettingFeedback(k.id, &m.fbColor, &m.fbSize);
            const float x = kPanelPad + i * (kCardW + kGap);
            m.box = OpenCard(rt, x, top, kCardW);
            void* card = m.box.node.Get();
            // .control-top: h3 (Georgia 20, margin-bottom 12) and the Session badge
            float badgeH = 0;
            const float badgeW = Badge(card, kCardW - kCardPad, kCardPad, LifetimeLabel("session"), &badgeH);
            Style h3{Face::Serif, 20, C("ink")};
            h3.wrap = true;
            const float titleH = Paragraph(card, "Title", kCardPad, kCardPad, kCardInner - badgeW - 14, h3, glyphs[i][1]) + 12;
            float cy = kCardPad + std::max(titleH, badgeH) + 6;
            // .control-description (no min-height in the Crafting content), margin-bottom 14
            Style ds{Face::Body, 12, C("muted")};
            ds.line = 1.65f;
            ds.wrap = true;
            cy += Paragraph(card, "Description", kCardPad, cy, kCardInner, ds, glyphs[i][2]) + 14;
            // label.craft-mode-label: "Chance", then the select (12px, the label's)
            Style ls{Face::Body, 12, C("muted")};
            ls.wrap = true;
            cy += Paragraph(card, "Label", kCardPad, cy, kCardInner, ls, Tr("Chance"));
            Style cs{Face::Body, 12, C("ink")};
            cs.align = kLeft;
            const float selH = SelectHeight(cs);
            const std::vector<std::string> modes{Tr("Game default"), Tr("Custom percentage")};
            Fill(card, "Select", kCardPad, cy, kCardInner, selH, Hex("#080e0e"), 3);
            void* frame = Outline(card, "Select border", kCardPad, cy, kCardInner, selH, Hex("#7b745a99"), 3);
            TextBox(card, "Choice", kCardPad + 11, cy, kCardInner - 40, selH, cs, modes[k.custom ? 1 : 0]);
            Style ar{Face::Body, 11, C("ink")};
            ar.align = kCenter;
            TextBox(card, "Arrow", kCardPad + kCardInner - 26, cy, 16, selH, ar, "\xE2\x8C\x84");
            {
                Hit hit;
                hit.kind = HitKind::Action;
                hit.name = k.id + "-mode";
                hit.x = x + kCardPad;
                hit.y = top + cy;
                hit.w = kCardInner;
                hit.h = selH;
                AddHover(hit, frame, Hex("#7b745a99"), Hex("#7b745a99"));
                const Ctx at = in;
                const float sx = x + kCardPad, sy = top + cy, sw = kCardInner;
                const std::string id = k.id;
                const int current = k.custom ? 1 : 0;
                hit.action = [at, sx, sy, sw, selH, modes, current, id] {
                    float wx = 0, wy = 0;
                    if (!WindowPoint(at, sx, sy, &wx, &wy)) return;
                    OpenList(wx, wy, sw, selH, modes, current, [current, id](int chosen) {
                        if (chosen == current) return;  // the select's change event
                        SetDraft(id, chosen ? 50 : std::nan(""));
                    });
                };
                in.Add(std::move(hit));
            }
            cy += selH;
            m.fbH = k.feedback.empty() ? 0 : LineHeight(Style{Face::Body, m.fbSize});
            if (!k.custom) {
                // p.craft-default: accent 13px, margin 18px 0
                Style ps{Face::Body, 13, C("accent")};
                ps.line = 1.65f;
                ps.wrap = true;
                cy += 18 + Paragraph(card, "Default", kCardPad, cy + 18, kCardInner, ps, Tr("Uses the game\xE2\x80\x99s normal chance.")) + 18;
                // .limits (margin-top 10): the feedback
                if (!k.feedback.empty()) {
                    Style fs{Face::Body, m.fbSize, m.fbColor};
                    TextBox(card, "Feedback", kCardPad, cy + 10, kCardInner, m.fbH, fs, k.feedback);
                }
                m.natural = cy + 10 + m.fbH + kCardPad;
            } else {
                m.natural = cy + 38 + 10 + m.fbH + kCardPad;  // the tools (margin-top auto), the limits
            }
        }
        const float rowH = std::max(made[0].natural, made[1].natural);
        for (int i = 0; i < 2; ++i) {
            GlyphCard& k = view->cards[i];
            Made& m = made[i];
            CloseCard(m.box, kCardW, rowH);
            if (!k.custom) continue;
            void* card = m.box.node.Get();
            const float x = kPanelPad + i * (kCardW + kGap);
            // .control-tools at the bottom (margin-top auto): the range, the stepper (its number
            // input only), "%"; then .limits with the feedback
            const float ty = rowH - kCardPad - m.fbH - 10 - 38;
            Style us{Face::Body, 13, C("gold")};
            us.align = kLeft;
            const float unitW = TextAdvance(Face::Body, 13, "%");
            const float unitX = kCardPad + kCardInner - unitW, stepX = unitX - 10 - 144;
            TextBox(card, "Unit", unitX, ty, unitW + 4, 38, us, "%");
            Outline(card, "Stepper", stepX, ty, 144, 38, C("line"), 3);
            k.trackX = kCardPad + 2;
            k.trackW = stepX - 10 - 2 - k.trackX;
            k.thumbY = ty + 15 + 4 - 22;
            Picture(card, "Track", k.trackX, ty + 15, k.trackW, 8, "track.png", false, kWhite);
            k.thumb.Set(TransformOf(Picture(card, "Thumb", 0, 0, 44, 44, "thumb.png", false, kWhite)));
            PlaceThumb(k, k.value);
            // the number input: 74 wide, padding 5, 15px centred (with the spin buttons' room at its right)
            Style vs{Face::Body, 15, C("ink")};
            vs.align = kCenter;
            InputLook look;
            look.filled = look.framed = false;
            look.padX = 5;
            look.padRight = 5 + 15;
            const std::string id = k.id;
            const Ctx at = c.At(card, kPadX + x, top);
            MakeInput(at, stepX + 1, ty + 4.5f, 74, 29, vs, look, Num(k.value), "", true, [id, sid](const std::string& text) {
                const std::string t = Trim(text);
                // an empty field: the game's default (null), shown once the field lets go
                if (t.empty()) { SetDraft(id, std::nan("")); return; }
                char* end = nullptr;
                const double v = std::strtod(t.c_str(), &end);
                if (!end || *end || !std::isfinite(v) || v < 0 || v > 100) return;  // not valid: the browser keeps the value
                SetDraft(id, v);
                if (Section* s2 = FindSection(sid)) {
                    if (auto view2 = std::static_pointer_cast<GlyphView>(s2->live))
                        for (auto& k2 : view2->cards) if (k2.id == id && k2.thumb) PlaceThumb(k2, v);
                }
            }, id + "-percent");
            // the range: dragged, the thumb follows; let go, the value is the draft (its change event)
            Hit track;
            track.kind = HitKind::Track;
            track.name = id;
            track.x = x + k.trackX - 2;
            track.y = top + ty;
            track.w = k.trackW + 4;
            track.h = 38;
            const float trackW = k.trackW;
            auto dragged = std::make_shared<double>(k.value);
            track.drag = [sid, i, trackW, dragged](float px) {
                const double t = std::clamp((px - 2 - 1 - 9.5) / (trackW - 2 - 19), 0.0, 1.0);
                *dragged = std::round(t * 100);
                if (Section* s2 = FindSection(sid))
                    if (auto view2 = std::static_pointer_cast<GlyphView>(s2->live)) PlaceThumb(view2->cards[i], *dragged);
            };
            track.release = [id, dragged] {
                double v = 0;
                if (!SettingValue(id, &v) || v != *dragged) SetDraft(id, *dragged);
            };
            in.Add(std::move(track));
            if (!k.feedback.empty()) {
                Style fs{Face::Body, m.fbSize, m.fbColor};
                TextBox(card, "Feedback", kCardPad, rowH - kCardPad - m.fbH, kCardInner, m.fbH, fs, k.feedback);
            }
        }
        const float h = top + rowH + kPanelPad;
        ClosePanel(panel, kPanelW, h);
        return h;
    };
    // a draft, Applying…, Applied, a value read: built again (two cards), unless a field has the
    // keyboard (then once it lets go)
    const auto changed = [](Section& sec) {
        if (!GlyphChanged(sec)) return;
        if (Typing(sec.id)) {
            if (auto view = std::static_pointer_cast<GlyphView>(sec.live)) view->stale = true;
            return;
        }
        Rebuild(sec.id);
    };
    s.onSetting = [changed](Section& sec, const std::string&) { changed(sec); };
    s.onData = [changed](Section& sec, const std::string&) { changed(sec); };
    s.tick = [](Section& sec) {
        auto view = std::static_pointer_cast<GlyphView>(sec.live);
        if (view && view->stale && !Typing(sec.id)) Rebuild(sec.id);
    };
}

// Advanced forge options → Craft from EpochPact: the tail of the Level requirement panel.
std::string ForgeShown() {
    const JValue* f = Forge();
    if (!f) return "-";
    return std::string(True(f->Find("canForge")) ? "1" : "0") + (*f)["message"].Str() + "|" + IdOf(f->Path("player.id"));
}
void RegisterForgeTail() {
    Tail tail;
    tail.reads = {"craftread"};
    tail.build = [](Section& s, const Ctx& c) {
        s.state["forge"] = ForgeShown();
        float y = 0;
        y += HeadWithButton(c, y, Tr("Craft from EpochPact"), Tr("Refresh selected craft"), [] { Read("craftread"); }, "craft_read") + 16;
        const JValue* f = Forge();
        const bool ready = f && True(f->Find("canForge"));
        const std::string message = f ? (*f)["message"].Str() : "";
        y += NoteBox(c.parent, kPanelPad, y, kPanelInner,
                     ready ? Tr("The selected craft is ready.")
                           : !message.empty() ? Tr(message) : Tr("Select an item and a shard or rune in the game\xE2\x80\x99s forge, then refresh.")) + 18;
        const Control& ctl = Ctl("craft_forge");
        std::string reason;
        if (Eligible(&reason)) {
            if (!f || IdOf(f->Path("player.id")).empty()) reason = Tr("Refresh live data to enable this action for the loaded character.");
            else if (!ready) reason = !message.empty() ? Tr(message) : Tr("Select an eligible item and material in the game forge, then read live.");
        }
        ActionRun run;
        run.command = ctl.command;
        run.expectId = f ? IdOf(f->Path("player.id")) : "";
        run.confirmTitle = Tr(ctl.label);
        run.confirmMessage = SaveMessage(ctl);
        run.success = [](const JValue&) { return Tr("Item forged once."); };
        run.reread = {"craftread"};
        CardBox card = ActionCard(c, kPanelPad, y, kPanelInner, ctl, ActionCaption(ctl), true, [run] { RunAction(run); }, reason, {}, "", "", 0);
        return y + card.h;
    };
    // built again only when what it shows changed (the read is repeated every second)
    tail.onData = [](Section& s, const std::string&) {
        if (s.state["forge"] != ForgeShown()) RebuildTail(s.id);
    };
    RegisterTail("craft_forge", std::move(tail));
}

// <details class="panel craft-advanced"> Restore game defaults: Restore normal crafting.
void MakeDefaults(Section& s) {
    s.build = [](Section& sec, const Ctx& c) {
        Panel panel = OpenPanel(c.parent, kPadX, 0, kPanelW);
        const Ctx in = c.At(panel.rt.Get(), kPadX, 0);
        const int id = sec.id;
        const bool open = Disclosure("defaults");
        float y = kPanelPad + Details(in, kPanelPad, kPanelPad, Tr("Restore game defaults"), open, [id] {
            Disclosure("defaults") = !Disclosure("defaults");
            Rebuild(id);
        }, 15);
        if (open) {
            const Control& ctl = Ctl("craft_reset");
            std::string reason;
            Eligible(&reason);
            ActionRun run;
            run.command = ctl.command;
            run.success = [](const JValue&) { return Tr("Normal crafting restored."); };
            run.reread = {"craftread"};
            run.done = [](const JValue&) { ClearDrafts("craft_"); };
            y += ActionCard(in, kPanelPad, y, kPanelInner, ctl, ActionCaption(ctl), false, [run] { RunAction(run); }, reason, {}, "Restore normal crafting",
                            "Reset all Forge settings. Crafted items and stocked materials are kept.", 0).h;
        }
        y += kPanelPad;
        ClosePanel(panel, kPanelW, y);
        return y;
    };
}

// --------------------------------------------------------------------------------- Materials
std::string MaterialsShown() {
    const JValue* r = Cached("craftmaterialsread");
    return r ? IdOf(r->Path("player.id")) + "|" + Num((*r)["kinds"]["shards"].Num(0)) + "|" + Num((*r)["kinds"]["runes"].Num(0)) + "|" +
                   Num((*r)["kinds"]["glyphs"].Num(0)) + "|" + (*r)["player"]["name"].Str()
             : "-";
}
void MakeMaterials(Section& s) {
    s.reads.insert("craftmaterialsread");
    s.build = [](Section& sec, const Ctx& c) {
        sec.state["shown"] = MaterialsShown();
        Panel panel = OpenPanel(c.parent, kPadX, 0, kPanelW);
        const Ctx in = c.At(panel.rt.Get(), kPadX, 0);
        void* rt = panel.rt.Get();
        float y = kPanelPad;
        y += HeadWithButton(in, y, Tr("Stock your materials"), Tr("Refresh materials"), [] { Read("craftmaterialsread"); }, "craft_materials_read") + 16;
        // p.muted (its 10px margins: the head's 16 above, the note's 10 below)
        y += Muted(rt, kPanelPad, y, kPanelInner, Tr("Add shards, runes and glyphs to your saved crafting stash in one action.")) + 10;
        const JValue* stock = Cached("craftmaterialsread");
        const bool current = stock && !IdOf(stock->Path("player.id")).empty();
        y += NoteBox(rt, kPanelPad, y, kPanelInner,
                     current ? Fmt("{0} shard types \xC2\xB7 {1} rune types \xC2\xB7 {2} glyph types",
                                   {Num((*stock)["kinds"]["shards"].Num(0)), Num((*stock)["kinds"]["runes"].Num(0)), Num((*stock)["kinds"]["glyphs"].Num(0))})
                             : Tr("Load an offline character and refresh to read your crafting stash.")) + 18;
        const Control& ctl = Ctl("craft_materials");
        Form& form = FormOf("craft_materials");
        if (!form.values.count("value")) form.values["value"] = "10000";
        std::string reason;
        if (Eligible(&reason) && !current) reason = Tr("Refresh live data to enable this action for the loaded character.");
        const std::string owner = current ? IdOf(stock->Path("player.id")) : "", name = current ? (*stock)["player"]["name"].Str() : "";
        const auto submit = [owner, name] {
            const Control& ctl2 = Ctl("craft_materials");
            std::string line, error;
            if (!FormCommand(ctl2, &line, &error)) { Toast(error, true); return; }
            const std::string value = Trim(FormOf("craft_materials").values["value"]);
            ActionRun run;
            run.command = line;
            run.expectId = owner;
            run.confirmTitle = Tr("Stock all materials?");
            run.confirmMessage = Fmt("Character: {0}", {name.empty() ? Tr("Loaded offline character") : name}) + "\n" +
                                 Fmt("Minimum stock: {0} of each shard, rune and glyph.", {Grouped(std::strtod(value.c_str(), nullptr))}) + "\n\n" +
                                 Tr("Higher quantities are kept. Adds materials once to the shared crafting stash and creates a recovery backup. This does not "
                                    "enable infinite materials.");
            run.success = [value](const JValue& v) {
                return Fmt("{0} material types stocked to at least {1}. Saved in your crafting stash.",
                           {Num(v["updatedKinds"].Num(0)), v["target"].IsNumber() ? Num(v["target"].number) : value});
            };
            run.reread = {"craftmaterialsread"};
            RunAction(run);
        };
        // the field: label, the number input (the typed value kept), and its span.note (margin-bottom 18)
        const Fields fields = [](const Ctx& fc, float fx, float fy, float fw) {
            const float colW = (fw - 14) / 2;
            float h = InputField(fc, fx, fy, colW, Tr("Minimum quantity per material"), FormOf("craft_materials").values["value"], "", true,
                                 [](const std::string& text) { FormOf("craft_materials").values["value"] = text; }, "craft_materials-value");
            h += 8 + NoteBox(fc.parent, fx, fy + h + 8, colW, Tr("Higher existing quantities are kept. Range: 1\xE2\x80\x93" "1,000,000 per material.")) + 18;
            return h;
        };
        y += ActionCard(in, kPanelPad, y, kPanelInner, ctl, ActionCaption(ctl), true, submit, reason, fields, "Minimum stock for every material",
                        "Raises each material to your chosen minimum. Higher quantities are kept. This adds a saved quantity once; it does not enable infinite "
                        "materials.", 0).h;
        y += 10 + Muted(rt, kPanelPad, y + 10, kPanelInner, Tr("To keep materials while crafting, use Preserve materials in Forge Settings.")) + 10;
        y += kPanelPad;
        ClosePanel(panel, kPanelW, y);
        return y;
    };
    s.onData = [](Section& sec, const std::string&) {
        if (sec.state["shown"] != MaterialsShown()) Rebuild(sec.id);
    };
}
}  // namespace

void RegisterCraftSections() {
    RegisterSection("craft_tabs", MakeTabs);
    RegisterSection("craft_note", MakeForgeNote);
    RegisterSection("glyph_chances", MakeGlyphChances);
    RegisterSection("craft_defaults", MakeDefaults);
    RegisterSection("craft_materials", MakeMaterials);
    RegisterForgeTail();
}

}  // namespace ep::ingame::view
