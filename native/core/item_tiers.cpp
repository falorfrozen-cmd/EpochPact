// Affix tiers on item icons and tier colours in item tooltips. Display only:
// item data is read and never changed; offline play only.
#include "item_tiers.hpp"
#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "lifecycle.hpp"
#include "mainthread.hpp"
#include "managed.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <atomic>
#include <cstdio>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace ep::tiers {
namespace {
using namespace managed;
using MethodInfo = const il2cpp::Method*;
struct Color { float r, g, b, a; };
struct Vec2 { float x, y; };
constexpr std::array<uint32_t, 8> kDefaultPalette{0xFFFFFF, 0x4DA6FF, 0xFFD84D, 0xE07A2E, 0xB36BFF, 0x4CD964, 0xE0315A, 0x7FE5F0};
std::array<uint32_t, 8> palette = kDefaultPalette;
std::atomic<bool> iconsOn{false}, tooltipsOn{false};
std::map<void*, std::unique_ptr<Root>> labels;  // InventoryItemUI -> our TMP label
struct SavedColor { std::unique_ptr<Root> text; Color color; };
std::map<void*, SavedColor> tooltipColors;
uint64_t iconUpdates = 0, tooltipUpdates = 0, faults = 0, scanned = 0, noItem = 0, noTiers = 0, noQuantity = 0, created = 0;
int lastTooltipTier = -1;
game::MethodRef m_updateContent, m_affixData, m_enable, m_affixesText, m_affixInit;
void (*o_update)(void*, MethodInfo) = nullptr;
void (*o_enable)(void*, MethodInfo) = nullptr;
void (*o_affixData)(void*, void*, MethodInfo) = nullptr;
// UITooltipItem.UpdateAffixesText(ItemData, ItemTooltipInfo) and
// UITooltipItemAffix.Init(ValueTuple<string,bool>): both value-type arguments
// are larger than 8 bytes and arrive by hidden pointer on x64.
void (*o_affixesText)(void*, void*, void*, MethodInfo) = nullptr;
void (*o_affixInit)(void*, void*, MethodInfo) = nullptr;
uint64_t tooltipMismatches = 0;
bool TooltipHooks() { return m_affixesText && m_affixInit; }

size_t F(const char* cls, const char* field, const char* ns = "") {
    static std::map<std::string, size_t> fields;
    const std::string key = std::string(ns) + ":" + cls + ":" + field;
    if (auto it = fields.find(key); it != fields.end()) return it->second;
    return fields.emplace(key, Offset(cls, field, ns)).first->second;
}
game::MethodRef M(const char* cls, const char* name, int arity, const char* ns = "", const char* image = "LE.dll") {
    static std::map<std::string, game::MethodRef> methods;
    const std::string key = std::string(image) + ":" + ns + ":" + cls + ":" + name + ":" + std::to_string(arity);
    if (auto it = methods.find(key); it != methods.end()) return it->second;
    auto m = game::FindMethod(image, ns, cls, name, arity);
    if (!m) throw std::runtime_error(std::string("required tier display method unavailable: ") + cls + "." + name);
    methods.emplace(key, m);
    return m;
}
const il2cpp::Method* Signature(const char* cls, const char* name, std::initializer_list<const char*> parameters,
                                const char* ns = "UnityEngine", const char* image = "UnityEngine.CoreModule.dll") {
    static std::map<std::string, const il2cpp::Method*> signatures;
    std::string key = std::string(image) + ":" + ns + ":" + cls + ":" + name;
    for (const char* p : parameters) key += std::string(":") + p;
    if (auto it = signatures.find(key); it != signatures.end()) return it->second;
    auto& a = il2cpp::api();
    auto* c = game::FindClass(image, ns, cls);
    void* iterator = nullptr;
    while (auto* m = a.class_get_methods(c, &iterator)) {
        if (std::string(a.method_get_name(m)) != name || a.method_get_param_count(m) != parameters.size()) continue;
        bool match = true; unsigned i = 0;
        for (const char* expected : parameters) {
            char* actual = a.type_get_name(a.method_get_param(m, i++));
            match &= actual && std::string(actual) == expected;
            if (actual) a.free(actual);
        }
        if (match) { signatures.emplace(key, m); return m; }
    }
    throw std::runtime_error(std::string("Missing overload: ") + cls + "." + name);
}
Color ToColor(uint32_t rgb) { return {((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, 1.f}; }
std::string Hex(uint32_t rgb) { char b[8]; std::snprintf(b, sizeof b, "%06X", rgb & 0xFFFFFF); return b; }
uint32_t TierColor(int displayTier) { return palette[static_cast<size_t>(std::clamp(displayTier, 1, 8) - 1)]; }
void SetActive(void* component, bool on) {
    Root go(Invoke(M("Component", "get_gameObject", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), component));
    if (Value<bool>(Method(go.Get(), "get_activeSelf", 0), go.Get()) == on) return;
    void* a[]{&on}; Invoke(Method(go.Get(), "SetActive", 1), go.Get(), a);
}

std::vector<int> Tiers(void* item) {
    std::vector<int> tiers;
    void* affixes = Get<void*>(item, F("ItemData", "affixes"));
    if (!affixes) return tiers;
    for (void* affix : Entries(affixes)) {
        if (!affix) continue;
        const int tier = Value<int>(M("ItemAffix", "get_DisplayTier", 0), affix);
        if (tier >= 1 && tier <= 8) tiers.push_back(tier);
    }
    std::sort(tiers.rbegin(), tiers.rend());
    return tiers;
}
std::string LabelText(const std::vector<int>& tiers) {
    std::string text;
    for (size_t i = 0; i < tiers.size(); ++i) {
        if (i) text += ' ';
        text += "<color=#" + Hex(TierColor(tiers[i])) + ">" + std::to_string(tiers[i]) + "</color>";
    }
    return text;
}
void* CreateLabel(void* ui) {
    void* quantity = Get<void*>(ui, F("InventoryItemUI", "quantityText"));
    if (!quantity || !game::IsAlive(quantity)) { ++noQuantity; return nullptr; }
    Root source(Invoke(M("Component", "get_gameObject", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), quantity));
    Root sourceTransform(Invoke(M("Component", "get_transform", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), quantity));
    Root parent(Invoke(M("Transform", "get_parent", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), sourceTransform.Get()));
    bool world = false; void* a[]{source.Get(), parent.Get(), &world};
    Root clone(Invoke(Signature("Object", "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform", "System.Boolean"}), nullptr, a));
    Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("Unity.TextMeshPro.dll", "TMPro", "TMP_Text"))));
    void* ta[]{type.Get()};
    void* text = Invoke(Signature("GameObject", "GetComponent", {"System.Type"}), clone.Get(), ta);
    if (!text) return nullptr;
    Root label(text);
    bool on = true, off = false; float size = 13; int bold = 1;  // TMPro.FontStyles.Bold
    int topLeft = 257;  // TMPro.TextAlignmentOptions.TopLeft
    void* onArgs[]{&on}; void* offArgs[]{&off}; void* sizeArgs[]{&size}; void* alignArgs[]{&topLeft};
    Invoke(Method(text, "set_richText", 1), text, onArgs);
    Invoke(Method(text, "set_enableAutoSizing", 1), text, offArgs);
    Invoke(Method(text, "set_fontSize", 1), text, sizeArgs);
    Invoke(Method(text, "set_alignment", 1), text, alignArgs);
    Invoke(Method(text, "set_raycastTarget", 1), text, offArgs);
    void* boldArgs[]{&bold}; Invoke(Method(text, "set_fontStyle", 1), text, boldArgs);
    Root rect(Invoke(M("Component", "get_transform", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), text));
    Vec2 minAnchor{0, 1}, maxAnchor{1, 1}, pivot{0, 1}, pos{3, -1}, sizeDelta{-6, 16};
    void* mn[]{&minAnchor}; void* mx[]{&maxAnchor}; void* pv[]{&pivot}; void* ps[]{&pos}; void* sz[]{&sizeDelta};
    Invoke(M("RectTransform", "set_anchorMin", 1, "UnityEngine", "UnityEngine.CoreModule.dll"), rect.Get(), mn);
    Invoke(M("RectTransform", "set_anchorMax", 1, "UnityEngine", "UnityEngine.CoreModule.dll"), rect.Get(), mx);
    Invoke(M("RectTransform", "set_pivot", 1, "UnityEngine", "UnityEngine.CoreModule.dll"), rect.Get(), pv);
    Invoke(M("RectTransform", "set_anchoredPosition", 1, "UnityEngine", "UnityEngine.CoreModule.dll"), rect.Get(), ps);
    Invoke(M("RectTransform", "set_sizeDelta", 1, "UnityEngine", "UnityEngine.CoreModule.dll"), rect.Get(), sz);
    Root name(il2cpp::api().string_new("EpochPact tier label"));
    void* na[]{name.Get()}; Invoke(M("Object", "set_name", 1, "UnityEngine", "UnityEngine.CoreModule.dll"), clone.Get(), na);
    return text;
}
void UpdateIcon(void* ui) {
    auto it = labels.find(ui);
    void* label = it != labels.end() ? it->second->Get() : nullptr;
    if (it != labels.end() && (!label || !game::IsAlive(label))) { labels.erase(it); label = nullptr; }
    std::vector<int> tiers;
    if (iconsOn.load()) {
        void* entry = Get<void*>(ui, F("InventoryItemUI", "<EntryRef>k__BackingField"));
        void* item = entry ? Get<void*>(entry, F("ItemContainerEntry", "data")) : nullptr;
        if (item) { tiers = Tiers(item); if (tiers.empty()) ++noTiers; } else ++noItem;
    }
    const bool show = iconsOn.load() && !tiers.empty();
    if (!label) {
        if (!show || labels.size() >= 4096) return;
        label = CreateLabel(ui);
        if (!label) return;
        ++created;
        labels.emplace(ui, std::make_unique<Root>(label));
    }
    if (show) { Root text(il2cpp::api().string_new(LabelText(tiers).c_str())); void* a[]{text.Get()}; Invoke(Method(label, "set_text", 1), label, a); }
    SetActive(label, show);
}
void d_update(void* self, MethodInfo m) {
    o_update(self, m);
    if (lifecycle::Stopping() || (!iconsOn.load() && labels.empty())) return;
    ++iconUpdates;
    if (!game::Guarded([&] { UpdateIcon(self); }, nullptr)) ++faults;
}
// Opening a panel re-enables pooled item icons without UpdateContent.
void d_enable(void* self, MethodInfo m) {
    o_enable(self, m);
    if (lifecycle::Stopping() || (!iconsOn.load() && labels.empty())) return;
    ++iconUpdates;
    if (!game::Guarded([&] { UpdateIcon(self); }, nullptr)) ++faults;
}
void ColorText(void* text, uint32_t rgb) {
    if (!text || !game::IsAlive(text)) return;
    if (!tooltipColors.contains(text)) {
        if (tooltipColors.size() >= 1024) return;
        const auto original = Value<Color>(M("Graphic", "get_color", 0, "UnityEngine.UI", "UnityEngine.UI.dll"), text);
        tooltipColors.emplace(text, SavedColor{std::make_unique<Root>(text), original});
    }
    Color c = ToColor(rgb); void* a[]{&c};
    Invoke(M("Graphic", "set_color", 1, "UnityEngine.UI", "UnityEngine.UI.dll"), text, a);
}
void d_affixData(void* self, void* info, MethodInfo m) {
    o_affixData(self, info, m);
    if (!tooltipsOn.load() || lifecycle::Stopping() || !info) return;
    ++tooltipUpdates;
    if (!game::Guarded([&] {
        const int tier = Value<int>(M("ItemTooltipAffixInfo", "get_Tier", 0, "LE.UI.Tooltips"), info);
        lastTooltipTier = tier;
        if (tier < 1 || tier > 8) return;
        const uint32_t rgb = TierColor(tier);
        ColorText(Get<void*>(self, F("ItemTooltipAffix", "_tierText", "LE.UI.Tooltips")), rgb);
        for (const char* name : {"_modifier1", "_modifier2"}) {
            void* modifier = Get<void*>(self, F("ItemTooltipAffix", name, "LE.UI.Tooltips"));
            if (modifier && game::IsAlive(modifier)) ColorText(Get<void*>(modifier, F("ItemTooltipModifier", "_modifierText", "LE.UI.Tooltips")), rgb);
        }
    }, nullptr)) ++faults;
}
// Removes our "<color=#RRGGBB>T7</color> " prefix (exactly 26 characters).
void StripTag(void* text) {
    const std::string current = Text(Invoke(Method(text, "get_text", 0), text));
    if (current.size() < 26 || current.rfind("<color=#", 0) != 0 || current.compare(14, 2, ">T") != 0 || current.compare(17, 9, "</color> ") != 0) return;
    Root value(il2cpp::api().string_new(current.substr(26).c_str())); void* a[]{value.Get()};
    Invoke(Method(text, "set_text", 1), text, a);
}
void RestoreColor(void* text, bool strip) {
    auto it = tooltipColors.find(text);
    if (it == tooltipColors.end()) return;
    if (game::IsAlive(text)) {
        Color c = it->second.color; void* a[]{&c};
        Invoke(M("Graphic", "set_color", 1, "UnityEngine.UI", "UnityEngine.UI.dll"), text, a);
        if (strip) StripTag(text);
    }
    tooltipColors.erase(it);
}
// A pooled line is re-used for another affix: return its own colour first.
void d_affixInit(void* self, void* data, MethodInfo m) {
    if (!tooltipColors.empty() && !lifecycle::Stopping())
        game::Guarded([&] { RestoreColor(Get<void*>(self, F("UITooltipItemAffix", "_text")), false); }, nullptr);  // Init replaces the text
    o_affixInit(self, data, m);
}
std::vector<void*> ActiveLines(void* list) {
    std::vector<void*> out;
    if (!list) return out;
    for (void* line : Entries(list)) {
        if (!line || !game::IsAlive(line)) continue;
        Root go(Invoke(M("Component", "get_gameObject", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), line));
        if (Value<bool>(Method(go.Get(), "get_activeSelf", 0), go.Get())) out.push_back(line);
    }
    return out;
}
void MarkLine(void* line, int tier) {
    void* text = Get<void*>(line, F("UITooltipItemAffix", "_text"));
    if (!text || !game::IsAlive(text) || tier < 1 || tier > 8) return;
    // The line keeps the game's own colour (readable on every rarity); only a
    // coloured tier tag is added. The line is remembered so the tag is removed
    // when the setting is switched off.
    const uint32_t rgb = TierColor(tier);
    if (!tooltipColors.contains(text)) {
        if (tooltipColors.size() >= 1024) return;
        tooltipColors.emplace(text, SavedColor{std::make_unique<Root>(text), Value<Color>(M("Graphic", "get_color", 0, "UnityEngine.UI", "UnityEngine.UI.dll"), text)});
    }
    const std::string current = Text(Invoke(Method(text, "get_text", 0), text));
    const std::string tag = "<color=#" + Hex(rgb) + ">T" + std::to_string(tier) + "</color> ";
    if (current.rfind(tag, 0) == 0) return;
    Root value(il2cpp::api().string_new((tag + current).c_str())); void* a[]{value.Get()};
    Invoke(Method(text, "set_text", 1), text, a);
}
void d_affixesText(void* self, void* itemData, void* info, MethodInfo m) {
    o_affixesText(self, itemData, info, m);
    if (!tooltipsOn.load() || lifecycle::Stopping() || !itemData) return;
    ++tooltipUpdates;
    if (!game::Guarded([&] {
        // Regular prefixes and suffixes are listed in the item's own order,
        // each in its own pooled line list; the sealed affix has its own line.
        std::vector<int> prefixes, suffixes; int sealedTier = 0;
        for (void* affix : Entries(Get<void*>(itemData, F("ItemData", "affixes")))) {
            if (!affix) continue;
            const int tier = Value<int>(M("ItemAffix", "get_DisplayTier", 0), affix);
            if (Value<bool>(M("ItemAffix", "get_IsSealed", 0), affix)) {
                if (Value<bool>(M("ItemAffix", "get_IsSealedRegular", 0), affix)) sealedTier = tier;
                continue;
            }
            const int type = Get<int>(affix, F("ItemAffix", "affixType"));
            if (type == 0) prefixes.push_back(tier); else if (type == 1) suffixes.push_back(tier);
        }
        const auto prefixLines = ActiveLines(Get<void*>(self, F("UITooltipItem", "prefixesText")));
        const auto suffixLines = ActiveLines(Get<void*>(self, F("UITooltipItem", "suffixesText")));
        if (prefixLines.size() != prefixes.size() || suffixLines.size() != suffixes.size()) { ++tooltipMismatches; return; }
        for (size_t i = 0; i < prefixes.size(); ++i) MarkLine(prefixLines[i], prefixes[i]);
        for (size_t i = 0; i < suffixes.size(); ++i) MarkLine(suffixLines[i], suffixes[i]);
        lastTooltipTier = prefixes.empty() ? (suffixes.empty() ? lastTooltipTier : suffixes.front()) : prefixes.front();
        void* sealed = Get<void*>(self, F("UITooltipItem", "sealedAffix"));
        if (sealedTier && sealed && game::IsAlive(sealed)) {
            Root go(Invoke(M("Component", "get_gameObject", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), sealed));
            if (Value<bool>(Method(go.Get(), "get_activeSelf", 0), go.Get())) MarkLine(sealed, sealedTier);
        }
    }, nullptr)) ++faults;
}
void RestoreTooltips() {
    std::vector<void*> texts;
    for (auto& [key, saved] : tooltipColors) texts.push_back(key);
    for (void* text : texts) game::Guarded([&] { RestoreColor(text, true); }, nullptr);
    tooltipColors.clear();
}
std::vector<void*> FindObjects(const char* image, const char* ns, const char* cls) {
    std::vector<void*> out;
    auto* klass = game::FindClass(image, ns, cls);
    if (!klass) return out;
    Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(klass)));
    bool inactive = true; void* a[]{type.Get(), &inactive};  // closed panels keep their pooled icons
    Root array(Invoke(M("Object", "FindObjectsOfType", 2, "UnityEngine", "UnityEngine.CoreModule.dll"), nullptr, a));
    const auto n = Get<uintptr_t>(array.Get(), 0x18);
    for (uintptr_t i = 0; i < n && i < 8192; ++i) out.push_back(Get<void*>(array.Get(), 0x20 + 8 * i));
    return out;
}
void RefreshIcons(bool searchScene) {
    if (searchScene)
        for (void* ui : FindObjects("LE.dll", "", "InventoryItemUI")) if (ui && game::IsAlive(ui)) { ++scanned; UpdateIcon(ui); }
    for (auto it = labels.begin(); it != labels.end();) {
        void* ui = it->first; ++it;
        game::Guarded([&] { if (game::IsAlive(ui)) UpdateIcon(ui); }, nullptr);
    }
}
std::string State() {
    std::ostringstream out;
    out << "{\"ok\":true,\"icons\":" << Boolean(iconsOn.load()) << ",\"tooltips\":" << Boolean(tooltipsOn.load()) << ",\"palette\":[";
    for (size_t i = 0; i < palette.size(); ++i) { if (i) out << ','; out << Json(Hex(palette[i])); }
    out << "],\"labels\":" << labels.size() << ",\"coloredTexts\":" << tooltipColors.size() << ",\"iconUpdates\":" << iconUpdates
        << ",\"tooltipUpdates\":" << tooltipUpdates << ",\"tooltipMismatches\":" << tooltipMismatches << ",\"scanned\":" << scanned << ",\"created\":" << created << ",\"noItem\":" << noItem << ",\"noTiers\":" << noTiers << ",\"noQuantityText\":" << noQuantity << ",\"lastTooltipTier\":" << lastTooltipTier << ",\"faults\":" << faults
        << ",\"available\":" << Boolean(m_updateContent && m_enable && TooltipHooks()) << '}';
    return out.str();
}
std::string Run(std::function<std::string()> job) {
    std::string result, why;
    if (!mainthread::Run([&] {
        try { result = job(); }
        catch (const std::exception& e) { result = "{\"ok\":false,\"error\":" + Json(e.what()) + "}"; }
    }, 5000, &why)) return "{\"ok\":false,\"error\":" + Json(why) + "}";
    return result;
}
void Require() {
    if (!game::IsOfflinePlay()) throw std::runtime_error("Tier display works in offline play only.");
    if (!m_updateContent || !m_enable || !TooltipHooks()) throw std::runtime_error("Tier display is unavailable in this game build.");
}
void EnsureHook(const game::MethodRef& ref, void* detour, void** original) {
    std::string why;
    if (!hook::IsInstalled(ref.code) && !hook::Install(ref.code, detour, original, &why)) throw std::runtime_error("Tier display hook refused: " + why);
}
}  // namespace

bool Init() {
    m_updateContent = game::FindMethod("LE.dll", "", "InventoryItemUI", "UpdateContent", 0);
    m_affixData = game::FindMethod("LE.dll", "LE.UI.Tooltips", "ItemTooltipAffix", "OnDataUpdate", 1);
    m_affixesText = game::FindMethod("LE.dll", "", "UITooltipItem", "UpdateAffixesText", 2);
    m_affixInit = game::FindMethod("LE.dll", "", "UITooltipItemAffix", "Init", 1);
    m_enable = game::FindMethod("LE.dll", "", "InventoryItemUI", "OnEnable", 0);
    Log("tiers: icon labels %s, tooltip colours %s", m_updateContent ? "ready" : "unavailable", TooltipHooks() ? "ready" : "unavailable");
    return m_updateContent && TooltipHooks();
}
std::string Read() { return Run([] { return State(); }); }
std::string Set(const std::string& key, int value) {
    if ((key != "icons" && key != "tooltips") || (value != 0 && value != 1))
        return "{\"ok\":false,\"error\":\"use tierset icons|tooltips 0|1\"}";
    return Run([&] {
        if (value) Require();
        if (key == "icons") {
            if (value) {
                EnsureHook(m_updateContent, reinterpret_cast<void*>(&d_update), reinterpret_cast<void**>(&o_update));
                EnsureHook(m_enable, reinterpret_cast<void*>(&d_enable), reinterpret_cast<void**>(&o_enable));
            }
            iconsOn = value != 0;
            RefreshIcons(value != 0);  // one scene search when switched on, so open panels update now
        } else {
            if (value) {
                // The item tooltip in use is UITooltipItem; the newer LE.UI.Tooltips
                // affix view is hooked too when the build has it.
                EnsureHook(m_affixInit, reinterpret_cast<void*>(&d_affixInit), reinterpret_cast<void**>(&o_affixInit));
                EnsureHook(m_affixesText, reinterpret_cast<void*>(&d_affixesText), reinterpret_cast<void**>(&o_affixesText));
                if (m_affixData) EnsureHook(m_affixData, reinterpret_cast<void*>(&d_affixData), reinterpret_cast<void**>(&o_affixData));
            }
            tooltipsOn = value != 0;
            if (!value) RestoreTooltips();
        }
        return State();
    });
}
std::string SetColor(const std::string& tier, const std::string& hex) {
    if (tier == "reset") return Run([] { palette = kDefaultPalette; RefreshIcons(false); return State(); });
    int index = 0;
    if (tier.size() != 1 || tier[0] < '1' || tier[0] > '8') return "{\"ok\":false,\"error\":\"use tiercolor <1-8> <RRGGBB> or tiercolor reset\"}";
    index = tier[0] - '1';
    if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return "{\"ok\":false,\"error\":\"colour must be six hexadecimal digits, for example E0315A\"}";
    const uint32_t rgb = static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
    return Run([&] { palette[static_cast<size_t>(index)] = rgb; RefreshIcons(false); return State(); });
}
void ReleaseRoots() { labels.clear(); tooltipColors.clear(); iconsOn = false; tooltipsOn = false; }

}  // namespace ep::tiers
