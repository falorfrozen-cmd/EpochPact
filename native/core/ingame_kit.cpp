// Widgets of the in-game window, drawn as ui/style.css draws their HTML. See ingame_window.hpp.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace ep::ingame::view {
using namespace managed;

void AddHover(Hit& h, void* graphic, Color normal, Color hover, float seconds) {
    Fade(graphic, normal, 0);
    h.hover.push_back({graphic, normal, hover, seconds});
}
int Ctx::Add(Hit h) const {
    h.x += ox;
    h.y += oy;
    h.area = area;
    hits->push_back(std::move(h));
    return static_cast<int>(hits->size()) - 1;
}

// ---------------------------------------------------------------------------------- buttons
// button: the gradient (none for .quiet), a 1px border, radius 3; hover: accent border, soft
// background under the gradient, in .15s. .primary: Georgia 17, its own gradient, accent border.
Button MakeButton(void* parent, const std::string& label, Look look, Hit& hit) {
    Style s{Face::Body, 14.f, C("ink")};
    float padX = 15, padY = 10;
    if (look == Look::Quiet) { s.size = 12; padX = 12; padY = 9; }
    if (look == Look::Small) { s.size = 11; padX = 10; padY = 8; }
    if (look == Look::Primary) { s = Style{Face::Serif, 17, Hex("#fff0d3")}; padX = 25; padY = 12; }
    s.align = kCenter;
    Button b;
    b.node = New("Button", parent, 0, 0, 10, 10);
    void* soft = look == Look::Primary ? nullptr : Fill(b.node.rt, "Hover", 1, 1, 8, 8, kWhite, 2);
    void* grad = look == Look::Quiet ? nullptr : Picture(b.node.rt, "Gradient", 1, 1, 8, 8, look == Look::Primary ? "grad-primary.png" : "grad-button.png", false, kWhite);
    void* ring = look == Look::Primary ? Picture(b.node.rt, "Ring", 1, 1, 8, 8, "band-2.png", true, Hex("#b9955133"), true) : nullptr;
    void* frame = Outline(b.node.rt, "Border", 0, 0, 10, 10, kWhite, 3);
    void* text = TextBox(b.node.rt, "Label", 0, 0, 10, 10, s, label);
    b.w = std::ceil(TextWidth(text)) + 2 * padX + 2;
    b.h = LineHeight(s) + 2 * padY + 2;
    Resize(b.node.rt, b.w, b.h);
    for (void* g : {soft, grad, ring}) if (g) Resize(TransformOf(g), b.w - 2, b.h - 2);
    Resize(TransformOf(frame), b.w, b.h);
    Resize(TransformOf(text), b.w, b.h);
    if (soft) AddHover(hit, soft, Alpha(C("soft"), 0), C("soft"));
    if (look == Look::Primary) Fade(frame, C("accent"), 0);
    else AddHover(hit, frame, C("line"), C("accent"));
    hit.w = b.w;
    hit.h = b.h;
    return b;
}
Button AddButton(const Ctx& c, float x, float y, const std::string& label, Look look, Action onClick, bool right, const std::string& name) {
    Hit h;
    h.kind = HitKind::Action;
    h.action = std::move(onClick);
    h.name = name.empty() ? label : name;
    Button b = MakeButton(c.parent, label, look, h);
    const float bx = right ? x - b.w : x;
    Move(b.node.rt, bx, y);
    h.x = bx;
    h.y = y;
    b.hit = c.Add(std::move(h));
    return b;
}
void SetEnabled(const Ctx& c, Button& b, bool on) {
    if (!b.group) b.group.Set(Add(b.node.go, "UnityEngine.UIModule.dll", "UnityEngine", "CanvasGroup"));
    float alpha = on ? 1.f : 0.45f;
    void* a[]{&alpha};
    Invoke(Find("UnityEngine.UIModule.dll", "UnityEngine", "CanvasGroup", "set_alpha", 1), b.group.Get(), a);
    if (b.hit >= 0) (*c.hits)[b.hit].enabled = on;
}
void SetSelected(const Ctx& c, Button& b, bool on) {
    if (b.hit < 0) return;
    Hit& h = (*c.hits)[b.hit];
    if (h.hover.size() < 2) return;
    h.hover[0].normal = on ? C("soft") : Alpha(C("soft"), 0);
    h.hover[1].normal = on ? C("accent") : C("line");
    Fade(h.hover[0].graphic, h.hover[0].normal, 0);
    Fade(h.hover[1].graphic, h.hover[1].normal, 0);
}

// .badge: 10px uppercase accent text, letter-spacing .7, 1px line border, radius 2, padding 5/7.
float Badge(void* parent, float right, float y, const std::string& text, float* height) {
    Style bs{Face::Body, 10, C("accent")};
    bs.spacing = 0.7f;
    bs.upper = true;
    bs.align = kCenter;
    void* t = TextBox(parent, "Badge", 0, 0, 10, 10, bs, text);
    const float w = std::ceil(TextWidth(t)) + 16, h = LineHeight(bs) + 12, x = right - w;
    Outline(parent, "Badge border", x, y, w, h, C("line"), 2);
    void* rt = TransformOf(t);
    Move(rt, x, y);
    Resize(rt, w, h);
    if (height) *height = h;
    return w;
}

// .panel: the border, a gradient over the panel colour, the 4px inset shadow, two gold corners.
// Pinned: a settings panel is filled a row of cards per frame, and between frames the game's GC
// may collect the managed wrapper of a native component (RectTransform) nothing else roots.
Panel OpenPanel(void* parent, float x, float y, float w) {
    Panel p;
    Node n = New("Panel", parent, x, y, w, 10);
    p.rt.Set(n.rt);
    p.parts[0].Set(TransformOf(Fill(n.rt, "Background", 0, 0, w, 10, C("panel"))));
    p.parts[1].Set(TransformOf(Picture(n.rt, "Gradient", 0, 0, w, 10, "grad-panel.png", false, kWhite)));
    p.parts[2].Set(TransformOf(Picture(n.rt, "Inset", 1, 1, w - 2, 8, "band-4.png", true, Hex("#02070466"), true)));
    p.parts[3].Set(TransformOf(Outline(n.rt, "Border", 0, 0, w, 10, C("line"))));
    return p;
}
void ClosePanel(Panel& p, float w, float h) {
    Resize(p.rt.Get(), w, h);
    Resize(p.parts[0].Get(), w, h);
    Resize(p.parts[1].Get(), w, h);
    Resize(p.parts[2].Get(), w - 2, h - 2);
    Resize(p.parts[3].Get(), w, h);
    // a panel that grows again (a live table) keeps its corners
    if (p.corner) { Move(p.corner.Get(), w - 5, h - 5); return; }
    Picture(p.rt.Get(), "Corner", 5, 5, 13, 13, "corner.png", false, C("gold"));
    void* br = Picture(p.rt.Get(), "Corner", w - 5, h - 5, 13, 13, "corner.png", false, C("gold"));
    Turn(TransformOf(br), 180);
    p.corner.Set(TransformOf(br));
}

// -------------------------------------------------------------------------------------- texts
float H2(void* parent, float x, float y, float w, const std::string& text) {
    Style h2{Face::Serif, 27, C("ink")};
    h2.wrap = true;
    return Paragraph(parent, "Heading", x, y, w, h2, text);
}
float H3(void* parent, float x, float y, float w, const std::string& text) {
    Style h3{Face::Serif, 20, C("ink")};
    h3.wrap = true;
    return Paragraph(parent, "Heading", x, y, w, h3, text);
}
float Muted(void* parent, float x, float y, float w, const std::string& text, float size, float line) {
    Style s{Face::Body, size, C("muted")};
    s.line = line;
    s.wrap = true;
    return Paragraph(parent, "Text", x, y, w, s, text);
}
// p.note / .note: padding 12px 16px, a 2px accent bar, soft background, 12px muted, line-height 1.7
float NoteBox(void* parent, float x, float y, float w, const std::string& text, NoteParts* parts) {
    Node note = New("Note", parent, x, y, w, 10);
    void* bg = Fill(note.rt, "Background", 0, 0, w, 10, C("soft"));
    void* bar = Fill(note.rt, "Bar", 0, 0, 2, 10, C("accent"));
    Style ns{Face::Body, 12, C("muted")};
    ns.line = 1.7f;
    ns.wrap = true;
    void* t = TextBox(note.rt, "Text", 18, 12, w - 34, LineHeight(ns), ns, text);
    const float h = 12 + (text.empty() ? 0 : TextLines(t) * LineHeight(ns)) + 12;  // an empty p has no line
    Resize(TransformOf(bg), w, h);
    Resize(TransformOf(bar), 2, h);
    Resize(note.rt, w, h);
    if (parts) {
        parts->root.Set(note.rt);
        parts->bg.Set(TransformOf(bg));
        parts->bar.Set(TransformOf(bar));
        parts->text.Set(t);
        parts->w = w;
        parts->h = h;
    }
    return h;
}
float SetNote(NoteParts& n, const std::string& text) {
    Style ns{Face::Body, 12, C("muted")};
    ns.line = 1.7f;
    SetText(n.text.Get(), text);
    n.h = 12 + (text.empty() ? 0 : TextLines(n.text.Get()) * LineHeight(ns)) + 12;
    Resize(n.bg.Get(), n.w, n.h);
    Resize(n.bar.Get(), 2, n.h);
    Resize(n.root.Get(), n.w, n.h);
    return n.h;
}
// .empty: padding 25px, centred, muted
float EmptyText(void* parent, float x, float y, float w, const std::string& text) {
    Style s{Face::Body, 14, C("muted")};
    s.align = kTop;
    s.wrap = true;
    s.line = 1.65f;
    return 25 + Paragraph(parent, "Empty", x + 25, y + 25, w - 50, s, text) + 25;
}
float SectionHead(const Ctx& c, float x, float y, float w, const std::string& title, const std::vector<std::pair<std::string, Action>>& buttons) {
    Style h2{Face::Serif, 27, C("ink")};
    const float lh = LineHeight(h2);
    // the buttons, right to left (.actions gap 10), centred on the heading line
    float right = x + w, tallest = lh;
    std::vector<Button> made;
    for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
        Button b = AddButton(c, right, y, it->first, Look::Normal, it->second, true);
        right -= b.w + 10;
        tallest = std::max(tallest, b.h);
        made.push_back(std::move(b));
    }
    for (auto& b : made) {
        const float by = y + (tallest - b.h) / 2;
        Move(b.node.rt, (*c.hits)[b.hit].x - c.ox, by);
        (*c.hits)[b.hit].y = c.oy + by;
    }
    TextBox(c.parent, "Title", x, y + (tallest - lh) / 2, right - x - 15, lh, h2, title);
    return tallest;
}
float SummaryTiles(void* parent, float x, float y, float w, const std::vector<std::pair<std::string, std::string>>& tiles, int columns) {
    const float tileW = (w - (columns - 1) * 14) / columns;
    Style strong{Face::Serif, 27, C("accent")}, label{Face::Body, 11, C("muted")};
    const float h = 1 + 18 + LineHeight(strong) + 8 + LineHeight(label) + 18 + 1;
    for (size_t i = 0; i < tiles.size(); ++i) {
        const float tx = x + (i % columns) * (tileW + 14), ty = y + (i / columns) * (h + 14);
        Node tile = New("Summary", parent, tx, ty, tileW, h);
        Fill(tile.rt, "Background", 0, 0, tileW, h, C("panel"));
        Outline(tile.rt, "Border", 0, 0, tileW, h, C("line"));
        TextBox(tile.rt, "Value", 19, 19, tileW - 38, LineHeight(strong), strong, tiles[i].first);
        TextBox(tile.rt, "Label", 19, 19 + LineHeight(strong) + 8, tileW - 38, LineHeight(label), label, tiles[i].second);
    }
    const int rows = static_cast<int>((tiles.size() + columns - 1) / columns);
    return rows * h + (rows > 0 ? (rows - 1) * 14 : 0);
}

// ------------------------------------------------------------------------------------ fields
namespace {
float FieldLabel(void* parent, float x, float y, float w, const std::string& label) {
    if (label.empty()) return 0;
    Style s{Face::Body, 12, C("muted")};
    s.wrap = true;
    return Paragraph(parent, "Label", x, y, w, s, label) + 8;  // .form-grid label: column, gap 8
}
}  // namespace
// input, select: #080e0e, border #7b745a99, radius 3, padding 10; the font of the label (12px)
float SelectField(const Ctx& c, float x, float y, float w, const std::string& label, const std::vector<std::string>& options, int current,
                  std::function<void(int)> chosen, const std::string& name) {
    const float top = FieldLabel(c.parent, x, y, w, label);
    Style s{Face::Body, 12, C("ink")};
    s.align = kLeft;
    const float h = LineHeight(s) + 22;
    Fill(c.parent, "Select", x, y + top, w, h, Hex("#080e0e"), 3);
    void* frame = Outline(c.parent, "Select border", x, y + top, w, h, Hex("#7b745a99"), 3);
    TextBox(c.parent, "Choice", x + 11, y + top, w - 40, h, s, current >= 0 && current < static_cast<int>(options.size()) ? options[current] : "");
    Style ar{Face::Body, 11, C("ink")};
    ar.align = kCenter;
    TextBox(c.parent, "Arrow", x + w - 26, y + top, 16, h, ar, "\xE2\x8C\x84");
    Hit hit;
    hit.kind = HitKind::Action;
    hit.name = name;
    hit.x = x;
    hit.y = y + top;
    hit.w = w;
    hit.h = h;
    AddHover(hit, frame, Hex("#7b745a99"), Hex("#7b745a99"));
    Ctx at = c;
    hit.action = [at, x, y, top, w, h, options, current, chosen]() {
        float wx = 0, wy = 0;
        if (WindowPoint(at, x, y + top, &wx, &wy)) OpenList(wx, wy, w, h, options, current, chosen);
    };
    c.Add(std::move(hit));
    return top + h;
}
float InputField(const Ctx& c, float x, float y, float w, const std::string& label, const std::string& value, const std::string& placeholder,
                 bool number, std::function<void(const std::string&)> onChange, const std::string& name) {
    const float top = FieldLabel(c.parent, x, y, w, label);
    Style s{Face::Body, 12, C("ink")};
    s.align = kLeft;
    const float h = LineHeight(s) + 22;
    Node box = New(name.empty() ? "Input" : name.c_str(), c.parent, x, y + top, w, h);
    // its own background takes the pointer: TMP_InputField places the caret and selects text
    void* bg = Add(box.go, "UnityEngine.UI.dll", "UnityEngine.UI", "Image");
    void* sa[]{assets::Sprite("fill-r3.png", true)};
    Invoke(UI("Image", "set_sprite", 1), bg, sa);
    int sliced = 1;
    void* ta[]{&sliced};
    Invoke(UI("Image", "set_type", 1), bg, ta);
    Tint(bg, Hex("#080e0e"));
    Raycast(bg, true);
    Outline(box.rt, "Border", 0, 0, w, h, Hex("#7b745a99"), 3);
    Node area = New("Text Area", box.rt, 11, 0, w - 22, h);
    Add(area.go, "UnityEngine.UI.dll", "UnityEngine.UI", "RectMask2D");
    Style ps = s;
    ps.color = Hex("#8b8f8c");
    void* hint = TextBox(area.rt, "Placeholder", 0, 0, w - 22, h, ps, placeholder);
    void* text = TextBox(area.rt, "Text", 0, 0, w - 22, h, s, "");
    void* field = Add(box.go, "Unity.TextMeshPro.dll", "TMPro", "TMP_InputField");
    const auto call = [&](const char* method, void* arg) {
        void* a[]{arg};
        Invoke(Find("Unity.TextMeshPro.dll", "TMPro", "TMP_InputField", method, 1), field, a);
    };
    int none = 0;
    void* transition[]{&none};
    Invoke(Find("UnityEngine.UI.dll", "UnityEngine.UI", "Selectable", "set_transition", 1), field, transition);
    call("set_textViewport", area.rt);
    call("set_textComponent", text);
    call("set_placeholder", hint);
    float size = 12;
    call("set_pointSize", &size);
    int contentType = number ? 3 : 0, lineType = 0;  // DecimalNumber / Standard; SingleLine
    call("set_contentType", &contentType);
    call("set_lineType", &lineType);
    bool off = false, yes = true;
    call("set_richText", &off);
    call("set_customCaretColor", &yes);
    Color caret = C("ink"), selection = Alpha(C("accent"), 0.35f);
    call("set_caretColor", &caret);
    call("set_selectionColor", &selection);
    Root valueText(il2cpp::api().string_new(value.c_str()));
    call("set_text", valueText.Get());
    WatchInput(c, field, value, std::move(onChange), name);
    // A click gives it the keyboard (TMP_InputField also takes the game's own pointer events,
    // which place the caret): it becomes the EventSystem's selected object and is activated.
    Hit hit;
    hit.kind = HitKind::Input;
    hit.name = name;
    hit.x = x;
    hit.y = y + top;
    hit.w = w;
    hit.h = h;
    void* go = box.go;
    hit.action = [field, go] {
        void* system = Invoke(Find("UnityEngine.UI.dll", "UnityEngine.EventSystems", "EventSystem", "get_current", 0));
        if (system) {
            void* a[]{go};
            Invoke(Sig("UnityEngine.UI.dll", "UnityEngine.EventSystems", "EventSystem", "SetSelectedGameObject", {"UnityEngine.GameObject"}), system, a);
        }
        Invoke(Find("Unity.TextMeshPro.dll", "TMPro", "TMP_InputField", "ActivateInputField", 0), field);
    };
    c.Add(std::move(hit));
    return top + h;
}
// .inline-check: flex, gap 8, centred, 12px; the 18px checkbox (accent-color accent)
float CheckField(const Ctx& c, float x, float y, const std::string& label, bool checked, std::function<void(bool)> changed, const std::string& name, float size,
                 float gap, float* width) {
    void* box = Fill(c.parent, "Check", x, y, 18, 18, checked ? C("accent") : Hex("#0f1513"), 3);
    void* frame = Outline(c.parent, "Check border", x, y, 18, 18, checked ? C("accent") : Hex("#7b745a99"), 3);
    (void)box;
    if (checked) {
        Style m{Face::Body, 13, C("bg")};
        m.align = kCenter;
        TextBox(c.parent, "Check mark", x, y, 18, 18, m, "\xE2\x9C\x93");
    }
    Style s{Face::Body, size, C("ink")};
    s.align = kLeft;
    const float lineH = std::max(18.f, LineHeight(s));
    void* t = TextBox(c.parent, "Check label", x + 18 + gap, y + (18 - lineH) / 2, 600, lineH, s, label);
    Hit hit;
    hit.kind = HitKind::Action;
    hit.name = name.empty() ? label : name;
    hit.x = x;
    hit.y = y;
    hit.w = 18 + gap + std::ceil(TextWidth(t));
    hit.h = 18;
    if (width) *width = hit.w;
    AddHover(hit, frame, checked ? C("accent") : Hex("#7b745a99"), C("accent"));
    hit.action = [changed, checked] { changed(!checked); };
    c.Add(std::move(hit));
    return 18;
}
// table: 12px, th gold 500, th/td padding 10px 12px, border-bottom 1px #6c755144
float Table(void* parent, float x, float y, float w, const std::vector<std::string>& head, const std::vector<std::vector<std::string>>& rows,
            const std::vector<float>& widths, float size) {
    std::vector<float> cols(head.size(), w / std::max<size_t>(1, head.size()));
    if (widths.size() == head.size()) for (size_t i = 0; i < cols.size(); ++i) cols[i] = widths[i] * w;
    Style th{Face::Body, size, C("gold")}, td{Face::Body, size, C("ink")};
    th.wrap = td.wrap = true;
    float cy = y;
    const auto row = [&](const std::vector<std::string>& cells, const Style& s) {
        float cx = x, rowH = LineHeight(s);
        for (size_t i = 0; i < cols.size() && i < cells.size(); ++i) {
            rowH = std::max(rowH, Paragraph(parent, "Cell", cx + 12, cy + 10, cols[i] - 24, s, cells[i]));
            cx += cols[i];
        }
        Solid(parent, "Row border", x, cy + rowH + 20, w, 1, Hex("#6c755144"));
        cy += rowH + 21;
    };
    row(head, th);
    for (const auto& r : rows) row(r, td);
    return cy - y;
}
float Details(const Ctx& c, float x, float y, const std::string& summary, bool open, Action toggle) {
    Style s{Face::Body, 12, C("gold")};
    s.align = kLeft;
    const float lh = LineHeight(s);
    Style m{Face::Body, 8, C("gold")};
    m.align = kLeft;
    TextBox(c.parent, "Marker", x, y, 12, lh, m, open ? "\xE2\x96\xBC" : "\xE2\x96\xB6");
    void* t = TextBox(c.parent, "Summary", x + 14, y, 900, lh, s, summary);
    Hit hit;
    hit.kind = HitKind::Action;
    hit.name = summary;
    hit.x = x;
    hit.y = y;
    hit.w = 14 + std::ceil(TextWidth(t));
    hit.h = lh;
    hit.action = std::move(toggle);
    c.Add(std::move(hit));
    return lh;
}

// ------------------------------------------------------------------------------------- cards
CardBox OpenCard(void* parent, float x, float y, float w) {
    CardBox b;
    Node n = New("Card", parent, x, y, w, 10);
    b.node.Set(n.rt);
    b.grad.Set(TransformOf(Picture(n.rt, "Gradient", 0, 0, w, 10, "grad-card.png", false, kWhite)));
    b.frame.Set(TransformOf(Outline(n.rt, "Border", 0, 0, w, 10, Hex("#5e644e77"), 4)));
    return b;
}
void CloseCard(CardBox& b, float w, float h) {
    b.h = h;
    Resize(b.node.Get(), w, h);
    Resize(b.frame.Get(), w, h);
    Resize(b.grad.Get(), w, h);
}
std::string LifetimeLabel(const std::string& lifetime) { return Tr(D()["lifetimes"][lifetime].Str(lifetime)); }
std::string ActionCaption(const Control& c) {
    if (c.id == "craft_materials") return "Stock all materials";
    if (c.id == "craft_forge") return "Craft once";
    if (c.id == "craft_reset") return "Restore game defaults";
    if (c.id == "mastery_choose") return "Choose Mastery";
    if (c.id == "loot_affixes" || c.id == "weaver_affixes") return "Apply selection";
    if (c.widget == "read") return "Read live";
    if (c.lifetime == "game_save") return "Apply action";
    if (c.widget == "preview") return "Preview";
    return "Apply";
}
CardBox ActionCard(const Ctx& c, float x, float y, float w, const Control& control, const std::string& caption, bool primary, Action submit,
                   const std::string& reason, Fields fields, const std::string& title, const std::string& description) {
    CardBox b = OpenCard(c.parent, x, y, w);
    void* rt = b.node.Get();
    const Ctx in = c.At(rt, x, y);
    const float pad = 20, inner = w - 2 * pad;
    // .control-top: h3.control-title (Georgia 15, weight 600, line-height 1.45, margin-bottom 8) and the badge
    float badgeH = 0;
    const float badgeW = Badge(rt, w - pad, pad, LifetimeLabel(control.lifetime), &badgeH);
    Style ts{Face::SerifBold, 15, C("ink")};
    ts.line = 1.45f;
    ts.wrap = true;
    const float titleH = Paragraph(rt, "Title", pad, pad, inner - badgeW - 14, ts, Tr(title.empty() ? control.label : title)) + 8;
    float cy = pad + std::max(titleH, badgeH);
    // .control-description: margin 6px 0 14px, min-height 40px
    Style ds{Face::Body, 12, C("muted")};
    ds.line = 1.65f;
    ds.wrap = true;
    cy += 6;
    cy += std::max(40.f, Paragraph(rt, "Description", pad, cy, inner, ds, Tr(description.empty() ? control.description : description))) + 14;
    if (fields) cy += fields(in, pad, cy, inner);
    if (!reason.empty()) {
        // .action-reason: gold 12px, line-height 1.6, margin-top 12
        Style rs{Face::Body, 12, C("gold")};
        rs.line = 1.6f;
        rs.wrap = true;
        cy += 12 + Paragraph(rt, "Reason", pad, cy + 12, inner, rs, reason);
    }
    // .actions: margin-top 18; a .primary button sits at the right (margin-left auto)
    cy += 18;
    Button button = AddButton(in, primary ? pad + inner : pad, cy, Tr(caption), primary ? Look::Primary : Look::Normal, std::move(submit), primary,
                              control.id);
    if (!reason.empty()) SetEnabled(in, button, false);
    cy += button.h + pad;
    CloseCard(b, w, cy);
    return b;
}
float CardGrid(const Ctx& c, float x, float y, float w, const std::vector<CardMaker>& makers) {
    const float colW = (w - 15) / 2;
    float cy = y;
    for (size_t i = 0; i < makers.size(); i += 2) {
        std::vector<CardBox> row;
        row.push_back(makers[i](c, x, cy, colW));
        if (i + 1 < makers.size()) row.push_back(makers[i + 1](c, x + colW + 15, cy, colW));
        float rowH = 0;
        for (const auto& b : row) rowH = std::max(rowH, b.h);
        for (auto& b : row) if (b.h < rowH) CloseCard(b, colW, rowH);
        cy += rowH + 15;
    }
    return makers.empty() ? 0 : cy - 15 - y;
}

// ---------------------------------------------------------------------------------- live table
namespace {
uint32_t Pack(Color c) {
    const auto b = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
    return b(c.r) << 24 | b(c.g) << 16 | b(c.b) << 8 | b(c.a);
}
}  // namespace
// table: th/td padding (padY, pad), border-bottom 1px #6c755144, th gold; single-line cells
std::shared_ptr<LiveTable> MakeLiveTable(void* parent, float x, float y, float w, const std::vector<std::string>& head, const std::vector<float>& widths,
                                         float size, float pad, float padY) {
    auto t = std::make_shared<LiveTable>();
    Node root = New("Table", parent, x, y, w, 10);
    t->root.Set(root.rt);
    t->w = w;
    t->size = size;
    t->pad = pad;
    for (size_t i = 0; i < head.size(); ++i) t->cols.push_back((i < widths.size() ? widths[i] : 1.f / head.size()) * w);
    Style th{Face::Body, size, C("gold")};
    th.align = kLeft;
    t->rowH = LineHeight(th) + 2 * padY + 1;
    t->headH = t->rowH;
    float cx = 0;
    for (size_t i = 0; i < head.size(); ++i) {
        t->head.emplace_back(TextBox(root.rt, "Head", cx + pad, 0, t->cols[i] - 2 * pad, t->rowH - 1, th, head[i]));
        cx += t->cols[i];
    }
    Solid(root.rt, "Head border", 0, t->rowH - 1, w, 1, Hex("#6c755144"));
    Resize(root.rt, w, t->headH);
    t->rows.reserve(64);
    // pad is kept in the first row's cell offsets
    t->rows.clear();
    t->visible = 0;
    (void)pad;
    return t;
}
float LiveTableHeight(const LiveTable& t) { return t.headH + t.visible * t.rowH; }
std::vector<float> AutoColumns(float w, const std::vector<std::string>& head, const std::vector<std::vector<std::string>>& rows, float size, float pad,
                               float first) {
    const auto plain = [](const std::string& s) {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '<') {
                const size_t end = s.find('>', i);
                if (end != std::string::npos) { i = end; continue; }
            }
            out += s[i];
        }
        return out;
    };
    std::vector<float> widths(head.size(), 0);
    for (size_t i = 0; i < head.size(); ++i) widths[i] = TextAdvance(Face::Body, size, head[i]) + 2 * pad;
    for (const auto& row : rows)
        for (size_t i = 0; i < row.size() && i < widths.size(); ++i) widths[i] = std::max(widths[i], TextAdvance(Face::Body, size, plain(row[i])) + 2 * pad);
    if (!widths.empty()) widths[0] = std::max(widths[0], first);
    float total = 0;
    for (float x : widths) total += std::ceil(x);
    for (auto& x : widths) x = std::ceil(x);
    if (total < w && total > 0) {
        const float extra = w - total;
        for (auto& x : widths) x += extra * x / total;
    }
    return widths;
}
void SetColumns(LiveTable& t, const std::vector<float>& widths) {
    if (widths.size() != t.cols.size()) return;
    bool same = true;
    for (size_t i = 0; i < widths.size(); ++i) same &= std::fabs(widths[i] - t.cols[i]) < 0.5f;
    if (same) return;
    t.cols = widths;
    const auto place = [&](const std::vector<Pin>& cells) {
        float cx = 0;
        for (size_t i = 0; i < cells.size() && i < t.cols.size(); ++i) {
            void* rt = TransformOf(cells[i].Get());
            Move(rt, cx + t.pad, 0);
            Resize(rt, t.cols[i] - 2 * t.pad, t.rowH - 1);
            cx += t.cols[i];
        }
    };
    place(t.head);
    for (const auto& row : t.rows) place(row.cells);
}
float UpdateLiveTable(LiveTable& t, const std::vector<std::vector<std::string>>& rows, const std::vector<std::vector<Color>>& colors, int maxNew,
                      const std::vector<bool>& strong) {
    const float pad = t.pad;
    Style td{Face::Body, t.size, C("ink")};
    td.align = kLeft;
    t.pending = false;
    size_t count = rows.size();
    for (size_t r = 0; r < count; ++r) {
        if (r >= t.rows.size()) {
            if (maxNew == 0) { t.pending = true; count = r; break; }
            if (maxNew > 0) --maxNew;
            LiveTable::Row row;
            Node n = New("Row", t.root.Get(), 0, t.headH + r * t.rowH, t.w, t.rowH);
            row.node.Set(n.rt);
            float cx = 0;
            for (size_t i = 0; i < t.cols.size(); ++i) {
                void* cell = TextBox(n.rt, "Cell", cx + pad, 0, t.cols[i] - 2 * pad, t.rowH - 1, td, "");
                int ellipsis = 1;
                void* a[]{&ellipsis};
                managed::Invoke(T("set_overflowMode", 1), cell, a);
                if (t.rich) { bool on = true; void* ra[]{&on}; managed::Invoke(T("set_richText", 1), cell, ra); }
                row.cells.emplace_back(cell);
                row.shown.emplace_back();
                row.colors.push_back(Pack(td.color));
                cx += t.cols[i];
            }
            Solid(n.rt, "Row border", 0, t.rowH - 1, t.w, 1, Hex("#6c755144"));
            t.rows.push_back(std::move(row));
        }
        LiveTable::Row& row = t.rows[r];
        if (static_cast<int>(r) >= t.visible) SetActive(GameObjectOf(row.node.Get()), true);
        const bool bold = r < strong.size() && strong[r];
        if (bold != row.strong) {
            row.strong = bold;
            void* fa[]{assets::Font(bold ? Face::BodySemi : Face::Body)};
            for (const auto& cell : row.cells) managed::Invoke(T("set_font", 1), cell.Get(), fa);
            if (bold && !row.topLine) row.topLine.Set(TransformOf(Solid(row.node.Get(), "Total line", 0, 0, t.w, 1, Hex("#92734080"))));
            if (row.topLine) SetActive(GameObjectOf(row.topLine.Get()), bold);
        }
        for (size_t i = 0; i < row.cells.size(); ++i) {
            const std::string& text = i < rows[r].size() ? rows[r][i] : std::string();
            if (row.shown[i] != text) { SetText(row.cells[i].Get(), text); row.shown[i] = text; }
            const Color color = r < colors.size() && i < colors[r].size() ? colors[r][i] : C("ink");
            if (row.colors[i] != Pack(color)) { Ink(row.cells[i].Get(), color); row.colors[i] = Pack(color); }
        }
    }
    for (size_t r = count; r < static_cast<size_t>(t.visible) && r < t.rows.size(); ++r) SetActive(GameObjectOf(t.rows[r].node.Get()), false);
    t.visible = static_cast<int>(count);
    Resize(t.root.Get(), t.w, LiveTableHeight(t));
    return LiveTableHeight(t);
}

// ------------------------------------------------------------------------------------ list box
// select[size] with a CSS height: #080e0e, border #7b745a99, radius 3; options padding 5px 8px in
// the label's 12px; the selected option highlighted (accent on soft). A row cut by the bottom edge
// shows in part, as in the browser.
std::shared_ptr<ListBox> MakeListBox(Section& s, const Ctx& c, float x, float y, float w, float h, const std::vector<std::string>& items, int selected,
                                     std::function<void(int)> onSelect, std::function<void(int)> onActivate, const std::string& name) {
    auto b = std::make_shared<ListBox>();
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    b->items = items;
    b->selected = selected;
    b->section = s.id;
    b->onSelect = std::move(onSelect);
    b->onActivate = std::move(onActivate);
    Style os{Face::Body, 12, C("ink")};
    b->rowH = LineHeight(os) + 10;
    b->rows = static_cast<int>(std::ceil((h - 2) / b->rowH));
    b->full = static_cast<int>(std::floor((h - 2) / b->rowH));
    Node root = New("List", c.parent, x, y, w, h);
    b->root.Set(root.rt);
    Fill(root.rt, "Background", 0, 0, w, h, Hex("#080e0e"), 3);
    Outline(root.rt, "Border", 0, 0, w, h, Hex("#7b745a99"), 3);
    Node view = New("Options", root.rt, 1, 1, w - 2, h - 2);
    Clip(view);
    if (selected >= b->full) b->offset = std::min(selected - b->full / 2, static_cast<int>(items.size()) - b->full);
    b->offset = std::max(0, b->offset);
    os.align = kLeft;
    std::weak_ptr<ListBox> weak = b;
    const float rowW = w - 2 - 10;  // room for the thumb
    for (int i = 0; i < b->rows; ++i) {
        const float ry = i * b->rowH;
        b->fills.emplace_back(Fill(view.rt, "Row", 0, ry, rowW, b->rowH, kWhite));
        void* text = TextBox(view.rt, "Option", 8, ry, rowW - 16, b->rowH, os, "");
        int ellipsis = 1;
        void* a[]{&ellipsis};
        managed::Invoke(T("set_overflowMode", 1), text, a);
        b->texts.emplace_back(text);
        Hit hit;
        hit.kind = HitKind::Action;
        hit.name = name + "#" + std::to_string(i);
        hit.x = x + 1;
        hit.y = y + 1 + ry;
        hit.w = rowW;
        hit.h = std::min(b->rowH, h - 2 - ry);
        hit.action = [weak, i] {
            auto box = weak.lock();
            if (!box) return;
            const int index = box->offset + i;
            if (index >= static_cast<int>(box->items.size())) return;
            const uint64_t now = GetTickCount64();
            const bool twice = box->lastClick == index && now - box->lastClickAt < 450;
            box->lastClick = twice ? -1 : index;
            box->lastClickAt = now;
            box->selected = index;
            // a row cut by the edge scrolls into view, as the browser does
            if (index >= box->offset + box->full) box->offset = index - box->full + 1;
            PaintListBox(*box);
            if (twice && box->onActivate) box->onActivate(index);
            else if (box->onSelect) box->onSelect(index);
        };
        c.Add(std::move(hit));
    }
    b->thumb.Set(TransformOf(Fill(view.rt, "Thumb", w - 2 - 8, 2, 5, 20, Alpha(C("gold"), 0.6f), 2)));
    Section::Wheel wheel;
    wheel.x = c.ox + x;
    wheel.y = c.oy + y;
    wheel.w = w;
    wheel.h = h;
    wheel.scroll = [weak](float notches) {
        auto box = weak.lock();
        if (!box) return;
        const int most = std::max(0, static_cast<int>(box->items.size()) - box->full);
        box->offset = std::clamp(box->offset - static_cast<int>(std::lround(notches * 3)), 0, most);
        PaintListBox(*box);
    };
    s.wheels.push_back(std::move(wheel));
    PaintListBox(*b);
    return b;
}
void SetListItems(ListBox& b, std::vector<std::string> items, int selected) {
    b.items = std::move(items);
    b.selected = selected < static_cast<int>(b.items.size()) ? selected : -1;
    b.offset = std::clamp(b.offset, 0, std::max(0, static_cast<int>(b.items.size()) - b.full));
    b.lastClick = -1;
    PaintListBox(b);
}
void PaintListBox(ListBox& b) {
    for (int i = 0; i < b.rows; ++i) {
        const int index = b.offset + i;
        const bool has = index < static_cast<int>(b.items.size());
        const std::string& text = has ? b.items[index] : std::string();
        if (static_cast<size_t>(i) >= b.shown.size()) b.shown.resize(i + 1, "\x01");
        if (b.shown[i] != text) { SetText(b.texts[i].Get(), text); b.shown[i] = text; }
        const bool on = has && index == b.selected;
        Tint(b.fills[i].Get(), on ? C("soft") : Alpha(C("soft"), 0));
        Ink(b.texts[i].Get(), on ? C("accent") : C("ink"));
    }
    const int count = static_cast<int>(b.items.size());
    const float track = b.h - 2 - 4, thumbH = count > b.full ? std::max(16.f, track * b.full / count) : 0;
    const float thumbY = count > b.full ? 2 + (track - thumbH) * b.offset / std::max(1, count - b.full) : 2;
    Move(b.thumb.Get(), b.w - 2 - 8, thumbY);
    Resize(b.thumb.Get(), 5, thumbH);
}

}  // namespace ep::ingame::view
