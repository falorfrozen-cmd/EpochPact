// Drawing primitives of the in-game window: Unity calls, images, texts and the theme's words.
// See ingame_ui.hpp.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_ui.hpp"
#include "ingame_i18n.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ep::ingame::ui {
using namespace managed;
using assets::Face;

// ------------------------------------------------------------------------------ basic types
Color Hex(std::string_view hex) {
    const auto part = [&](size_t at) { return static_cast<float>(std::strtol(std::string(hex.substr(at, 2)).c_str(), nullptr, 16)) / 255.f; };
    if ((hex.size() != 7 && hex.size() != 9) || hex[0] != '#') return {1, 0, 1, 1};
    return {part(1), part(3), part(5), hex.size() == 9 ? part(7) : 1.f};
}
Color Alpha(Color c, float a) { return {c.r, c.g, c.b, a}; }

// ----------------------------------------------------------------------------- Unity calls
const il2cpp::Method* Find(const char* image, const char* ns, const char* cls, const char* name, int count, bool required) {
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
namespace {
std::map<std::string, std::unique_ptr<managed::Root>> typeRoots;
}  // namespace
void ReleaseTypes() { typeRoots.clear(); }
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
void* Picture(void* parent, const char* name, float x, float y, float w, float h, const std::string& file, bool sliced, Color c, bool hollow) {
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
void* Fill(void* parent, const char* name, float x, float y, float w, float h, Color c, int radius) {
    if (!radius) return Solid(parent, name, x, y, w, h, c);
    return Picture(parent, name, x, y, w, h, "fill-r" + std::to_string(radius) + ".png", true, c);
}
// A 1px CSS border (with border-radius).
void* Outline(void* parent, const char* name, float x, float y, float w, float h, Color c, int radius) {
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
bool scaleStatic = false;
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
    // This TMP lays out with textWrappingMode; the older enableWordWrapping reaches it only later,
    // after the text was measured (every paragraph then measures one line).
    if (const auto* mode = Find("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_textWrappingMode", 1, false)) {
        int wrapping = wrap ? 1 : 0;  // TextWrappingModes.Normal / NoWrap
        void* ma[]{&wrapping};
        Invoke(mode, text, ma);
    } else Invoke(T("set_enableWordWrapping", 1), text, wa);
    Ink(text, s.color);
    SetText(text, s.upper ? Upper(value) : value);
    return text;
}
float TextWidth(void* text) { return Value<V2>(Sig("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "GetPreferredValues", {}), text).x; }
struct Measure { int lines = 0, first = 0; float marginBefore = 0, marginAfter = 0; };
float TextAdvance(Face face, float size, const std::string& text, float spacing) {
    static std::map<int, std::unordered_map<char32_t, float>> tables;
    auto& table = tables[static_cast<int>(face)];
    if (table.empty()) {
        const char* key = face == Face::BodySemi ? "semi" : face == Face::BodyBold ? "bold" : face == Face::Serif ? "serif" : face == Face::SerifBold ? "serifBold" : "body";
        for (const auto& [cp, v] : D()["advances"][key].members) table.emplace(static_cast<char32_t>(std::strtoul(cp.c_str(), nullptr, 10)), static_cast<float>(v.Num(0.6)));
        if (table.empty()) table.emplace(0, 0.6f);
    }
    float em = 0;
    int count = 0;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = text[i];
        char32_t cp = c;
        size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (n > 1) cp = c & (0xFF >> (n + 1));
        for (size_t k = 1; k < n && i + k < text.size(); ++k) cp = cp << 6 | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        i += n;
        auto it = table.find(cp);
        em += it != table.end() ? it->second : 0.6f;
        ++count;
    }
    return em * size + spacing * count;
}
std::unordered_map<void*, Measure> measures;
int DrawnLines(void* text) {
    void* info = Invoke(T("get_textInfo", 0), text);
    static size_t offset = 0;
    if (!offset) {
        const auto* f = il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(info), "lineCount");
        if (!f) throw std::runtime_error("TMP_TextInfo.lineCount unavailable");
        offset = il2cpp::api().field_get_offset(f);
    }
    return std::max(1, Get<int>(info, offset));
}
float MarginWidth(void* text) {
    static size_t offset = 0;
    if (!offset) {
        const auto* f = il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(text), "m_marginWidth");
        if (!f) return -1;
        offset = il2cpp::api().field_get_offset(f);
    }
    return Get<float>(text, offset);
}
// TextMeshProUGUI.ForceMeshUpdate: TMP_Text's own is an empty virtual, and runtime_invoke calls
// the method it is given (no virtual dispatch), so the text class's override is called.
void Layout(void* text) {
    bool ignoreActive = true, reparse = true;
    void* a[]{&ignoreActive, &reparse};
    Invoke(Find("Unity.TextMeshPro.dll", "TMPro", "TextMeshProUGUI", "ForceMeshUpdate", 2), text, a);
}
// The lines of a text as laid out now (a layout pass of its own).
int TextLines(void* text) {
    Measure m;
    m.marginBefore = MarginWidth(text);
    Layout(text);
    m.lines = m.first = DrawnLines(text);
    m.marginAfter = MarginWidth(text);
    measures[text] = m;
    return m.lines;
}
int MeasuredLines(void* text) {
    auto it = measures.find(text);
    return it == measures.end() ? -1 : it->second.lines;
}
int FirstPassLines(void* text) {
    auto it = measures.find(text);
    return it == measures.end() ? -1 : it->second.first;
}
std::string MeasureDetail(void* text) {
    auto it = measures.find(text);
    if (it == measures.end()) return "null";
    char b[160];
    std::snprintf(b, sizeof b, "{\"first\":%d,\"marginBefore\":%.1f,\"marginAfter\":%.1f,\"marginNow\":%.1f}", it->second.first, it->second.marginBefore,
                  it->second.marginAfter, MarginWidth(text));
    return b;
}
void ForgetMeasures() { measures.clear(); }
// A wrapped paragraph; returns its height.
float Paragraph(void* parent, const char* name, float x, float y, float w, const Style& s, const std::string& value) {
    void* t = TextBox(parent, name, x, y, w, LineHeight(s), s, value);
    return value.empty() ? 0 : TextLines(t) * LineHeight(s);  // an empty p has no line box
}

// -------------------------------------------------------------------------------- the data
const JValue& D() { return assets::Data(); }
Color C(const char* token) { return Hex(D()["theme"][token].Str("#ff00ff")); }

// The desktop app's words: ui.json carries every text this window shows, translated at build
// time by the app's own translator (ui/i18n.js).
std::string language = "en";
// A text known when the window is built: its translation from the build (a hash index of
// ui.json "i18n"); any other text: the app's translator, here (ingame_i18n.cpp), remembered.
std::string Tr(const std::string& en) {
    if (language == "en" || en.empty()) return en;
    struct Language {
        std::unordered_map<std::string_view, const std::string*> built;
        std::unique_ptr<i18n::Translator> live;
        std::unordered_map<std::string, std::string> seen;
    };
    static std::map<std::string, Language> languages;
    Language& l = languages[language];
    if (l.built.empty())
        for (const auto& [source, target] : D()["i18n"][language].members)
            if (target.IsString()) l.built.emplace(source, &target.text);
    if (auto it = l.built.find(en); it != l.built.end()) return *it->second;
    if (auto it = l.seen.find(en); it != l.seen.end()) return it->second;
    if (!l.live) l.live = std::make_unique<i18n::Translator>(D()["dictionaries"][language], language == "tr" || language == "ru");
    if (l.seen.size() > 4096) l.seen.clear();  // live texts (numbers, names) never pile up
    return l.seen.emplace(en, l.live->Translate(en).value_or(en)).first->second;
}
// The English text with its values, then translated whole, as the app's translator sees the page.
std::string Fmt(const std::string& templ, const std::vector<std::string>& args) {
    std::string text = templ;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string key = "{" + std::to_string(i) + "}";
        for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + args[i].size())) text.replace(at, key.size(), args[i]);
    }
    return Tr(text);
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

}  // namespace ep::ingame::ui
