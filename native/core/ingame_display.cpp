// Display & Camera and World & Exploration sections with the app's own stored choices (app.js
// tierPalette, ruleColorRow, fontPanel, areaColorPanel): the colours are kept in the app's
// preferences under the same keys and sent to the game as the app sends them.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
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
float InPanel(const Ctx& c, const std::function<float(const Ctx& in, float y)>& draw) {
    Panel p = OpenPanel(c.parent, kPadX, 0, kPanelW);
    const Ctx in = c.At(p.rt.Get(), kPadX, 0);
    const float h = draw(in, kPanelPad) + kPanelPad;
    ClosePanel(p, kPanelW, h);
    return h;
}
bool Hex6(const std::string& v) { return v.size() == 6 && std::all_of(v.begin(), v.end(), [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)) != 0; }); }
std::string Upper(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return v;
}
std::string Quoted(const std::string& v) { return "\"" + v + "\""; }  // a hex or a word: nothing to escape
// Sends a command quietly (runTask {silent:true}); a refusal is shown.
void Quiet(const std::string& line, std::function<void()> done = {}) {
    Send(line, [done](const std::string& reply) {
        std::string why;
        if (Refused(reply, &why)) Toast(PlayerError(why), true);
        else if (done) done();
    });
}
// input[type=color]: the input's box (border, padding) around the colour; the picker on click
void ColorInput(const Ctx& c, float x, float y, float w, float h, const std::string& hex, bool enabled, std::function<void(const std::string&)> chosen,
                const std::string& name) {
    Fill(c.parent, "Color input", x, y, w, h, Hex("#080e0e"), 3);
    void* frame = Outline(c.parent, "Color border", x, y, w, h, Hex("#7b745a99"), 3);
    Solid(c.parent, "Color", x + 4, y + 4, w - 8, h - 8, Hex("#" + hex));
    Outline(c.parent, "Color edge", x + 4, y + 4, w - 8, h - 8, Hex("#00000066"));
    if (!enabled) {
        Solid(c.parent, "Disabled", x, y, w, h, Hex("#07120f80"));
        return;
    }
    Hit hit;
    hit.kind = HitKind::Action;
    hit.name = name;
    hit.x = x;
    hit.y = y;
    hit.w = w;
    hit.h = h;
    AddHover(hit, frame, Hex("#7b745a99"), C("accent"));
    const Ctx at = c;
    hit.action = [at, x, y, h, hex, chosen] {
        float wx = 0, wy = 0;
        if (WindowPoint(at, x, y + h + 4, &wx, &wy)) OpenColorPicker(wx, wy, hex, chosen);
    };
    c.Add(std::move(hit));
}
// a select in a label column (.rule-color-row label: column, gap 6, 12px muted; select width auto)
float ColumnSelect(const Ctx& c, float x, float y, const std::string& label, const std::vector<std::string>& options, int current, bool enabled,
                   std::function<void(int)> chosen, const std::string& name, float* width) {
    Style ls{Face::Body, 12, C("muted")};
    TextBox(c.parent, "Label", x, y, 600, LineHeight(ls), ls, label);
    const float top = LineHeight(ls) + 6;
    Style cs{Face::Body, 12, C("ink")};
    cs.align = kLeft;
    float widest = 0;
    for (const auto& o : options) widest = std::max(widest, TextAdvance(Face::Body, 12, o));
    const float w = std::ceil(widest) + 20 + 24, h = LineHeight(cs) + 22;
    *width = std::max(w, std::ceil(TextAdvance(Face::Body, 12, label)));
    Fill(c.parent, "Select", x, y + top, w, h, Hex("#080e0e"), 3);
    void* frame = Outline(c.parent, "Select border", x, y + top, w, h, Hex("#7b745a99"), 3);
    TextBox(c.parent, "Choice", x + 11, y + top, w - 30, h, cs, current >= 0 && current < static_cast<int>(options.size()) ? options[current] : "");
    Style ar{Face::Body, 11, C("ink")};
    ar.align = kCenter;
    TextBox(c.parent, "Arrow", x + w - 24, y + top, 16, h, ar, "\xE2\x8C\x84");
    if (!enabled) {
        Solid(c.parent, "Disabled", x, y + top, w, h, Hex("#07120f80"));
        return top + h;
    }
    Hit hit;
    hit.kind = HitKind::Action;
    hit.name = name;
    hit.x = x;
    hit.y = y + top;
    hit.w = w;
    hit.h = h;
    AddHover(hit, frame, Hex("#7b745a99"), Hex("#7b745a99"));
    const Ctx at = c;
    hit.action = [at, x, y, top, w, h, options, current, chosen] {
        float wx = 0, wy = 0;
        if (WindowPoint(at, x, y + top, &wx, &wy)) OpenList(wx, wy, w, h, options, current, chosen);
    };
    c.Add(std::move(hit));
    return top + h;
}

// ------------------------------------------------------------------------------- tier colours
const char* const kDefaultPalette[8] = {"FFFFFF", "2E8BFF", "FFD700", "AA55FF", "FF5500", "00FF00", "FF0044", "00FFFF"};
std::vector<std::string> StoredPalette() {
    std::vector<std::string> out(kDefaultPalette, kDefaultPalette + 8);
    const JValue p = Preference("epochpact.tier-palette.v1");
    if (p.IsArray() && p.items.size() == 8 && std::all_of(p.items.begin(), p.items.end(), [](const JValue& x) { return x.IsString() && Hex6(x.text); }))
        for (size_t i = 0; i < 8; ++i) out[i] = Upper(p.items[i].text);
    return out;
}
std::string StoredRuleColor() {  // "label" or a hex
    const JValue v = Preference("epochpact.rule-line-color.v1");
    if (v.IsString() && (Upper(v.text) == "LABEL" || Hex6(v.text))) return Upper(v.text) == "LABEL" ? "label" : Upper(v.text);
    return "label";
}
void StorePalette(const std::vector<std::string>& palette) {
    std::string text = "[";
    for (size_t i = 0; i < palette.size(); ++i) text += (i ? "," : "") + Quoted(palette[i]);
    text += "]";
    StorePreference("epochpact.tier-palette.v1", &text);
}
void MakeTierColours(Section& s) {
    s.build = [](Section& sec, const Ctx& c) {
        const int id = sec.id;
        return InPanel(c, [&](const Ctx& in, float y) {
            std::string reason;
            const bool eligible = Eligible(&reason);
            // .section-head: Tier colours and Reset colours
            Style h2{Face::Serif, 27, C("ink")};
            const float lh = LineHeight(h2);
            Button reset = AddButton(in, kPanelPad + kPanelInner, y, Tr("Reset colours"), Look::Normal, [id] {
                StorePreference("epochpact.tier-palette.v1", nullptr);
                StorePreference("epochpact.rule-line-color.v1", nullptr);
                Quiet("tiercolor reset", [id] {
                    Toast(Tr("Tier colours reset."));
                    Rebuild(id);
                });
            }, true, "tier_color");
            if (!eligible) SetEnabled(in, reset, false);
            const float row = std::max(lh, reset.h);
            Move(reset.node.rt, kPanelPad + kPanelInner - reset.w, y + (row - reset.h) / 2);
            (*in.hits)[reset.hit].y = in.oy + y + (row - reset.h) / 2;
            TextBox(in.parent, "Title", kPanelPad, y + (row - lh) / 2, kPanelInner - reset.w - 15, lh, h2, Tr("Tier colours"));
            y += row + 16;
            y += Muted(in.parent, kPanelPad, y, kPanelInner,
                       Tr("Used for tier digits on icons and coloured tooltip lines. Your colours are remembered on this device and applied when a character is "
                          "ready.")) + 10;
            // .tier-palette: auto-fill columns of at least 88px, gap 10; a label: T1, gap 8, a 44x30 colour
            const auto palette = StoredPalette();
            const int columns = std::max(1, static_cast<int>((kPanelInner + 10) / (88 + 10)));
            const float cellW = (kPanelInner - (columns - 1) * 10) / columns;
            Style ts{Face::Body, 12, C("ink")};
            ts.align = kLeft;
            for (int i = 0; i < 8; ++i) {
                const float x = kPanelPad + (i % columns) * (cellW + 10), ty = y + (i / columns) * (30 + 10);
                const std::string label = "T" + std::to_string(i + 1);
                const float tw = std::ceil(TextAdvance(Face::Body, 12, label));
                TextBox(in.parent, "Tier", x, ty, tw + 4, 30, ts, label);
                ColorInput(in, x + tw + 8, ty, 44, 30, palette[i], eligible, [i, id](const std::string& hex) {
                    auto now = StoredPalette();
                    now[i] = hex;
                    StorePalette(now);
                    Quiet("tiercolor " + std::to_string(i + 1) + " " + hex);
                    Rebuild(id);  // the swatch shows the new colour
                }, "tier-color-" + std::to_string(i + 1));
            }
            y += ((8 + columns - 1) / columns) * 40 - 10;
            // .rule-color-row (margin-top 16; gap 12px 18px; labels at the bottom line)
            y += 16;
            const std::string rule = StoredRuleColor();
            const bool custom = rule != "label";
            float selW = 0;
            const float selH = ColumnSelect(in, kPanelPad, y, Tr("Rule line colour"),
                                            {Tr("Like the item's label on the ground"), Tr("One colour for every rule")}, custom ? 1 : 0, eligible,
                                            [id](int i) {
                                                const std::string value = i == 1 ? (StoredRuleColor() == "label" ? std::string("FFD27F") : StoredRuleColor()) : "label";
                                                const std::string text = Quoted(value);
                                                StorePreference("epochpact.rule-line-color.v1", &text);
                                                Quiet("tiercolor rule " + value);
                                                Rebuild(id);
                                            }, "rule-color-mode", &selW);
            Style ls{Face::Body, 12, C("muted")};
            const float colorTop = LineHeight(ls) + 6, cx = kPanelPad + selW + 18;
            const float cy = y + selH - (colorTop + 34);  // the row aligns its items at the bottom
            TextBox(in.parent, "Label", cx, cy, 200, LineHeight(ls), ls, Tr("Colour"));
            ColorInput(in, cx, cy + colorTop, 56, 34, custom ? rule : "FFD27F", custom && eligible, [id](const std::string& hex) {
                const std::string text = Quoted(hex);
                StorePreference("epochpact.rule-line-color.v1", &text);
                Quiet("tiercolor rule " + hex);
                Rebuild(id);
            }, "rule-color");
            y += std::max(selH, colorTop + 34);
            y += 10 + Muted(in.parent, kPanelPad, y + 10, kPanelInner,
                            Tr("The loot filter rule line in item tooltips. Like the label: the rule's own colour when it recolours the item's label, otherwise the "
                               "item's colour (grey for Hide rules).")) + 10;
            return y;
        });
    };
}

// ---------------------------------------------------------------------------- text and font
// fontPanel: the Windows fonts (fontread, loaded on request) and the interface font
struct FontChoice { std::string search; } fontChoice;
std::vector<std::pair<std::string, std::string>> FontFaces() {
    std::vector<std::pair<std::string, std::string>> out;
    const JValue* r = Cached("fontread");
    if (!r) return out;
    for (const auto& f : (*r)["families"].items) {
        if (!f.IsString()) continue;
        out.emplace_back(f.text, f.text);
        for (const auto& st : (*r)["styles"][f.text].items) {
            if (!st.IsString()) continue;
            std::string lower = st.text;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (lower == "regular" || lower == "normal" || lower == "book" || lower == "roman") continue;
            out.emplace_back(f.text + "|" + st.text, f.text + " \xE2\x80\x94 " + st.text);
        }
    }
    return out;
}
// fontOptions: Game font, then the faces whose label has the search text (the current one stays)
void FontOptions(const std::string& current, std::vector<std::string>& values, std::vector<std::string>& labels) {
    values = {"game"};
    labels = {Tr("Game font")};
    std::string q = Trim(fontChoice.search);
    std::transform(q.begin(), q.end(), q.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    for (const auto& [value, label] : FontFaces()) {
        std::string lower = label;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (!q.empty() && lower.find(q) == std::string::npos && value != current) continue;
        values.push_back(value);
        labels.push_back(label);
    }
}
std::string FontArgument(const std::string& family) {
    std::string out;
    for (char ch : family) out += ch == ' ' ? std::string("%20") : ch == '|' ? std::string("%7C") : std::string(1, ch);
    return out;
}
void MakeInterfaceFont(Section& s) {
    // fontread asks Windows for every font and style: read only on "Load Windows fonts", as the app does
    s.build = [](Section& sec, const Ctx& c) {
        const int id = sec.id;
        return InPanel(c, [&](const Ctx& in, float y) {
            const JValue* r = Cached("fontread");
            const bool loaded = r && !(*r)["families"].items.empty();
            Style h2{Face::Serif, 27, C("ink")};
            const float lh = LineHeight(h2);
            float row = lh, titleW = kPanelInner;
            if (!loaded) {
                Button load = AddButton(in, kPanelPad + kPanelInner, y, Tr("Load Windows fonts"), Look::Normal, [id] {
                    Send("fontread", [id](const std::string& reply) {
                        Keep("fontread", reply);
                        Rebuild(id);
                    });
                }, true, "font_read");
                row = std::max(lh, load.h);
                Move(load.node.rt, kPanelPad + kPanelInner - load.w, y + (row - load.h) / 2);
                (*in.hits)[load.hit].y = in.oy + y + (row - load.h) / 2;
                titleW -= load.w + 15;
            }
            TextBox(in.parent, "Title", kPanelPad, y + (row - lh) / 2, titleW, lh, h2, Tr("Text and font"));
            y += row + 16;
            y += Muted(in.parent, kPanelPad, y, kPanelInner,
                       Tr(Ctl("font_family").description + " Every style a font has (Bold, Light, Semibold, Italic \xE2\x80\xA6) is listed. The choice is undone when you "
                                                           "choose Game font or close EpochPact.")) + 10;
            // label Search fonts + input (100%); label Interface font + count + select (100%)
            Style ls{Face::Body, 14, C("ink")};
            const float lineH = LineHeight(ls);
            const std::string current = r ? (*r)["family"].Str("game") : "game";
            std::vector<std::string> values, labels;
            FontOptions(current, values, labels);
            TextBox(in.parent, "Label", kPanelPad, y, kPanelInner, lineH, ls, Tr("Search fonts"));
            y += lineH;
            Ctx field = in;
            auto countText = std::make_shared<Pin>();  // the section keeps it (no Pin outlives the window)
            sec.live = countText;
            std::weak_ptr<Pin> weakCount = countText;
            y += InputField(field, kPanelPad, y, kPanelInner, "", fontChoice.search, Tr("Font name or style"), false, [current, weakCount](const std::string& text) {
                // the list narrows as you type: the count now, the list when it opens
                fontChoice.search = text;
                std::vector<std::string> v, l;
                FontOptions(current, v, l);
                auto count = weakCount.lock();
                if (count && *count && Cached("fontread")) SetText(count->Get(), Fmt("{0} fonts", {std::to_string(v.size() - 1)}));
            }, "font-search") + 4;
            const std::string count = loaded ? Fmt("{0} fonts", {std::to_string(values.size() - 1)}) : "";
            Style cs{Face::Body, 11, C("muted")};
            const float labelW = std::ceil(TextAdvance(Face::Body, 14, Tr("Interface font")));
            TextBox(in.parent, "Label", kPanelPad, y, labelW + 4, lineH, ls, Tr("Interface font"));
            countText->Set(TextBox(in.parent, "Count", kPanelPad + labelW + 4, y + 2, 300, lineH, cs, count));
            y += lineH;
            int at = 0;
            for (size_t i = 0; i < values.size(); ++i) if (values[i] == current) at = static_cast<int>(i);
            Style vs{Face::Body, 14, C("ink")};
            vs.align = kLeft;
            const float selH = LineHeight(vs) + 22;
            Fill(in.parent, "Select", kPanelPad, y, kPanelInner, selH, Hex("#080e0e"), 3);
            void* frame = Outline(in.parent, "Select border", kPanelPad, y, kPanelInner, selH, Hex("#7b745a99"), 3);
            TextBox(in.parent, "Choice", kPanelPad + 11, y, kPanelInner - 40, selH, vs, labels[at]);
            Style ar{Face::Body, 11, C("ink")};
            ar.align = kCenter;
            TextBox(in.parent, "Arrow", kPanelPad + kPanelInner - 26, y, 16, selH, ar, "\xE2\x8C\x84");
            if (!loaded) Solid(in.parent, "Disabled", kPanelPad, y, kPanelInner, selH, Hex("#07120f80"));
            else {
                Hit hit;
                hit.kind = HitKind::Action;
                hit.name = "font_family";
                hit.x = kPanelPad;
                hit.y = y;
                hit.w = kPanelInner;
                hit.h = selH;
                AddHover(hit, frame, Hex("#7b745a99"), Hex("#7b745a99"));
                const Ctx at2 = in;
                const float top = y;
                hit.action = [at2, top, selH, current, id] {
                    float wx = 0, wy = 0;
                    if (!WindowPoint(at2, kPanelPad, top, &wx, &wy)) return;
                    std::vector<std::string> values, labels;
                    FontOptions(current, values, labels);
                    int at = 0;
                    for (size_t i = 0; i < values.size(); ++i) if (values[i] == current) at = static_cast<int>(i);
                    OpenList(wx, wy, kPanelInner, selH, labels, at, [values, id](int i) {
                        Send("fontset family " + FontArgument(values[i]), [id](const std::string& reply) {
                            std::string why;
                            if (Refused(reply, &why)) { Toast(PlayerError(why), true); return; }
                            Keep("fontread", reply);  // the reply is the font state
                            Rebuild(id);
                        });
                    });
                };
                in.Add(std::move(hit));
            }
            y += selH;
            return y;
        });
    };
}

// ----------------------------------------------------------------------- interaction area colour
std::string StoredAreaColor() {
    const JValue v = Preference("epochpact.area-color.v1");
    return v.IsString() && Hex6(v.text) ? Upper(v.text) : "3FD0FF";
}
void MakeAreaColour(Section& s) {
    s.build = [](Section& sec, const Ctx& c) {
        const int id = sec.id;
        return InPanel(c, [&](const Ctx& in, float y) {
            y += H2(in.parent, kPanelPad, y, kPanelInner, Tr("Interaction area colour")) + 14;
            y += Muted(in.parent, kPanelPad, y, kPanelInner, Tr(Ctl("qol_areacolor").description)) + 10;
            y += 16 - 10;  // .rule-color-row margin-top 16 (the p's 10 collapses into it)
            Style ls{Face::Body, 12, C("muted")};
            TextBox(in.parent, "Label", kPanelPad, y, 200, LineHeight(ls), ls, Tr("Colour"));
            const float top = LineHeight(ls) + 6;
            ColorInput(in, kPanelPad, y + top, 56, 34, StoredAreaColor(), Eligible(), [id](const std::string& hex) {
                const std::string text = "\"" + hex + "\"";
                StorePreference("epochpact.area-color.v1", &text);
                Send("qolarea color " + hex, [id](const std::string& reply) {
                    std::string why;
                    if (Refused(reply, &why)) Toast(PlayerError(why), true);
                    Rebuild(id);
                });
            }, "qol_areacolor");
            return y + top + 34;
        });
    };
}
}  // namespace

void RegisterDisplaySections() {
    RegisterSection("tier_colours", MakeTierColours);
    RegisterSection("interface_font", MakeInterfaceFont);
    RegisterSection("area_colour", MakeAreaColour);
}

}  // namespace ep::ingame::view
