// Affix tiers on item icons and tier colours in item tooltips. Display only:
// item data is read and never changed; offline play only.
#include "item_tiers.hpp"
#include "item_tiers_text.hpp"
#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "lifecycle.hpp"
#include "mainthread.hpp"
#include "managed.hpp"
#include "smart_loot.hpp"
#include "stash_rules.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ep::tiers {
namespace {
using namespace managed;
using MethodInfo = const il2cpp::Method*;
struct Color { float r, g, b, a; };
struct Vec2 { float x, y; };
struct Vec4 { float x, y, z, w; };
// The game's rarity order: T1 normal white, T2 magic blue, T3 rare yellow, T4 exalted purple,
// T5 unique orange, T6 set green, T7 legendary red (T8 cyan).
constexpr std::array<uint32_t, 8> kDefaultPalette{0xFFFFFF, 0x2E8BFF, 0xFFD700, 0xAA55FF, 0xFF5500, 0x00FF00, 0xFF0044, 0x00FFFF};
// Personal, Champion and Experimental affixes: a small square of this colour before the line.
constexpr std::array<uint32_t, 4> kSpecialColor{0, 0x4FC3F7, 0xFFC107, 0xE040FB};
std::atomic<bool> specialOn{true};
std::atomic<int> ruleParts{text::kRuleAll};
// Item 4: colour, bold, italic, underline and strikethrough tags in loot filter rule names are
// shown (the game sanitises every tag in names it displays; tierset namecolors 0 keeps that).
std::atomic<bool> nameColors{true};
game::MethodRef m_sanitize;
void* (*o_sanitize)(void*, MethodInfo) = nullptr;
uint64_t sanitized = 0;
// Item 6: more loot filter colours. While on, the game's colour list (MasterColorList
// lootFilterColorPresets) has the extra colours after its own; the filter window offers them and
// labels on the ground use them. Without EpochPact a rule with one of them shows the game's last
// colour (the game limits the colour number to its list). Off: the game's own list again.
std::atomic<bool> filterColorsOn{false};
// One per colour list: label colours and beam colours.
struct ColorList { const char* field; std::unique_ptr<Root> original, extended; std::vector<std::unique_ptr<Root>> made; int gameCount = 0; };
ColorList colorLists[]{{"lootFilterColorPresets"}, {"lootFilterBeamColorPresets"}};  // what the rule line shows (tierset ruleparts; ruleplain 1 = name only)
std::array<uint32_t, 8> palette = kDefaultPalette;
std::atomic<bool> iconsOn{false}, tooltipsOn{false}, linesOn{false}, rulesOn{false}, rulePlain{false};
// Where the loot filter rule line goes: 0 under the item type, 1 before the
// affixes, 2 at the end of the tooltip. One TMP line per place, made on first use.
std::atomic<int> rulePosition{0};
std::array<std::unique_ptr<Root>, 3> ruleLabels;
uint64_t ruleUpdates = 0;
std::string lastRuleText;
uint32_t lastRuleColor = 0;
// The rule line's colour: the item's label colour on the ground (default) or
// one colour the player chose (tiercolor rule <RRGGBB|label>).
std::atomic<bool> ruleCustom{false};
std::atomic<uint32_t> ruleCustomColor{0xFFFFFF};
std::map<void*, std::unique_ptr<Root>> labels;  // InventoryItemUI -> our TMP label
// The item category view (catshow): inventory items of other categories are drawn faint through the item's own
// CanvasGroup; these are the ones made faint, set back when the view changes.
std::set<void*> dimmed;
uint64_t dimUpdates = 0;
// A tooltip text we changed: its own colour, and for affix lines the game's
// original text and the affix tier, so it can be re-drawn or restored exactly.
struct SavedColor { std::unique_ptr<Root> text; Color color; std::string original; int tier = 0; int kind = 0; };  // kind: 1 personal, 2 champion, 3 experimental
std::map<void*, SavedColor> tooltipColors;
uint64_t iconUpdates = 0, tooltipUpdates = 0, faults = 0, scanned = 0, noItem = 0, noTiers = 0, noQuantity = 0, created = 0;
int lastTooltipTier = -1;
game::MethodRef m_updateContent, m_affixData, m_enable, m_affixesText, m_compareAffixesText, m_affixInit;
void (*o_update)(void*, MethodInfo) = nullptr;
void (*o_enable)(void*, MethodInfo) = nullptr;
void (*o_affixData)(void*, void*, MethodInfo) = nullptr;
// UITooltipItem.UpdateAffixesText(ItemData, ItemTooltipInfo) and
// UITooltipItemAffix.Init(ValueTuple<string,bool>): both value-type arguments
// are larger than 8 bytes and arrive by hidden pointer on x64.
void (*o_affixesText)(void*, void*, void*, MethodInfo) = nullptr;
void (*o_compareAffixesText)(void*, void*, void*, MethodInfo) = nullptr;  // the compared item's tooltip
void (*o_affixInit)(void*, void*, MethodInfo) = nullptr;
uint64_t tooltipMismatches = 0, compareUpdates = 0;
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
constexpr int kInventoryContainer = 1;  // ContainerID.INVENTORY
bool CategoryView() { return stashrules::CategoryFilter() >= 0 || !dimmed.empty(); }
void Dim(void* ui) {
    const int filter = stashrules::CategoryFilter();
    bool faint = false;
    if (filter >= 0) {
        void* parent = Get<void*>(ui, F("InventoryItemUI", "parentContainer"));
        void* entry = Get<void*>(ui, F("InventoryItemUI", "<EntryRef>k__BackingField"));
        void* item = entry ? Get<void*>(entry, F("ItemContainerEntry", "data")) : nullptr;
        faint = parent && item && Get<int>(parent, F("ItemContainerUI", "identifier")) == kInventoryContainer && stashrules::CategoryOf(item) != filter;
    }
    if (!faint && !dimmed.count(ui)) return;
    void* group = Get<void*>(ui, F("InventoryItemUI", "group"));
    if (!group || !game::IsAlive(group)) { dimmed.erase(ui); return; }
    float alpha = faint ? .22f : 1.f;
    void* a[]{&alpha};
    Invoke(M("CanvasGroup", "set_alpha", 1, "UnityEngine", "UnityEngine.UIModule.dll"), group, a);
    if (faint) dimmed.insert(ui); else dimmed.erase(ui);
    ++dimUpdates;
}
void d_update(void* self, MethodInfo m) {
    o_update(self, m);
    if (lifecycle::Stopping() || (!iconsOn.load() && labels.empty() && !CategoryView())) return;
    ++iconUpdates;
    if (!game::Guarded([&] { UpdateIcon(self); Dim(self); }, nullptr)) ++faults;
}
// Opening a panel re-enables pooled item icons without UpdateContent.
void d_enable(void* self, MethodInfo m) {
    o_enable(self, m);
    if (lifecycle::Stopping() || (!iconsOn.load() && labels.empty() && !CategoryView())) return;
    ++iconUpdates;
    if (!game::Guarded([&] { UpdateIcon(self); Dim(self); }, nullptr)) ++faults;
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
void SetText(void* text, const std::string& value) {
    Root s(il2cpp::api().string_new(value.c_str())); void* a[]{s.Get()};
    Invoke(Method(text, "set_text", 1), text, a);
}
void RestoreColor(void* text, bool restoreText) {
    auto it = tooltipColors.find(text);
    if (it == tooltipColors.end()) return;
    if (game::IsAlive(text)) {
        Color c = it->second.color; void* a[]{&c};
        Invoke(M("Graphic", "set_color", 1, "UnityEngine.UI", "UnityEngine.UI.dll"), text, a);
        if (restoreText && it->second.tier) SetText(text, it->second.original);
    }
    tooltipColors.erase(it);
}
// Tag "<color=#RRGGBB>T7</color> " and/or the whole line in the tier colour.
// Rich-text colour is used because the game re-sets the text's vertex colour.
// Exalted, experimental, personal, corrupted and similar affixes carry the
// game's own colour tag inside the line, which would win over ours, so the
// line is coloured without them (the original text is kept for restoring).
std::string Compose(const SavedColor& saved) {
    const std::string hex = Hex(TierColor(saved.tier));
    std::string out = specialOn.load() && saved.kind >= 1 && saved.kind <= 3 ? "<mark=#" + Hex(kSpecialColor[static_cast<size_t>(saved.kind)]) + "FF>  </mark> " : "";
    out += tooltipsOn.load() ? "<color=#" + hex + ">T" + std::to_string(saved.tier) + "</color> " : "";
    out += linesOn.load() ? "<color=#" + hex + ">" + text::StripColorTags(saved.original) + "</color>" : saved.original;
    return out;
}
// The game keeps a built tooltip for the same item; ask it to rebuild so a
// setting or palette change also reaches tooltips built before the change.
void RebuildGameTooltip() {
    game::Guarded([] { if (auto m = game::FindMethod("LE.dll", "", "TooltipItemManager", "ForceUpdateTooltip", 0)) Invoke(m); }, nullptr);
}
// After a setting or palette change: re-draw the lines of a tooltip that is open.
void RedrawTooltips() {
    if (!tooltipsOn.load() && !linesOn.load()) {
        std::vector<void*> texts;
        for (auto& [key, saved] : tooltipColors) texts.push_back(key);
        for (void* text : texts) game::Guarded([&] { RestoreColor(text, true); }, nullptr);
        tooltipColors.clear();
        return;
    }
    for (auto& [text, saved] : tooltipColors)
        if (saved.tier && game::IsAlive(text)) game::Guarded([&] { SetText(text, Compose(saved)); }, nullptr);
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
void MarkLine(void* line, int tier, int kind = 0) {
    void* text = Get<void*>(line, F("UITooltipItemAffix", "_text"));
    if (!text || !game::IsAlive(text) || tier < 1 || tier > 8) return;
    // The game's text is remembered once per Init, so a re-draw never stacks
    // tags and switching off restores the exact original line.
    auto it = tooltipColors.find(text);
    if (it == tooltipColors.end()) {
        if (tooltipColors.size() >= 1024) return;
        it = tooltipColors.emplace(text, SavedColor{std::make_unique<Root>(text), Value<Color>(M("Graphic", "get_color", 0, "UnityEngine.UI", "UnityEngine.UI.dll"), text),
            Text(Invoke(Method(text, "get_text", 0), text)), tier}).first;
    }
    it->second.tier = tier; it->second.kind = kind;
    SetText(text, Compose(it->second));
}
// The rule line's colour, as the game colours the item's label on the ground:
// the rule's own colour when it recolours labels, else the item's colour. Hide
// rules leave no label on the ground, so their line is grey.
uint32_t RuleColor(void* itemData, const smartloot::RuleMatch& match) {
    if (match.outcome == 1) return 0x8C8C8C;
    void* ecolor = nullptr;
    if (match.color >= 0) {
        int id = match.color; uint8_t rarity = Get<uint8_t>(itemData, F("ItemData", "rarity"));
        void* a[]{&id, &rarity};
        ecolor = Invoke(M("EpochColorManager", "GetLootfilterRuleColor", 2), nullptr, a);
    }
    if (!ecolor) { void* a[]{itemData}; ecolor = Invoke(M("EpochColorManager", "GetEpochColor", 1), nullptr, a); }
    if (!ecolor) return 0xFFFFFF;
    const Color c = Get<Color>(ecolor, F("EpochColor.Ecolor", "color"));
    auto byte = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
    return byte(c.r) << 16 | byte(c.g) << 8 | byte(c.b);
}
// "Loot filter: Rule 3 · Show · <name>", or only the rule name, from the game's
// own filter match. Empty when no filter is active.
std::string RuleText(void* itemData, uint32_t* rgb) {
    Root unpacked(Invoke(Method(itemData, "getAsUnpacked", 0), itemData));
    smartloot::RuleMatch match;
    if (!smartloot::RuleFor(unpacked.Get(), &match) || match.outcome < 0) return "";
    std::string name;
    if (!match.generated && nameColors.load()) name = text::KeepNameTags(match.name);  // a player's own colours
    else for (size_t i = 0; i < match.name.size(); ++i) {  // the game's descriptions carry rich-text tags
        if (match.name[i] == '<') { const auto end = match.name.find('>', i); if (end != std::string::npos) { i = end; continue; } }
        name += match.name[i];
    }
    *rgb = ruleCustom.load() ? ruleCustomColor.load() : RuleColor(itemData, match);
    // A description the game made up for an unnamed rule loses its "Show All" words.
    if (match.generated) name = text::RuleWords(name);
    return text::RuleLine(rulePlain.load() ? text::kRuleName : ruleParts.load(), match.number, match.outcome, name,
                          "<color=#" + Hex(*rgb) + ">", "<color=#9FB4C7>Loot filter:</color>");
}
void* TransformOf(void* component) { return Invoke(M("Component", "get_transform", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), component); }
void* ParentOf(void* transform) { return Invoke(M("Transform", "get_parent", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), transform); }
int ElementIndex(void* group, void* transform) {
    int i = 0;
    for (void* element : Entries(Invoke(Method(group, "get_Elements", 0), group))) {
        if (element && Get<void*>(element, F("SimpleLayoutGroup.SimpleLayoutElement", "_rectTransformReference")) == transform) return i;
        ++i;
    }
    return -1;
}
// Our line for one place in this tooltip. The header holds the item type; the
// body holds the affixes, the lore text and the footer. A line is made once
// from a copy of the item type line and added to the group's own element list,
// so the game lays it out: under the type, before the affixes, or after the lore.
void* RuleLabel(void* tooltip, int position) {
    void* typeText = Get<void*>(tooltip, F("UITooltipItem", "typeText"));
    void* anchor = position == 0 ? typeText : Get<void*>(tooltip, F("UITooltipItem", "loreText"));
    if (!typeText || !anchor || !game::IsAlive(typeText) || !game::IsAlive(anchor)) return nullptr;
    Root anchorTransform(TransformOf(anchor));
    Root parent(ParentOf(anchorTransform.Get()));
    auto& slot = ruleLabels[static_cast<size_t>(position)];
    if (slot) {
        void* label = slot->Get();
        if (label && game::IsAlive(label)) {
            Root labelTransform(TransformOf(label));
            if (ParentOf(labelTransform.Get()) == parent.Get()) return label;
            SetActive(label, false);  // another tooltip instance
        }
        slot.reset();
    }
    Root parentObject(Invoke(M("Component", "get_gameObject", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), parent.Get()));
    Root groupType(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("LE.dll", "", "SimpleLayoutGroup"))));
    void* ga[]{groupType.Get()};
    void* group = Invoke(Signature("GameObject", "GetComponent", {"System.Type"}), parentObject.Get(), ga);
    if (!group) return nullptr;  // unknown layout: no line rather than a misplaced one
    Root groupRoot(group);
    Root source(Invoke(M("Component", "get_gameObject", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), typeText));
    bool world = false; void* a[]{source.Get(), parent.Get(), &world};
    Root clone(Invoke(Signature("Object", "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform", "System.Boolean"}), nullptr, a));
    Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("Unity.TextMeshPro.dll", "TMPro", "TMP_Text"))));
    void* ta[]{type.Get()};
    void* label = Invoke(Signature("GameObject", "GetComponent", {"System.Type"}), clone.Get(), ta);
    if (!label) return nullptr;
    slot = std::make_unique<Root>(label);
    bool on = true; void* ra[]{&on}; Invoke(Method(label, "set_richText", 1), label, ra);
    // The item type and affix lines' fonts draw lowercase letters as small
    // capitals: the rule name is shown as written, in the font of the footer's
    // "Requires: Level" line (true lowercase), without upper/lower-case or
    // small-caps styles. Without that line: the affix lines' font.
    void* ruleFont = nullptr;
    if (void* requirement = Get<void*>(tooltip, F("UITooltipItem", "requirementLevelText")); requirement && game::IsAlive(requirement))
        ruleFont = Invoke(Method(requirement, "get_font", 0), requirement);
    if (!ruleFont) for (const char* list : {"prefixesText", "suffixesText", "uniqueAffixesText"}) {
        for (void* line : Entries(Get<void*>(tooltip, F("UITooltipItem", list)))) {
            void* text = line && game::IsAlive(line) ? Get<void*>(line, F("UITooltipItemAffix", "_text")) : nullptr;
            if (text && game::IsAlive(text)) { ruleFont = Invoke(Method(text, "get_font", 0), text); break; }
        }
        if (ruleFont) break;
    }
    if (!ruleFont) {  // an item without affixes: the implicit line uses the same font
        void* implicit = Get<void*>(tooltip, F("UITooltipItem", "implicitText"));
        if (implicit && game::IsAlive(implicit)) ruleFont = Invoke(Method(implicit, "get_font", 0), implicit);
    }
    if (ruleFont) { void* fa[]{ruleFont}; Invoke(Method(label, "set_font", 1), label, fa); }
    int style = Value<int>(Method(label, "get_fontStyle", 0), label) & ~(8 | 16 | 32);  // LowerCase, UpperCase, SmallCaps
    void* sa[]{&style}; Invoke(Method(label, "set_fontStyle", 1), label, sa);
    if (position != 0) {
        // In the body: as wide as the lore line, its text indented like the affix lines.
        Root labelRect(TransformOf(label));
        const char* properties[][2]{{"get_anchorMin", "set_anchorMin"}, {"get_anchorMax", "set_anchorMax"}, {"get_pivot", "set_pivot"}, {"get_sizeDelta", "set_sizeDelta"}};
        for (const auto& [get, set] : properties) {
            Vec2 value = Value<Vec2>(M("RectTransform", get, 0, "UnityEngine", "UnityEngine.CoreModule.dll"), anchorTransform.Get());
            void* va[]{&value}; Invoke(M("RectTransform", set, 1, "UnityEngine", "UnityEngine.CoreModule.dll"), labelRect.Get(), va);
        }
        const auto lines = Entries(Get<void*>(tooltip, F("UITooltipItem", "prefixesText")));
        void* affixText = !lines.empty() && lines.front() && game::IsAlive(lines.front()) ? Get<void*>(lines.front(), F("UITooltipItemAffix", "_text")) : nullptr;
        if (affixText) {
            const Vec4 m = Value<Vec4>(Method(affixText, "get_margin", 0), affixText);
            Vec4 margin{m.x, 0, m.z, 0}; void* ma[]{&margin};
            Invoke(Method(label, "set_margin", 1), label, ma);
        }
    }
    Root name(il2cpp::api().string_new("EpochPact loot filter rule"));
    void* na[]{name.Get()}; Invoke(M("Object", "set_name", 1, "UnityEngine", "UnityEngine.CoreModule.dll"), clone.Get(), na);
    void* aa[]{clone.Get()};
    Invoke(Method(group, "AddElement", 1), group, aa);
    int at = position == 1 ? 0 : ElementIndex(group, anchorTransform.Get());
    if (position != 1 && at >= 0) ++at;  // right after the type or the lore line
    if (at >= 0) { void* oa[]{clone.Get(), &at}; Invoke(Method(group, "SetItemOrder", 2), group, oa); }
    return label;
}
// The rule line in the chosen place; the other places are hidden. Hidden when
// no filter is active or the setting is off.
void SetRuleLine(void* tooltip, void* itemData) {
    uint32_t rgb = 0;
    const std::string text = rulesOn.load() && itemData ? RuleText(itemData, &rgb) : std::string();
    const int chosen = std::clamp(rulePosition.load(), 0, 2);
    for (int position = 0; position < 3; ++position) {
        auto& slot = ruleLabels[static_cast<size_t>(position)];
        const bool show = !text.empty() && position == chosen;
        void* label = show ? RuleLabel(tooltip, position) : slot ? slot->Get() : nullptr;
        if (!label || !game::IsAlive(label)) continue;
        if (!show) { SetActive(label, false); continue; }
        Root value(il2cpp::api().string_new(text.c_str())); void* va[]{value.Get()};
        Invoke(Method(label, "set_text", 1), label, va);
        SetActive(label, true);
        ++ruleUpdates; lastRuleText = text; lastRuleColor = rgb;
    }
}
bool AnyRuleLabel() { return std::any_of(ruleLabels.begin(), ruleLabels.end(), [](const auto& slot) { return slot != nullptr; }); }
void ColourTooltip(void* self, void* itemData, bool compare);
void d_affixesText(void* self, void* itemData, void* info, MethodInfo m) {
    o_affixesText(self, itemData, info, m);
    if (!lifecycle::Stopping()) ColourTooltip(self, itemData, false);
}
// The tooltip of the item it is compared with is filled the same way, by its own method.
void d_compareAffixesText(void* self, void* itemData, void* info, MethodInfo m) {
    o_compareAffixesText(self, itemData, info, m);
    ++compareUpdates;
    if (!lifecycle::Stopping()) ColourTooltip(self, itemData, true);
}
// The hovered item's lines, or (compare) the compared item's lines in the tooltip's compare section.
void ColourTooltip(void* self, void* itemData, bool compare) {
    if (!compare && (rulesOn.load() || AnyRuleLabel())) { if (!game::Guarded([&] { SetRuleLine(self, itemData); }, nullptr)) ++faults; }
    if ((!tooltipsOn.load() && !linesOn.load()) || !itemData) return;
    ++tooltipUpdates;
    if (!game::Guarded([&] {
        // Regular prefixes and suffixes are listed in the item's own order,
        // each in its own pooled line list; sealed (regular, primordial and
        // corrupted) affixes each have their own line.
        std::vector<int> prefixes, suffixes, prefixKinds, suffixKinds; int sealedTier = 0, primordialTier = 0, corruptedTier = 0;
        for (void* affix : Entries(Get<void*>(itemData, F("ItemData", "affixes")))) {
            if (!affix) continue;
            const int tier = Value<int>(M("ItemAffix", "get_DisplayTier", 0), affix);
            if (Value<bool>(M("ItemAffix", "get_IsSealed", 0), affix)) {
                if (Value<bool>(M("ItemAffix", "get_IsSealedRegular", 0), affix)) sealedTier = tier;
                else if (Value<bool>(M("ItemAffix", "get_IsSealedPrimordial", 0), affix)) primordialTier = tier;
                else if (Value<bool>(M("ItemAffix", "get_IsSealedCorrupted", 0), affix)) corruptedTier = tier;
                continue;
            }
            const int type = Get<int>(affix, F("ItemAffix", "affixType"));
            const int kind = Value<bool>(M("ItemAffix", "get_IsPersonal", 0), affix) ? 1 : Value<bool>(M("ItemAffix", "get_IsChampion", 0), affix) ? 2
                : Value<bool>(M("ItemAffix", "get_IsExperimental", 0), affix) ? 3 : 0;
            if (type == 0) { prefixes.push_back(tier); prefixKinds.push_back(kind); } else if (type == 1) { suffixes.push_back(tier); suffixKinds.push_back(kind); }
        }
        const auto prefixLines = ActiveLines(Get<void*>(self, F("UITooltipItem", compare ? "comparePrefixes" : "prefixesText")));
        const auto suffixLines = ActiveLines(Get<void*>(self, F("UITooltipItem", compare ? "compareSuffixes" : "suffixesText")));
        if (prefixLines.size() != prefixes.size() || suffixLines.size() != suffixes.size()) { ++tooltipMismatches; return; }
        for (size_t i = 0; i < prefixes.size(); ++i) MarkLine(prefixLines[i], prefixes[i], prefixKinds[i]);
        for (size_t i = 0; i < suffixes.size(); ++i) MarkLine(suffixLines[i], suffixes[i], suffixKinds[i]);
        if (!compare) lastTooltipTier = prefixes.empty() ? (suffixes.empty() ? lastTooltipTier : suffixes.front()) : prefixes.front();
        const std::pair<const char*, int> sealedLines[]{{compare ? "compareSealedAffix" : "sealedAffix", sealedTier},
            {compare ? "compareSealedPrimordialAffix" : "sealedPrimordialAffix", primordialTier}, {compare ? "compareSealedCorruptedAffix" : "sealedCorruptedAffix", corruptedTier}};
        for (const auto& [field, tier] : sealedLines) {
            void* line = tier ? Get<void*>(self, F("UITooltipItem", field)) : nullptr;
            if (!line || !game::IsAlive(line)) continue;
            Root go(Invoke(M("Component", "get_gameObject", 0, "UnityEngine", "UnityEngine.CoreModule.dll"), line));
            if (Value<bool>(Method(go.Get(), "get_activeSelf", 0), go.Get())) MarkLine(line, tier);
        }
    }, nullptr)) ++faults;
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
    out << "{\"ok\":true,\"icons\":" << Boolean(iconsOn.load()) << ",\"tooltips\":" << Boolean(tooltipsOn.load()) << ",\"lines\":" << Boolean(linesOn.load()) << ",\"rules\":" << Boolean(rulesOn.load()) << ",\"rulePosition\":" << rulePosition.load() << ",\"rulePlain\":" << Boolean(rulePlain.load()) << ",\"special\":" << Boolean(specialOn.load()) << ",\"ruleParts\":" << ruleParts.load() << ",\"nameColors\":" << Boolean(nameColors.load() && m_sanitize) << ",\"sanitized\":" << sanitized
        << ",\"filterColors\":" << Boolean(filterColorsOn.load()) << ",\"filterColorsGame\":" << colorLists[0].gameCount << ",\"beamColorsGame\":" << colorLists[1].gameCount
        << ",\"filterColorsExtra\":" << (colorLists[0].extended ? std::size(text::kExtraFilterColors) : 0)
        << ",\"ruleUpdates\":" << ruleUpdates << ",\"lastRuleText\":" << Json(lastRuleText) << ",\"lastRuleColor\":" << Json(Hex(lastRuleColor))
        << ",\"ruleColor\":" << Json(ruleCustom.load() ? Hex(ruleCustomColor.load()) : std::string("label")) << ",\"palette\":[";
    for (size_t i = 0; i < palette.size(); ++i) { if (i) out << ','; out << Json(Hex(palette[i])); }
    out << "],\"labels\":" << labels.size() << ",\"coloredTexts\":" << tooltipColors.size() << ",\"iconUpdates\":" << iconUpdates
        << ",\"tooltipUpdates\":" << tooltipUpdates << ",\"tooltipMismatches\":" << tooltipMismatches << ",\"compareUpdates\":" << compareUpdates << ",\"scanned\":" << scanned << ",\"created\":" << created << ",\"noItem\":" << noItem << ",\"noTiers\":" << noTiers << ",\"noQuantityText\":" << noQuantity << ",\"lastTooltipTier\":" << lastTooltipTier << ",\"dimmed\":" << dimmed.size() << ",\"dimUpdates\":" << dimUpdates << ",\"faults\":" << faults
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
// TMPExtensions.SanitizeUserInputForDisplay(string), used by Rule.GetRuleDescription for a rule's
// own name: the allowed tags pass, the rest of the name is sanitised by the game as before.
void* d_sanitize(void* value, MethodInfo m) {
    if (!nameColors.load() || !value || lifecycle::Stopping()) return o_sanitize(value, m);
    void* result = nullptr;
    game::Guarded([&] {
        const std::string name = Text(value);
        if (name.find('<') == std::string::npos) return;
        const auto parts = text::NameParts(name);
        if (std::none_of(parts.begin(), parts.end(), [](const text::NamePiece& p) { return p.tag; })) return;
        std::string out;
        for (const auto& part : parts) {
            if (part.tag) { out += part.text; continue; }
            Root piece(il2cpp::api().string_new(part.text.c_str()));
            out += Text(o_sanitize(piece.Get(), m));
        }
        result = il2cpp::api().string_new(out.c_str());
        ++sanitized;
    }, nullptr);
    return result ? result : o_sanitize(value, m);
}
void* PresetsOwner(const char* name, const il2cpp::Field** field) {
    void* master = Invoke(M("GlobalAssets", "get_MasterColorList", 0, "LE.AssetManagement"));
    if (!master) throw std::runtime_error("The game's colour list is not loaded yet.");
    *field = il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(master), name);
    if (!*field) throw std::runtime_error("The loot filter colour list is unavailable in this game build.");
    return master;
}
void* Clone(void* object) { return Invoke(game::FindMethod("mscorlib.dll", "System", "Object", "MemberwiseClone", 0), object); }
void SetReference(void* object, const char* name, void* value) {
    const auto* field = il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(object), name);
    if (!field) throw std::runtime_error(std::string("missing colour field: ") + name);
    il2cpp::api().field_set_value(object, field, value);
}
// The list holds EcolorPair values (16 bytes: the primary and secondary colour objects).
struct ColorPair { void* primary; void* secondary; };
ColorPair* PairAt(void* array, int i) { return reinterpret_cast<ColorPair*>(static_cast<char*>(array) + 0x20 + sizeof(ColorPair) * static_cast<size_t>(i)); }
void ExtendColorList(ColorList& list, bool on) {
    const il2cpp::Field* field = nullptr;
    void* master = PresetsOwner(list.field, &field);
    void* current = nullptr; il2cpp::api().field_get_value(master, field, &current);
    if (!on) {
        if (list.original && current == (list.extended ? list.extended->Get() : nullptr)) { void* original = list.original->Get(); il2cpp::api().field_set_value(master, field, original); }
        list.extended.reset(); list.original.reset(); list.made.clear(); list.gameCount = 0;
        return;
    }
    if (list.extended && current == list.extended->Get()) return;
    if (!current) throw std::runtime_error("The loot filter colour list is empty.");
    const auto n = static_cast<int>(Get<uintptr_t>(current, 0x18));
    if (n < 1 || n > 256) throw std::runtime_error("Unexpected loot filter colour count.");
    const auto* pairClass = game::FindClass("LE.dll", "", "EpochColor.EcolorPair");
    if (!pairClass || il2cpp::api().class_get_type(pairClass) == nullptr) throw std::runtime_error("The loot filter colour type is unavailable in this game build.");
    const size_t colorField = F("EpochColor.Ecolor", "color");
    // The template: the game's most saturated, brightest colour.
    int best = -1; float score = -1;
    for (int i = 0; i < n; ++i) {
        void* primary = PairAt(current, i)->primary;
        if (!primary) continue;
        const Color c = Get<Color>(primary, colorField); const auto hsv = text::ToHsv({c.r, c.g, c.b});
        if (hsv.s * hsv.v > score) { score = hsv.s * hsv.v; best = i; }
    }
    if (best < 0) throw std::runtime_error("The loot filter colour list has no colour to copy.");
    const ColorPair templatePair = *PairAt(current, best);
    const Color templateColor = Get<Color>(templatePair.primary, colorField);
    const std::string templateHex = Text(Get<void*>(templatePair.primary, F("EpochColor.Ecolor", "hexCode")));
    constexpr size_t extra = std::size(text::kExtraFilterColors);
    // Every colour object made here stays held by EpochPact while the list uses it.
    std::vector<std::unique_ptr<Root>> made;
    std::vector<ColorPair> pairs;
    const char* shades[]{"color", "lightColor", "buttonColor", "highlightColor", "borderColor", "shineColor", "tooltipNameColor", "tooltipBackgroundColor", "tooltipOrnamentationColor", "beamColor"};
    for (size_t k = 0; k < extra; ++k) {
        const auto target = text::FromRgb(text::kExtraFilterColors[k]);
        ColorPair pair{};
        for (void** side : {&pair.primary, &pair.secondary}) {
            void* source = side == &pair.primary ? templatePair.primary : templatePair.secondary;
            if (!source) continue;
            void* color = Clone(source); made.push_back(std::make_unique<Root>(color));
            for (const char* shade : shades) {
                const size_t at = F("EpochColor.Ecolor", shade);
                Color c = Get<Color>(color, at);
                const auto moved = text::Recolor({c.r, c.g, c.b}, {templateColor.r, templateColor.g, templateColor.b}, target);
                c.r = moved.r; c.g = moved.g; c.b = moved.b;
                *reinterpret_cast<Color*>(static_cast<char*>(color) + at) = c;
            }
            Root hex(il2cpp::api().string_new(((templateHex.rfind('#', 0) == 0 ? "#" : "") + Hex(text::kExtraFilterColors[k]) + (templateHex.size() >= 8 ? "FF" : "")).c_str()));
            SetReference(color, "hexCode", hex.Get());
            *side = color;
        }
        pairs.push_back(pair);
    }
    Root array(il2cpp::api().array_new(pairClass, static_cast<uintptr_t>(n) + extra));
    if (!array.Get() || Get<uintptr_t>(array.Get(), 0x18) != static_cast<uintptr_t>(n) + extra) throw std::runtime_error("The longer colour list could not be made.");
    made.push_back(std::make_unique<Root>(array.Get()));
    // The game's own pairs first, then ours; every object written here is held (by the
    // game's own list or by EpochPact), so the new list never refers to a freed object.
    for (int i = 0; i < n; ++i) *PairAt(array.Get(), i) = *PairAt(current, i);
    for (size_t k = 0; k < extra; ++k) *PairAt(array.Get(), n + static_cast<int>(k)) = pairs[k];
    list.original = std::make_unique<Root>(current);
    list.extended = std::make_unique<Root>(array.Get());
    list.made = std::move(made);
    list.gameCount = n;
    void* extended = array.Get();
    il2cpp::api().field_set_value(master, field, extended);
}
void ExtendFilterColors(bool on) {
    if (!on) { for (auto& list : colorLists) ExtendColorList(list, false); return; }
    try { for (auto& list : colorLists) ExtendColorList(list, true); }
    catch (...) { for (auto& list : colorLists) game::Guarded([&] { ExtendColorList(list, false); }, nullptr); throw; }
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
    m_compareAffixesText = game::FindMethod("LE.dll", "", "UITooltipItem", "updateCompareAffixesText", 2);
    m_affixInit = game::FindMethod("LE.dll", "", "UITooltipItemAffix", "Init", 1);
    m_enable = game::FindMethod("LE.dll", "", "InventoryItemUI", "OnEnable", 0);
    // Installed while the game starts (it is on by default), before any rule name is shown.
    m_sanitize = game::FindMethod("LE.dll", "", "TMPExtensions", "SanitizeUserInputForDisplay", 1);
    if (m_sanitize) {
        std::string why;
        if (!hook::IsInstalled(m_sanitize.code) && !hook::Install(m_sanitize.code, reinterpret_cast<void*>(&d_sanitize), reinterpret_cast<void**>(&o_sanitize), &why))
            { Log("tiers: rule name colours unavailable: %s", why.c_str()); m_sanitize = {}; }
    }
    Log("tiers: icon labels %s, tooltip colours %s (comparison %s)", m_updateContent ? "ready" : "unavailable", TooltipHooks() ? "ready" : "unavailable",
        m_compareAffixesText ? "ready" : "unavailable");
    return m_updateContent && TooltipHooks();
}
void RefreshCategoryView() {
    if (stashrules::CategoryFilter() >= 0) {
        if (!m_updateContent || !m_enable) throw std::runtime_error("The category view is unavailable in this game build.");
        EnsureHook(m_updateContent, reinterpret_cast<void*>(&d_update), reinterpret_cast<void**>(&o_update));
        EnsureHook(m_enable, reinterpret_cast<void*>(&d_enable), reinterpret_cast<void**>(&o_enable));
    }
    for (void* ui : FindObjects("LE.dll", "", "InventoryItemUI")) if (ui && game::IsAlive(ui)) game::Guarded([&] { Dim(ui); }, nullptr);
}
std::string Read() { return Run([] { return State(); }); }
std::string Set(const std::string& key, int value) {
    const bool flag = key == "icons" || key == "tooltips" || key == "lines" || key == "rules" || key == "ruleplain" || key == "special" || key == "namecolors" || key == "filtercolors";
    if (!(flag && (value == 0 || value == 1)) && !(key == "rulepos" && value >= 0 && value <= 2) && !(key == "ruleparts" && text::ValidRuleParts(value)))
        return "{\"ok\":false,\"error\":\"use tierset icons|tooltips|lines|rules|ruleplain|special|namecolors|filtercolors 0|1, tierset rulepos 0|1|2 or tierset ruleparts 2..15\"}";
    return Run([&] {
        if (key == "filtercolors") {
            if (value && !game::IsOfflinePlay()) throw std::runtime_error("More loot filter colours work in offline play only.");
            ExtendFilterColors(value != 0);
            filterColorsOn = value != 0;
            return State();
        }
        if (key == "namecolors") {
            if (value && !m_sanitize) throw std::runtime_error("Rule name colours are unavailable in this game build.");
            nameColors = value != 0;
            if (rulesOn.load()) RebuildGameTooltip();
            return State();
        }
        if (key == "ruleparts") {
            // What the rule line shows: 1 "Loot filter:", 2 the number, 4 Show/Hide/Highlight, 8 the name.
            ruleParts = value; rulePlain = false;
            if (rulesOn.load()) RebuildGameTooltip();
            return State();
        }
        if (key == "special") {
            // The square before Personal, Champion and Experimental affix lines (shown with tier tooltips or lines).
            specialOn = value != 0; RedrawTooltips(); RebuildGameTooltip();
            return State();
        }
        if (key == "rulepos" || key == "ruleplain") {
            // How the rule line looks; shown only while "rules" is on.
            if (key == "rulepos") rulePosition = value; else rulePlain = value != 0;
            if (rulesOn.load()) RebuildGameTooltip();
            return State();
        }
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
                if (m_compareAffixesText) EnsureHook(m_compareAffixesText, reinterpret_cast<void*>(&d_compareAffixesText), reinterpret_cast<void**>(&o_compareAffixesText));
                if (m_affixData) EnsureHook(m_affixData, reinterpret_cast<void*>(&d_affixData), reinterpret_cast<void**>(&o_affixData));
            }
            (key == "lines" ? linesOn : key == "rules" ? rulesOn : tooltipsOn) = value != 0;
            RedrawTooltips();
            RebuildGameTooltip();
        }
        return State();
    });
}
std::string SetColor(const std::string& tier, const std::string& hex) {
    if (tier == "reset") return Run([] { palette = kDefaultPalette; ruleCustom = false; RefreshIcons(false); RedrawTooltips(); RebuildGameTooltip(); return State(); });
    if (tier == "rule") {
        if (hex != "label" && (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos))
            return "{\"ok\":false,\"error\":\"use tiercolor rule <RRGGBB> or tiercolor rule label\"}";
        return Run([&] {
            if (hex == "label") ruleCustom = false; else { ruleCustomColor = static_cast<uint32_t>(std::stoul(hex, nullptr, 16)); ruleCustom = true; }
            if (rulesOn.load()) RebuildGameTooltip();
            return State();
        });
    }
    int index = 0;
    if (tier.size() != 1 || tier[0] < '1' || tier[0] > '8') return "{\"ok\":false,\"error\":\"use tiercolor <1-8> <RRGGBB>, tiercolor rule <RRGGBB|label> or tiercolor reset\"}";
    index = tier[0] - '1';
    if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return "{\"ok\":false,\"error\":\"colour must be six hexadecimal digits, for example E0315A\"}";
    const uint32_t rgb = static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
    return Run([&] { palette[static_cast<size_t>(index)] = rgb; RefreshIcons(false); RedrawTooltips(); RebuildGameTooltip(); return State(); });
}
void ReleaseRoots() {
    // The game is closing: the colour list keeps its own references; only ours are let go.
    for (auto& list : colorLists) { list.extended.reset(); list.original.reset(); list.made.clear(); list.gameCount = 0; }
    filterColorsOn = false;
    labels.clear(); tooltipColors.clear(); dimmed.clear();
    for (auto& slot : ruleLabels) slot.reset();
    iconsOn = false; tooltipsOn = false; linesOn = false; rulesOn = false; rulePlain = false; rulePosition = 0; ruleCustom = false;
}

}  // namespace ep::tiers
