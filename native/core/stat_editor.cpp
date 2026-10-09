#include "stat_editor.hpp"
#include "common.hpp"
#include "game.hpp"
#include "mainthread.hpp"
#include "managed.hpp"
#include "hook.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ep::statedit {
namespace {
using il2cpp::Method;
using game::MethodRef;
struct Root {
    uintptr_t handle = 0;
    Root() = default;
    explicit Root(void* obj) : handle(il2cpp::api().gchandle_new(obj, false)) {
        if (!handle) throw std::runtime_error("GC root allocation failed");
    }
    Root(const Root&) = delete;
    Root& operator=(const Root&) = delete;
    Root(Root&& r) noexcept : handle(std::exchange(r.handle, 0)) {}
    Root& operator=(Root&& r) noexcept {
        if (this != &r) { Clear(); handle = std::exchange(r.handle, 0); }
        return *this;
    }
    ~Root() { Clear(); }
    void Clear() { if (handle) il2cpp::api().gchandle_free(std::exchange(handle, 0)); }
    void* Get() const { return handle ? il2cpp::api().gchandle_get_target(handle) : nullptr; }
};

// Everything below is accessed only from a mainthread::Run job.
MethodRef m_actor, m_ctor, m_recalculate, m_isOffline, m_gameObject, m_children, m_name, m_tileClick;
MethodRef m_totalAdded, m_totalIncreased, m_totalMore, m_findObjects, m_playOffline, m_active;
MethodRef m_sheetOpen, m_sheetClose, m_sheetIsOpen;
MethodRef m_attributeApply, m_attributeValue;
using AttributeFn = void (*)(void*, const Method*);
AttributeFn attributeOriginal = nullptr;
bool attributeHook = false;
const il2cpp::Class* c_landing = nullptr;
const il2cpp::Class* c_stat = nullptr;
const il2cpp::Class* c_display = nullptr;
const il2cpp::Field* f_sheet = nullptr;
const il2cpp::Field* f_select = nullptr;
const il2cpp::Field* f_ui = nullptr;
size_t o_mutator = 0, o_stats = 0, o_tracker = 0, o_data = 0, o_charName = 0, o_level = 0;
size_t o_list = 0, o_dirty = 0, o_sp = 0, o_tags = 0, o_special = 0, o_extra = 0;
size_t o_added = 0, o_increased = 0, o_more = 0, o_dontCollapse = 0, o_sheetStats = 0;
size_t o_text = 0, o_itemDisplay = 0, o_tiles = 0, o_onlineTab = 0, o_listLoading = 0, o_loading = 0;
size_t o_itemText = 0;
size_t o_tileData = 0, o_tileOnline = 0;
bool ready = false, sheetReady = false;
std::map<uint8_t, std::string> properties;
std::map<std::string, size_t> displayOffsets;
std::vector<std::pair<std::string, size_t>> directItems;
struct Manual { std::string name; size_t offset; Key key; bool derived; };
std::vector<Manual> manual;
struct Owned { Key key; Root entry; float flat = 0, increase = 0, more = 0; };
// The core lives for the process lifetime. Do not call into IL2CPP from a DLL CRT
// destructor after Unity has shut the runtime down. Actor switches and reset free
// roots explicitly; the final bounded context is reclaimed with the process.
struct Context { Root owner; std::vector<Owned> owned; };
Context& context = *new Context;
Root& owner = context.owner;
std::vector<Owned>& owned = context.owned;
constexpr size_t Items = 0x10, Count = 0x18, ArrayCount = 0x18, ArrayFirst = 0x20;

template<class T> T& At(void* p, size_t offset) { return *reinterpret_cast<T*>(static_cast<char*>(p) + offset); }
void* Invoke(const Method* method, void* self, void** args = nullptr) {
    if (!method) throw std::runtime_error("required method missing");
    void* exception = nullptr;
    void* result = il2cpp::api().runtime_invoke(method, self, args, &exception);
    if (exception) {
        const auto* cls = il2cpp::api().object_get_class(exception);
        throw std::runtime_error(std::string("managed exception: ") + (cls ? il2cpp::api().class_get_name(cls) : "unknown"));
    }
    return result;
}
void* Invoke(MethodRef method, void* self, void** args = nullptr) { return Invoke(method.info, self, args); }
const Method* ListMethod(void* list, const char* name, int arity) {
    if (!list) throw std::runtime_error("null managed list");
    const auto* cls = il2cpp::api().object_get_class(list);
    const auto* method = il2cpp::api().class_get_method_from_name(cls, name, arity);
    if (!method) throw std::runtime_error(std::string("list method missing: ") + name);
    return method;
}
std::vector<void*> Entries(void* list) {
    if (!list) throw std::runtime_error("null stats list");
    const int n = At<int>(list, Count);
    void* arr = At<void*>(list, Items);
    if (n < 0 || n > 4096 || !arr || static_cast<uintptr_t>(n) > At<uintptr_t>(arr, ArrayCount))
        throw std::runtime_error("invalid managed list bounds");
    std::vector<void*> entries;
    entries.reserve(n);
    for (int i = 0; i < n; ++i) entries.push_back(At<void*>(arr, ArrayFirst + sizeof(void*) * i));
    return entries;
}
std::vector<float> MoreValues(void* entry) {
    void* list = At<void*>(entry, o_more);
    if (!list) throw std::runtime_error("unconstructed Stats.Stat (restart with the corrected core)");
    const int n = At<int>(list, Count);
    void* arr = At<void*>(list, Items);
    if (n < 0 || n > 4096 || !arr || static_cast<uintptr_t>(n) > At<uintptr_t>(arr, ArrayCount))
        throw std::runtime_error("invalid moreValues bounds");
    std::vector<float> values;
    for (int i = 0; i < n; ++i) values.push_back(At<float>(arr, ArrayFirst + sizeof(float) * i));
    return values;
}
Key EntryKey(void* entry) {
    return {At<uint8_t>(entry, o_sp), At<int32_t>(entry, o_tags), At<uint8_t>(entry, o_special), At<int32_t>(entry, o_extra)};
}
std::string KeyText(Key key) {
    return "SP=" + std::to_string(key.sp) + " (" + properties.at(key.sp) + ") tags=" + std::to_string(key.tags) +
        " special=" + std::to_string(key.special) + " extra=" + std::to_string(key.extra);
}
std::string String(void* s) {
    if (!s) return "";
    const int n = At<int>(s, 0x10);
    if (n < 0 || n > 4096) throw std::runtime_error("invalid string length");
    const wchar_t* chars = reinterpret_cast<wchar_t*>(static_cast<char*>(s) + 0x14);
    const int len = WideCharToMultiByte(CP_UTF8, 0, chars, n, nullptr, 0, nullptr, nullptr);
    std::string result(len, '\0');
    if (len) WideCharToMultiByte(CP_UTF8, 0, chars, n, result.data(), len, nullptr, nullptr);
    return result;
}
std::string Run(std::function<std::string()> fn) {
    std::string reply, why;
    if (!mainthread::Run([&] {
        try { reply = fn(); }
        catch (const std::exception& e) { reply = std::string("stat: refused: ") + e.what(); Log("%s", reply.c_str()); }
    }, 5000, &why)) return "stat: refused: " + why;
    return reply.empty() ? "stat: guarded failure; see core.log" : reply;
}
void RequireKey(Key key) {
    if (!ready) throw std::runtime_error("stat editor metadata unavailable");
    if (!properties.contains(key.sp)) throw std::runtime_error("SP is not defined in this game build");
}
void* Player(void** dataOut = nullptr) {
    if (!ready) throw std::runtime_error("stat editor metadata unavailable");
    if (!game::IsOfflinePlay()) throw std::runtime_error(game::GateText());
    managed::RequireSession(4);
    void* actor = Invoke(m_actor, nullptr);
    if (!actor || !game::IsAlive(actor)) throw std::runtime_error("enter a zone with an offline character first");
    void* tracker = At<void*>(actor, o_tracker);
    void* data = tracker ? At<void*>(tracker, o_data) : nullptr;
    void* boxed = data ? Invoke(m_isOffline, data) : nullptr;
    if (!boxed || !*static_cast<bool*>(il2cpp::api().object_unbox(boxed)))
        throw std::runtime_error("loaded CharacterData is not confirmed offline");
    void* mutator = At<void*>(actor, o_mutator);
    void* stats = mutator ? At<void*>(mutator, o_stats) : nullptr;
    if (!stats || !game::IsAlive(stats)) throw std::runtime_error("player stats unavailable");
    if (dataOut) *dataOut = data;
    if (owner.Get() != stats) {
        owned.clear();  // old actors keep their own session modifiers until destroyed
        owner = Root(stats);
    }
    return stats;
}
void Recalculate(void* stats) {
    At<uint8_t>(stats, o_dirty) = 1;
    // Resolve the actual override, including CharacterStats attribute recalculation.
    const Method* method = il2cpp::api().object_get_virtual_method(stats, m_recalculate.info);
    Invoke(method, stats);
}
void ListAdd(void* list, void* entry) { void* args[]{entry}; Invoke(ListMethod(list, "Add", 1), list, args); }
Owned* FindOwned(Key key) {
    for (auto& item : owned) if (item.key == key) return &item;
    return nullptr;
}
bool Present(void* stats, void* entry) {
    const auto entries = Entries(At<void*>(stats, o_list));
    return std::find(entries.begin(), entries.end(), entry) != entries.end();
}
Owned& EnsureOwned(void* stats, Key key) {
    if (auto* item = FindOwned(key)) {
        if (!Present(stats, item->entry.Get())) ListAdd(At<void*>(stats, o_list), item->entry.Get());
        return *item;
    }
    Root root(il2cpp::api().object_new(c_stat));
    void* entry = root.Get();
    Invoke(m_ctor, entry);
    MoreValues(entry);  // refuse before insertion if construction was incomplete
    At<uint8_t>(entry, o_sp) = key.sp;
    At<int32_t>(entry, o_tags) = key.tags;
    At<uint8_t>(entry, o_special) = key.special;
    At<int32_t>(entry, o_extra) = key.extra;
    // GetExactStatMatch deliberately excludes these. Equipment/buffs therefore cannot
    // collapse into our modifier or remove its value when the equipment changes.
    At<bool>(entry, o_dontCollapse) = true;
    owned.push_back({key, std::move(root)});
    try { ListAdd(At<void*>(stats, o_list), owned.back().entry.Get()); }
    catch (...) { owned.pop_back(); throw; }
    return owned.back();
}
bool CoreAttribute(Key key) { return key.sp >= 19 && key.sp <= 23 && !key.tags && !key.special && !key.extra; }
void TranslateAttributes(void* stats) {
    // The game prunes zero-value entries. A percent bonus on a zero-base
    // attribute must still become effective after new equipment adds points.
    for (auto& item : owned) if (CoreAttribute(item.key) && !Present(stats, item.entry.Get()))
        ListAdd(At<void*>(stats, o_list), item.entry.Get());
    float base[5]{};
    for (void* entry : Entries(At<void*>(stats, o_list))) {
        if (!entry) continue;
        const auto key = EntryKey(entry);
        auto* own = FindOwned(key);
        if (own && CoreAttribute(key) && own->entry.Get() == entry) continue;
        if (key.sp >= 19 && key.sp <= 23) base[key.sp - 19] += At<float>(entry, o_added);
        else if (key.sp == 46) for (auto& value : base) value += At<float>(entry, o_added);
    }
    // Validate all translations before writing any: no partial overflow change.
    std::vector<std::pair<void*, float>> changes;
    for (auto& item : owned) if (CoreAttribute(item.key)) {
        const double value = AttributeContribution(base[item.key.sp - 19], item.flat, item.increase, item.more);
        if (!std::isfinite(value)) throw std::runtime_error("attribute result is outside the supported range (+/-1,000,000)");
        changes.emplace_back(item.entry.Get(), static_cast<float>(value));
    }
    for (auto [entry, value] : changes) At<float>(entry, o_added) = value;
}
void AttributeDetour(void* stats, const Method* method) {
    if (stats == owner.Get() && game::IsOfflinePlay()) {
        std::string why;
        if (!game::Guarded([&] { TranslateAttributes(stats); }, &why))
            Log("attributes: translation refused: %s", why.c_str());
    }
    attributeOriginal(stats, method);
}
void EnsureAttributeHook() {
    if (attributeHook) return;
    std::string why;
    if (!m_attributeApply || !m_attributeValue || !hook::Install(m_attributeApply.code,
        reinterpret_cast<void*>(&AttributeDetour), reinterpret_cast<void**>(&attributeOriginal), &why))
        throw std::runtime_error("attribute percentage support unavailable: " + why);
    attributeHook = true;
}
void RemoveIdleAttributeHook() {
    if (!attributeHook || std::any_of(owned.begin(), owned.end(), [](const auto& item) { return CoreAttribute(item.key); })) return;
    std::string why;
    if (hook::Remove(m_attributeApply.code, &why)) attributeHook = false;
    else Log("attributes: hook removal refused: %s", why.c_str());
}
std::string ReadImpl(void* stats, Key key) {
    RequireKey(key);
    double added = 0, increased = 0, multiplier = 1;
    int matches = 0;
    for (void* entry : Entries(At<void*>(stats, o_list))) {
        if (!entry || !(EntryKey(entry) == key)) continue;
        ++matches;
        added += At<float>(entry, o_added);
        increased += At<float>(entry, o_increased);
        for (float value : MoreValues(entry)) multiplier *= 1.0 + value;
    }
    std::ostringstream out;
    out.precision(9);
    out << KeyText(key) << ": exact-key sum added=" << added << " increased=" << increased << " moreMultiplier=" << multiplier
        << " entries=" << matches;
    bool matchZero = false;
    void* args[]{&key.sp, &key.tags, &key.special, &key.extra, &matchZero};
    const auto floatResult = [](void* box) {
        if (!box) throw std::runtime_error("stat getter returned null");
        return *static_cast<float*>(il2cpp::api().object_unbox(box));
    };
    out << "; applicableAdded=" << floatResult(Invoke(m_totalAdded, stats, args))
        << " applicableIncreased=" << floatResult(Invoke(m_totalIncreased, stats, args))
        << " applicableMore=" << floatResult(Invoke(m_totalMore, stats, args));
    if (auto* item = FindOwned(key)) {
        void* entry = item->entry.Get();
        out << "; EpochPact added=" << (CoreAttribute(key) ? item->flat : At<float>(entry, o_added))
            << " increased=" << (CoreAttribute(key) ? item->increase : At<float>(entry, o_increased)) << " more=";
        const auto values = MoreValues(entry);
        out << (CoreAttribute(key) ? item->more : values.empty() ? 0 : values.front()) << " attached=" << Present(stats, entry);
    }
    if (CoreAttribute(key) && m_attributeValue) {
        const int attributes[]{0, 1, 2, 3, 4}; int attribute = attributes[key.sp - 19];
        void* attributeArgs[]{&attribute}; Root value(Invoke(m_attributeValue, stats, attributeArgs));
        out << "; gameValue=" << *static_cast<int*>(il2cpp::api().object_unbox(value.Get()));
    }
    return out.str();
}
void RemoveOwned(void* stats, Key key) {
    auto it = std::find_if(owned.begin(), owned.end(), [key](const Owned& item) { return item.key == key; });
    if (it == owned.end()) return;
    void* list = At<void*>(stats, o_list);
    if (Present(stats, it->entry.Get())) {
        void* args[]{it->entry.Get()};
        void* result = Invoke(ListMethod(list, "Remove", 1), list, args);
        if (!result || !*static_cast<bool*>(il2cpp::api().object_unbox(result))) throw std::runtime_error("modifier removal failed");
    }
    owned.erase(it);
}
std::string SetImpl(void* stats, Key key, Mode mode, double value) {
    RequireKey(key);
    if (!ValidValue(mode, value)) throw std::runtime_error("value must fit a finite float; increased/more must be >= -1");
    if ((key.sp >= 19 && key.sp <= 23) && !CoreAttribute(key))
        throw std::runtime_error("core attributes require their untagged character key");
    if (key.sp == 46 && mode != Mode::Added)
        throw std::runtime_error("All Attributes supports Flat bonus; set percentages on individual attributes");
    if (CoreAttribute(key)) {
        const bool existed = FindOwned(key) != nullptr;
        auto& item = EnsureOwned(stats, key);
        const auto flat = item.flat, increase = item.increase, more = item.more;
        try {
            (mode == Mode::Added ? item.flat : mode == Mode::Increased ? item.increase : item.more) = static_cast<float>(value);
            EnsureAttributeHook(); TranslateAttributes(stats); Recalculate(stats);
        } catch (...) {
            item.flat = flat; item.increase = increase; item.more = more;
            if (!existed) RemoveOwned(stats, key);
            TranslateAttributes(stats); Recalculate(stats); RemoveIdleAttributeHook(); throw;
        }
        if (!item.flat && !item.increase && !item.more) { RemoveOwned(stats, key); Recalculate(stats); RemoveIdleAttributeHook(); }
        return std::string("EpochPact ") + ModeName(mode) + "=" + std::to_string(value) + "; " + ReadImpl(stats, key);
    }
    Owned* prior = FindOwned(key);
    const bool existed = prior != nullptr;
    const bool wasPresent = prior && Present(stats, prior->entry.Get());
    void* entry = EnsureOwned(stats, key).entry.Get();
    const float oldAdded = At<float>(entry, o_added), oldIncreased = At<float>(entry, o_increased);
    const auto oldMore = MoreValues(entry);
    const float v = static_cast<float>(value);
    try {
      if (mode == Mode::More) {
        void* list = At<void*>(entry, o_more);
        const auto previous = MoreValues(entry);
        // Only this mod's own list is replaced; equipment multipliers remain separate.
        Invoke(ListMethod(list, "Clear", 0), list);
        try {
            if (v != 0) { float copy = v; void* args[]{&copy}; Invoke(ListMethod(list, "Add", 1), list, args); }
        } catch (...) {
            Invoke(ListMethod(list, "Clear", 0), list);
            for (float copy : previous) { void* args[]{&copy}; Invoke(ListMethod(list, "Add", 1), list, args); }
            throw;
        }
      } else At<float>(entry, mode == Mode::Added ? o_added : o_increased) = v;
      Recalculate(stats);
    } catch (...) {
        // A failed calculation must not leave a setting applied while reporting refusal.
        try {
            if (!existed) RemoveOwned(stats, key);
            else {
                At<float>(entry, o_added) = oldAdded; At<float>(entry, o_increased) = oldIncreased;
                void* list = At<void*>(entry, o_more);
                Invoke(ListMethod(list, "Clear", 0), list);
                for (float copy : oldMore) { void* args[]{&copy}; Invoke(ListMethod(list, "Add", 1), list, args); }
                if (wasPresent && !Present(stats, entry)) ListAdd(At<void*>(stats, o_list), entry);
                if (!wasPresent && Present(stats, entry)) {
                    void* listStats = At<void*>(stats, o_list); void* args[]{entry};
                    Invoke(ListMethod(listStats, "Remove", 1), listStats, args);
                }
            }
            Recalculate(stats);
        } catch (...) {
            Log("statedit: rollback/recalculation failed; stop edits and restart the game");
            throw std::runtime_error("calculation and rollback failed; stop edits and restart the game");
        }
        throw;
    }
    if (At<float>(entry, o_added) == 0 && At<float>(entry, o_increased) == 0 && MoreValues(entry).empty()) RemoveOwned(stats, key);
    return std::string("EpochPact ") + ModeName(mode) + "=" + std::to_string(v) + "; " + ReadImpl(stats, key);
}
bool Property(const std::string& s, uint8_t* sp) {
    int64_t n = 0;
    if (Integer(s, 0, 255, &n) && properties.contains(static_cast<uint8_t>(n))) { *sp = static_cast<uint8_t>(n); return true; }
    for (const auto& [id, name] : properties) if (NormalName(name) == NormalName(s)) { *sp = id; return true; }
    return false;
}
std::string Action(void* stats, Key key, const std::vector<std::string>& args, size_t start) {
    if (args.size() == start) return ReadImpl(stats, key);
    if (args[start] == "reset" && args.size() == start + 1) {
        RemoveOwned(stats, key); Recalculate(stats); RemoveIdleAttributeHook(); return "EpochPact modifier reset; " + ReadImpl(stats, key);
    }
    Mode mode{}; double value = 0;
    if (args.size() != start + 2 || !ParseMode(args[start], &mode) || !Number(args[start + 1], &value))
        return "stat: refused: use [added|increased|more <raw value>] or [reset]; fractions: 0.65 = 65%";
    return SetImpl(stats, key, mode, value);
}

struct Row { void* display; std::string name; Key base; Key modifier; bool useModifier = false; void* label = nullptr; bool derived = false; };
template<class T> T Field(void* display, const char* name) { return At<T>(display, displayOffsets.at(name)); }
Row MakeRow(void* display, const std::string& source = {}) {
    const int minion = Field<bool>(display, "minionStat") ? 8192 : 0;
    Row row{display, source.empty() ? String(Invoke(m_name, display)) : source,
        {Field<uint8_t>(display, "property"), Field<int32_t>(display, "tags") | minion,
            Field<uint8_t>(display, "specialTag"), Field<int32_t>(display, "extraTag")},
        {Field<uint8_t>(display, "modifierProperty"), Field<int32_t>(display, "modifierTags") | minion,
            Field<uint8_t>(display, "modifierSpecialTag"), Field<uint8_t>(display, "modifierExtraTag")},
        Field<bool>(display, "useModifierStat")};
    return row;
}
std::vector<Row> Rows(void* stats) {
    if (!sheetReady) throw std::runtime_error("character sheet metadata unavailable");
    void* sheet = game::StaticObject(f_sheet);
    if (!sheet || !game::IsAlive(sheet)) throw std::runtime_error("character sheet unavailable");
    if (At<void*>(sheet, o_sheetStats) != stats) throw std::runtime_error("sheet is not bound to the current player yet");
    void* go = Invoke(m_gameObject, sheet);
    void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(c_display));
    bool includeInactive = true;
    void* args[]{type, &includeInactive};
    Root array(Invoke(m_children, go, args));
    const uintptr_t n = At<uintptr_t>(array.Get(), ArrayCount);
    if (n > 1024) throw std::runtime_error("invalid character sheet display count");
    std::vector<Row> rows;
    for (uintptr_t i = 0; i < n; ++i) {
        void* display = At<void*>(array.Get(), ArrayFirst + sizeof(void*) * i);
        if (display && game::IsAlive(display)) rows.push_back(MakeRow(display));
    }
    // Some basic-sheet LineItems are referenced from CharacterSheet instead of being
    // children. Include them too, but never duplicate the same display component.
    for (const auto& [name, offset] : directItems) {
        void* item = At<void*>(sheet, offset);
        if (!item || !game::IsAlive(item)) continue;
        void* display = At<void*>(item, o_itemDisplay);
        if (!display || !game::IsAlive(display)) {
            // These basic-sheet values are set directly by CharacterSheet.UpdateSheet;
            // the LineItem has no CharacterStatDisplay component at all.
            uint8_t sp = 54;
            if (name == "enduranceThreshold") sp = 76;
            else if (name == "glancingBlow") sp = 62;
            else if (name == "parryChance") sp = 121;
            else if (name == "maximumCompanions") sp = 61;
            else if (name == "PotionHealth") sp = 48;
            void* label = o_itemText ? At<void*>(item, o_itemText) : nullptr;
            if (sp != 54 && label && game::IsAlive(label)) rows.push_back({nullptr, name, {sp, 0, 0, 0}, {}, false, label, false});
            continue;
        }
        const auto it = std::find_if(rows.begin(), rows.end(), [display](const Row& row) { return row.display == display; });
        if (it == rows.end()) rows.push_back(MakeRow(display, name));
        else if (name == "parryChance") it->name = name;
    }
    for (const auto& item : manual) {
        void* label = At<void*>(sheet, item.offset);
        if (label && game::IsAlive(label)) rows.push_back({nullptr, item.name, item.key, {}, false, label, item.derived});
    }
    return rows;
}
std::string RowText(const Row& row, size_t index) {
    std::ostringstream out;
    if (!row.display) {
        out << index << " name=" << row.name << " " << KeyText(row.base) << " derived=" << row.derived
            << " cachedText=" << String(At<void*>(row.label, o_text)) << " command=sheetstat " << index << " added|increased|more <value>";
        return out.str();
    }
    void* label = Field<void*>(row.display, "statText");
    if (!label) {
        void* item = Field<void*>(row.display, "_lineItemParent");
        if (item) {
            const size_t offset = game::FieldOffset("LE.dll", "", "CharacterStatDisplayLineItem", "_statValueDisplay");
            if (offset) label = At<void*>(item, offset);
        }
    }
    out << index << " name=" << row.name << " " << KeyText(row.base)
        << " display=" << Field<int>(row.display, "displayType")
        << " ignoreAdded=" << Field<bool>(row.display, "ignoreAdded")
        << " ignoreIncreased=" << Field<bool>(row.display, "ignoreIncreased")
        << " exact=" << Field<bool>(row.display, "requireExactTagMatch")
        << " base=" << Field<float>(row.display, "baseValue")
        << " quotient=" << Field<bool>(row.display, "useQuotient")
        << " inverse=" << Field<bool>(row.display, "needInverse")
        << " attackRate=" << Field<bool>(row.display, "modifiedByAttackRate")
        << " cap=" << (Field<bool>(row.display, "hasCap") ? std::to_string(Field<float>(row.display, "cap")) : "none")
        << " cachedText=" << (label ? String(At<void*>(label, o_text)) : "<inactive>");
    if (row.useModifier) out << " modifier={" << KeyText(row.modifier) << "}";
    out << " command=sheetstat " << index << " added|increased|more <value>";
    return out.str();
}
}  // namespace

bool Init() {
    m_attributeApply = game::FindMethod("LE.dll", "", "CharacterStats", "ApplyCoreAttributeModifiers", 0);
    m_attributeValue = game::FindMethod("LE.dll", "", "CharacterStats", "GetAttributeValue", 1);
    m_actor = game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0);
    m_ctor = game::FindMethod("LE.dll", "", "Stats.Stat", ".ctor", 0);
    m_recalculate = game::FindMethod("LE.dll", "", "BaseStats", "UpdateStatsInternal", 0);
    m_isOffline = game::FindMethod("LE.dll", "LE.Data", "CharacterData", "get_IsOffline", 0);
    m_tileClick = game::FindMethod("LE.dll", "", "CharacterSelect", "OnCharacterTileDoubleClicked", 1);
    m_totalAdded = game::FindMethod("LE.dll", "", "Stats", "GetTotalAdded", 5);
    m_totalIncreased = game::FindMethod("LE.dll", "", "Stats", "GetTotalIncreased", 4);
    m_totalMore = game::FindMethod("LE.dll", "", "Stats", "GetTotalMore", 4);
    c_landing = game::FindClass("LE.dll", "LE.UI.Login.UnityUI", "LandingZonePanel");
    m_playOffline = game::FindMethod("LE.dll", "LE.UI.Login.UnityUI", "LandingZonePanel", "OnPlayOfflineClicked", 0);
    m_active = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Behaviour", "get_isActiveAndEnabled", 0);
    m_sheetOpen = game::FindMethod("LE.dll", "", "UIBase", "OpenCharacterStats", 1);
    m_sheetClose = game::FindMethod("LE.dll", "", "UIBase", "CloseCharacterStats", 0);
    m_sheetIsOpen = game::FindMethod("LE.dll", "", "UIBase", "IsCharacterStatsOpen", 0);
    f_ui = game::FindStaticField("LE.dll", "", "UIBase", "instance");
    c_stat = game::FindClass("LE.dll", "", "Stats.Stat");
    c_display = game::FindClass("LE.dll", "", "CharacterStatDisplay");
    f_sheet = game::FindStaticField("LE.dll", "", "CharacterSheet", "instance");
    f_select = game::FindStaticField("LE.dll", "", "CharacterSelect", "instance");
    auto offset = [](const char* cls, const char* field) { return game::FieldOffset("LE.dll", "", cls, field); };
    o_mutator = offset("Actor", "characterMutator"); o_stats = offset("CharacterMutator", "myStats");
    o_tracker = offset("Actor", "characterDataTracker"); o_data = offset("CharacterDataTracker", "charData");
    o_charName = game::FieldOffset("LE.dll", "LE.Data", "CharacterData", "<CharacterName>k__BackingField");
    o_level = game::FieldOffset("LE.dll", "LE.Data", "CharacterData", "<Level>k__BackingField");
    o_list = offset("Stats", "stats"); o_dirty = offset("BaseStats", "statsNeedToBeUpdatedNextFrame");
    o_sp = offset("Stats.Stat", "property"); o_tags = offset("Stats.Stat", "tags");
    o_special = offset("Stats.Stat", "specialTag"); o_extra = offset("Stats.Stat", "extraTag");
    o_added = offset("Stats.Stat", "addedValue"); o_increased = offset("Stats.Stat", "increasedValue");
    o_more = offset("Stats.Stat", "moreValues"); o_dontCollapse = offset("Stats.Stat", "dontCollapse");
    o_sheetStats = offset("CharacterSheet", "characterStats"); o_itemDisplay = offset("CharacterStatDisplayLineItem", "_characterStatDisplay");
    o_itemText = offset("CharacterStatDisplayLineItem", "_statValueDisplay");
    o_text = game::FieldOffset("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "m_text");
    o_tiles = offset("CharacterSelect", "availableCharacterTiles"); o_onlineTab = offset("CharacterSelect", "isOnlineTabShowing");
    o_listLoading = offset("CharacterSelect", "isListActivelyLoading"); o_loading = offset("CharacterSelect", "isLoadingCharacter");
    o_tileData = offset("CharacterTile", "characterData"); o_tileOnline = offset("CharacterTile", "onlineCharacter");
    if (const auto* cls = game::FindClass("LE.dll", "", "SP")) {
        void* iter = nullptr;
        while (const auto* field = il2cpp::api().class_get_fields(cls, &iter)) {
            if (!(il2cpp::api().field_get_flags(field) & il2cpp::kFieldLiteral)) continue;
            uint8_t value = 0;
            il2cpp::api().field_static_get_value(field, &value);
            properties.emplace(value, il2cpp::api().field_get_name(field));
        }
    }
    ready = m_actor && m_ctor && m_recalculate && m_isOffline && m_totalAdded && m_totalIncreased && m_totalMore && c_stat && o_mutator && o_stats && o_tracker && o_data &&
        o_charName && o_level && o_list && o_dirty && o_sp && o_tags && o_special && o_extra && o_added && o_increased && o_more &&
        o_dontCollapse && !properties.empty();
    m_gameObject = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_gameObject", 0);
    m_name = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    if (const auto* cls = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject")) {
        void* iter = nullptr;
        while (const auto* method = il2cpp::api().class_get_methods(cls, &iter)) {
            if (std::string(il2cpp::api().method_get_name(method)) != "GetComponentsInChildren" || il2cpp::api().method_get_param_count(method) != 2) continue;
            char* type = il2cpp::api().type_get_name(il2cpp::api().method_get_param(method, 0));
            const bool match = type && std::string(type) == "System.Type";
            if (type) il2cpp::api().free(type);
            if (match) { m_children = {method, *reinterpret_cast<void* const*>(method)}; break; }
        }
    }
    if (const auto* cls = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Object")) {
        void* iter = nullptr;
        while (const auto* method = il2cpp::api().class_get_methods(cls, &iter)) {
            if (std::string(il2cpp::api().method_get_name(method)) != "FindObjectsOfType" || il2cpp::api().method_get_param_count(method) != 2) continue;
            char* type = il2cpp::api().type_get_name(il2cpp::api().method_get_param(method, 0));
            const bool match = type && std::string(type) == "System.Type";
            if (type) il2cpp::api().free(type);
            if (match) { m_findObjects = {method, *reinterpret_cast<void* const*>(method)}; break; }
        }
    }
    const char* fields[]{"property", "tags", "specialTag", "extraTag", "modifierProperty", "modifierTags", "modifierSpecialTag", "modifierExtraTag",
        "useModifierStat", "displayType", "ignoreAdded", "ignoreIncreased", "baseValue", "requireExactTagMatch", "minionStat", "useQuotient",
        "needInverse", "modifiedByAttackRate", "hasCap", "cap", "statText", "_lineItemParent"};
    bool fieldsReady = true;
    for (const char* field : fields) { const size_t o = offset("CharacterStatDisplay", field); displayOffsets.emplace(field, o); fieldsReady &= o != 0; }
    const char* items[]{"enduranceThreshold", "glancingBlow", "parryChance", "maximumCompanions", "minionPower", "PotionHealth"};
    for (const char* field : items) { const size_t o = offset("CharacterSheet", field); if (o) directItems.emplace_back(field, o); }
    const auto addManual = [&](const char* name, uint8_t sp, bool derived = false) {
        const size_t o = offset("CharacterSheet", name);
        if (o) manual.push_back({name, o, {sp, 0, 0, 0}, derived});
    };
    addManual("PhysicalRes", 64); addManual("LightningRes", 15); addManual("ColdRes", 14); addManual("FireRes", 13);
    addManual("VoidRes", 26); addManual("NecroticRes", 27); addManual("PoisonRes", 28);
    addManual("armour", 10); addManual("armourPercent", 10, true); addManual("dodge", 11); addManual("DodgeChance", 11, true);
    addManual("blockChance", 29); addManual("blockEffectiveness", 53); addManual("blockMitigation", 53, true);
    sheetReady = ready && fieldsReady && f_sheet && c_display && m_gameObject && m_children && m_name && o_sheetStats && o_itemDisplay && o_text;
    Log("statedit: %zu SP properties; editor %s; sheet %s", properties.size(), ready ? "ready" : "unavailable", sheetReady ? "ready" : "unavailable");
    return ready;
}
std::string Read(Key key) { return Run([key] { RequireKey(key); return ReadImpl(Player(), key); }); }
std::string Set(Key key, Mode mode, double value) {
    if (!ValidValue(mode, value)) return "stat: refused: value must fit a finite float; increased/more must be >= -1";
    return Run([=] { RequireKey(key); return SetImpl(Player(), key, mode, value); });
}
std::string RawCommand(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::string out = "statraw <SP name/id> <tags> <special> <extra> [added|increased|more <raw value>|reset]\n";
        for (const auto& [sp, name] : properties) out += std::to_string(sp) + "=" + name + " ";
        return out + "\nPercent fractions: 0.65=65%; more 0.5=x1.5. Set replaces only the EpochPact contribution. statreset removes all mod contributions.";
    }
    Key key{}; int64_t tags = 0, special = 0, extra = 0;
    if (args.size() < 4 || !Property(args[0], &key.sp) || !Integer(args[1], INT32_MIN, INT32_MAX, &tags) ||
        !Integer(args[2], 0, 255, &special) || !Integer(args[3], INT32_MIN, INT32_MAX, &extra))
        return "statraw: refused: use a defined SP, int32 tags/extra, and byte special (0..255)";
    key.tags = static_cast<int32_t>(tags); key.special = static_cast<uint8_t>(special); key.extra = static_cast<int32_t>(extra);
    return Run([=] { return Action(Player(), key, args, 4); });
}
std::string Catalog() {
    return Run([] {
        void* stats = Player(); const auto rows = Rows(stats);
        std::string out = "sheetstats: " + std::to_string(rows.size()) + " display components; all full keys editable via sheetstat/statraw.\n"
            "Row IDs belong to this sheet instance. Labels on inactive tabs may be cached; values below are display metadata, not final combat totals.";
        for (size_t i = 0; i < rows.size(); ++i) out += "\n" + RowText(rows[i], i);
        return out;
    });
}
std::string SheetCommand(const std::vector<std::string>& args) {
    if (args.empty()) return Catalog();
    return Run([args] {
        void* stats = Player(); const auto rows = Rows(stats);
        size_t selected = rows.size(); int64_t id = 0;
        if (Integer(args[0], 0, static_cast<int64_t>(rows.size()) - 1, &id)) selected = static_cast<size_t>(id);
        else for (size_t i = 0; i < rows.size(); ++i) if (NormalName(rows[i].name) == NormalName(args[0])) {
            if (selected != rows.size()) return std::string("sheetstat: ambiguous name; use the numeric row ID from sheetstats");
            selected = i;
        }
        if (selected == rows.size()) return std::string("sheetstat: row not found; run sheetstats");
        const Row& row = rows[selected];
        size_t start = 1; Key key = row.base;
        if (args.size() > 1 && args[1] == "modifier") {
            if (!row.useModifier) return std::string("sheetstat: this row has no secondary modifier");
            key = row.modifier; ++start;
        }
        return RowText(row, selected) + "\n" + Action(stats, key, args, start);
    });
}
std::string ResetAll() {
    return Run([] {
        void* stats = Player(); const size_t n = owned.size();
        while (!owned.empty()) RemoveOwned(stats, owned.back().key);
        Recalculate(stats); RemoveIdleAttributeHook(); return "statreset: removed " + std::to_string(n) + " EpochPact modifiers; equipment entries preserved";
    });
}
std::string PlayerRead() {
    return Run([] { void* data = nullptr; Player(&data); return "playerread: name=" + String(At<void*>(data, o_charName)) +
        " level=" + std::to_string(At<int>(data, o_level)) + " CharacterData.IsOffline=true"; });
}
std::string PlayOffline() {
    return Run([] {
        managed::RequireSession(2); // Login, with system loading fully completed.
        if (!m_findObjects || !c_landing || !m_playOffline || !m_active) return std::string("playoffline: panel lookup unavailable");
        // On-demand typed lookup also finds a panel whose OnEnable ran before the capture hook.
        void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(c_landing));
        bool inactive = true; void* args[]{type, &inactive}; Root arr(Invoke(m_findObjects, nullptr, args));
        const uintptr_t n = At<uintptr_t>(arr.Get(), ArrayCount);
        if (n > 32) return std::string("playoffline: invalid panel count");
        void* panel = nullptr; int matches = 0;
        for (uintptr_t i = 0; i < n; ++i) {
            void* p = At<void*>(arr.Get(), ArrayFirst + sizeof(void*) * i);
            if (!p || !game::IsAlive(p)) continue;
            void* box = Invoke(m_active, p);
            if (!box || !*static_cast<bool*>(il2cpp::api().object_unbox(box))) continue;
            panel = p; ++matches;
        }
        if (matches != 1) return "playoffline: expected one active landing panel; active=" + std::to_string(matches) + " found=" + std::to_string(n);
        Invoke(m_playOffline, panel);
        return std::string("playoffline: active LandingZonePanel.OnPlayOfflineClicked called");
    });
}
std::string SheetOpen(bool open) {
    return Run([open] {
        Player();
        if (!f_ui || !m_sheetOpen || !m_sheetClose || !m_sheetIsOpen) return std::string("sheetopen: UIBase methods unavailable");
        void* ui = game::StaticObject(f_ui);
        if (!ui || !game::IsAlive(ui)) return std::string("sheetopen: UIBase unavailable");
        void* boxed = Invoke(m_sheetIsOpen, ui);
        if (!boxed) return std::string("sheetopen: panel state unavailable");
        const bool isOpen = *static_cast<bool*>(il2cpp::api().object_unbox(boxed));
        if (open != isOpen) {
            if (open) { bool closeOverlapping = false; void* args[]{&closeOverlapping}; Invoke(m_sheetOpen, ui, args); }
            else Invoke(m_sheetClose, ui);
        }
        return std::string("sheetopen: ") + (open ? "open" : "closed") + " via the game's UIBase";
    });
}
std::string Characters(const std::string& name, bool load, int level) {
    return Run([=] {
        if (load) managed::RequireSession(3); // CharacterSelect, no transition in flight.
        if (!f_select || !o_tiles || !o_onlineTab || !o_loading || !o_listLoading || !o_tileData || !o_tileOnline || !m_isOffline || !o_charName || !o_level)
            return std::string("characters: metadata unavailable");
        void* select = game::StaticObject(f_select);
        if (!select || !game::IsAlive(select)) return std::string("characters: character selection unavailable");
        if (At<bool>(select, o_onlineTab)) return std::string("characters: select the offline tab first");
        if (At<bool>(select, o_loading) || At<bool>(select, o_listLoading)) return std::string("characters: list/character is still loading");
        std::string out = "offline characters:"; void* match = nullptr; int matches = 0;
        for (void* tile : Entries(At<void*>(select, o_tiles))) {
            if (!tile || At<bool>(tile, o_tileOnline)) continue;
            void* data = At<void*>(tile, o_tileData); if (!data) continue;
            void* boxed = Invoke(m_isOffline, data);
            if (!boxed || !*static_cast<bool*>(il2cpp::api().object_unbox(boxed))) continue;
            const std::string charName = String(At<void*>(data, o_charName)); const int charLevel = At<int>(data, o_level);
            out += "\n" + charName + " level=" + std::to_string(charLevel);
            if (charName == name && (level < 0 || level == charLevel)) { match = tile; ++matches; }
        }
        if (!load) return out;
        if (matches != 1) return out + "\nloadname: refused: name/level must match exactly one offline character";
        if (!m_tileClick) return std::string("loadname: normal tile load method missing");
        void* args[]{match}; Invoke(m_tileClick, select, args);
        return "loadname: normal tile double-click requested for " + name + " (verify playerread before edits)";
    });
}
std::string Waypoints(const std::string& target, const std::string& expectedId) {
    return Run([target, expectedId] {
        std::string destination = target;
        int gateFilter = -1;
        const auto colon = target.find(':');
        if (colon != std::string::npos) {
            int64_t gate = -1;
            if (!Integer(target.substr(colon + 1), 0, 255, &gate)) return std::string("travel: refused: invalid waypoint gate");
            destination = target.substr(0, colon); gateFilter = static_cast<int>(gate);
        }
        void* data = nullptr; Player(&data); // environment AND loaded character must be offline
        if (!expectedId.empty() && String(Invoke(managed::Method(data, "get_Id", 0), data)) != expectedId)
            return std::string("travel: refused: loaded offline save id changed; refresh live data");
        const size_t unlocked = game::FieldOffset("LE.dll", "LE.Data", "CharacterData", "<UnlockedWaypointScenes>k__BackingField");
        const size_t scene = game::FieldOffset("LE.dll", "", "UIWaypoint", "sceneName");
        const size_t active = game::FieldOffset("LE.dll", "", "UIWaypoint", "isActive");
        const size_t here = game::FieldOffset("LE.dll", "", "UIWaypoint", "playerIsHere");
        const size_t gateOffset = game::FieldOffset("LE.dll", "", "UIWaypoint", "gate");
        const auto* cls = game::FindClass("LE.dll", "", "UIWaypointStandard");
        const auto isOpen = game::FindMethod("LE.dll", "", "UIBase", "IsWorldMapPanelOpen", 0);
        const auto open = game::FindMethod("LE.dll", "", "UIBase", "MapKeyDown", 0);
        const auto load = game::FindMethod("LE.dll", "", "UIWaypointStandard", "LoadWaypointScene", 0);
        if (!unlocked || !scene || !active || !here || !gateOffset || !cls || !isOpen || !open || !load || !m_findObjects || !f_ui)
            return std::string("travel: refused: waypoint metadata unavailable");
        std::vector<std::string> scenes;
        for (void* name : Entries(At<void*>(data, unlocked))) scenes.push_back(String(name));
        if (!target.empty() && std::find(scenes.begin(), scenes.end(), destination) == scenes.end())
            return std::string("travel: refused: target is not an unlocked waypoint for this offline character");
        void* ui = game::StaticObject(f_ui);
        if (!ui || !game::IsAlive(ui)) return std::string("travel: refused: UIBase unavailable");
        void* boxed = Invoke(isOpen, ui);
        if (!boxed) return std::string("travel: refused: map state unavailable");
        if (!*static_cast<bool*>(il2cpp::api().object_unbox(boxed))) Invoke(open, ui);
        void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));
        bool inactive = true; void* args[]{type, &inactive}; Root arr(Invoke(m_findObjects, nullptr, args));
        const uintptr_t n = At<uintptr_t>(arr.Get(), ArrayCount);
        if (n > 2048) return std::string("travel: refused: invalid waypoint count");
        std::string out = "waypoints: unlocked="; for (const auto& name : scenes) out += name + " ";
        void* selected = nullptr; int matches = 0;
        for (uintptr_t i = 0; i < n; ++i) {
            void* point = At<void*>(arr.Get(), ArrayFirst + sizeof(void*) * i);
            if (!point || !game::IsAlive(point)) continue;
            const auto name = String(At<void*>(point, scene));
            if (std::find(scenes.begin(), scenes.end(), name) == scenes.end()) continue;
            out += "\n" + name + " active=" + (At<bool>(point, active) ? "true" : "false") +
                   " here=" + (At<bool>(point, here) ? "true" : "false") + " gate=" + std::to_string(At<uint8_t>(point, gateOffset));
            if (name == destination && At<bool>(point, active) && !At<bool>(point, here) &&
                (gateFilter < 0 || At<uint8_t>(point, gateOffset) == gateFilter)) { selected = point; ++matches; }
        }
        if (target.empty()) return out;
        if (matches != 1) return out + "\ntravel: refused: expected exactly one active unlocked destination outside the current scene";
        Invoke(load, selected);
        return "travel: normal waypoint transition requested for " + target + "; verify playerread after loading";
    });
}
std::string Exits(const std::string& target) {
    return Run([target] {
        Player();
        const size_t scene = game::FieldOffset("LE.dll", "", "TransitionOrGateInteraction", "sceneName");
        const size_t handler = game::FieldOffset("LE.dll", "", "BaseInteraction", "conditionHandler");
        const auto* cls = game::FindClass("LE.dll", "", "LoadSceneInteraction");
        const auto check = game::FindMethod("LE.dll", "", "ConditionHandler", "CheckConditionReadiness", 1);
        const auto trigger = game::FindMethod("LE.dll", "", "ConditionHandler", "TryTriggerInteraction", 1);
        const auto closeMap = game::FindMethod("LE.dll", "", "UIBase", "closeMap", 0);
        if (!scene || !handler || !cls || !check || !trigger || !m_findObjects || !m_active)
            return std::string("exit: refused: scene-exit metadata unavailable");
        if (!target.empty() && closeMap && f_ui) {
            void* ui = game::StaticObject(f_ui); if (ui && game::IsAlive(ui)) Invoke(closeMap, ui);
        }
        void* actor = Invoke(m_actor, nullptr); void* source = Invoke(m_gameObject, actor);
        if (!source || !game::IsAlive(source)) return std::string("exit: refused: player source unavailable");
        void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));
        bool inactive = true; void* args[]{type, &inactive}; Root arr(Invoke(m_findObjects, nullptr, args));
        const uintptr_t n = At<uintptr_t>(arr.Get(), ArrayCount);
        if (n > 512) return std::string("exit: refused: invalid scene-exit count");
        std::string out = "exits: game ConditionHandler readiness checked";
        void* selected = nullptr; int matches = 0; void* sourceArgs[]{source};
        for (uintptr_t i = 0; i < n; ++i) {
            void* exit = At<void*>(arr.Get(), ArrayFirst + sizeof(void*) * i);
            if (!exit || !game::IsAlive(exit)) continue;
            void* enabled = Invoke(m_active, exit);
            if (!enabled || !*static_cast<bool*>(il2cpp::api().object_unbox(enabled))) continue;
            void* condition = At<void*>(exit, handler);
            if (!condition || !game::IsAlive(condition)) continue;
            enabled = Invoke(m_active, condition);
            if (!enabled || !*static_cast<bool*>(il2cpp::api().object_unbox(enabled))) continue;
            const auto name = String(At<void*>(exit, scene));
            void* can = Invoke(check, condition, sourceArgs);
            const bool allowed = can && *static_cast<bool*>(il2cpp::api().object_unbox(can));
            out += "\n" + name + " ready=" + (allowed ? "true" : "false");
            if (name == target && allowed) { selected = condition; ++matches; }
        }
        if (target.empty()) return out;
        if (matches != 1) return out + "\nexit: refused: target must match exactly one active ready scene exit";
        Invoke(trigger, selected, sourceArgs);
        return "exit: normal ConditionHandler.TryTriggerInteraction requested for " + target + "; verify playerread after loading";
    });
}
}  // namespace ep::statedit
