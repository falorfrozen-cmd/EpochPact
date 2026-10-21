// Drawing primitives of the in-game window (F6): Unity calls, the layout constants of the desktop
// interface at 1440x900, images (embedded sprites, plain quads, CSS borders), TextMeshPro texts
// placed like CSS line boxes, and the interface's words (ui.json, translated at build time).
// Main thread only.
#pragma once
#include "ingame_assets.hpp"
#include "ingame_json.hpp"
#include "managed.hpp"
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace ep::ingame::ui {
using assets::Face;
using JValue = json::Value;

struct V2 { float x, y; };
struct V3 { float x, y, z; };
struct Color { float r, g, b, a; };

Color Hex(std::string_view hex);  // "#rrggbb" or "#rrggbbaa"
Color Alpha(Color c, float a);
inline constexpr Color kWhite{1, 1, 1, 1};

// A strong GC handle that keeps the raw pointer, so reading it costs no call into the game. The
// window's own objects live as long as the window; Destroy drops them all at once.
struct Pin {
    uintptr_t handle = 0;
    void* object = nullptr;
    Pin() = default;
    explicit Pin(void* o) { Set(o); }
    Pin(const Pin&) = delete;
    Pin& operator=(const Pin&) = delete;
    Pin(Pin&& o) noexcept : handle(o.handle), object(o.object) { o.handle = 0; o.object = nullptr; }
    Pin& operator=(Pin&& o) noexcept {
        if (this != &o) { Reset(); handle = o.handle; object = o.object; o.handle = 0; o.object = nullptr; }
        return *this;
    }
    ~Pin() { Reset(); }
    void Set(void* o) { Reset(); if (o) { handle = il2cpp::api().gchandle_new(o, false); object = o; } }
    void Reset() { if (handle) il2cpp::api().gchandle_free(handle); handle = 0; object = nullptr; }
    void* Get() const { return object; }
    explicit operator bool() const { return object != nullptr; }
};

// ------------------------------------------------------------- layout (style.css at 1440x900)
inline float kW = 1440, kH = 900;
inline constexpr float kSideInner = 222, kBar = 15, kSideW = 238;                  // inside the 1px shell border
inline constexpr float kTopH = 67;                                                  // with its bottom border
inline constexpr float kMainX = 239, kMainY = 68, kFootH = 83;
inline float kMainW = 1200, kMainH = 748, kContentW = kMainW - kBar, kFootY = kMainY + kMainH;
inline constexpr float kHeroH = 226, kPadX = 28, kPadTop = 24, kPadBottom = 30;
inline constexpr float kHeroCompactH = 156;                                         // .main-scroll:has(.craft-categories) .hero
inline constexpr float kPanelPad = 24, kGap = 15, kCardPad = 20;
inline float kPanelW = kContentW - 2 * kPadX, kPanelInner = kPanelW - 2 * kPanelPad;
inline float kCardW = (kPanelInner - kGap) / 2, kCardInner = kCardW - 2 * kCardPad;
inline float kHeroTextW = (kContentW - 64) * 0.7f;
// The window's size in css px: the main column, its panels and cards take the width left of the
// sidebar. CSS zoom (the text size) shrinks this logical viewport and enlarges its canvas together,
// keeping the whole window on screen; a resized window gets its own width and height.
inline void ConfigureSize(float w, float h) {
    kW = w; kH = h; kMainW = kW - kMainX - 1; kMainH = kH - kMainY - kFootH - 1;
    kContentW = kMainW - kBar; kFootY = kMainY + kMainH;
    kPanelW = kContentW - 2 * kPadX; kPanelInner = kPanelW - 2 * kPanelPad;
    kCardW = (kPanelInner - kGap) / 2; kCardInner = kCardW - 2 * kCardPad; kHeroTextW = (kContentW - 64) * .7f;
}
inline void ConfigureGeometry(float zoom) { ConfigureSize(1440 / zoom, 900 / zoom); }
inline constexpr int kTopLeft = 257, kTop = 258, kTopRight = 260, kLeft = 513, kCenter = 514, kRight = 516;  // TMP alignments

// ----------------------------------------------------------------------------- Unity calls
const il2cpp::Method* Find(const char* image, const char* ns, const char* cls, const char* name, int count, bool required = true);
const il2cpp::Method* U(const char* cls, const char* name, int n);   // UnityEngine.CoreModule
const il2cpp::Method* UI(const char* cls, const char* name, int n);  // UnityEngine.UI
const il2cpp::Method* T(const char* name, int n);                    // TMPro.TMP_Text
const il2cpp::Method* Sig(const char* image, const char* ns, const char* cls, const char* name, std::initializer_list<const char*> params);
void* Type(const char* image, const char* ns, const char* name);
void* Add(void* go, const char* image, const char* ns, const char* name);
void SetActive(void* go, bool on);
void Enable(void* behaviour, bool on);
void* GameObjectOf(void* component);
void* TransformOf(void* component);
void Tint(void* graphic, Color c);
void Raycast(void* graphic, bool on);
void Fade(void* graphic, Color c, float seconds);  // the renderer's colour, like a CSS transition
void ReleaseTypes();                                // runtime shutdown

// A GameObject with a RectTransform at (x, y) css px from its parent's top-left corner.
struct Node { void* go = nullptr; void* rt = nullptr; };
Node New(const char* name, void* parent, float x, float y, float w, float h);
void Move(void* rt, float x, float y);
void Resize(void* rt, float w, float h);
void Turn(void* rt, float degrees);
void* AddCanvas(void* go);

// ------------------------------------------------------------------------------- graphics
void* Solid(void* parent, const char* name, float x, float y, float w, float h, Color c);
void* Picture(void* parent, const char* name, float x, float y, float w, float h, const std::string& file, bool sliced, Color c, bool hollow = false);
void* Fill(void* parent, const char* name, float x, float y, float w, float h, Color c, int radius = 0);
void* Outline(void* parent, const char* name, float x, float y, float w, float h, Color c, int radius = 0);
void Clip(Node n);

// ------------------------------------------------------------------------------------ texts
struct Style {
    Face face = Face::Body;
    float size = 14;
    Color color{};
    float line = 0;      // CSS line-height as a multiple of the size; 0 = normal
    float spacing = 0;   // letter-spacing, px
    int align = kTopLeft;
    bool wrap = false;
    bool upper = false;
    bool rich = false;   // TMP tags (<b>, <color>): the caller wraps outside words in <noparse>
};
std::string Upper(const std::string& s);
// The CSS line box: line-height times the size, or for "normal" what Chrome uses: the face's
// ascent and descent at this size, each rounded to a whole pixel.
float LineHeight(const Style& s);
// A select's height in Chrome: its line, the menu list's own 1px above and below, padding 10, border 1.
inline float SelectHeight(const Style& s) { return LineHeight(s) + 24; }
// A text's colour. TMP draws this game's text alpha as if gamma-corrected (alpha a looks like
// a^(1/2.2) next to the browser's): a translucent colour's alpha is a^2.2, so it looks like CSS a.
void Ink(void* text, Color c);
void InkRaw(void* text, Color c);  // the alpha as given (a CanvasGroup's dimming adds to it)
inline constexpr float kDisabledTextAlpha = 0.3832f;  // with a CanvasGroup at .45: 0.45^2.2 / 0.45, the label looks .45 as in CSS
void SetText(void* text, const std::string& value);
extern bool scaleStatic;  // TMP_Text.isTextObjectScaleStatic exists in this game
void* TextBox(void* parent, const char* name, float x, float y, float w, float h, const Style& s, const std::string& value);
// How far below the top of its CSS line box TextBox puts a top-aligned text, so its baseline is
// where the browser draws it.
float TopOffset(const Style& s);
float TextWidth(void* text);
// A single-line text's width from the faces' character advances (ui.json "advances"; no kerning):
// for layouts the browser computes from text widths (table columns), without a pass in the game.
float TextAdvance(Face face, float size, const std::string& text, float spacing = 0);
void Layout(void* text);      // lay a text out now (TextMeshProUGUI.ForceMeshUpdate)
int TextLines(void* text);
// For "panel measure": the line count a text had when it was measured, and the one TMP drew last.
int MeasuredLines(void* text);  // -1: never measured
int DrawnLines(void* text);     // no new layout pass
int FirstPassLines(void* text);  // the lines before TMP took the box width
std::string MeasureDetail(void* text);  // JSON: the first pass's lines and the box widths TMP used
void ForgetMeasures();
float Paragraph(void* parent, const char* name, float x, float y, float w, const Style& s, const std::string& value);

// -------------------------------------------------------------------------------- the words
const json::Value& D();
Color C(const char* token);  // a theme colour: "bg", "panel", "ink", "muted", "gold", "line", "accent", "glow", "soft"
extern std::string language;  // "en", "tr", "ru"
std::string Tr(const std::string& en);
std::string Fmt(const std::string& templ, const std::vector<std::string>& args);
std::string Num(double v);  // String(number) as JavaScript writes it
std::string Trim(const std::string& s);

}  // namespace ep::ingame::ui
