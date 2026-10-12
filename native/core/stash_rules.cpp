// Stash tabs from loot filter rules. Offline only; the game's own stash
// objects and commands are used, and every change writes a recovery backup.
//
// Tab priority: the game's StashPriority conditions derive from the loot
// filter's condition classes (RarityCondition, SubTypeCondition, ...), so a
// rule's rarity, type, class, item level, potentials, factions, uniques and
// corruption carry over exactly. Affix and character-relative conditions have
// no tab-priority equivalent; "Sort by filter rule" uses the real filter match
// for those.
#include "stash_rules.hpp"
#include "common.hpp"
#include "game.hpp"
#include "mainthread.hpp"
#include "managed.hpp"
#include "progression.hpp"
#include "smart_loot.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace ep::stashrules {
namespace {
using namespace managed;
auto& api() { return il2cpp::api(); }

size_t F(const char* cls, const char* field, const char* ns = "") {
    static std::map<std::string, size_t> fields;
    const std::string key = std::string(ns) + ":" + cls + ":" + field;
    if (auto it = fields.find(key); it != fields.end()) return it->second;
    return fields.emplace(key, Offset(cls, field, ns)).first->second;
}
game::MethodRef M(const char* cls, const char* name, int arity, const char* ns = "") {
    static std::map<std::string, game::MethodRef> methods;
    const std::string key = std::string(ns) + ":" + cls + ":" + name + ":" + std::to_string(arity);
    if (auto it = methods.find(key); it != methods.end()) return it->second;
    auto m = game::FindMethod("LE.dll", ns, cls, name, arity);
    if (!m) throw std::runtime_error(std::string("Stash method unavailable in this game build: ") + cls + "." + name);
    return methods.emplace(key, m).first->second;
}
// One overload by parameter types (several methods share their arity).
const il2cpp::Method* Typed(const il2cpp::Class* cls, const char* name, std::initializer_list<const char*> types) {
    void* iterator = nullptr;
    while (auto* m = cls ? api().class_get_methods(cls, &iterator) : nullptr) {
        if (std::strcmp(api().method_get_name(m), name) || api().method_get_param_count(m) != types.size()) continue;
        bool match = true; uint32_t i = 0;
        for (const char* expected : types) {
            char* actual = api().type_get_name(api().method_get_param(m, i++));
            match &= actual && std::string(actual) == expected;
            if (actual) api().free(actual);
        }
        if (match) return m;
    }
    throw std::runtime_error(std::string("Stash method overload unavailable: ") + name);
}
const il2cpp::Class* Class(const char* cls, const char* ns = "") {
    auto* c = game::FindClass("LE.dll", ns, cls);
    if (!c) throw std::runtime_error(std::string("Stash class unavailable in this game build: ") + cls);
    return c;
}
// New managed object through its parameterless constructor.
void* New(const il2cpp::Class* cls) {
    void* object = api().object_new(cls);
    if (!object) throw std::runtime_error("managed allocation failed");
    Invoke(api().class_get_method_from_name(cls, ".ctor", 0), object);
    return object;
}
struct NullableInt { bool has; int value; };
static_assert(sizeof(NullableInt) == 8, "Nullable<int> layout");

struct Ctx { void* actor; void* manager; std::string id; };
Ctx Current(const std::string& expected) {
    if (!game::IsOfflinePlay()) throw std::runtime_error("Stash tabs from loot filters work in offline play only.");
    const auto session = SessionState();
    if (session.state != 4 || session.transitioning) throw std::runtime_error("Load an offline character and finish loading the area first.");
    void* actor = Invoke(M("PlayerFinder", "getPlayerActor", 0));
    if (!actor || !game::IsAlive(actor)) throw std::runtime_error("Load an offline character and finish loading the area first.");
    void* data = Get<void*>(Get<void*>(actor, F("Actor", "characterDataTracker")), F("CharacterDataTracker", "charData"));
    if (!Value<bool>(Method(data, "get_IsOffline", 0), data)) throw std::runtime_error("Stash tabs from loot filters work in offline play only.");
    const std::string id = Text(Invoke(Method(data, "get_Id", 0), data));
    if (!expected.empty() && id != expected) throw std::runtime_error("The loaded character changed. Refresh and try again.");
    void* manager = Invoke(M("ItemContainersManager", "get_Instance", 0));
    if (!manager || Get<void*>(manager, F("ItemContainersManager", "actor")) != actor || !Value<bool>(Method(manager, "get_IsFullyInitializedAndLoaded", 0), manager))
        throw std::runtime_error("Wait for the character inventory to finish loading.");
    return {actor, manager, id};
}

struct StashView { void* holder; int index; void* container; void* stash; };
StashView View(void* manager) {
    void* holder = Get<void*>(manager, F("ItemContainersManager", "stash"));
    if (!holder) throw std::runtime_error("The stash has not loaded yet.");
    const int index = Value<int>(Method(holder, "get_CurrentStashIndex", 0), holder);
    void* container = Invoke(Method(holder, "get_CurrentContainer", 0), holder);
    void* stash = container ? Invoke(Method(container, "get_LinkedStash", 0), container) : nullptr;
    if (!container || !stash) throw std::runtime_error("The stash has not loaded yet.");
    return {holder, index, container, stash};
}
std::vector<void*> Tabs(void* stash) {
    void* list = Get<void*>(stash, F("Stash", "Tabs", "LE.Data"));
    const int n = Value<int>(Method(list, "get_Count", 0), list);
    if (n < 0 || n > 1000) throw std::runtime_error("Unexpected stash tab count.");
    std::vector<void*> tabs;
    for (int i = 0; i < n; ++i) { void* a[]{&i}; tabs.push_back(Invoke(Method(list, "get_Item", 1), list, a)); }
    return tabs;
}
std::vector<void*> Folders(void* stash) { return Entries(Get<void*>(stash, F("Stash", "<Categories>k__BackingField", "LE.Data"))); }
int TabId(void* tab) { return Get<int>(tab, F("StashTab", "_tabId", "LE.Data")); }
std::string TabName(void* tab) { return Text(Get<void*>(tab, F("StashTab", "_displayName", "LE.Data"))); }

// ---- the active loot filter
void* Filter() {
    void* manager = game::StaticObject(game::FindStaticField("LE.dll", "ItemFiltering", "ItemFilterManager", "Instance"));
    return manager ? Get<void*>(manager, F("ItemFilterManager", "<Filter>k__BackingField", "ItemFiltering")) : nullptr;
}
std::string FilterName(void* filter) { return Text(Get<void*>(filter, F("ItemFilter", "name", "ItemFiltering"))); }
std::vector<void*> Rules(void* filter) { return Entries(Get<void*>(filter, F("ItemFilter", "rules", "ItemFiltering"))); }
int RuleNumber(void* rule) { return Value<int>(Method(rule, "get_Order", 0), rule) + 1; }  // the game's "Rule N"
std::string StripTags(std::string text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '<') { const size_t end = text.find('>', i); if (end != std::string::npos) { i = end; continue; } }
        out += text[i];
    }
    return out;
}
std::string RuleLabel(void* rule) {
    std::string name = Text(Get<void*>(rule, F("Rule", "nameOverride", "ItemFiltering")));
    if (name.empty()) name = StripTags(Text(Invoke(Method(rule, "GetRuleDescription", 0), rule)));
    return name;
}
std::string ClassName(void* object) { return api().class_get_name(api().object_get_class(object)); }

// ---- persistent rule -> tab links: "<filter>\t<rule number>\t<tab id>"
std::filesystem::path LinksPath() { return std::filesystem::path(PluginDir()) / L"stash-rule-tabs.txt"; }
using Links = std::map<int, int>;  // rule number -> tab id, for one filter
std::map<std::string, Links> LoadLinks() {
    std::map<std::string, Links> all;
    std::ifstream in(LinksPath(), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto a = line.find('\t'), b = line.find('\t', a == std::string::npos ? a : a + 1);
        if (a == std::string::npos || b == std::string::npos) continue;
        try { all[line.substr(0, a)][std::stoi(line.substr(a + 1, b - a - 1))] = std::stoi(line.substr(b + 1)); } catch (...) {}
    }
    return all;
}
void SaveLinks(const std::map<std::string, Links>& all) {
    const std::filesystem::path path = LinksPath();
    const std::filesystem::path temp = path.wstring() + L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        for (const auto& [filter, links] : all)
            for (const auto& [rule, tab] : links) out << filter << '\t' << rule << '\t' << tab << '\n';
        if (!out) throw std::runtime_error("The rule-tab links could not be saved.");
    }
    std::filesystem::rename(temp, path);
}
// Links to tabs that no longer exist are dropped.
Links LiveLinks(const std::map<std::string, Links>& all, const std::string& filter, const std::vector<void*>& tabs) {
    Links result;
    auto it = all.find(filter);
    if (it == all.end()) return result;
    std::set<int> ids;
    for (void* tab : tabs) ids.insert(TabId(tab));
    for (const auto& [rule, tab] : it->second) if (ids.contains(tab)) result[rule] = tab;
    return result;
}

// ---- rule conditions -> StashPriority
// Builds the tab priority only through the game's own constructors and
// Deserialize methods; nothing is written into managed objects directly.
struct Priority { std::unique_ptr<Root> object; std::vector<std::string> mapped, unmapped; };
void* Construct(const char* cls, std::initializer_list<const char*> types, void** args) {
    const auto* klass = Class(cls);
    void* object = api().object_new(klass);
    if (!object) throw std::runtime_error("managed allocation failed");
    Root keep(object);
    Invoke(types.size() ? Typed(klass, ".ctor", types) : api().class_get_method_from_name(klass, ".ctor", 0), object, args);
    return keep.Get();
}
Priority BuildPriority(void* rule) {
    Priority result;
    NullableInt potential[8]{};  // LP min/max, WW min/max, WT min/max, FP min/max
    int rarity = 0, classReq = 0, corruption = 0;
    NullableInt minLevel{}, maxLevel{};
    std::unique_ptr<Root> types, subTypes, factions, uniques;
    for (void* condition : Entries(Get<void*>(rule, F("Rule", "conditions", "ItemFiltering")))) {
        if (!condition) continue;
        const std::string name = ClassName(condition);
        if (name == "RarityCondition") {
            rarity = Get<int>(condition, F("RarityCondition", "rarity", "ItemFiltering"));
            const char* fields[]{"minLegendaryPotential", "maxLegendaryPotential", "minWeaversWill", "maxWeaversWill"};
            for (int i = 0; i < 4; ++i) {
                const auto value = Get<NullableInt>(condition, F("RarityCondition", fields[i], "ItemFiltering"));
                if (value.has) potential[i] = value;
            }
            result.mapped.push_back("rarity");
        } else if (name == "PotentialCondition") {
            const char* fields[]{"MinLegendaryPotential", "MaxLegendaryPotential", "MinWeaversWill", "MaxWeaversWill", "MinWeaversTouch", "MaxWeaversTouch", "MinForgingPotential", "MaxForgingPotential"};
            for (int i = 0; i < 8; ++i) {
                const auto value = Get<NullableInt>(condition, F("PotentialCondition", fields[i], "ItemFiltering"));
                if (value.has) potential[i] = value;
            }
            result.mapped.push_back("potential");
        } else if (name == "SubTypeCondition") {
            types = std::make_unique<Root>(Invoke(Method(condition, "GetTypes", 0), condition));
            subTypes = std::make_unique<Root>(Invoke(Method(condition, "GetSubTypes", 0), condition));
            result.mapped.push_back("type");
        } else if (name == "ClassCondition") {
            classReq = Get<int>(condition, F("ClassCondition", "req", "ItemFiltering"));
            result.mapped.push_back("class");
        } else if (name == "LevelCondition") {
            const int threshold = Get<int>(condition, F("LevelCondition", "treshold", "ItemFiltering"));
            const int kind = Get<int>(condition, F("LevelCondition", "type", "ItemFiltering"));
            // BELOW_LEVEL = 0, ABOVE_LEVEL = 1; the other kinds follow the character's level.
            if (kind == 0) { maxLevel = {true, threshold}; result.mapped.push_back("level"); }
            else if (kind == 1) { minLevel = {true, threshold}; result.mapped.push_back("level"); }
            else result.unmapped.push_back("level (relative to the character)");
        } else if (name == "FactionCondition") {
            factions = std::make_unique<Root>(Invoke(Method(condition, "GetEligibleFactions", 0), condition));
            result.mapped.push_back("faction");
        } else if (name == "UniquesCondition") {
            uniques = std::make_unique<Root>(Invoke(Method(condition, "GetUniqueIds", 0), condition));
            result.mapped.push_back("uniques");
        } else if (name == "CorruptionCondition") {
            corruption = Get<int>(condition, F("CorruptionCondition", "Corruption", "ItemFiltering"));
            result.mapped.push_back("corruption");
        } else {
            std::string label = name;
            if (label.size() > 9 && label.compare(label.size() - 9, 9, "Condition") == 0) label.resize(label.size() - 9);
            result.unmapped.push_back(label);
        }
    }
    const char* N = "System.Nullable<System.Int32>";
    void* pa[]{&potential[0], &potential[1], &potential[2], &potential[3], &potential[4], &potential[5], &potential[6], &potential[7]};
    Root potentialCondition(Construct("PotentialStashPriorityCondition", {N, N, N, N, N, N, N, N}, pa));
    void* ra[]{&rarity};
    Root rarityCondition(Construct("RarityStashPriorityCondition", {"ItemFiltering.RarityCondition.Rarity"}, ra));
    Root typeCondition(Construct("TypeStashPriorityCondition", {}, nullptr));
    if (types || subTypes) {
        void* ta[]{types ? types->Get() : nullptr, subTypes ? subTypes->Get() : nullptr};
        Invoke(Typed(Class("TypeStashPriorityCondition"), "Deserialize", {"System.Int32[]", "System.Int32[]"}), typeCondition.Get(), ta);
    }
    void* ca[]{&classReq};
    Root classCondition(Construct("ClassStashPriorityCondition", {"ItemList.ClassRequirement"}, ca));
    void* la[]{&minLevel, &maxLevel};
    Root levelCondition(Construct("LevelStashPriorityCondition", {N, N}, la));
    Root factionCondition(Construct("FactionStashPriorityCondition", {}, nullptr));
    if (factions) { void* fa[]{factions->Get()}; Invoke(Typed(Class("FactionStashPriorityCondition"), "Deserialize", {"System.Int32[]"}), factionCondition.Get(), fa); }
    Root uniquesCondition(Construct("UniquesStashPriorityCondition", {}, nullptr));
    if (uniques) { void* ua[]{uniques->Get()}; Invoke(Typed(Class("UniquesStashPriorityCondition"), "Deserialize", {"System.Int32[]"}), uniquesCondition.Get(), ua); }
    void* xa[]{&corruption};
    Root corruptionCondition(Construct("CorruptionStashPriorityCondition", {"ItemFiltering.CorruptionCondition.CorruptionType"}, xa));
    void* args[]{potentialCondition.Get(), rarityCondition.Get(), typeCondition.Get(), classCondition.Get(), levelCondition.Get(), factionCondition.Get(), uniquesCondition.Get(), corruptionCondition.Get()};
    result.object = std::make_unique<Root>(Construct("StashPriority", {"PotentialStashPriorityCondition", "RarityStashPriorityCondition", "TypeStashPriorityCondition", "ClassStashPriorityCondition",
        "LevelStashPriorityCondition", "FactionStashPriorityCondition", "UniquesStashPriorityCondition", "CorruptionStashPriorityCondition"}, args));
    return result;
}
std::string List(const std::vector<std::string>& items) {
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) { if (i) out += ','; out += Json(items[i]); }
    return out + "]";
}

// Loot filter colour preset -> nearest stash tab colour.
struct Rgb { float r, g, b; };
std::vector<Rgb> TabColors() {
    std::vector<Rgb> colors;
    auto* cls = game::FindClass("LE.dll", "", "StashTabbedUIControls");
    if (!cls) return colors;
    Root type(api().type_get_object(api().class_get_type(cls)));
    bool inactive = true; void* a[]{type.Get(), &inactive};
    Root found(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, a));
    if (!Get<uintptr_t>(found.Get(), 0x18)) return colors;
    void* controls = Get<void*>(found.Get(), 0x20);
    void* list = Get<void*>(controls, F("StashTabbedUIControls", "tabColors"));
    struct C { float r, g, b, a; };
    void* items = list ? Get<void*>(list, 0x10) : nullptr;
    const int size = list ? Get<int>(list, 0x18) : 0;
    for (int i = 0; items && i < size && i < 64; ++i) { const C c = Get<C>(items, 0x20 + sizeof(C) * static_cast<size_t>(i)); colors.push_back({c.r, c.g, c.b}); }
    return colors;
}
int TabColorFor(void* rule, const std::vector<Rgb>& palette) {
    if (palette.empty() || !Get<bool>(rule, F("Rule", "recolor", "ItemFiltering"))) return 0;
    const int index = Get<int>(rule, F("Rule", "color", "ItemFiltering"));
    void* manager = game::StaticObject(game::FindStaticField("LE.dll", "", "EpochColorManager", "instance"));
    if (!manager) return 0;
    void* colors = Invoke(Method(manager, "get_colorList", 0), manager);
    void* presets = colors ? Get<void*>(colors, F("EpochColor", "lootFilterColorPresets")) : nullptr;
    if (!presets || index < 0 || static_cast<uintptr_t>(index) >= Get<uintptr_t>(presets, 0x18)) return 0;
    void* primary = Get<void*>(presets, 0x20 + 16 * static_cast<size_t>(index));  // EcolorPair {primary, secondary}
    if (!primary) return 0;
    struct C { float r, g, b, a; };
    const C c = Get<C>(primary, F("EpochColor.Ecolor", "color"));
    int best = 0; float bestDistance = 1e9f;
    for (size_t i = 0; i < palette.size(); ++i) {
        const float dr = palette[i].r - c.r, dg = palette[i].g - c.g, db = palette[i].b - c.b, d = dr * dr + dg * dg + db * db;
        if (d < bestDistance) { bestDistance = d; best = static_cast<int>(i); }
    }
    return best;
}

std::string TabJson(void* tab, int items) {
    void* priority = Get<void*>(tab, F("StashTab", "_tabPriority", "LE.Data"));
    std::ostringstream out;
    out << "{\"id\":" << TabId(tab) << ",\"items\":" << items << ",\"name\":" << Json(TabName(tab)) << ",\"folder\":" << Get<int>(tab, F("StashTab", "_categoryId", "LE.Data"))
        << ",\"color\":" << Get<int>(tab, F("StashTab", "_colorId", "LE.Data")) << ",\"priority\":" << Boolean(priority && Value<bool>(Method(priority, "HasValue", 0), priority)) << '}';
    return out.str();
}
std::string State(const Ctx& c) {
    const auto view = View(c.manager);
    const auto tabs = Tabs(view.stash);
    void* filter = Filter();
    std::ostringstream out;
    out << "{\"ok\":true,\"player\":{\"id\":" << Json(c.id) << "},\"tabCount\":" << tabs.size()
        << ",\"canAddTab\":" << Boolean(Value<bool>(M("ItemContainersManager", "CanAddNewStashTab", 1), c.manager, std::array<void*, 1>{view.container}.data()))
        << ",\"folders\":[";
    bool comma = false;
    for (void* f : Folders(view.stash)) {
        if (!f) continue; if (comma) out << ','; comma = true;
        out << "{\"id\":" << Get<int>(f, F("StashCategory", "_categoryId", "LE.Data")) << ",\"name\":" << Json(Text(Get<void*>(f, F("StashCategory", "_displayName", "LE.Data")))) << '}';
    }
    out << "],\"tabs\":[";
    comma = false;
    const auto containers = Entries(Invoke(Method(view.container, "get_Containers", 0), view.container));
    for (size_t i = 0; i < tabs.size(); ++i) {
        if (!tabs[i]) continue; if (comma) out << ','; comma = true;
        const int items = i < containers.size() && containers[i] ? Value<int>(Method(containers[i], "ContentCount", 0), containers[i]) : -1;
        out << TabJson(tabs[i], items);
    }
    out << "],\"filter\":";
    if (!filter) return out.str() + "null,\"rules\":[]}";
    const std::string name = FilterName(filter);
    const auto links = LiveLinks(LoadLinks(), name, tabs);
    out << Json(name) << ",\"rules\":[";
    comma = false;
    for (void* rule : Rules(filter)) {
        if (!rule) continue; if (comma) out << ','; comma = true;
        const int number = RuleNumber(rule);
        std::vector<std::string> mapped, unmapped;
        for (void* condition : Entries(Get<void*>(rule, F("Rule", "conditions", "ItemFiltering")))) {
            if (!condition) continue;
            std::string n = ClassName(condition);
            const bool known = n == "RarityCondition" || n == "PotentialCondition" || n == "SubTypeCondition" || n == "ClassCondition" || n == "FactionCondition" || n == "UniquesCondition" || n == "CorruptionCondition"
                || (n == "LevelCondition" && Get<int>(condition, F("LevelCondition", "type", "ItemFiltering")) <= 1);
            if (n.size() > 9 && n.compare(n.size() - 9, 9, "Condition") == 0) n.resize(n.size() - 9);
            (known ? mapped : unmapped).push_back(n);
        }
        const auto link = links.find(number);
        out << "{\"number\":" << number << ",\"type\":" << Get<int>(rule, F("Rule", "type", "ItemFiltering")) << ",\"enabled\":" << Boolean(Get<bool>(rule, F("Rule", "isEnabled", "ItemFiltering")))
            << ",\"name\":" << Json(RuleLabel(rule)) << ",\"mapped\":" << List(mapped) << ",\"unmapped\":" << List(unmapped)
            << ",\"tab\":" << (link == links.end() ? std::string("null") : std::to_string(link->second)) << '}';
    }
    return out.str() + "]}";
}

std::string Backup(const std::string& id, const char* operation) {
    progression::SnapshotData snapshot; std::string why;
    if (!mainthread::Run([&] { snapshot = progression::CaptureSnapshotCurrent(id, operation); }, 5000, &why)) throw std::runtime_error(why);
    return progression::WriteCapturedSnapshot(snapshot);
}
std::string TabTitle(void* rule) {
    std::string label, raw = RuleLabel(rule);
    for (char ch : raw) { if (std::isspace(static_cast<unsigned char>(ch))) { if (!label.empty() && label.back() != 0x20) label += 0x20; } else label += ch; }
    while (!label.empty() && label.back() == ' ') label.pop_back();
    std::string title = std::to_string(RuleNumber(rule)) + " " + label;
    // The game separates tab names in its sync string with '|'; keep names short.
    std::replace(title.begin(), title.end(), '|', '/');
    if (title.size() > 24) { title.resize(24); while (!title.empty() && (static_cast<unsigned char>(title.back()) & 0xC0) == 0x80) title.pop_back(); if (!title.empty() && static_cast<unsigned char>(title.back()) >= 0xC0) title.pop_back(); }
    return title;
}

// Rebuilds the open stash panel's folder and tab bar from the stash data,
// as the game does after its own tab purchase reaches the client.
int UpdateStashUi(const StashView& view) {
    auto* controlsClass = game::FindClass("LE.dll", "", "StashTabbedUIControls");
    if (!controlsClass) return 0;
    Root type(api().type_get_object(api().class_get_type(controlsClass)));
    bool inactive = true; void* fa[]{type.Get(), &inactive};
    Root found(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, fa));
    const auto count = Get<uintptr_t>(found.Get(), 0x18);
    if (!count) return 0;
    // A List<StashTab> of the current tabs, created through the runtime.
    const auto* field = api().class_get_field_from_name(Class("ClientStashInfo"), "tabs");
    Root listType(api().type_get_object(api().field_get_type(field)));
    void* ca[]{listType.Get()};
    auto* activator = game::FindClass("mscorlib.dll", "System", "Activator");
    Root list(Invoke(Typed(activator, "CreateInstance", {"System.Type"}), nullptr, ca));
    for (void* tab : Tabs(view.stash)) { void* aa[]{tab}; Invoke(Method(list.Get(), "Add", 1), list.Get(), aa); }
    void* folders = Get<void*>(view.stash, F("Stash", "<Categories>k__BackingField", "LE.Data"));
    int updated = 0;
    for (uintptr_t i = 0; i < count && i < 8; ++i) {
        void* controls = Get<void*>(found.Get(), 0x20 + 8 * i);
        if (!controls || !game::IsAlive(controls)) continue;
        void* ua[]{folders, list.Get()};
        Invoke(Method(controls, "UpdateTabsUIAsync", 2), controls, ua);
        ++updated;
    }
    return updated;
}
void Refresh(void* manager, const StashView& view) {
    // Sends the new folder/tab metadata, as a purchase does, and rebuilds an
    // open stash panel's folder and tab bar (it does not re-read them itself).
    Invoke(M("ItemContainersManager", "sendStashMetaDataToClient", 0), manager);
    if (!game::Guarded([&] { UpdateStashUi(view); }, nullptr)) Log("stash rules: the stash panel will show the new tabs when it is reopened");
}
std::string WithBackup(std::string result, const std::string& backup) { result.pop_back(); return result + ",\"backup\":" + Json(backup) + "}"; }
}  // namespace

std::string Read(const std::string& id) { return Run([=] { return State(Current(id)); }); }

std::string Create(const std::string& id, bool hidden, bool disabled) {
    std::string backup;
    try { backup = Backup(id, "stash-rule-tabs"); }
    catch (const std::exception& e) { return "{\"ok\":false,\"error\":" + Json(std::string("Recovery backup failed; nothing was changed: ") + e.what()) + "}"; }
    return WithBackup(Run([=] {
        const auto c = Current(id);
        const auto view = View(c.manager);
        void* filter = Filter();
        if (!filter) throw std::runtime_error("Select a loot filter in the game first.");
        const std::string filterName = FilterName(filter);
        auto all = LoadLinks();
        auto tabs = Tabs(view.stash);
        auto links = LiveLinks(all, filterName, tabs);
        // One folder per filter, named after it.
        int folderId = -1;
        for (void* f : Folders(view.stash))
            if (f && Text(Get<void*>(f, F("StashCategory", "_displayName", "LE.Data"))) == filterName) folderId = Get<int>(f, F("StashCategory", "_categoryId", "LE.Data"));
        if (folderId < 0) {
            Root name(api().string_new(filterName.substr(0, 24).c_str()));
            int index = view.index; void* a[]{c.manager, &index, name.Get()};
            Invoke(M("ServerStashCommands", "ServerAddNewCategory", 3), nullptr, a);
            for (void* f : Folders(view.stash))
                if (f && Text(Get<void*>(f, F("StashCategory", "_displayName", "LE.Data"))) == filterName.substr(0, 24)) folderId = Get<int>(f, F("StashCategory", "_categoryId", "LE.Data"));
            if (folderId < 0) throw std::runtime_error("The stash folder could not be created (the game allows a limited number of folders).");
        }
        const auto palette = TabColors();
        int created = 0, existing = 0, skipped = 0, atLimit = 0;
        std::vector<std::string> notes;
        for (void* rule : Rules(filter)) {
            if (!rule) continue;
            const int number = RuleNumber(rule);
            const bool enabled = Get<bool>(rule, F("Rule", "isEnabled", "ItemFiltering"));
            const int type = Get<int>(rule, F("Rule", "type", "ItemFiltering"));  // 0 SHOW, 1 HIDE, 2 HIGHLIGHT
            if ((!enabled && !disabled) || (type == 1 && !hidden)) { ++skipped; continue; }
            if (links.contains(number)) { ++existing; continue; }
            void* ca[]{view.container};
            if (!Value<bool>(M("ItemContainersManager", "CanAddNewStashTab", 1), c.manager, ca)) { ++atLimit; continue; }
            auto priority = BuildPriority(rule);
            Root name(api().string_new(TabTitle(rule).c_str()));
            int folder = folderId, order = static_cast<int>(tabs.size()), icon = 0, color = TabColorFor(rule, palette);
            void* a[]{view.container, &folder, &order, name.Get(), &icon, &color, priority.object->Get(), nullptr};
            void* tab = Invoke(M("ItemContainersManager", "AddNewStashTab", 8), c.manager, a);
            if (!tab) throw std::runtime_error("The game did not create the stash tab.");
            links[number] = TabId(tab);
            tabs = Tabs(view.stash);
            ++created;
            if (!priority.unmapped.empty()) notes.push_back("Rule " + std::to_string(number) + ": " + [&] { std::string s; for (auto& u : priority.unmapped) s += (s.empty() ? "" : ", ") + u; return s; }());
        }
        all[filterName] = links;
        SaveLinks(all);
        Refresh(c.manager, view);
        progression::SaveCurrent(id);
        std::string state = State(c);
        state.pop_back();
        return state + ",\"created\":" + std::to_string(created) + ",\"existing\":" + std::to_string(existing) + ",\"skipped\":" + std::to_string(skipped)
            + ",\"atLimit\":" + std::to_string(atLimit) + ",\"notExact\":" + List(notes) + "}";
    }), backup);
}

std::string Assign(const std::string& id, int tabId, int ruleNumber) {
    std::string backup;
    try { backup = Backup(id, "stash-rule-assign"); }
    catch (const std::exception& e) { return "{\"ok\":false,\"error\":" + Json(std::string("Recovery backup failed; nothing was changed: ") + e.what()) + "}"; }
    return WithBackup(Run([=] {
        const auto c = Current(id);
        const auto view = View(c.manager);
        void* filter = Filter();
        if (!filter) throw std::runtime_error("Select a loot filter in the game first.");
        const std::string filterName = FilterName(filter);
        void* tab = nullptr;
        for (void* t : Tabs(view.stash)) if (t && TabId(t) == tabId) tab = t;
        if (!tab) throw std::runtime_error("That stash tab no longer exists. Refresh and try again.");
        void* rule = nullptr;
        if (ruleNumber > 0) for (void* r : Rules(filter)) if (r && RuleNumber(r) == ruleNumber) rule = r;
        if (ruleNumber > 0 && !rule) throw std::runtime_error("That rule is not in the active loot filter.");
        std::unique_ptr<Root> priority;
        std::vector<std::string> unmapped;
        if (rule) { auto built = BuildPriority(rule); priority = std::move(built.object); unmapped = built.unmapped; }
        else priority = std::make_unique<Root>(New(Class("StashPriority")));  // no conditions: the tab takes nothing automatically
        Root name(api().string_new(TabName(tab).c_str()));
        int index = view.index, icon = Get<int>(tab, F("StashTab", "_iconId", "LE.Data")), color = Get<int>(tab, F("StashTab", "_colorId", "LE.Data")),
            parent = Get<int>(tab, F("StashTab", "_categoryId", "LE.Data"));
        bool ignore = Get<bool>(tab, F("StashTab", "_ignoreFolderPriority", "LE.Data"));
        int tabValue = tabId;
        void* a[]{c.manager, &index, &tabValue, &icon, &color, &parent, name.Get(), priority->Get(), &ignore};
        Invoke(M("ServerStashCommands", "ServerSetTabInfo", 9), nullptr, a);
        auto all = LoadLinks();
        auto& links = all[filterName];
        for (auto it = links.begin(); it != links.end();) it = it->second == tabId ? links.erase(it) : std::next(it);
        if (rule) links[ruleNumber] = tabId;
        SaveLinks(all);
        Refresh(c.manager, view);
        progression::SaveCurrent(id);
        std::string state = State(c);
        state.pop_back();
        return state + ",\"assigned\":" + std::to_string(ruleNumber) + ",\"notExact\":" + List(unmapped) + "}";
    }), backup);
}

std::string Sort(const std::string& id) {
    std::string backup;
    try { backup = Backup(id, "stash-rule-sort"); }
    catch (const std::exception& e) { return "{\"ok\":false,\"error\":" + Json(std::string("Recovery backup failed; nothing was changed: ") + e.what()) + "}"; }
    struct Move { int x, y, tabIndex; };
    std::vector<Move> moves;
    std::string result, why;
    size_t next = 0;
    int moved = 0, full = 0, unmatched = 0, unlinked = 0;
    bool planned = false;
    // Plan once, then move a few items per frame like Stash all.
    if (!mainthread::RunSteps([&] {
        try {
            const auto c = Current(id);
            const auto view = View(c.manager);
            void* inventory = Get<void*>(c.manager, F("ItemContainersManager", "inventory"));
            const auto tabs = Tabs(view.stash);
            auto containers = Entries(Invoke(Method(view.container, "get_Containers", 0), view.container));
            if (containers.size() != tabs.size()) throw std::runtime_error("The stash layout is not loaded yet. Open the stash once and try again.");
            if (!planned) {
                planned = true;
                void* filter = Filter();
                if (!filter) throw std::runtime_error("Select a loot filter in the game first.");
                const auto links = LiveLinks(LoadLinks(), FilterName(filter), tabs);
                if (links.empty()) throw std::runtime_error("No stash tab is linked to a rule of the active loot filter yet.");
                std::map<int, int> indexOf;
                for (size_t i = 0; i < tabs.size(); ++i) indexOf[TabId(tabs[i])] = static_cast<int>(i);
                Root content(Invoke(Method(inventory, "GetContent", 0), inventory));
                for (void* entry : Entries(content.Get())) {
                    if (!entry) continue;
                    void* data = Get<void*>(entry, F("ItemContainerEntry", "data"));
                    Root unpacked(Invoke(Method(data, "getAsUnpacked", 0), data));
                    smartloot::RuleMatch match;
                    if (!smartloot::RuleFor(unpacked.Get(), &match) || match.number <= 0) { ++unmatched; continue; }
                    const auto link = links.find(match.number);
                    if (link == links.end()) { ++unlinked; continue; }
                    struct XY { int x, y; } p = Get<XY>(entry, F("ItemContainerEntry", "<Position>k__BackingField"));
                    moves.push_back({p.x, p.y, indexOf.at(link->second)});
                }
            }
            for (int n = 0; n < 4 && next < moves.size(); ++n, ++next) {
                const auto& m = moves[next];
                struct XY { int x, y; } p{m.x, m.y};
                void* pa[]{&p};
                void* entry = Invoke(Method(inventory, "GetEntryAt", 1), inventory, pa);
                if (!entry) continue;
                void* data = Get<void*>(entry, F("ItemContainerEntry", "data"));
                int quantity = Get<int>(entry, F("ItemContainerEntry", "quantity"));
                void* target = containers[static_cast<size_t>(m.tabIndex)];
                void* ra[]{data, &quantity};
                if (!Value<bool>(Method(target, "CanReceiveItem", 2), target, ra)) { ++full; continue; }
                Root keep(data);
                int context = 0;  // Context.DEFAULT
                void* remove[]{entry, &quantity, &context};
                if (!Value<bool>(Method(inventory, "TryRemoveItem", 3), inventory, remove)) { ++full; continue; }
                void* add[]{keep.Get(), &quantity, &context};
                if (Value<bool>(Typed(api().object_get_class(target), "TryAddItem", {"ItemData", "System.Int32", "Context"}), target, add)) { ++moved; continue; }
                // Put it back where it was; the slot was just freed.
                void* back[]{keep.Get(), &quantity, &p, &context};
                if (!Value<bool>(Typed(api().object_get_class(inventory), "TryAddItem", {"ItemData", "System.Int32", "UnityEngine.Vector2Int", "Context"}), inventory, back))
                    throw std::runtime_error("An item could not be returned to the inventory; restore the recovery backup.");
                ++full;
            }
            if (next < moves.size()) return false;
            progression::SaveCurrent(id);
            std::string state = State(c);
            state.pop_back();
            result = state + ",\"moved\":" + std::to_string(moved) + ",\"full\":" + std::to_string(full) + ",\"unmatched\":" + std::to_string(unmatched) + ",\"unlinked\":" + std::to_string(unlinked) + "}";
        } catch (const std::exception& e) {
            result = "{\"ok\":false,\"error\":" + Json(e.what()) + ",\"moved\":" + std::to_string(moved) + "}";
        }
        return true;
    }, 10000, &why)) result = "{\"ok\":false,\"error\":" + Json(why) + "}";
    if (result.empty()) result = "{\"ok\":false,\"error\":\"The stash operation did not complete.\"}";
    return WithBackup(result, backup);
}

#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
// Isolated steps for diagnosing the creation path on the disposable character.
std::string Test(const std::string& id, const std::string& action) {
    return Run([=]() -> std::string {
        const auto c = Current(id);
        const auto view = View(c.manager);
        std::ostringstream out; out << "{\"ok\":true,\"action\":" << Json(action);
        if (action == "folder") {
            Root name(api().string_new("EP Test Folder"));
            int index = view.index; void* a[]{c.manager, &index, name.Get()};
            Invoke(M("ServerStashCommands", "ServerAddNewCategory", 3), nullptr, a);
            out << ",\"folders\":" << Folders(view.stash).size();
        } else if (action == "priority-empty") {
            Root p(New(Class("StashPriority")));
            out << ",\"hasValue\":" << Boolean(Value<bool>(Method(p.Get(), "HasValue", 0), p.Get()));
        } else if (action.rfind("priority-rule-", 0) == 0) {
            const int number = std::stoi(action.substr(14));
            void* rule = nullptr;
            for (void* r : Rules(Filter())) if (r && RuleNumber(r) == number) rule = r;
            if (!rule) throw std::runtime_error("rule not found");
            auto built = BuildPriority(rule);
            out << ",\"hasValue\":" << Boolean(Value<bool>(Method(built.object->Get(), "HasValue", 0), built.object->Get())) << ",\"mapped\":" << List(built.mapped) << ",\"unmapped\":" << List(built.unmapped);
        } else if (action == "tab-null" || action == "tab-empty" || action.rfind("tab-rule-", 0) == 0) {
            std::unique_ptr<Root> priority;
            if (action == "tab-empty") priority = std::make_unique<Root>(New(Class("StashPriority")));
            else if (action != "tab-null") {
                const int number = std::stoi(action.substr(9));
                for (void* r : Rules(Filter())) if (r && RuleNumber(r) == number) priority = std::move(BuildPriority(r).object);
            }
            Root name(api().string_new(("EP " + action).c_str()));
            int folder = -1, order = static_cast<int>(Tabs(view.stash).size()), icon = 0, color = 1;
            for (void* f : Folders(view.stash)) if (f) folder = Get<int>(f, F("StashCategory", "_categoryId", "LE.Data"));
            void* a[]{view.container, &folder, &order, name.Get(), &icon, &color, priority ? priority->Get() : nullptr, nullptr};
            void* tab = Invoke(M("ItemContainersManager", "AddNewStashTab", 8), c.manager, a);
            out << ",\"tab\":" << (tab ? std::to_string(TabId(tab)) : std::string("null")) << ",\"tabs\":" << Tabs(view.stash).size();
        } else if (action == "ui-update") {
            out << ",\"updated\":" << UpdateStashUi(view);
        } else if (action == "gc") {
            Invoke(game::FindMethod("mscorlib.dll", "System", "GC", "Collect", 0));
            out << ",\"collected\":true";
        } else if (action.rfind("repeat-", 0) == 0) {
            // repeat-<rule>-<count>: build the rule's priority repeatedly, then collect.
            const auto dash = action.find('-', 7);
            const int number = std::atoi(action.substr(7, dash - 7).c_str()), count = std::atoi(action.substr(dash + 1).c_str());
            void* rule = nullptr;
            for (void* r : Rules(Filter())) if (r && RuleNumber(r) == number) rule = r;
            if (!rule) throw std::runtime_error("rule not found");
            for (int i = 0; i < count; ++i) { auto built = BuildPriority(rule); }
            Invoke(game::FindMethod("mscorlib.dll", "System", "GC", "Collect", 0));
            out << ",\"built\":" << count;
        } else if (action == "refresh") {
            Refresh(c.manager, view);
        } else if (action == "save") {
            progression::SaveCurrent(id);
        } else throw std::runtime_error("unknown stash rules test action");
        return out.str() + "}";
    });
}
#endif

}  // namespace ep::stashrules
