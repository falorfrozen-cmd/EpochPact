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
// Settings go through commands::SubmitUi as ordinary command lines, so each command's own limits
// and the offline-only gate apply unchanged. As in the desktop app, automatic settings are sent
// 250 ms after the last change and the others wait as a draft for Apply changes.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_panel.hpp"
#include "ingame_assets.hpp"
#include "ingame_json.hpp"
#include "commands.hpp"
#include "lifecycle.hpp"
#include "managed.hpp"
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
using JValue = json::Value;
using assets::Face;

// ------------------------------------------------------------------------------ basic types
struct V2 { float x, y; };
struct V3 { float x, y, z; };
struct Color { float r, g, b, a; };

Color Hex(std::string_view hex) {
    const auto part = [&](size_t at) { return static_cast<float>(std::strtol(std::string(hex.substr(at, 2)).c_str(), nullptr, 16)) / 255.f; };
    if ((hex.size() != 7 && hex.size() != 9) || hex[0] != '#') return {1, 0, 1, 1};
    return {part(1), part(3), part(5), hex.size() == 9 ? part(7) : 1.f};
}
Color Alpha(Color c, float a) { return {c.r, c.g, c.b, a}; }
constexpr Color kWhite{1, 1, 1, 1};

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
constexpr float kW = 1440, kH = 900;
constexpr float kSideInner = 222, kBar = 15, kSideW = 238;                  // inside the 1px shell border
constexpr float kTopH = 67;                                                  // with its bottom border
constexpr float kMainX = 239, kMainY = 68, kMainW = 1200, kMainH = 748, kContentW = kMainW - kBar;
constexpr float kFootY = kMainY + kMainH, kFootH = 83;
constexpr float kHeroH = 226, kPadX = 28, kPadTop = 24, kPadBottom = 30;
constexpr float kPanelW = kContentW - 2 * kPadX, kPanelPad = 24;           // 1px border + 23 padding
constexpr float kPanelInner = kPanelW - 2 * kPanelPad, kGap = 15;
constexpr float kCardW = (kPanelInner - kGap) / 2, kCardPad = 20;          // 1px border + 19 padding
constexpr float kCardInner = kCardW - 2 * kCardPad;
constexpr float kHeroTextW = (kContentW - 64) * 0.7f;                       // .hero>div max-width 70%
constexpr int kTopLeft = 257, kTop = 258, kTopRight = 260, kLeft = 513, kCenter = 514, kRight = 516;  // TMP alignments

// ----------------------------------------------------------------------------- Unity calls
const il2cpp::Method* Find(const char* image, const char* ns, const char* cls, const char* name, int count, bool required = true) {
    static std::map<std::string, const il2cpp::Method*> cache;
    const std::string key = std::string(image) + ":" + ns + ":" + cls + ":" + name + ":" + std::to_string(count);
    if (auto it = cache.find(key); it != cache.end()) {
        if (!it->second && required) throw std::runtime_error("In-game window method unavailable: " + key);
        return it->second;
    }
    auto m = game::FindMethod(image, ns, cls, name, count);
    cache.emplace(key, m ? m.info : nullptr);
    if (!m && required) throw std::runtime_error("In-game window method unavailable: " + key);
    return m ? m.info : nullptr;
}
const il2cpp::Method* U(const char* cls, const char* name, int n) { return Find("UnityEngine.CoreModule.dll", "UnityEngine", cls, name, n); }
const il2cpp::Method* UI(const char* cls, const char* name, int n) { return Find("UnityEngine.UI.dll", "UnityEngine.UI", cls, name, n); }
const il2cpp::Method* T(const char* name, int n) { return Find("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", name, n); }
const il2cpp::Method* Sig(const char* image, const char* ns, const char* cls, const char* name, std::initializer_list<const char*> params) {
    static std::map<std::string, const il2cpp::Method*> cache;
    std::string key = std::string(image) + ":" + ns + ":" + cls + ":" + name;
    for (auto p : params) key += std::string(":") + p;
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    auto& a = il2cpp::api();
    auto* c = game::FindClass(image, ns, cls);
    void* it = nullptr;
    if (c) while (auto* m = a.class_get_methods(c, &it)) {
        if (std::string(a.method_get_name(m)) != name || a.method_get_param_count(m) != params.size()) continue;
        bool match = true;
        unsigned n = 0;
        for (auto p : params) {
            char* actual = a.type_get_name(a.method_get_param(m, n++));
            match &= actual && std::string(actual) == p;
            if (actual) a.free(actual);
        }
        if (match) return cache.emplace(key, m).first->second;
    }
    throw std::runtime_error("In-game window overload unavailable: " + key);
}
std::map<std::string, std::unique_ptr<Root>> typeRoots;
void* Type(const char* image, const char* ns, const char* name) {
    const std::string key = std::string(image) + ":" + ns + ":" + name;
    if (auto it = typeRoots.find(key); it != typeRoots.end()) return it->second->Get();
    auto* c = game::FindClass(image, ns, name);
    if (!c) throw std::runtime_error("In-game window type unavailable: " + key);
    return typeRoots.emplace(key, std::make_unique<Root>(il2cpp::api().type_get_object(il2cpp::api().class_get_type(c)))).first->second->Get();
}
void* Add(void* go, const char* image, const char* ns, const char* name) {
    void* a[]{Type(image, ns, name)};
    return Invoke(Sig("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject", "AddComponent", {"System.Type"}), go, a);
}
void SetActive(void* go, bool on) { void* a[]{&on}; Invoke(U("GameObject", "SetActive", 1), go, a); }
void Enable(void* behaviour, bool on) { void* a[]{&on}; Invoke(U("Behaviour", "set_enabled", 1), behaviour, a); }
void* GameObjectOf(void* component) { return Invoke(U("Component", "get_gameObject", 0), component); }
void* TransformOf(void* component) { return Invoke(U("Component", "get_transform", 0), component); }
void Tint(void* graphic, Color c) { void* a[]{&c}; Invoke(UI("Graphic", "set_color", 1), graphic, a); }
void Raycast(void* graphic, bool on) { void* a[]{&on}; Invoke(UI("Graphic", "set_raycastTarget", 1), graphic, a); }
// The renderer's colour (multiplied with the graphic's), faded like a CSS transition.
void Fade(void* graphic, Color c, float seconds) {
    bool ignoreTimeScale = true, useAlpha = true;
    void* a[]{&c, &seconds, &ignoreTimeScale, &useAlpha};
    Invoke(UI("Graphic", "CrossFadeColor", 4), graphic, a);
}

// A GameObject with a RectTransform at (x, y) css px from its parent's top-left corner.
struct Node { void* go = nullptr; void* rt = nullptr; };
Node New(const char* name, void* parent, float x, float y, float w, float h) {
    auto& a = il2cpp::api();
    auto* goClass = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
    auto* typeClass = game::FindClass("mscorlib.dll", "System", "Type");
    if (!goClass || !typeClass) throw std::runtime_error("In-game window constructor types unavailable");
    Root go(a.object_new(goClass)), nameString(a.string_new(name)), types(a.array_new(typeClass, 1));
    int index = 0;
    void* element[]{Type("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform"), &index};
    Invoke(Sig("mscorlib.dll", "System", "Array", "SetValue", {"System.Object", "System.Int32"}), types.Get(), element);
    void* ctor[]{nameString.Get(), types.Get()};
    Invoke(Sig("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject", ".ctor", {"System.String", "System.Type[]"}), go.Get(), ctor);
    Node n{go.Get(), Invoke(U("GameObject", "get_transform", 0), go.Get())};
    int layer = 5;  // UI
    void* la[]{&layer};
    Invoke(U("GameObject", "set_layer", 1), n.go, la);
    if (parent) {
        bool world = false;
        void* p[]{parent, &world};
        Invoke(Sig("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "SetParent", {"UnityEngine.Transform", "System.Boolean"}), n.rt, p);
    }
    V2 corner{0, 1};
    V3 unit{1, 1, 1};
    void* ca[]{&corner};
    void* sa[]{&unit};
    for (const char* field : {"set_anchorMin", "set_anchorMax", "set_pivot"}) Invoke(U("RectTransform", field, 1), n.rt, ca);
    Invoke(U("Transform", "set_localScale", 1), n.rt, sa);
    V2 pos{x, -y}, size{w, h};
    void* pa[]{&pos};
    void* za[]{&size};
    Invoke(U("RectTransform", "set_anchoredPosition", 1), n.rt, pa);
    Invoke(U("RectTransform", "set_sizeDelta", 1), n.rt, za);
    return n;
}
void Move(void* rt, float x, float y) { V2 pos{x, -y}; void* a[]{&pos}; Invoke(U("RectTransform", "set_anchoredPosition", 1), rt, a); }
void Resize(void* rt, float w, float h) { V2 size{w, h}; void* a[]{&size}; Invoke(U("RectTransform", "set_sizeDelta", 1), rt, a); }
void Turn(void* rt, float degrees) { V3 e{0, 0, degrees}; void* a[]{&e}; Invoke(U("Transform", "set_localEulerAngles", 1), rt, a); }
void* AddCanvas(void* go) { return Add(go, "UnityEngine.UIModule.dll", "UnityEngine", "Canvas"); }

// ------------------------------------------------------------------------------- graphics
// A plain white quad (no texture), tinted: the cheapest graphic, and all of them batch together.
void* Solid(void* parent, const char* name, float x, float y, float w, float h, Color c) {
    Node n = New(name, parent, x, y, w, h);
    void* image = Add(n.go, "UnityEngine.UI.dll", "UnityEngine.UI", "Image");
    Raycast(image, false);
    Tint(image, c);
    return image;
}
// An embedded image: stretched, or 9-sliced with the border named in ui.json "slices".
void* Picture(void* parent, const char* name, float x, float y, float w, float h, const std::string& file, bool sliced, Color c, bool hollow = false) {
    Node n = New(name, parent, x, y, w, h);
    void* image = Add(n.go, "UnityEngine.UI.dll", "UnityEngine.UI", "Image");
    void* sa[]{assets::Sprite(file, sliced)};
    Invoke(UI("Image", "set_sprite", 1), image, sa);
    if (sliced) { int type = 1; void* ta[]{&type}; Invoke(UI("Image", "set_type", 1), image, ta); }
    if (hollow) { bool off = false; void* fa[]{&off}; Invoke(UI("Image", "set_fillCenter", 1), image, fa); }
    Raycast(image, false);
    Tint(image, c);
    return image;
}
void* Fill(void* parent, const char* name, float x, float y, float w, float h, Color c, int radius = 0) {
    if (!radius) return Solid(parent, name, x, y, w, h, c);
    return Picture(parent, name, x, y, w, h, "fill-r" + std::to_string(radius) + ".png", true, c);
}
// A 1px CSS border (with border-radius).
void* Outline(void* parent, const char* name, float x, float y, float w, float h, Color c, int radius = 0) {
    return Picture(parent, name, x, y, w, h, "frame-r" + std::to_string(radius) + ".png", true, c, true);
}
// Stencil clipping for a scrolling area: nothing is checked per frame (RectMask2D would test
// every child every frame, even with the window closed).
void Clip(Node n) {
    void* image = Add(n.go, "UnityEngine.UI.dll", "UnityEngine.UI", "Image");
    Raycast(image, false);
    void* mask = Add(n.go, "UnityEngine.UI.dll", "UnityEngine.UI", "Mask");
    bool show = false;
    void* a[]{&show};
    Invoke(UI("Mask", "set_showMaskGraphic", 1), mask, a);
}

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
};
std::string Upper(const std::string& s) {
    if (s.empty()) return s;
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), static_cast<int>(w.size()));
    std::wstring up(w.size(), L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, w.c_str(), static_cast<int>(w.size()), up.data(), static_cast<int>(up.size()), nullptr, nullptr, 0);
    std::string out(WideCharToMultiByte(CP_UTF8, 0, up.data(), static_cast<int>(up.size()), nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, up.data(), static_cast<int>(up.size()), out.data(), static_cast<int>(out.size()), nullptr, nullptr);
    return out;
}
float LineHeight(const Style& s) { return (s.line > 0 ? s.line : assets::FontMetrics(s.face).line) * s.size; }
// A text's colour: TMP_Text.color, not Graphic.color (a call through the base setter is not virtual).
void Ink(void* text, Color c) { void* a[]{&c}; Invoke(T("set_color", 1), text, a); }
void SetText(void* text, const std::string& value) {
    Root s(il2cpp::api().string_new(value.c_str()));
    void* a[]{s.Get()};
    Invoke(T("set_text", 1), text, a);
}
bool scaleStatic = false;  // TMP_Text.isTextObjectScaleStatic exists in this game
// A TMP text whose CSS line box starts at (x, y). Top-aligned text sits half a leading lower,
// where the browser draws the first line; further lines advance by the CSS line height.
void* TextBox(void* parent, const char* name, float x, float y, float w, float h, const Style& s, const std::string& value) {
    const auto m = assets::FontMetrics(s.face);
    const float lineH = LineHeight(s), half = (lineH - (m.ascent - m.descent) * s.size) / 2;
    const bool top = s.align == kTopLeft || s.align == kTop || s.align == kTopRight;
    Node n = New(name, parent, x, top ? y + half : y, w, top ? std::max(h - half, lineH) : h);
    void* text = Add(n.go, "Unity.TextMeshPro.dll", "TMPro", "TextMeshProUGUI");
    Raycast(text, false);
    // Our texts never change scale on their own: no per-frame scale check in TMP's update loop.
    if (const auto* staticScale = Find("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_isTextObjectScaleStatic", 1, false)) {
        bool yes = true;
        void* a[]{&yes};
        Invoke(staticScale, text, a);
        scaleStatic = true;
    }
    void* fa[]{assets::Font(s.face)};
    Invoke(T("set_font", 1), text, fa);
    bool off = false, wrap = s.wrap;
    void* ra[]{&off};
    Invoke(T("set_richText", 1), text, ra);
    float size = s.size, spacing = s.spacing / s.size * 100.f, lineSpacing = (lineH / s.size - m.line) * 100.f;
    void* za[]{&size};
    void* ca[]{&spacing};
    void* la[]{&lineSpacing};
    Invoke(T("set_fontSize", 1), text, za);
    Invoke(T("set_characterSpacing", 1), text, ca);
    Invoke(T("set_lineSpacing", 1), text, la);
    int align = s.align, overflow = 0;
    void* aa[]{&align};
    void* oa[]{&overflow};
    void* wa[]{&wrap};
    Invoke(T("set_alignment", 1), text, aa);
    Invoke(T("set_overflowMode", 1), text, oa);
    Invoke(T("set_enableWordWrapping", 1), text, wa);
    Ink(text, s.color);
    SetText(text, s.upper ? Upper(value) : value);
    return text;
}
float TextWidth(void* text) { return Value<V2>(Sig("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "GetPreferredValues", {}), text).x; }
int TextLines(void* text) {
    bool ignoreActive = true, forced = false;
    void* a[]{&ignoreActive, &forced};
    Invoke(T("ForceMeshUpdate", 2), text, a);
    void* info = Invoke(T("get_textInfo", 0), text);
    static size_t offset = 0;
    if (!offset) {
        const auto* f = il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(info), "lineCount");
        if (!f) throw std::runtime_error("TMP_TextInfo.lineCount unavailable");
        offset = il2cpp::api().field_get_offset(f);
    }
    return std::max(1, Get<int>(info, offset));
}
// A wrapped paragraph; returns its height.
float Paragraph(void* parent, const char* name, float x, float y, float w, const Style& s, const std::string& value) {
    void* t = TextBox(parent, name, x, y, w, LineHeight(s), s, value);
    return TextLines(t) * LineHeight(s);
}

// -------------------------------------------------------------------------------- the data
const JValue& D() { return assets::Data(); }
Color C(const char* token) { return Hex(D()["theme"][token].Str("#ff00ff")); }

// The desktop app's words: ui.json carries every text this window shows, translated at build
// time by the app's own translator (ui/i18n.js).
std::string language = "en";
std::string Tr(const std::string& en) {
    if (language == "en" || en.empty()) return en;
    const JValue* t = D()["i18n"][language].Find(en);
    return t && t->IsString() ? t->text : en;
}
std::string Fmt(const std::string& templ, const std::vector<std::string>& args) {
    std::string text = Tr(templ);
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string key = "{" + std::to_string(i) + "}";
        for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + args[i].size())) text.replace(at, key.size(), args[i]);
    }
    return text;
}
// String(number) as JavaScript writes it: the shortest text that reads back the same.
std::string Num(double v) {
    if (!std::isfinite(v)) return "";
    char b[40];
    if (std::floor(v) == v && std::fabs(v) < 1e15) { std::snprintf(b, sizeof b, "%.0f", v); return b; }
    for (int digits = 1; digits <= 17; ++digits) {
        std::snprintf(b, sizeof b, "%.*g", digits, v);
        if (std::strtod(b, nullptr) == v) break;
    }
    return b;
}
std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

struct Control {
    std::string id, widget, label, description, lifetime, command, unit, icon, read, path, defaultText;
    double min = 0, max = 1, def = 0;
    bool integer = false, hasDefault = false;
    std::vector<double> choices;
    std::vector<std::string> choiceLabels;
};
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
bool statusKnown = false, offlinePlay = false;

// ------------------------------------------------------------------------------ the window
enum class HitKind { Nav, Link, Minus, Plus, Track, Default, Toggle, Choice, Option, Refresh, Apply, Discard, Support, PanelRefresh };
enum class Area { Fixed, Sidebar, Main, Popup };
struct HoverPart { void* graphic; Color normal, hover; float seconds; };
struct Hit {
    HitKind kind{};
    int a = -1, b = -1;
    Area area = Area::Fixed;
    int page = -1;                       // Main hits belong to one page
    float x = 0, y = 0, w = 0, h = 0;    // window coords (Fixed, Popup), sidebar or page content coords
    std::vector<HoverPart> hover;
    bool enabled = true, visible = true;
};
struct Card {
    const Control* c = nullptr;
    bool automatic = true;
    int page = -1;
    float x = 0, y = 0, h = 0;           // page content coords; natural height
    float toolsY = 0;                    // the tools row, pushed down when a neighbour is taller
    float trackX = 0, trackW = 0;        // card coords
    Pin node, frame, grad, tools, value, thumb, knob, knobImage, switchFill, switchFrame, onOff, feedback, choiceText;
    std::vector<int> hits;
    std::string valueShown = "\x01", feedbackShown = "\x01";
    bool placeholder = false;
    float knobFrom = 4, knobTo = 4;
    double knobStart = -1;
};
struct Page {
    std::string id, nav, symbol, symbolFace, title, subtitle;
    Pin root, canvas;
    float height = 0, cursor = 0;
    bool built = false, queued = false;
    std::set<std::string> reads;
};
struct Window {
    Pin rootGo, rootCanvas, windowRt, listener, mainContent, sideContent, mainThumb, sideThumb, crumb, connection, draftLabel, applyGroup, applyGo, discardGo,
        heroEyebrow, heroTitle, heroSub, popup;
    std::vector<Pin> navFill, navFrame, navBar;
    std::vector<Page> pages;
    std::vector<Card> cards;
    std::vector<Hit> hits;
    int page = 0, applyHit = -1, discardHit = -1;
    bool built = false, shown = false;
    float scale = 1, mainScroll = 0, mainTarget = 0, sideScroll = 0, sideTarget = 0, sideHeight = 0;
    float thumbsAt[2] = {-1, -1};
    std::string lang;
} win;

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
    double openMs = 0, closeMs = 0, pageMs = 0, buildMs = 0, firstBuildMs = 0;
    double lastFrame = 0, frameAvgClosed = 0, frameAvgOpen = 0, stepAvgOpen = 0, stepMaxOpen = 0, idleCostMax = 0;
    double spikeAfterOpen = 0, spikeAfterClose = 0;
    int framesSinceOpen = 1000, framesSinceClose = 1000;
    uint64_t framesOpen = 0, framesClosed = 0, slowOpen = 0, slowClosed = 0;  // slow: over 50 ms, away from a toggle
} timing;

// -------------------------------------------------------------------------------- commands
// One command at a time through SubmitUi; replies arrive on the IPC worker and are handled on
// the main thread, in Frame.
struct Job { std::string line; std::function<void(const std::string&)> done; };
std::deque<Job> commandQueue;
bool commandInFlight = false;
std::mutex replyLock;
std::vector<std::pair<std::function<void(const std::string&)>, std::string>> replies;

void Send(std::string line, std::function<void(const std::string&)> done) { commandQueue.push_back({std::move(line), std::move(done)}); }
void PumpCommands() {
    std::vector<std::pair<std::function<void(const std::string&)>, std::string>> ready;
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

// ---------------------------------------------------------------------------- value display
void UpdateFooter();
void Paint(Card& k, bool animate);
void PaintId(const std::string& id) {
    for (auto& k : win.cards) if (k.c->id == id) Paint(k, true);
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

// ---------------------------------------------------------------------------------- pieces
void AddHover(Hit& h, void* graphic, Color normal, Color hover, float seconds = 0.15f) {
    Fade(graphic, normal, 0);
    h.hover.push_back({graphic, normal, hover, seconds});
}
int PushHit(Hit h) { win.hits.push_back(std::move(h)); return static_cast<int>(win.hits.size()) - 1; }

// A button as style.css draws one: the button gradient (none for .quiet), a 1px border, radius 3;
// on hover the border turns accent and the background soft (under the gradient), in .15s.
struct Button { Node node; float w = 0, h = 0; };
enum class Look { Normal, Quiet, Primary };
Button MakeButton(void* parent, const std::string& label, Look look, Hit& hit) {
    Style s{Face::Body, look == Look::Quiet ? 12.f : 14.f, C("ink")};
    float padX = look == Look::Quiet ? 12.f : 15.f, padY = look == Look::Quiet ? 9.f : 10.f;
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
// .badge: 10px uppercase accent text, letter-spacing .7, 1px line border, radius 2, padding 5/7.
float Badge(void* parent, float right, float y, const std::string& text, float* height = nullptr) {
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
struct Panel { Pin rt; Pin parts[4]; };
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
    Picture(p.rt.Get(), "Corner", 5, 5, 13, 13, "corner.png", false, C("gold"));
    void* br = Picture(p.rt.Get(), "Corner", w - 5, h - 5, 13, 13, "corner.png", false, C("gold"));
    Turn(TransformOf(br), 180);
}

// ----------------------------------------------------------------------------------- cards
// settingCard (app.js) at (x, y) in the panel, which sits at (ox, oy) in the page.
float BuildCard(int pageIndex, void* panel, float ox, float oy, float x, float y, const Control& c, bool automatic, const JValue* override_) {
    stage = "card " + c.id;
    Card k;
    k.c = &c;
    k.automatic = automatic;
    k.page = pageIndex;
    k.x = ox + x;
    k.y = oy + y;
    const int index = static_cast<int>(win.cards.size());
    Node card = New(c.id.c_str(), panel, x, y, kCardW, 10);
    k.node.Set(card.rt);
    k.grad.Set(TransformOf(Picture(card.rt, "Gradient", 0, 0, kCardW, 10, "grad-card.png", false, kWhite)));
    k.frame.Set(TransformOf(Outline(card.rt, "Border", 0, 0, kCardW, 10, Hex("#5e644e77"), 4)));
    const std::string label = Tr(override_ ? (*override_)["label"].Str(c.label) : c.label);
    const std::string description = Tr(override_ ? (*override_)["description"].Str(c.description) : c.description);
    const bool choice = c.widget == "choice";
    // .control-top: icon, title, badge (margin-left auto); aligned to the top
    float badgeH = 0;
    const float badgeW = Badge(card.rt, kCardW - kCardPad, kCardPad, Tr(D()["lifetimes"][c.lifetime].Str(c.lifetime)), &badgeH);
    const float titleRight = kCardW - kCardPad - badgeW - 14;
    float topH = badgeH;
    if (choice) {
        Style h3{Face::Serif, 20, C("ink")};
        h3.wrap = true;
        topH = std::max(topH, Paragraph(card.rt, "Title", kCardPad, kCardPad, titleRight - kCardPad, h3, label) + 12);
    } else {
        Picture(card.rt, "Icon", kCardPad, kCardPad, 55, 61, "icon-" + c.icon + ".png", false, kWhite);
        Style ts{Face::BodySemi, 15, C("ink")};
        ts.line = 1.45f;
        ts.wrap = true;
        const float tx = kCardPad + 55 + 14;
        topH = std::max({topH, 61.f, Paragraph(card.rt, "Title", tx, kCardPad, titleRight - tx, ts, label) + 8});
    }
    // .control-description: margin 6px 0 14px, min-height 40px
    Style ds{Face::Body, 12, C("muted")};
    ds.line = 1.65f;
    ds.wrap = true;
    float cy = kCardPad + topH + 6;
    cy += std::max(40.f, Paragraph(card.rt, "Description", kCardPad, cy, kCardInner, ds, description)) + 14;
    k.toolsY = cy;
    Node tools = New("Tools", card.rt, 0, cy, kCardW, 10);
    k.tools.Set(tools.rt);
    float toolsH = 0;
    Hit def;
    def.kind = HitKind::Default;
    def.area = Area::Main;
    def.page = pageIndex;
    def.a = index;
    // tools coordinates: from the top-left of the card at the tools row
    const auto place = [&](Hit& h, float hx, float hy) { h.x = k.x + hx; h.y = k.y + cy + hy; };
    if (c.widget == "number") {
        toolsH = 38;
        Button d = MakeButton(tools.rt, Tr("Default"), Look::Quiet, def);
        const float dx = kCardW - kCardPad - d.w, dy = (toolsH - d.h) / 2;
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
            h.page = pageIndex;
            h.a = index;
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
            k.hits.push_back(PushHit(std::move(h)));
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
        track.page = pageIndex;
        track.a = index;
        track.w = k.trackW + 4;
        track.h = toolsH;
        place(track, k.trackX - 2, 0);
        k.hits.push_back(PushHit(std::move(track)));
    } else if (c.widget == "toggle") {
        // label.toggle (margin-left auto): "On"/"Off", gap 10, the 48x25 switch; gap 10, Default
        Button d = MakeButton(tools.rt, Tr("Default"), Look::Quiet, def);
        toolsH = d.h;
        const float dx = kCardW - kCardPad - d.w;
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
        toggle.page = pageIndex;
        toggle.a = index;
        toggle.w = 48 + 10 + 34;
        toggle.h = toolsH;
        place(toggle, sx - 10 - 34, 0);
        k.hits.push_back(PushHit(std::move(toggle)));
    } else if (choice) {
        // select (width 100%), then Default wrapped onto the next line (row gap 10)
        Style cs{Face::Body, 14, C("ink")};
        cs.align = kLeft;
        const float selH = LineHeight(cs) + 22;
        Fill(tools.rt, "Select", kCardPad, 0, kCardInner, selH, Hex("#080e0e"), 3);
        Outline(tools.rt, "Select border", kCardPad, 0, kCardInner, selH, Hex("#7b745a99"), 3);
        k.choiceText.Set(TextBox(tools.rt, "Choice", kCardPad + 11, 0, kCardInner - 40, selH, cs, ""));
        Style ar{Face::Body, 12, C("ink")};
        ar.align = kCenter;
        TextBox(tools.rt, "Arrow", kCardPad + kCardInner - 26, 0, 16, selH, ar, "\xE2\x8C\x84");
        Hit sel;
        sel.kind = HitKind::Choice;
        sel.area = Area::Main;
        sel.page = pageIndex;
        sel.a = index;
        sel.w = kCardInner;
        sel.h = selH;
        place(sel, kCardPad, 0);
        k.hits.push_back(PushHit(std::move(sel)));
        Button d = MakeButton(tools.rt, Tr("Default"), Look::Quiet, def);
        Move(d.node.rt, kCardPad, selH + 10);
        place(def, kCardPad, selH + 10);
        toolsH = selH + 10 + d.h;
    }
    k.hits.push_back(PushHit(std::move(def)));
    // .limits (margin-top 10px): the default text, a space, then the feedback (margin-left .6rem)
    Style ls{Face::Body, 10, C("muted")};
    const float ly = toolsH + 10;
    float limitsH = 0, fx = kCardPad;
    if (!choice) {
        void* lt = TextBox(tools.rt, "Limits", kCardPad, ly, kCardInner, LineHeight(ls), ls, Tr(c.defaultText));
        limitsH = LineHeight(ls);
        fx += std::ceil(TextWidth(lt)) + 2.8f + 8.4f;
    }
    Style fs{Face::Body, 10, C("accent")};
    k.feedback.Set(TextBox(tools.rt, "Feedback", fx, ly - 0.5f, kCardW - kCardPad - fx, 16, fs, ""));
    k.h = cy + ly + limitsH + kCardPad;
    Resize(tools.rt, kCardW, ly + limitsH);
    Resize(card.rt, kCardW, k.h);
    Resize(k.frame.Get(), kCardW, k.h);
    Resize(k.grad.Get(), kCardW, k.h);
    win.cards.push_back(std::move(k));
    Paint(win.cards.back(), false);
    if (!c.read.empty()) win.pages[pageIndex].reads.insert(c.read);
    if (c.id == "speed") win.pages[pageIndex].reads.insert("statraw 9 0 0 0");
    return win.cards.back().h;
}
// CSS grid rows stretch their cards, and .control-tools (margin-top auto) goes to the bottom.
void Stretch(int cardIndex, float rowH) {
    Card& k = win.cards[cardIndex];
    const float extra = rowH - k.h;
    if (extra <= 0.01f) return;
    Resize(k.node.Get(), kCardW, rowH);
    Resize(k.frame.Get(), kCardW, rowH);
    Resize(k.grad.Get(), kCardW, rowH);
    Move(k.tools.Get(), 0, k.toolsY + extra);
    for (int h : k.hits) win.hits[h].y += extra;
}

// ---------------------------------------------------------------------------------- sections
// settingPanel (app.js): the heading with its badge (or the Weaver's Will Refresh button), an
// optional muted paragraph, the 2-column grid of cards; an optional note after the panel.
// One job per row of cards, so building never holds a frame for long.
struct SectionBuild {
    int page = 0;
    const JValue* spec = nullptr;
    Panel panel;
    float top = 0, y = 0;
    std::vector<const Control*> list;
    std::vector<bool> automatic;
};
void QueueSettings(std::vector<std::function<void()>>& out, int pageIndex, const JValue& spec) {
    auto s = std::make_shared<SectionBuild>();
    s->page = pageIndex;
    s->spec = &spec;
    out.push_back([s] {
        Page& p = win.pages[s->page];
        const JValue& spec = *s->spec;
        const auto& ids = spec["controls"].items;
        for (size_t i = 0; i < ids.size(); ++i) {
            auto it = controls.find(ids[i].Str());
            if (it == controls.end()) continue;
            s->list.push_back(&it->second);
            s->automatic.push_back(i < spec["automatic"].items.size() ? spec["automatic"].items[i].boolean : true);
        }
        const bool allAutomatic = std::all_of(s->automatic.begin(), s->automatic.end(), [](bool b) { return b; });
        s->top = p.cursor;
        s->panel = OpenPanel(p.root.Get(), kPadX, p.cursor, kPanelW);
        void* rt = s->panel.rt.Get();
        // .section-head: h2 (Georgia 27) and, centred beside it, the badge; margin-bottom 16
        Style h2{Face::Serif, 27, C("ink")};
        const float lh = LineHeight(h2);
        TextBox(rt, "Title", kPanelPad, kPanelPad, kPanelInner - 300, lh, h2, Tr(spec["title"].Str()));
        if (spec["head"].Str() == "refresh") {
            Hit h;
            h.kind = HitKind::PanelRefresh;
            h.area = Area::Main;
            h.page = s->page;
            Button b = MakeButton(rt, Tr("Refresh"), Look::Normal, h);
            const float bx = kPanelW - kPanelPad - b.w, by = kPanelPad + (lh - b.h) / 2;
            Move(b.node.rt, bx, by);
            h.x = kPadX + bx;
            h.y = s->top + by;
            PushHit(std::move(h));
        } else {
            const float badgeY = kPanelPad + (lh - (LineHeight(Style{Face::Body, 10}) + 12)) / 2;
            Badge(rt, kPanelW - kPanelPad, badgeY, Tr(allAutomatic ? "Changes apply automatically" : "Use Apply changes below"));
        }
        s->y = kPanelPad + lh + 16;
        if (spec["text"].IsString()) {
            // p.muted: its 10px top margin collapses into the heading's 16
            Style ms{Face::Body, 14, C("muted")};
            ms.line = 1.65f;
            ms.wrap = true;
            s->y += Paragraph(rt, "Text", kPanelPad, s->y, kPanelInner, ms, Tr(spec["text"].Str())) + 10;
        }
    });
    const size_t count = spec["controls"].items.size();
    for (size_t row = 0; row < (count + 1) / 2; ++row) {
        out.push_back([s, row] {
            const size_t first = row * 2;
            if (first >= s->list.size()) return;
            const JValue* overrides = s->spec->Find("overrides");
            const auto over = [&](const Control* c) { return overrides ? overrides->Find(c->id) : nullptr; };
            const int before = static_cast<int>(win.cards.size());
            float rowH = BuildCard(s->page, s->panel.rt.Get(), kPadX, s->top, kPanelPad, s->y, *s->list[first], s->automatic[first], over(s->list[first]));
            if (first + 1 < s->list.size())
                rowH = std::max(rowH, BuildCard(s->page, s->panel.rt.Get(), kPadX, s->top, kPanelPad + kCardW + kGap, s->y, *s->list[first + 1],
                                                s->automatic[first + 1], over(s->list[first + 1])));
            for (int i = before; i < static_cast<int>(win.cards.size()); ++i) Stretch(i, rowH);
            s->y += rowH + kGap;
        });
    }
    out.push_back([s] {
        Page& p = win.pages[s->page];
        const float h = (s->list.empty() ? s->y : s->y - kGap) + kPanelPad;
        ClosePanel(s->panel, kPanelW, h);
        p.cursor += h + 20;
        if ((*s->spec)["note"].IsString()) {
            // p.note: padding 12px 16px, a 2px accent bar, soft background; margin-bottom 18
            Style ns{Face::Body, 12, C("muted")};
            ns.line = 1.7f;
            ns.wrap = true;
            Node note = New("Note", p.root.Get(), kPadX, p.cursor, kPanelW, 10);
            void* bg = Fill(note.rt, "Background", 0, 0, kPanelW, 10, C("soft"));
            void* bar = Fill(note.rt, "Bar", 0, 0, 2, 10, C("accent"));
            const float nh = 12 + Paragraph(note.rt, "Text", 18, 12, kPanelW - 34, ns, Tr((*s->spec)["note"].Str())) + 12;
            Resize(TransformOf(bg), kPanelW, nh);
            Resize(TransformOf(bar), 2, nh);
            Resize(note.rt, kPanelW, nh);
            p.cursor += nh + 18;
        }
    });
}
// A desktop section the in-game window does not have yet: its heading and a pointer to the app.
void BuildPending(int pageIndex, const JValue& spec) {
    Page& p = win.pages[pageIndex];
    Panel panel = OpenPanel(p.root.Get(), kPadX, p.cursor, kPanelW);
    Style h2{Face::Serif, 27, C("ink")}, ms{Face::Body, 14, C("muted")};
    ms.line = 1.65f;
    ms.wrap = true;
    float y = kPanelPad;
    TextBox(panel.rt.Get(), "Title", kPanelPad, y, kPanelInner, LineHeight(h2), h2, Tr(spec["title"].Str()));
    y += LineHeight(h2) + 14;
    y += Paragraph(panel.rt.Get(), "Text", kPanelPad, y, kPanelInner, ms, Tr("This section is not in the in-game window yet. Use the EpochPact app for it."));
    y += kPanelPad;
    ClosePanel(panel, kPanelW, y);
    p.cursor += y + 20;
}
// .summary-grid: four tiles, gap 14, margin-bottom 22.
void BuildSummary(int pageIndex, const JValue& spec) {
    Page& p = win.pages[pageIndex];
    const float tileW = (kPanelW - 3 * 14) / 4;
    Style strong{Face::Serif, 27, C("accent")}, label{Face::Body, 11, C("muted")};
    const float h = 1 + 18 + LineHeight(strong) + 8 + LineHeight(label) + 18 + 1;
    for (size_t i = 0; i < spec["items"].items.size() && i < 4; ++i) {
        const auto& item = spec["items"].items[i];
        Node tile = New("Summary", p.root.Get(), kPadX + i * (tileW + 14), p.cursor, tileW, h);
        Fill(tile.rt, "Background", 0, 0, tileW, h, C("panel"));
        Outline(tile.rt, "Border", 0, 0, tileW, h, C("line"));
        TextBox(tile.rt, "Value", 19, 19, tileW - 38, LineHeight(strong), strong, item.items.size() > 0 ? item.items[0].Str() : "");
        TextBox(tile.rt, "Label", 19, 19 + LineHeight(strong) + 8, tileW - 38, LineHeight(label), label, Tr(item.items.size() > 1 ? item.items[1].Str() : ""));
    }
    p.cursor += h + 22;
}
// The overview's "Explore your workshop": a panel of button.settings-card links.
void BuildLinks(int pageIndex, const JValue& spec) {
    Page& p = win.pages[pageIndex];
    const float top = p.cursor;
    Panel panel = OpenPanel(p.root.Get(), kPadX, top, kPanelW);
    Style h2{Face::Serif, 27, C("ink")}, h3{Face::Serif, 20, C("ink")}, ms{Face::Body, 14, C("muted")};
    h3.wrap = true;
    ms.line = 1.65f;
    ms.wrap = true;
    float y = kPanelPad;
    TextBox(panel.rt.Get(), "Title", kPanelPad, y, kPanelInner, LineHeight(h2), h2, Tr(spec["title"].Str()));
    y += LineHeight(h2) + 14;
    const auto& links = spec["pages"].items;
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
            hit.area = Area::Main;
            hit.page = pageIndex;
            AddHover(hit, m.fill, C("panel"), C("soft"));
            AddHover(hit, m.frame, C("line"), C("accent"));
            // padding 22, h3 margin 4px 0 10px, p.muted margin 10px 0 (collapsing with the h3's)
            float cy = 23 + 4;
            cy += Paragraph(m.inner.rt, "Title", 23, cy, kCardW - 46, h3, Tr(t.symbol + " " + t.nav)) + 10;
            cy += Paragraph(m.inner.rt, "Text", 23, cy, kCardW - 46, ms, Tr(t.subtitle)) + 10;
            m.h = cy + 23;
            hit.x = kPadX + x;
            hit.y = top + y;
            hit.w = kCardW;
            hit.h = m.h;
            m.hit = PushHit(std::move(hit));
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
            win.hits[m.hit].h = rowH;
        }
        y += rowH + kGap;
    }
    y += kPanelPad - kGap;
    ClosePanel(panel, kPanelW, y);
    p.cursor += y + 20;
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
        q.cursor = kHeroH + kPadTop;
    });
    const JValue& spec = D()["pages"].items[index];
    for (const JValue& section : spec["sections"].items) {
        const std::string kind = section["kind"].Str();
        if (kind == "settings") QueueSettings(list, index, section);
        else list.push_back([index, &section, kind] {
            if (kind == "summary") BuildSummary(index, section);
            else if (kind == "links") BuildLinks(index, section);
            else BuildPending(index, section);
        });
    }
    list.push_back([index] {
        Page& q = win.pages[index];
        q.height = q.cursor + kPadBottom;
        Resize(q.root.Get(), kContentW, q.height);
        q.built = true;
        // hidden until shown: a page whose Canvas is off costs nothing
        if (index != win.page) Enable(q.canvas.Get(), false);
    });
    if (front) for (auto it = list.rbegin(); it != list.rend(); ++it) jobs.push_front(std::move(*it));
    else for (auto& j : list) jobs.push_back(std::move(j));
}

// ------------------------------------------------------------------------------- the shell
void BuildBase() {
    Node rootGo = New("EpochPact In-game Window", nullptr, 0, 0, 0, 0);
    win.rootGo.Set(rootGo.go);
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
    Add(w.go, "UnityEngine.UI.dll", "UnityEngine.UI", "GraphicRaycaster");
    // The page colour behind everything catches the pointer, so the game treats the window like
    // one of its own panels: no clicks go through to the world.
    void* catcher = Solid(w.rt, "Background", 0, 0, kW, kH, C("bg"));
    Raycast(catcher, true);
    win.listener.Set(Add(GameObjectOf(catcher), "LE.dll", "", "UIMouseListener"));
    Enable(win.listener.Get(), false);
    // main: the radial background, the clipped scrolling content with the hero
    Picture(w.rt, "Main background", kMainX, kMainY, kMainW, kMainH, "main.png", false, kWhite);
    Node viewport = New("Main viewport", w.rt, kMainX, kMainY, kContentW, kMainH);
    Clip(viewport);
    Node content = New("Main content", viewport.rt, 0, 0, kContentW, kMainH);
    win.mainContent.Set(content.rt);
    Picture(content.rt, "Hero", 0, 0, kContentW, kHeroH, "hero.jpg", false, kWhite);
    Solid(content.rt, "Hero border", 0, kHeroH - 1, kContentW, 1, C("line"));
    Style eyebrow{Face::Body, 10, C("accent")}, h1{Face::Serif, 43.2f, C("ink")}, sub{Face::Body, 15, Hex("#c6d1c8")};
    eyebrow.spacing = 2;
    h1.spacing = -0.5f;
    h1.wrap = true;
    sub.line = 1.65f;
    sub.wrap = true;
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
    Hit refresh;
    refresh.kind = HitKind::Refresh;
    Button b = MakeButton(w, Tr("↻ Refresh"), Look::Quiet, refresh);
    const float rx = searchX - 16 - b.w, ry = 1 + (kTopH - 1 - b.h) / 2;
    Move(b.node.rt, rx, ry);
    refresh.x = rx;
    refresh.y = ry;
    PushHit(std::move(refresh));
    Style cs{Face::Body, 12, C("muted")};
    cs.align = kRight;
    cs.line = 1.3f;
    win.connection.Set(TextBox(w, "Connection", rx - 16 - 380, 1, 380, kTopH - 1, cs, Tr("Checking connection")));
}
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
    discard.kind = HitKind::Discard;
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
    win.applyGo.Set(a.node.go);
    win.discardGo.Set(d.node.go);
    win.applyHit = PushHit(std::move(apply));
    win.discardHit = PushHit(std::move(discard));
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
        PushHit(std::move(hit));
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
    link.x = 20;
    link.y = y;
    link.w = 182;
    link.h = LineHeight(support);
    PushHit(std::move(link));
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
    Node popup = New("Popup", w, 0, 0, kW, kH);
    win.popup.Set(popup.rt);
    SetActive(popup.go, false);
}

// ------------------------------------------------------------------------------ show a page
void PlaceHero() {
    const Page& p = win.pages[win.page];
    SetText(win.heroTitle.Get(), Tr(p.title));
    SetText(win.heroSub.Get(), Tr(p.subtitle));
    // .hero: padding 35px 32px, its text block centred. The eyebrow's line box is the 14px strut
    // and the eyebrow sits on its baseline; h1 margin 12/14; the subtitle 15px/1.65.
    const auto body = assets::FontMetrics(Face::Body), serif = assets::FontMetrics(Face::Serif);
    const float strut = body.line * 14, h1Line = serif.line * 43.2f, subLine = 15 * 1.65f;
    const int titleLines = TextLines(win.heroTitle.Get()), subLines = TextLines(win.heroSub.Get());
    const float block = strut + 12 + titleLines * h1Line + 14 + subLines * subLine;
    const float top = 35 + (kHeroH - 70 - block) / 2;
    const float baseline = top + (strut - (body.ascent - body.descent) * 14) / 2 + body.ascent * 14;
    Move(TransformOf(win.heroEyebrow.Get()), 32, baseline - body.ascent * 10);
    Move(TransformOf(win.heroTitle.Get()), 32, top + strut + 12);
    Move(TransformOf(win.heroSub.Get()), 32, top + strut + 12 + titleLines * h1Line + 14 + (subLine - (body.ascent - body.descent) * 15) / 2);
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
    win.hits[win.applyHit].enabled = canApply;
    // hidden on the pages whose settings all apply automatically (and on Game setup)
    const bool buttons = !autoPage && id != "game_setup";
    if (buttons != win.hits[win.discardHit].visible) {
        SetActive(win.applyGo.Get(), buttons);
        SetActive(win.discardGo.Get(), buttons);
        win.hits[win.applyHit].visible = win.hits[win.discardHit].visible = buttons;
    }
}
void RequestValues();
void ShowPage(int index) {
    if (index < 0 || index >= static_cast<int>(win.pages.size())) return;
    const double start = Ms();
    const int previous = win.page;
    Page& p = win.pages[index];
    if (!p.built) {
        QueuePage(index, true);
        while (!p.built && !jobs.empty()) { auto j = std::move(jobs.front()); jobs.pop_front(); j(); }
    }
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
    RequestValues();
    win.thumbsAt[0] = -1;
    timing.pageMs = Ms() - start;
}

// ------------------------------------------------------------------------------ the values
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
        if (json::Parse(reply, v)) sessionState = v["state"].Str(sessionState);
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
        JValue v;
        if (!json::Parse(reply, v) || (v["ok"].IsBool() && !v["ok"].boolean)) return;
        for (auto& [id, c] : controls) {
            if (c.read != read || pending.count(id) || drafts.count(id) || sendingId == id) continue;
            const JValue* x = v.Path(c.path);
            if (x && (x->IsNumber() || x->IsBool())) values[id] = appliedValues[id] = x->Num();
        }
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
void RequestValues() {
    if (!win.built) return;
    std::set<std::string> reads = win.pages[win.page].reads;
    reads.insert("status");
    reads.insert("sessionread");
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
    for (size_t at = line.find("{value}"); at != std::string::npos; at = line.find("{value}")) line.replace(at, 7, Num(v));
    return line;
}
void SendSetting(const std::string& id, double v, std::function<void()> after = {}) {
    const Control& c = controls.at(id);
    sendingId = id;
    Send(CommandFor(c, v), [id, v, after](const std::string& reply) {
        if (sendingId == id) sendingId.clear();
        std::string why;
        if (Refused(reply, &why)) {
            errors[id] = why;
            if (appliedValues.count(id)) values[id] = appliedValues[id];
        } else {
            appliedValues[id] = v;
            applied.insert(id);
        }
        PaintId(id);
        const Control& sent = controls.at(id);
        if (!sent.read.empty() && sent.read != "status") Send(sent.read, [r = sent.read](const std::string& text) { ReadInto(r, text); });
        if (id == "speed") Send("statraw 9 0 0 0", [](const std::string& text) { ReadInto("statraw 9 0 0 0", text); });
        if (after) after();
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
void ApplyDrafts() {
    if (drafts.empty() || !sendingId.empty()) return;
    const std::string id = *drafts.begin();
    drafts.erase(drafts.begin());
    SendSetting(id, values[id], [] { ApplyDrafts(); });
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
float MaxMain() { return std::max(0.f, win.pages[win.page].height - kMainH); }
float MaxSide() { return std::max(0.f, win.sideHeight - (kH - 2)); }
// A hit's place in window coords, after scrolling; false when it belongs to another page.
bool Placed(const Hit& h, float* x, float* y) {
    *x = h.x;
    *y = h.y;
    if (h.area == Area::Main) {
        if (h.page != win.page) return false;
        *x += kMainX;
        *y += kMainY - win.mainScroll;
    } else if (h.area == Area::Sidebar) {
        *x += 1;
        *y += 1 - win.sideScroll;
    }
    return true;
}
bool Inside(const Hit& h, float px, float py) {
    float x, y;
    if (!h.visible || !Placed(h, &x, &y) || px < x || px >= x + h.w || py < y || py >= y + h.h) return false;
    if (h.area == Area::Main) return px >= kMainX && px < kMainX + kContentW && py >= kMainY && py < kMainY + kMainH;
    if (h.area == Area::Sidebar) return px >= 1 && px < 1 + kSideInner && py >= 1 && py < kH - 1;
    return true;
}

int hovered = -1, pressed = -1, dragCard = -1, openChoice = -1;
bool mouseDown = false;
enum class Drag { None, Main, Side } scrollDrag = Drag::None;
float dragStartY = 0, dragStartScroll = 0;

void SetHover(int index) {
    if (index == hovered) return;
    if (hovered >= 0 && hovered < static_cast<int>(win.hits.size()))
        for (auto& part : win.hits[hovered].hover) Fade(part.graphic, part.normal, part.seconds);
    hovered = index;
    if (hovered >= 0) for (auto& part : win.hits[hovered].hover) Fade(part.graphic, part.hover, part.seconds);
}
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
void CloseChoice() {
    if (openChoice < 0) return;
    openChoice = -1;
    SetHover(-1);
    win.hits.erase(std::remove_if(win.hits.begin(), win.hits.end(), [](const Hit& h) { return h.kind == HitKind::Option; }), win.hits.end());
    void* rt = win.popup.Get();
    const int n = Value<int>(U("Transform", "get_childCount", 0), rt);
    for (int i = n - 1; i >= 0; --i) {
        void* a[]{&i};
        void* d[]{GameObjectOf(Invoke(U("Transform", "GetChild", 1), rt, a))};
        Invoke(U("Object", "Destroy", 1), nullptr, d);
    }
    SetActive(GameObjectOf(rt), false);
}
// The choice list, below the select (above it when there is no room), like the browser's.
void OpenChoice(const Hit& select) {
    CloseChoice();
    const Control& c = *win.cards[select.a].c;
    float x, y;
    Placed(select, &x, &y);
    void* rt = win.popup.Get();
    const float rowH = 28, h = rowH * c.choices.size() + 2;
    float top = y + select.h;
    if (top + h > kH - 2) top = y - h;
    Fill(rt, "List", x, top, select.w, h, Hex("#080e0e"));
    Outline(rt, "List border", x, top, select.w, h, C("gold"));
    for (size_t i = 0; i < c.choices.size(); ++i) {
        Hit option;
        option.kind = HitKind::Option;
        option.a = select.a;
        option.b = static_cast<int>(i);
        option.area = Area::Popup;
        option.x = x + 1;
        option.y = top + 1 + i * rowH;
        option.w = select.w - 2;
        option.h = rowH;
        const bool current = values.count(c.id) && values[c.id] == c.choices[i];
        void* fill = Fill(rt, "Option", option.x, option.y, option.w, rowH, kWhite);
        AddHover(option, fill, current ? C("soft") : Alpha(C("soft"), 0), Alpha(C("accent"), 0.3f), 0);
        Style s{Face::Body, 14, C("ink")};
        s.align = kLeft;
        TextBox(rt, "Option text", option.x + 10, option.y, option.w - 20, rowH, s, ChoiceLabel(c, i));
        PushHit(std::move(option));
    }
    SetActive(GameObjectOf(rt), true);
    openChoice = select.a;
}
void Click(const Hit& h) {
    switch (h.kind) {
    case HitKind::Nav:
    case HitKind::Link: ShowPage(h.a); break;
    case HitKind::Refresh:
    case HitKind::PanelRefresh: RequestValues(); break;
    case HitKind::Apply: if (h.enabled) ApplyDrafts(); break;
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
    case HitKind::Choice: OpenChoice(h); break;
    case HitKind::Option: {
        const Control& c = *win.cards[h.a].c;
        const double v = c.choices[h.b];
        CloseChoice();
        Change(c, v, 0);
        break;
    }
    case HitKind::Track: break;
    }
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
void Input(float dt) {
    float px = -1, py = -1;
    const bool inside = Cursor(&px, &py);
    if (!mouseDown) {
        int found = -1;
        if (inside)
            for (int pass = 0; pass < 2 && found < 0; ++pass)
                for (size_t i = 0; i < win.hits.size(); ++i) {
                    if ((pass == 0) != (win.hits[i].area == Area::Popup)) continue;
                    if (Inside(win.hits[i], px, py)) { found = static_cast<int>(i); break; }
                }
        SetHover(found);
    }
    const int vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    const bool down = inside && (GetAsyncKeyState(vk) & 0x8000) != 0;
    if (down && !mouseDown) {
        pressed = hovered;
        if (openChoice >= 0 && (pressed < 0 || win.hits[pressed].kind != HitKind::Option)) { CloseChoice(); pressed = -1; }
        else if (pressed >= 0 && win.hits[pressed].kind == HitKind::Track) { dragCard = win.hits[pressed].a; DragSlider(dragCard, px); }
        else if (px >= kMainX + kContentW && px < kMainX + kMainW && py >= kMainY && py < kMainY + kMainH && MaxMain() > 0) {
            scrollDrag = Drag::Main;
            dragStartY = py;
            dragStartScroll = win.mainTarget;
        } else if (px >= 1 + kSideInner && px < kSideW && py >= 1 && py < kH - 1 && MaxSide() > 0) {
            scrollDrag = Drag::Side;
            dragStartY = py;
            dragStartScroll = win.sideTarget;
        }
    } else if (down && mouseDown) {
        if (dragCard >= 0) DragSlider(dragCard, px);
        if (scrollDrag == Drag::Main)
            win.mainTarget = win.mainScroll = std::clamp(dragStartScroll + (py - dragStartY) * win.pages[win.page].height / (kMainH - 2 * kBar), 0.f, MaxMain());
        else if (scrollDrag == Drag::Side)
            win.sideTarget = win.sideScroll = std::clamp(dragStartScroll + (py - dragStartY) * win.sideHeight / (kH - 2 - 2 * kBar), 0.f, MaxSide());
    } else if (!down && mouseDown) {
        if (dragCard < 0 && scrollDrag == Drag::None && pressed >= 0 && pressed < static_cast<int>(win.hits.size()) && Inside(win.hits[pressed], px, py)) {
            const Hit h = win.hits[pressed];  // a copy: a click may add hits (a page built on demand)
            Click(h);
        }
        dragCard = pressed = -1;
        scrollDrag = Drag::None;
    }
    mouseDown = down;
    // the wheel: 100 px a notch, eased like the browser's smooth scrolling
    const float wheel = inside ? WheelDelta() : 0;
    if (wheel != 0 && openChoice < 0) {
        if (px >= 0 && px < kSideW) win.sideTarget = std::clamp(win.sideTarget - wheel * 100, 0.f, MaxSide());
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
    if (win.mainScroll != mainBefore) { Move(win.mainContent.Get(), 0, -win.mainScroll); CloseChoice(); }
    if (win.sideScroll != sideBefore) Move(win.sideContent.Get(), 0, -win.sideScroll);
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

// --------------------------------------------------------------------------- open and close
float ScaleFor() { return std::max(0.4f, std::min(screenW / kW, screenH / kH) * 0.94f); }
// The desktop app's language (preferences.json "epochpact.language", a JSON string); read only.
std::string ReadLanguage() {
    wchar_t buffer[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (!n || n >= MAX_PATH) return "en";
    std::ifstream in(std::filesystem::path(buffer) / L"EpochPact" / L"preferences.json", std::ios::binary);
    if (!in) return "en";
    std::stringstream text;
    text << in.rdbuf();
    JValue prefs, inner;
    if (!json::Parse(text.str(), prefs)) return "en";
    const JValue* stored = prefs["values"].Find("epochpact.language");
    if (!stored || !stored->IsString() || !json::Parse(stored->text, inner) || !inner.IsString()) return "en";
    return inner.text == "tr" || inner.text == "ru" ? inner.text : "en";
}
void Show(bool open) {
    const double start = Ms();
    if (open) {
        Enable(win.listener.Get(), true);
        Enable(win.rootCanvas.Get(), true);
        win.shown = true;
        ++opens;
        timing.framesSinceOpen = 0;
        timing.spikeAfterOpen = 0;
        RequestValues();
        timing.openMs = Ms() - start;
    } else {
        CloseChoice();
        SetHover(-1);
        Enable(win.rootCanvas.Get(), false);
        // A disabled canvas never tells the listener the pointer left: give the world its clicks back.
        bool no = false;
        void* a[]{&no};
        Invoke(Find("LE.dll", "", "UIMouseListener", "set_underMouse", 1), win.listener.Get(), a);
        Enable(win.listener.Get(), false);
        win.shown = false;
        mouseDown = false;
        dragCard = pressed = -1;
        scrollDrag = Drag::None;
        ++closes;
        timing.framesSinceClose = 0;
        timing.spikeAfterClose = 0;
        timing.closeMs = Ms() - start;
    }
}
void Destroy() {
    if (void* go = win.rootGo.Get()) { void* a[]{go}; Invoke(U("Object", "Destroy", 1), nullptr, a); }
    win = Window{};
    jobs.clear();
    hovered = pressed = openChoice = dragCard = -1;
    mouseDown = false;
}
// Build everything ahead of time: the shell, the page shown first, then every other page.
void QueueAll() {
    win = Window{};
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
        win.pages.push_back(std::move(p));
    }
    for (size_t i = 0; i < win.pages.size(); ++i) if (win.pages[i].id == "general") win.page = static_cast<int>(i);
    for (auto step : {BuildBase, BuildTopbar, BuildFooter, BuildSidebarTop, BuildNav, BuildCaption, BuildFrame}) jobs.push_back(step);
    jobs.push_back([] {
        QueuePage(win.page);
        for (size_t i = 0; i < win.pages.size(); ++i) if (static_cast<int>(i) != win.page) QueuePage(static_cast<int>(i));
        jobs.push_back([] {
            win.built = true;
            ShowPage(win.page);
            PlaceThumbs();
            timing.firstBuildMs = timing.buildMs;
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
        timing.buildMs += Ms() - t;
    }
}
bool InGameNow() {
    if (!game::IsOfflinePlay()) return false;
    const auto s = SessionState();
    return s.state == 4 && !s.transitioning;
}

uint64_t nextBuildCheck = 0, inGameSince = 0;
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
    stage = "commands";
    PumpCommands();
    if (!win.shown) return;
    FlushPending();
    stage = "input";
    Input(dt);
    stage = "animate";
    Animate();
    PlaceThumbs();
    if (Now() >= nextRead && commandQueue.empty() && !commandInFlight) RequestValues();
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
    case HitKind::Option: return "option";
    case HitKind::Refresh: return "refresh";
    case HitKind::Apply: return "apply";
    case HitKind::Discard: return "discard";
    case HitKind::Support: return "support";
    case HitKind::PanelRefresh: return "panelrefresh";
    }
    return "?";
}
// "panel read": the state, timings and every clickable place in window css px (a test turns
// them into screen pixels with scale and screen).
std::string State() {
    std::ostringstream out;
    bool ok = false;
    const bool prepared = assets::Prepared(&ok);
    out << "{\"ok\":true,\"key\":" << Json(KeyName(hotkey.load())) << ",\"prepared\":" << Boolean(prepared && ok) << ",\"built\":" << Boolean(win.built)
        << ",\"shown\":" << Boolean(win.shown) << ",\"language\":" << Json(language) << ",\"page\":" << Json(win.built ? win.pages[win.page].id : "")
        << ",\"jobs\":" << jobs.size() << ",\"cards\":" << win.cards.size() << ",\"scale\":" << win.scale << ",\"screen\":[" << screenW << ',' << screenH << ']'
        << ",\"scaleStatic\":" << Boolean(scaleStatic) << ",\"opens\":" << opens.load() << ",\"closes\":" << closes.load() << ",\"faults\":" << faults.load()
        << ",\"broken\":" << Boolean(broken) << ",\"error\":" << Json(lastError) << ",\"scroll\":" << win.mainScroll << ",\"sideScroll\":" << win.sideScroll
        << ",\"pending\":" << pending.size() << ",\"drafts\":" << drafts.size() << ",\"errors\":" << errors.size() << ",\"sending\":" << Json(sendingId)
        << ",\"connection\":" << Json((offlinePlay ? "offline " : "online ") + sessionState) << ",\"fonts\":" << assets::FontReport()
        << ",\"timing\":{\"openMs\":" << timing.openMs << ",\"closeMs\":" << timing.closeMs << ",\"pageMs\":" << timing.pageMs << ",\"buildMs\":" << timing.buildMs
        << ",\"firstBuildMs\":" << timing.firstBuildMs << ",\"frameAvgClosed\":" << timing.frameAvgClosed << ",\"frameAvgOpen\":" << timing.frameAvgOpen
        << ",\"stepAvgOpen\":" << timing.stepAvgOpen << ",\"stepMaxOpen\":" << timing.stepMaxOpen << ",\"idleCostMax\":" << timing.idleCostMax
        << ",\"spikeAfterOpen\":" << timing.spikeAfterOpen << ",\"spikeAfterClose\":" << timing.spikeAfterClose << ",\"framesOpen\":" << timing.framesOpen
        << ",\"slowOpen\":" << timing.slowOpen << ",\"framesClosed\":" << timing.framesClosed << ",\"slowClosed\":" << timing.slowClosed << '}';
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
    for (const auto& [id, v] : values) { out << (comma ? "," : "") << Json(id) << ':' << v; comma = true; }
    out << "},\"feedback\":{";
    comma = false;
    for (const auto& k : win.cards) {
        if (k.feedbackShown.empty() || k.feedbackShown == "\x01") continue;
        out << (comma ? "," : "") << Json(k.c->id) << ':' << Json(k.feedbackShown);
        comma = true;
    }
    out << "},\"heights\":{";
    comma = false;
    for (const auto& p : win.pages) { out << (comma ? "," : "") << Json(p.id) << ':' << p.height; comma = true; }
    out << "},\"hits\":[";
    comma = false;
    for (const auto& h : win.hits) {
        float x, y;
        if (!h.visible || !Placed(h, &x, &y)) continue;
        const bool cardHit = h.a >= 0 && h.a < static_cast<int>(win.cards.size()) && h.kind != HitKind::Nav && h.kind != HitKind::Link;
        out << (comma ? "," : "") << "{\"kind\":\"" << KindName(h.kind) << "\",\"a\":" << h.a << ",\"b\":" << h.b
            << ",\"id\":" << Json(cardHit ? win.cards[h.a].c->id : "") << ",\"x\":" << x << ",\"y\":" << y << ",\"w\":" << h.w << ",\"h\":" << h.h << '}';
        comma = true;
    }
    out << "]}";
    return out.str();
}
}  // namespace

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
}  // namespace

bool Init() {
    std::ifstream in(KeyFile());
    std::string saved;
    if (in >> saved) { const int code = KeyCode(saved); if (code >= 0) hotkey = code; }
    std::string why;
    if (!assets::Start(&why)) Log("in-game window unavailable: %s", why.c_str());
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
    if (!win.shown && win.built && jobs.empty() && !toggle && commandQueue.empty() && !commandInFlight) return;
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
        if (timing.framesSinceOpen > 2) timing.stepMaxOpen = std::max(timing.stepMaxOpen, cost);
    } else if (win.built && jobs.empty()) timing.idleCostMax = std::max(timing.idleCostMax, cost);
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

std::string Panel(const std::string& action) {
    if (action == "read") return Run([] { return State(); });
    if (action != "0" && action != "1") return "{\"ok\":false,\"error\":\"use panel read|0|1\"}";
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
    typeRoots.clear();
    assets::Release();
}

}  // namespace ep::ingame
