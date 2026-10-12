// Interface font and text size. Display only and session only: each text's
// own font, material and sizes are remembered on first change and restored
// exactly. A chosen Windows font gets the game's fonts as fallbacks, so
// characters and symbols it lacks still render.
#include "fonts.hpp"
#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "lifecycle.hpp"
#include "mainthread.hpp"
#include "managed.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace ep::fonts {
namespace {
using namespace managed;
using MethodInfo = const il2cpp::Method*;
auto& api() { return il2cpp::api(); }

struct Saved { std::unique_ptr<Root> text, font, material; float size, min, max; };
std::map<void*, Saved> texts;               // TextMeshProUGUI -> its own values
std::map<std::string, std::unique_ptr<Root>> assets;  // family -> created font asset
std::set<void*> fallbacksAdded;             // original fonts already added as fallbacks
std::string family = "game";
double scale = 1;
std::atomic<bool> active{false};
uint64_t applied = 0, enables = 0, restored = 0, faults = 0, created = 0;
game::MethodRef m_enable;
void (*o_enable)(void*, MethodInfo) = nullptr;

game::MethodRef T(const char* cls, const char* name, int arity) {
    static std::map<std::string, game::MethodRef> methods;
    const std::string key = std::string(cls) + "." + name + ":" + std::to_string(arity);
    if (auto it = methods.find(key); it != methods.end()) return it->second;
    auto m = game::FindMethod("Unity.TextMeshPro.dll", "TMPro", cls, name, arity);
    if (!m) throw std::runtime_error(std::string("Font method unavailable in this game build: ") + cls + "." + name);
    return methods.emplace(key, m).first->second;
}
const il2cpp::Method* Typed(const char* image, const char* ns, const char* cls, const char* name, std::initializer_list<const char*> types) {
    auto* c = game::FindClass(image, ns, cls);
    void* iterator = nullptr;
    while (auto* m = c ? api().class_get_methods(c, &iterator) : nullptr) {
        if (std::strcmp(api().method_get_name(m), name) || api().method_get_param_count(m) != types.size()) continue;
        bool match = true; uint32_t i = 0;
        for (const char* expected : types) {
            char* actual = api().type_get_name(api().method_get_param(m, i++));
            match &= actual && std::string(actual) == expected;
            if (actual) api().free(actual);
        }
        if (match) return m;
    }
    throw std::runtime_error(std::string("Font method overload unavailable: ") + cls + "." + name);
}
bool Custom() { return family != "game" || scale != 1; }

void* FontAsset(const std::string& name) {
    if (auto it = assets.find(name); it != assets.end() && it->second->Get()) return it->second->Get();
    Root familyName(api().string_new(name.c_str())), style(api().string_new("Regular"));
    int pointSize = 90;
    void* a[]{familyName.Get(), style.Get(), &pointSize};
    void* asset = Invoke(Typed("Unity.TextMeshPro.dll", "TMPro", "TMP_FontAsset", "CreateFontAsset", {"System.String", "System.String", "System.Int32"}), nullptr, a);
    if (!asset) throw std::runtime_error("Windows could not provide the font \"" + name + "\".");
    ++created;
    return assets.insert_or_assign(name, std::make_unique<Root>(asset)).first->second->Get();
}
// Missing characters fall back to the text's own game font.
void AddFallback(void* asset, void* original) {
    if (!original || original == asset || fallbacksAdded.contains(original)) return;
    void* list = Invoke(T("TMP_FontAsset", "get_fallbackFontAssetTable", 0), asset);
    if (!list) return;  // a created font asset normally has an empty fallback list
    void* a[]{original};
    Invoke(Method(list, "Add", 1), list, a);
    fallbacksAdded.insert(original);
}
void Restore(void* text, Saved& saved) {
    if (!game::IsAlive(text)) return;
    void* fa[]{saved.font->Get()}; Invoke(T("TMP_Text", "set_font", 1), text, fa);
    if (saved.material) { void* ma[]{saved.material->Get()}; Invoke(T("TMP_Text", "set_fontSharedMaterial", 1), text, ma); }
    float size = saved.size, min = saved.min, max = saved.max;
    void* sa[]{&size}; Invoke(T("TMP_Text", "set_fontSize", 1), text, sa);
    void* mi[]{&min}; Invoke(T("TMP_Text", "set_fontSizeMin", 1), text, mi);
    void* mx[]{&max}; Invoke(T("TMP_Text", "set_fontSizeMax", 1), text, mx);
    ++restored;
}
void Apply(void* text) {
    if (!text || !game::IsAlive(text)) return;
    auto it = texts.find(text);
    if (it == texts.end()) {
        if (!Custom()) return;
        if (texts.size() >= 20000) {  // drop destroyed texts
            for (auto e = texts.begin(); e != texts.end();) e = game::IsAlive(e->first) ? std::next(e) : texts.erase(e);
            if (texts.size() >= 20000) return;
        }
        void* font = Invoke(T("TMP_Text", "get_font", 0), text);
        if (!font) return;
        void* material = Invoke(T("TMP_Text", "get_fontSharedMaterial", 0), text);
        Saved saved{std::make_unique<Root>(text), std::make_unique<Root>(font), material ? std::make_unique<Root>(material) : nullptr,
            Value<float>(T("TMP_Text", "get_fontSize", 0), text), Value<float>(T("TMP_Text", "get_fontSizeMin", 0), text), Value<float>(T("TMP_Text", "get_fontSizeMax", 0), text)};
        it = texts.emplace(text, std::move(saved)).first;
    }
    Saved& saved = it->second;
    if (!Custom()) { Restore(text, saved); texts.erase(it); return; }
    if (family == "game") {
        void* current = Invoke(T("TMP_Text", "get_font", 0), text);
        if (current != saved.font->Get()) {
            void* fa[]{saved.font->Get()}; Invoke(T("TMP_Text", "set_font", 1), text, fa);
            if (saved.material) { void* ma[]{saved.material->Get()}; Invoke(T("TMP_Text", "set_fontSharedMaterial", 1), text, ma); }
        }
    } else {
        void* asset = FontAsset(family);
        AddFallback(asset, saved.font->Get());
        if (Invoke(T("TMP_Text", "get_font", 0), text) != asset) { void* fa[]{asset}; Invoke(T("TMP_Text", "set_font", 1), text, fa); }
    }
    float size = static_cast<float>(saved.size * scale), min = static_cast<float>(saved.min * scale), max = static_cast<float>(saved.max * scale);
    void* sa[]{&size}; Invoke(T("TMP_Text", "set_fontSize", 1), text, sa);
    void* mi[]{&min}; Invoke(T("TMP_Text", "set_fontSizeMin", 1), text, mi);
    void* mx[]{&max}; Invoke(T("TMP_Text", "set_fontSizeMax", 1), text, mx);
    ++applied;
}
void d_enable(void* self, MethodInfo m) {
    o_enable(self, m);
    if (!active.load() || lifecycle::Stopping()) return;
    ++enables;
    if (!game::Guarded([&] { Apply(self); }, nullptr)) ++faults;
}
std::vector<void*> AllTexts() {
    std::vector<void*> out;
    auto* cls = game::FindClass("Unity.TextMeshPro.dll", "TMPro", "TextMeshProUGUI");
    Root type(api().type_get_object(api().class_get_type(cls)));
    bool inactive = true; void* a[]{type.Get(), &inactive};
    Root array(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, a));
    const auto n = Get<uintptr_t>(array.Get(), 0x18);
    for (uintptr_t i = 0; i < n && i < 50000; ++i) out.push_back(Get<void*>(array.Get(), 0x20 + 8 * i));
    return out;
}
void ApplyAll() {
    for (void* text : AllTexts()) if (!game::Guarded([&] { Apply(text); }, nullptr)) ++faults;
    if (!Custom()) {  // texts not found any more (inactive scenes) are restored too
        for (auto& [text, saved] : texts) game::Guarded([&] { Restore(text, saved); }, nullptr);
        texts.clear();
    }
}
std::string State() {
    std::ostringstream out;
    out << "{\"ok\":true,\"family\":" << Json(family) << ",\"scale\":" << scale << ",\"texts\":" << texts.size() << ",\"applied\":" << applied
        << ",\"enables\":" << enables << ",\"restored\":" << restored << ",\"fontsCreated\":" << created << ",\"faults\":" << faults << ",\"families\":[";
    // Windows font families the game can load.
    std::set<std::string> names;
    try {
        Root array(Invoke(game::FindMethod("UnityEngine.TextRenderingModule.dll", "UnityEngine", "Font", "GetOSInstalledFontNames", 0)));
        const auto n = Get<uintptr_t>(array.Get(), 0x18);
        for (uintptr_t i = 0; i < n && i < 2000; ++i) if (void* s = Get<void*>(array.Get(), 0x20 + 8 * i)) names.insert(Text(s));
    } catch (...) {}
    bool comma = false;
    for (const auto& name : names) { if (comma) out << ','; comma = true; out << Json(name); }
    return out.str() + "]}";
}
std::string Run(std::function<std::string()> job) {
    std::string result, why;
    if (!mainthread::Run([&] {
        try { result = job(); }
        catch (const std::exception& e) { result = "{\"ok\":false,\"error\":" + Json(e.what()) + "}"; }
    }, 10000, &why)) return "{\"ok\":false,\"error\":" + Json(why) + "}";
    return result;
}
}  // namespace

bool Init() {
    m_enable = game::FindMethod("Unity.TextMeshPro.dll", "TMPro", "TextMeshProUGUI", "OnEnable", 0);
    Log("fonts: %s", m_enable ? "ready" : "unavailable");
    return static_cast<bool>(m_enable);
}
std::string Read() { return Run([] { return State(); }); }
std::string Set(const std::string& key, const std::string& value) {
    std::string decoded = value;
    for (size_t at; (at = decoded.find("%20")) != std::string::npos;) decoded.replace(at, 3, " ");
    if (key == "family") {
        if (decoded.empty() || decoded.size() > 64 || decoded.find_first_of("\r\n\t<>") != std::string::npos)
            return "{\"ok\":false,\"error\":\"use fontset family <Windows font name | game>\"}";
    } else if (key == "scale") {
        char* end = nullptr; const double v = std::strtod(decoded.c_str(), &end);
        if (!end || *end || !std::isfinite(v) || v < 0.7 || v > 1.5) return "{\"ok\":false,\"error\":\"use fontset scale 0.7-1.5\"}";
    } else return "{\"ok\":false,\"error\":\"use fontset family <name|game> | fontset scale 0.7-1.5\"}";
    return Run([=] {
        if (!game::IsOfflinePlay()) throw std::runtime_error("Font settings work in offline play only.");
        if (!m_enable) throw std::runtime_error("Font settings are unavailable in this game build.");
        const std::string previousFamily = family; const double previousScale = scale;
        if (key == "family") {
            family = decoded == "game" || decoded == "Game" ? "game" : decoded;
            if (family != "game") {
                try { FontAsset(family); }
                catch (...) { family = previousFamily; throw; }
            }
        } else scale = std::strtod(decoded.c_str(), nullptr);
        if (Custom() && !hook::IsInstalled(m_enable.code)) {
            std::string why;
            if (!hook::Install(m_enable.code, reinterpret_cast<void*>(&d_enable), reinterpret_cast<void**>(&o_enable), &why)) {
                family = previousFamily; scale = previousScale;
                throw std::runtime_error("Font hook refused: " + why);
            }
        }
        active = Custom();
        ApplyAll();
        return State();
    });
}
void ReleaseRoots() { texts.clear(); assets.clear(); fallbacksAdded.clear(); active = false; }

}  // namespace ep::fonts
