#include "progression.hpp"
#include "file_safety.hpp"
#include "common.hpp"
#include "game.hpp"
#include "mainthread.hpp"
#include "managed.hpp"
#include "hook.hpp"

#include <algorithm>
#include <functional>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ep::progression {
namespace {
using game::MethodRef;
using il2cpp::Method;
constexpr size_t kArrayCount = 0x18, kArrayFirst = 0x20;
struct Root {
    uintptr_t handle = 0;
    explicit Root(void* object) {
        if (!object || !(handle = il2cpp::api().gchandle_new(object, false)))
            throw std::runtime_error("managed object/root unavailable");
    }
    Root(const Root&) = delete;
    ~Root() { if (handle) il2cpp::api().gchandle_free(handle); }
    void* Get() const { return il2cpp::api().gchandle_get_target(handle); }
};
template<class T> T Get(void* object, size_t offset) {
    if (!object || !offset) throw std::runtime_error("required object/field unavailable");
    return *reinterpret_cast<const T*>(static_cast<const char*>(object) + offset);
}
void* Invoke(const Method* method, void* self = nullptr, void** args = nullptr) {
    if (!method) throw std::runtime_error("required method unavailable");
    void* exception = nullptr;
    void* result = il2cpp::api().runtime_invoke(method, self, args, &exception);
    if (exception) {
        const auto* cls = il2cpp::api().object_get_class(exception);
        const auto* getter = il2cpp::api().class_get_method_from_name(cls, "get_Message", 0);
        void* second = nullptr;
        void* message = getter ? il2cpp::api().runtime_invoke(getter, exception, nullptr, &second) : nullptr;
        throw std::runtime_error(std::string("managed exception: ") + il2cpp::api().class_get_name(cls) +
            (message && !second ? ": " + managed::Text(message) : ""));
    }
    return result;
}
void* Invoke(MethodRef method, void* self = nullptr, void** args = nullptr) { return Invoke(method.info, self, args); }
template<class T> T Value(MethodRef method, void* self = nullptr, void** args = nullptr) {
    Root result(Invoke(method, self, args));
    return *static_cast<T*>(il2cpp::api().object_unbox(result.Get()));
}
std::string Text(void* object) {
    if (!object) return {};
    const int length = Get<int>(object, 0x10);
    if (length < 0 || length > 16 * 1024 * 1024) throw std::runtime_error("invalid managed string");
    const auto* chars = reinterpret_cast<const wchar_t*>(static_cast<const char*>(object) + 0x14);
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, length, nullptr, 0, nullptr, nullptr);
    if (length && !bytes) throw std::runtime_error("invalid string encoding");
    std::string text(bytes, '\0');
    if (bytes) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, length, text.data(), bytes, nullptr, nullptr);
    return text;
}
std::string Json(const std::string& value) {
    std::string result = "\"";
    for (unsigned char c : value) {
        if (c == '\\' || c == '"') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 0x20) { char escaped[7]; std::snprintf(escaped, sizeof escaped, "\\u%04x", c); result += escaped; }
        else result += static_cast<char>(c);
    }
    return result + '"';
}
const char* Boolean(bool value) { return value ? "true" : "false"; }
size_t Offset(const char* cls, const char* field, const char* ns = "") {
    const size_t result = game::FieldOffset("LE.dll", ns, cls, field);
    if (!result) throw std::runtime_error(std::string("missing field: ") + cls + "." + field);
    return result;
}
std::vector<void*> Entries(void* list) {
    if (!list) throw std::runtime_error("required managed list unavailable");
    const auto* cls = il2cpp::api().object_get_class(list);
    const auto* size = il2cpp::api().class_get_field_from_name(cls, "_size");
    const auto* items = il2cpp::api().class_get_field_from_name(cls, "_items");
    if (!size || !items) throw std::runtime_error("managed list layout unavailable");
    const int n = Get<int>(list, il2cpp::api().field_get_offset(size));
    void* array = Get<void*>(list, il2cpp::api().field_get_offset(items));
    if (n < 0 || n > 8192 || !array || static_cast<uintptr_t>(n) > Get<uintptr_t>(array, kArrayCount))
        throw std::runtime_error("invalid managed list bounds");
    std::vector<void*> result;
    result.reserve(n);
    for (int i = 0; i < n; ++i) result.push_back(Get<void*>(array, kArrayFirst + sizeof(void*) * i));
    return result;
}
MethodRef m_actor, m_offline, m_questList, m_holder, m_stateList, m_state, m_stateful;
MethodRef m_passives, m_idols, m_attributes, m_findObjects, m_mapOpen, m_mapToggle, m_mapClose;
MethodRef m_complete, m_saveQuests, m_dirty, m_addWaypoint, m_serialize, m_json, m_saveData;
MethodRef m_currentLevel, m_cumulativeXp, m_adjustXp;
MethodRef m_checkWaypoint, m_monolith, m_sceneDetails;
MethodRef m_gainDirect, m_levelXp;
MethodRef m_currentScene;
MethodRef m_refreshDisplay;
MethodRef m_recordAnalytics;
using DisplayFn = void (*)(void*, const Method*);
DisplayFn originalDisplay = nullptr;
thread_local void* batchedQuestList = nullptr;
thread_local unsigned deferredDisplays = 0;
thread_local bool syntheticProgression = false;
thread_local unsigned skippedAnalytics = 0;
using AnalyticsFn = void (*)(void*, void*, void*, bool, void*, const Method*);
AnalyticsFn originalAnalytics = nullptr;
// Only the nested display calls of our explicit offline completion are coalesced.
// Quest state, objective events, rewards and ordinary gameplay remain native.
void DisplayDetour(void* self, const Method* method) {
    if (self == batchedQuestList) { ++deferredDisplays; return; }
    originalDisplay(self, method);
}
void AnalyticsDetour(void* self, void* eventName, void* properties, bool immediate, void* context, const Method* method) {
    // Bulk offline progression is a mod action, not hundreds of naturally played
    // quest/level analytics events. Gpp reserializes the whole queued payload on
    // each event (quadratic cost). Only synchronous analytics from OUR verified
    // native mutation calls are omitted; ordinary gameplay and saving pass through.
    if (syntheticProgression) { ++skippedAnalytics; return; }
    originalAnalytics(self, eventName, properties, immediate, context, method);
}
struct DisplayScope {
    void* previousQuestList = batchedQuestList;
    bool previousSynthetic = syntheticProgression;
    explicit DisplayScope(void* quests) { batchedQuestList = quests; syntheticProgression = true; }
    ~DisplayScope() { batchedQuestList = previousQuestList; syntheticProgression = previousSynthetic; }
};
struct StepTiming {
    LARGE_INTEGER start{}, frequency{};
    double& maximum;
    explicit StepTiming(double& value) : maximum(value) { QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start); }
    ~StepTiming() {
        LARGE_INTEGER end{}; QueryPerformanceCounter(&end);
        maximum = (std::max)(maximum, (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart);
    }
};
const il2cpp::Class* c_waypoint = nullptr;
const il2cpp::Field* f_ui = nullptr;
std::unique_ptr<Root> waypointUiRoot, waypointObjectsRoot;
bool ready = false;
std::map<std::string, int> questTypes, questStates, chapters;

std::map<std::string, int> Enum(const char* name) {
    const auto* cls = game::FindClass("LE.dll", "", name);
    if (!cls) throw std::runtime_error(std::string("missing enum: ") + name);
    std::map<std::string, int> values;
    void* iter = nullptr;
    while (const auto* field = il2cpp::api().class_get_fields(cls, &iter)) {
        if (!(il2cpp::api().field_get_flags(field) & il2cpp::kFieldLiteral)) continue;
        int value = 0;
        il2cpp::api().field_static_get_value(field, &value);
        values.emplace(il2cpp::api().field_get_name(field), value);
    }
    return values;
}
struct Player {
    void* actor = nullptr;
    void* data = nullptr;
    void* tracker = nullptr;
    void* quests = nullptr;
    std::string name, id;
};
Player Current() {
    if (!ready) throw std::runtime_error("progression metadata unavailable");
    if (!game::IsOfflinePlay()) throw std::runtime_error(game::GateText());
    managed::RequireSession(4);
    Player player;
    player.actor = Invoke(m_actor);
    if (!player.actor || !game::IsAlive(player.actor)) throw std::runtime_error("enter a zone with an offline character first");
    player.tracker = Get<void*>(player.actor, Offset("Actor", "characterDataTracker"));
    player.data = Get<void*>(player.tracker, Offset("CharacterDataTracker", "charData"));
    if (!player.data || !Value<bool>(m_offline, player.data)) throw std::runtime_error("loaded character is not offline");
    void* holder = Invoke(m_holder, player.actor);
    if (!holder || !game::IsAlive(holder)) throw std::runtime_error("player quest holder unavailable");
    player.quests = Invoke(m_stateList, holder);
    if (!player.quests || Get<void*>(player.quests, Offset("StatefulQuestList", "actor")) != player.actor)
        throw std::runtime_error("quest list does not belong to the loaded character");
    player.name = Text(Get<void*>(player.data, Offset("CharacterData", "<CharacterName>k__BackingField", "LE.Data")));
    // CharacterData inherits its save identifier; resolve through the actual class.
    const auto* getter = il2cpp::api().class_get_method_from_name(il2cpp::api().object_get_class(player.data), "get_Id", 0);
    if (!getter) throw std::runtime_error("character save identifier unavailable");
    player.id = Text(Invoke(getter, player.data));
    return player;
}
int State(const Player& player, void* quest) { void* args[]{quest}; return Value<int>(m_state, player.quests, args); }
void* Stateful(const Player& player, void* quest) { void* args[]{quest}; return Invoke(m_stateful, player.quests, args); }
std::string Route(void* quest, void* first) {
    if (!first) return "no starting step";
    std::set<void*> visited;
    for (void* step = first; step;) {
        if (!visited.insert(step).second || visited.size() > 99) return "cyclic/overlong completion route";
        if (Get<void*>(step, Offset("QuestStep", "quest")) != quest) return "foreign quest step";
        const int state = Get<int>(step, Offset("QuestStep", "questStateDuringThisStep"));
        if (state == questStates.at("Completed")) return {};
        if (state != questStates.at("Active")) return "completion route ends in a different state";
        for (void* objective : Entries(Get<void*>(step, Offset("QuestStep", "objectives")))) {
            if (!objective || Get<void*>(objective, Offset("Objective", "quest")) != quest) return "foreign/missing objective";
        }
        step = Get<void*>(step, Offset("QuestStep", "stepAfterCompletion"));
    }
    return "completion route has no completed step";
}
struct QuestInfo {
    void* object = nullptr;
    int id = -1, chapter = 0, state = 0, xp = 0, gold = 0, passive = 0, idol = 0, attributes = 0;
    bool main = false, visible = false, test = false, eligible = false;
    uint8_t type = 0;
    std::string name, reason;
};
std::vector<QuestInfo> Quests(const Player& player) {
    void* catalog = Invoke(m_questList);
    if (!catalog) throw std::runtime_error("quest catalog unavailable");
    std::vector<QuestInfo> result;
    std::set<int> ids;
    for (void* quest : Entries(Get<void*>(catalog, Offset("QuestList", "quests")))) {
        if (!quest) continue;
        QuestInfo q;
        q.object = quest;
        q.id = Get<int>(quest, Offset("Quest", "id"));
        if (q.id < 0 || !ids.insert(q.id).second) throw std::runtime_error("invalid/duplicate quest identifier");
        q.name = Text(Get<void*>(quest, Offset("Quest", "displayName")));
        q.chapter = Get<int>(quest, Offset("Quest", "chapter"));
        q.main = Get<bool>(quest, Offset("Quest", "mainLineQuest"));
        q.visible = Get<bool>(quest, Offset("Quest", "playerVisible"));
        q.test = Get<bool>(quest, Offset("Quest", "testQuest"));
        q.type = Get<uint8_t>(quest, Offset("Quest", "questType"));
        q.state = q.type == questTypes.at("Normal") ? State(player, quest) : -1;
        q.xp = Get<int>(quest, Offset("Quest", "experienceReward"));
        q.gold = Get<int>(quest, Offset("Quest", "goldReward"));
        q.passive = Get<int>(quest, Offset("Quest", "passivePointsReward"));
        q.idol = Get<int>(quest, Offset("Quest", "idolUnlockReward"));
        q.attributes = Get<int>(quest, Offset("Quest", "allAttributesReward"));
        q.eligible = q.type == questTypes.at("Normal") && !q.test && q.chapter != chapters.at("Minilith");
        if (q.eligible && q.state != questStates.at("Completed")) {
            const auto steps = Entries(Get<void*>(quest, Offset("Quest", "steps")));
            void* stateful = Stateful(player, quest);
            void* start = stateful ? Get<void*>(stateful, Offset("StatefulQuest", "currentStep")) : (steps.empty() ? nullptr : steps.front());
            q.reason = Route(quest, start);
        }
        result.push_back(std::move(q));
    }
    std::stable_sort(result.begin(), result.end(), [](const QuestInfo& a, const QuestInfo& b) {
        return std::pair(a.chapter, a.id) < std::pair(b.chapter, b.id);
    });
    return result;
}
struct WaypointInfo { void* string = nullptr; std::string scene; bool unlocked = false, monolith = false, always = false, noWaypoint = false, active = false; };
std::vector<WaypointInfo> Waypoints(const Player& player) {
    void* ui = game::StaticObject(f_ui);
    if (!ui || !game::IsAlive(ui)) throw std::runtime_error("world map unavailable");
    // Inactive waypoint components remain readable; opening and closing the
    // entire world map on every read needlessly rebuilds its UI. Initialize its
    // lazy prefab only when no waypoint components exist yet.
    bool validCache = waypointUiRoot && waypointUiRoot->Get() == ui && waypointObjectsRoot;
    if (validCache) {
        const auto n = Get<uintptr_t>(waypointObjectsRoot->Get(), kArrayCount);
        validCache = n > 0 && n <= 2048;
        for (uintptr_t i = 0; validCache && i < n; ++i)
            validCache = game::IsAlive(Get<void*>(waypointObjectsRoot->Get(), kArrayFirst + sizeof(void*) * i));
    }
    void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(c_waypoint));
    bool inactive = true; void* args[]{type, &inactive};
    if (!validCache) {
        waypointObjectsRoot = std::make_unique<Root>(Invoke(m_findObjects, nullptr, args));
        waypointUiRoot = std::make_unique<Root>(ui);
    }
    auto* objects = waypointObjectsRoot.get();
    bool opened = false;
    if (!Get<uintptr_t>(objects->Get(), kArrayCount) && !Value<bool>(m_mapOpen, ui)) {
        Invoke(m_mapToggle, ui); opened = true;
        waypointObjectsRoot = std::make_unique<Root>(Invoke(m_findObjects, nullptr, args));
        objects = waypointObjectsRoot.get();
    }
    std::vector<WaypointInfo> result;
    try {
        std::set<std::string> unlocked, seen;
        for (void* scene : Entries(Get<void*>(player.data, Offset("CharacterData", "<UnlockedWaypointScenes>k__BackingField", "LE.Data"))))
            unlocked.insert(Text(scene));
        void* scenes = Get<void*>(player.data, Offset("CharacterData", "<UnlockedWaypointScenes>k__BackingField", "LE.Data"));
        void* monolith = Invoke(m_monolith);
        const uintptr_t count = Get<uintptr_t>(objects->Get(), kArrayCount);
        if (count > 2048) throw std::runtime_error("invalid waypoint catalog size");
        for (uintptr_t i = 0; i < count; ++i) {
            void* point = Get<void*>(objects->Get(), kArrayFirst + sizeof(void*) * i);
            if (!point || !game::IsAlive(point)) continue;
            WaypointInfo w;
            w.string = Get<void*>(point, Offset("UIWaypoint", "sceneName"));
            w.scene = Text(w.string);
            if (w.scene.empty()) continue;
            void* sceneArgs[]{w.string};
            if (!Invoke(m_sceneDetails, nullptr, sceneArgs)) continue;
            // Inactive era panels have stale flags until shown. Use the same
            // check as UIWaypointController.OnEnable, retaining timeline gates.
            void* checkArgs[]{scenes, monolith}; Invoke(m_checkWaypoint, point, checkArgs);
            w.active = Get<bool>(point, Offset("UIWaypoint", "isActive"));
            if (!seen.insert(w.scene).second) {
                auto existing = std::find_if(result.begin(), result.end(), [&](const auto& p) { return p.scene == w.scene; });
                if (existing != result.end()) existing->active |= w.active;
                continue;
            }
            w.unlocked = unlocked.contains(w.scene);
            w.monolith = Get<bool>(point, Offset("UIWaypoint", "monolithWaypoint"));
            w.always = Get<bool>(point, Offset("UIWaypoint", "alwaysUnlocked"));
            w.noWaypoint = Get<bool>(point, Offset("UIWaypoint", "noWaypointInScene"));
            result.push_back(std::move(w));
        }
    } catch (...) { if (opened) Invoke(m_mapClose, ui); throw; }
    if (opened) Invoke(m_mapClose, ui);
    if (result.empty()) throw std::runtime_error("world map waypoint catalog is empty");
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.scene < b.scene; });
    return result;
}
std::string Run(std::function<std::string()> work) {
    std::string reply, why;
    if (!mainthread::Run([&] {
        try { reply = work(); }
        catch (const std::exception& e) { reply = "{\"ok\":false,\"error\":" + Json(e.what()) + "}"; }
    }, 5000, &why)) return "{\"ok\":false,\"error\":" + Json(why) + "}";
    return reply.empty() ? "{\"ok\":false,\"error\":\"guarded failure; see core.log\"}" : reply;
}
const Method* ObjectMethod(void* object, const char* name, int arity) {
    if (!object) throw std::runtime_error("required save object unavailable");
    const auto* method = il2cpp::api().class_get_method_from_name(il2cpp::api().object_get_class(object), name, arity);
    if (!method) throw std::runtime_error(std::string("missing save method: ") + name);
    return method;
}
std::string PathText(const std::filesystem::path& path) {
    const auto chars = path.wstring();
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars.data(), static_cast<int>(chars.size()), nullptr, 0, nullptr, nullptr);
    std::string result(n, '\0');
    if (n) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars.data(), static_cast<int>(chars.size()), result.data(), n, nullptr, nullptr);
    return result;
}
void Write(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    file.flush();
    if (!file) throw std::runtime_error("backup write failed");
}
struct RewardState {
    int level = 0, gold = 0, passives = 0, idols = 0, attributes = 0;
    int64_t xp = 0;
};
RewardState Rewards(const Player& player) {
    void* experience = Get<void*>(player.quests, Offset("StatefulQuestList", "experienceTracker"));
    void* gold = Get<void*>(player.quests, Offset("StatefulQuestList", "goldTracker"));
    struct Total { int64_t current, next; };
    Root total(Invoke(m_cumulativeXp, experience));
    RewardState result;
    result.xp = static_cast<Total*>(il2cpp::api().object_unbox(total.Get()))->current;
    result.level = Value<int>(m_currentLevel, experience);
    result.gold = Get<int>(gold, Offset("GoldTracker", "value"));
    result.passives = Value<int>(m_passives, player.quests);
    bool refresh = false; void* args[]{&refresh};
    result.idols = Value<uint8_t>(m_idols, player.quests, args);
    result.attributes = Value<int>(m_attributes, player.quests);
    return result;
}
std::string RewardJson(const RewardState& s) {
    return "{\"level\":" + std::to_string(s.level) + ",\"cumulativeXp\":" + std::to_string(s.xp) +
        ",\"gold\":" + std::to_string(s.gold) + ",\"passivePoints\":" + std::to_string(s.passives) +
        ",\"idolUnlock\":" + std::to_string(s.idols) + ",\"allAttributes\":" + std::to_string(s.attributes) + "}";
}
void CheckId(const Player& player, const std::string& id) {
    if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos || id != player.id)
        throw std::runtime_error("loaded offline save id changed; refresh the panel");
}
SnapshotData Capture(const Player& player, const char* operation) {
    if (!m_saveQuests || !m_serialize || !m_json) throw std::runtime_error("backup serializers unavailable");
    void* saveArgs[]{player.quests}; Invoke(m_saveQuests, player.data, saveArgs);
    void* characterArgs[]{player.data}; Root character(Invoke(m_serialize, nullptr, characterArgs));
    const auto characterText = Text(character.Get());
    void* globalTracker = Get<void*>(player.actor, Offset("Actor", "globalDataTracker"));
    void* stash = Get<void*>(globalTracker, Offset("GlobalDataTracker", "<stash>k__BackingField"));
    void* global = Get<void*>(globalTracker, Offset("GlobalDataTracker", "globalData"));
    const auto stashId = Text(Invoke(ObjectMethod(stash, "get_Id", 0), stash));
    if (stashId.empty() || stashId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
        throw std::runtime_error("unsafe stash identifier");
    void* stashArgs[]{stash}; Root stashJson(Invoke(m_json, nullptr, stashArgs));
    void* globalArgs[]{global}; Root globalJson(Invoke(m_json, nullptr, globalArgs));
    return {characterText, Text(stashJson.Get()), Text(globalJson.Get()), stashId,
        player.name, player.id, operation, RewardJson(Rewards(player))};
}
// Pure filesystem work on the command worker for campaign/waypoint actions.
std::string WriteSnapshot(const SnapshotData& snapshot) {
    wchar_t profile[32768];
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", profile, static_cast<DWORD>(std::size(profile)));
    if (!n || n >= std::size(profile)) throw std::runtime_error("offline save directory unavailable");
    const auto source = std::filesystem::path(profile) / L"AppData" / L"LocalLow" / L"Eleventh Hour Games" / L"Last Epoch" / L"Saves";
    if (!std::filesystem::is_directory(source)) throw std::runtime_error("offline save directory missing");
    const auto backup = std::filesystem::path(PluginDir()) / L"backups" / L"progression" /
        (std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    if (!std::filesystem::create_directories(backup / L"saves")) throw std::runtime_error("backup directory collision");
    int copied = 0;
    for (const auto& entry : std::filesystem::directory_iterator(source)) {
        if (!entry.is_regular_file() || entry.is_symlink()) continue;
        std::filesystem::copy_file(entry.path(), backup / L"saves" / entry.path().filename());
        ++copied;
    }
    if (!copied) throw std::runtime_error("empty offline save backup");
    Write(backup / L"live-character.json", snapshot.character);
    Write(backup / L"live-stash.json", snapshot.stash);
    Write(backup / L"live-global.json", snapshot.global);
    const auto manifest = "{\"format\":1,\"operation\":" + Json(snapshot.operation) + ",\"player\":" + Json(snapshot.name) +
        ",\"id\":" + Json(snapshot.id) + ",\"stashId\":" + Json(snapshot.stashId) + ",\"saveDirectory\":" +
        Json(PathText(source)) + ",\"before\":" + snapshot.rewards + "}";
    Write(backup / L".epochpact-retention", "1\n");
    Write(backup / L"manifest.json", manifest); // Commit marker: mutations start only after every snapshot succeeded.
    Log("progression: %s backup ready for %s (id %s): %s", snapshot.operation.c_str(), snapshot.name.c_str(), snapshot.id.c_str(), PathText(backup).c_str());
    try { files::Prune(backup.parent_path(), 30, backup); }
    catch (const std::exception& e) { Log("backup retention deferred: %s", e.what()); }
    return PathText(backup);
}
void Save(const Player& player) {
    void* args[]{player.quests}; Invoke(m_saveQuests, player.data, args);
    bool immediate = true; void* dirty[]{&immediate}; Invoke(m_dirty, player.tracker, dirty);
    Invoke(m_saveData, player.data); // The game's asynchronous offline save path; no direct active-save writes.
}
}  // namespace
bool Init() {
    std::string why;
    ready = game::Guarded([] {
        m_actor = game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0);
        m_offline = game::FindMethod("LE.dll", "LE.Data", "CharacterData", "get_IsOffline", 0);
        m_questList = game::FindMethod("LE.dll", "", "QuestList", "get", 0);
        m_holder = game::FindMethod("LE.dll", "", "Actor", "getPlayerQuestListHolder", 0);
        m_stateList = game::FindMethod("LE.dll", "", "StatefulQuestListHolder", "get_StatefulQuestList", 0);
        m_state = game::FindMethod("LE.dll", "", "StatefulQuestList", "getQuestState", 1);
        m_stateful = game::FindMethod("LE.dll", "", "StatefulQuestList", "getStatefulQuest", 1);
        m_passives = game::FindMethod("LE.dll", "", "StatefulQuestList", "getPassivePointsFromQuestRewards", 0);
        m_idols = game::FindMethod("LE.dll", "", "StatefulQuestList", "getIdolUnlockFromQuestRewards", 1);
        m_attributes = game::FindMethod("LE.dll", "", "StatefulQuestList", "getAllAttributesBonusFromQuestRewards", 0);
        m_findObjects = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2);
        m_mapOpen = game::FindMethod("LE.dll", "", "UIBase", "IsWorldMapPanelOpen", 0);
        m_mapToggle = game::FindMethod("LE.dll", "", "UIBase", "MapKeyDown", 0);
        m_mapClose = game::FindMethod("LE.dll", "", "UIBase", "closeMap", 0);
        m_complete = game::FindMethod("LE.dll", "", "Quest", "completeQuest", 2);
        m_saveQuests = game::FindMethod("LE.dll", "LE.Data", "CharacterData", "SaveQuests", 1);
        m_dirty = game::FindMethod("LE.dll", "", "CharacterDataTracker", "MarkDirty", 1);
        m_addWaypoint = game::FindMethod("LE.dll", "LE.Data", "CharacterData", "AddUnlockedWaypointScene", 1);
        m_serialize = game::FindMethod("LE.dll", "", "CharacterDataSerializer", "Serialize", 1);
        m_json = game::FindMethod("Newtonsoft.Json.dll", "Newtonsoft.Json", "JsonConvert", "SerializeObject", 1);
        m_saveData = game::FindMethod("LE.dll", "LE.Data", "CharacterData", "SaveData", 0);
        m_currentLevel = game::FindMethod("LE.dll", "", "ExperienceTracker", "get_CurrentLevel", 0);
        m_cumulativeXp = game::FindMethod("LE.dll", "", "ExperienceTracker", "GetCumulativeXP", 0);
        m_adjustXp = game::FindMethod("LE.dll", "", "Quest", "GetExperienceRewardAdjustedForLevel", 1);
        m_checkWaypoint = game::FindMethod("LE.dll", "", "UIWaypoint", "CheckWaypoint", 2);
        m_monolith = game::FindMethod("LE.dll", "", "PlayerFinder", "getMonolithProgressManager", 0);
        m_sceneDetails = game::FindMethod("LE.dll", "", "SceneList", "FindSceneDetails", 1);
        m_gainDirect = game::FindMethod("LE.dll", "", "ExperienceTracker", "GainExpDirect", 2);
        m_levelXp = game::FindMethod("LE.dll", "", "PlayerUtility", "NextLevelExpFromLevel", 1);
        m_currentScene = game::FindMethod("LE.dll", "", "SceneList", "GetCurrentSceneDetails", 0);
        m_refreshDisplay = game::FindMethod("LE.dll", "", "StatefulQuestList", "refreshDisplay", 0);
        m_recordAnalytics = game::FindMethod("LE.dll", "LE.Telemetry", "ClientSessionAnalytics", "RecordEvent", 4);
        c_waypoint = game::FindClass("LE.dll", "", "UIWaypointStandard");
        f_ui = game::FindStaticField("LE.dll", "", "UIBase", "instance");
        questTypes = Enum("QuestType"); questStates = Enum("QuestState"); chapters = Enum("ZoneChapterManager.Chapter");
        if (!m_actor || !m_offline || !m_questList || !m_holder || !m_stateList || !m_state || !m_stateful ||
            !m_passives || !m_idols || !m_attributes || !m_findObjects || !m_mapOpen || !m_mapToggle || !m_mapClose || !c_waypoint || !f_ui ||
            !m_complete || !m_saveQuests || !m_dirty || !m_addWaypoint || !m_serialize || !m_json || !m_saveData || !m_currentLevel || !m_cumulativeXp || !m_adjustXp || !m_checkWaypoint || !m_monolith || !m_sceneDetails || !m_gainDirect || !m_levelXp || !m_currentScene)
            throw std::runtime_error("required progression methods unavailable");
        (void)questTypes.at("Normal"); (void)questStates.at("Completed"); (void)questStates.at("Active");
        (void)chapters.at("Minilith");
    }, &why);
    Log("progression: %s%s", ready ? "read metadata ready" : "unavailable: ", ready ? "" : why.c_str());
    return ready;
}
std::string Read() {
    return Run([] {
        const auto player = Current();
        const auto quests = Quests(player);
        const auto waypoints = Waypoints(player);
        bool refresh = false; void* args[]{&refresh};
        std::ostringstream out;
        void* scene = Invoke(m_currentScene);
        out << "{\"ok\":true,\"player\":{\"name\":" << Json(player.name) << ",\"id\":" << Json(player.id)
            << ",\"scene\":" << Json(scene ? Text(Get<void*>(scene, Offset("SceneDetails", "Name"))) : "")
            << ",\"level\":" << Get<int>(player.data, Offset("CharacterData", "<Level>k__BackingField", "LE.Data"))
            << ",\"xp\":" << Get<int64_t>(player.data, Offset("CharacterData", "<CurrentExp>k__BackingField", "LE.Data"))
            << "},\"rewards\":" << RewardJson(Rewards(player)) << ",\"passivePoints\":" << Value<int>(m_passives, player.quests)
            << ",\"idolUnlock\":" << static_cast<int>(Value<uint8_t>(m_idols, player.quests, args))
            << ",\"allAttributes\":" << Value<int>(m_attributes, player.quests) << ",\"quests\":[";
        bool comma = false;
        for (const auto& q : quests) {
            if (comma) out << ','; comma = true;
            out << "{\"id\":" << q.id << ",\"name\":" << Json(q.name) << ",\"chapter\":" << q.chapter
                << ",\"type\":" << static_cast<int>(q.type) << ",\"state\":" << q.state << ",\"main\":" << Boolean(q.main)
                << ",\"visible\":" << Boolean(q.visible) << ",\"test\":" << Boolean(q.test) << ",\"eligible\":" << Boolean(q.eligible)
                << ",\"routeError\":" << Json(q.reason) << ",\"xp\":" << q.xp << ",\"gold\":" << q.gold
                << ",\"passive\":" << q.passive << ",\"idol\":" << q.idol << ",\"attributes\":" << q.attributes << '}';
        }
        out << "],\"waypoints\":["; comma = false;
        for (const auto& w : waypoints) {
            if (comma) out << ','; comma = true;
            out << "{\"scene\":" << Json(w.scene) << ",\"unlocked\":" << Boolean(w.unlocked)
                << ",\"monolith\":" << Boolean(w.monolith) << ",\"always\":" << Boolean(w.always)
                << ",\"active\":" << Boolean(w.active)
                << ",\"noWaypoint\":" << Boolean(w.noWaypoint) << '}';
        }
        out << "]}";
        return out.str();
    });
}
std::string Identity() {
    return Run([] {
        const auto player = Current();
        void* scene = Invoke(m_currentScene);
        return "{\"ok\":true,\"offline\":true,\"player\":{\"id\":" + Json(player.id) +
            ",\"name\":" + Json(player.name) + ",\"scene\":" + Json(scene ? Text(Get<void*>(scene, Offset("SceneDetails", "Name"))) : "") + "}}";
    });
}

namespace {
std::string Perform(const std::string& expectedId, bool campaign) {
    Player player;
    std::unique_ptr<Root> actorRoot, dataRoot, questRoot;
    std::vector<QuestInfo> quests;
    std::vector<WaypointInfo> points;
    SnapshotData snapshot;
    RewardState before, after;
    std::string why, error, backup;
    int prepare = 0, pending = 0, pointPending = 0;
    double prepareMax[4]{}, phaseMax[6]{};
    const auto check = [&] {
        const auto current = Current(); CheckId(current, expectedId);
        if (current.actor != player.actor || current.data != player.data)
            throw std::runtime_error("loaded actor changed; operation stopped");
    };
    const bool prepared = mainthread::RunSteps([&] {
        StepTiming timing(prepareMax[prepare]);
        try {
            if (prepare == 0) {
                player = Current(); CheckId(player, expectedId);
                actorRoot = std::make_unique<Root>(player.actor); dataRoot = std::make_unique<Root>(player.data);
                questRoot = std::make_unique<Root>(player.quests);
                before = Rewards(player);
            } else {
                check();
                if (prepare == 1 && campaign) {
                    quests = Quests(player);
                    for (const auto& q : quests) if (q.eligible && q.state != questStates.at("Completed")) {
                        if (!q.reason.empty()) throw std::runtime_error("quest " + std::to_string(q.id) + ": " + q.reason);
                        ++pending;
                    }
                } else if (prepare == 2) {
                    points = Waypoints(player);
                    for (const auto& p : points) if (!p.noWaypoint && !p.unlocked &&
                        (!campaign || p.monolith || p.scene == "EoT" || p.scene == "M_Rest")) ++pointPending;
                } else if (prepare == 3) {
                    if (pending || pointPending || (campaign && before.level < 55))
                        snapshot = Capture(player, campaign ? "questscomplete" : "waypointsunlock");
                    return true;
                }
            }
            ++prepare; return false;
        } catch (const std::exception& e) { error = e.what(); return true; }
    }, 5000, &why);
    if (!prepared || !error.empty()) return "{\"ok\":false,\"error\":" + Json(error.empty() ? why : error) + "}";
    if (!snapshot.id.empty()) {
        try { backup = WriteSnapshot(snapshot); }
        catch (const std::exception& e) { return "{\"ok\":false,\"error\":" + Json(e.what()) + "}"; }
    }
    if (pending && (!m_refreshDisplay || !hook::Install(m_refreshDisplay.code,
            reinterpret_cast<void*>(&DisplayDetour), reinterpret_cast<void**>(&originalDisplay), &why)))
        return "{\"ok\":false,\"error\":" + Json("quest display batching unavailable: " + why) + "}";
    const bool batch = pending || (campaign && before.level < 55);
    if (batch && (!m_recordAnalytics || !hook::Install(m_recordAnalytics.code,
            reinterpret_cast<void*>(&AnalyticsDetour), reinterpret_cast<void**>(&originalAnalytics), &why))) {
        if (pending) hook::Remove(m_refreshDisplay.code, nullptr);
        return "{\"ok\":false,\"error\":" + Json("offline progression analytics scope unavailable: " + why) + "}";
    }
    unsigned displayCalls = 0, analyticsCalls = 0;
    size_t questIndex = 0, pointIndex = 0;
    int completed = 0, added = 0, phase = 0, remaining = pending;
    int64_t expectedXp = 0, topUpXp = 0;
    unsigned steps = 0;
    const auto started = GetTickCount64();
    const bool ran = mainthread::RunSteps([&] {
        StepTiming timing(phaseMax[phase]);
        try {
            check(); ++steps;
            if (phase == 0) {
                while (questIndex < quests.size()) {
                    const auto& q = quests[questIndex++];
                    if (!q.eligible || State(player, q.object) == questStates.at("Completed")) continue;
                    int level = Rewards(player).level; void* xpArgs[]{&level};
                    const int xp = Value<int>(m_adjustXp, q.object, xpArgs);
                    bool silent = true; void* args[]{player.actor, &silent};
                    { DisplayScope scope(player.quests); Invoke(m_complete, q.object, args); }
                    if (State(player, q.object) != questStates.at("Completed"))
                        throw std::runtime_error("quest " + std::to_string(q.id) + " did not reach Completed");
                    expectedXp += xp; ++completed; --remaining;
                    return false; // Exactly one rewarded quest per frame. No disk logging here.
                }
                phase = 1; return false;
            }
            if (phase == 1) {
                const auto rewards = Rewards(player);
                if (campaign && rewards.level < 55) {
                    int64_t target = 0;
                    for (int level = 1; level <= rewards.level; ++level) {
                        void* args[]{&level}; const auto xp = Value<int64_t>(m_levelXp, nullptr, args);
                        if (xp <= 0 || xp > INT64_MAX - target) throw std::runtime_error("invalid experience table");
                        target += xp;
                    }
                    int64_t amount = target - rewards.xp;
                    if (amount <= 0) throw std::runtime_error("experience and level disagree");
                    void* experience = Get<void*>(player.quests, Offset("StatefulQuestList", "experienceTracker"));
                    bool noFavour = true; void* args[]{&amount, &noFavour};
                    { DisplayScope scope(player.quests); Invoke(m_gainDirect, experience, args); }
                    if (Rewards(player).level != rewards.level + 1) throw std::runtime_error("level top-up verification failed");
                    topUpXp += amount; return false; // LevelUp events are also spread across frames.
                }
                phase = 2; return false;
            }
            if (phase == 2) {
                while (pointIndex < points.size()) {
                    const auto& p = points[pointIndex++];
                    if (p.noWaypoint || p.unlocked || (campaign && !p.monolith && p.scene != "EoT" && p.scene != "M_Rest")) continue;
                    // Strings are recreated from stable copied scene names between frames.
                    Root scene(il2cpp::api().string_new(p.scene.c_str()));
                    void* args[]{scene.Get()}; Invoke(m_addWaypoint, player.data, args); ++added; return false;
                }
                phase = 3; return false;
            }
            if (phase == 3) {
                if (!backup.empty()) Save(player);
                phase = 4; return false;
            }
            if (phase == 4) {
                // The normal map opens will refresh their buttons themselves.
                // No map popup or extra prefab rebuild is needed for this save.
                phase = 5; return false;
            }
            if (pending) {
                displayCalls = deferredDisplays; deferredDisplays = 0;
                Invoke(m_refreshDisplay, player.quests);
            }
            analyticsCalls = skippedAnalytics; skippedAnalytics = 0;
            after = Rewards(player); return true;
        } catch (const std::exception& e) { error = e.what(); return true; }
    }, 5000, &why);
    if (!ran && error.empty()) error = why;
    if (batch) {
        // Clear scope even after a guarded native fault; do not repeat a reward.
        std::string cleanupWhy;
        mainthread::Run([&] {
            batchedQuestList = nullptr;
            syntheticProgression = false;
            analyticsCalls += skippedAnalytics; skippedAnalytics = 0;
            if (!error.empty()) {
                try {
                    check();
                    if (deferredDisplays) { deferredDisplays = 0; Invoke(m_refreshDisplay, player.quests); }
                    after = Rewards(player);
                    // Keep already awarded native progress after a partial error.
                    if (completed || topUpXp || added) Save(player);
                } catch (const std::exception& e) { error += "; finalization: "; error += e.what(); }
            }
        }, 5000, &cleanupWhy);
        if (pending && !hook::Remove(m_refreshDisplay.code, &cleanupWhy)) {
            if (error.empty()) error = "quest display hook removal failed: " + cleanupWhy;
        }
        if (!hook::Remove(m_recordAnalytics.code, &cleanupWhy) && error.empty()) error = "analytics scope removal failed: " + cleanupWhy;
    }
    Log("progression: %s completed=%d added=%d steps=%u elapsed=%llu ms error=%s",
        campaign ? "questscomplete" : "waypointsunlock", completed, added, steps, GetTickCount64() - started, error.c_str());
    return "{\"ok\":" + std::string(Boolean(error.empty() && (!campaign || !remaining))) +
        ",\"completed\":" + std::to_string(completed) + ",\"remaining\":" + std::to_string(remaining) +
        ",\"added\":" + std::to_string(added) + ",\"endgameWaypointsAdded\":" + std::to_string(campaign ? added : 0) +
        ",\"expectedQuestXp\":" + std::to_string(expectedXp) + ",\"levelTopUpXp\":" + std::to_string(topUpXp) +
        ",\"before\":" + RewardJson(before) + ",\"after\":" + RewardJson(after) +
        ",\"backup\":" + Json(backup) + ",\"steps\":" + std::to_string(steps) +
        ",\"deferredDisplays\":" + std::to_string(displayCalls) + ",\"skippedSyntheticAnalytics\":" + std::to_string(analyticsCalls) + ",\"timingMaxMs\":{\"identity\":" + std::to_string(prepareMax[0]) +
        ",\"catalog\":" + std::to_string(prepareMax[1]) + ",\"waypointCatalog\":" + std::to_string(prepareMax[2]) +
        ",\"snapshot\":" + std::to_string(prepareMax[3]) + ",\"quest\":" + std::to_string(phaseMax[0]) +
        ",\"level\":" + std::to_string(phaseMax[1]) + ",\"waypoint\":" + std::to_string(phaseMax[2]) +
        ",\"save\":" + std::to_string(phaseMax[3]) + ",\"display\":" + std::to_string(phaseMax[5]) + "},\"error\":" + Json(error) + "}";
}
}
std::string Complete(const std::string& expectedId) { return Perform(expectedId, true); }
std::string UnlockWaypoints(const std::string& expectedId) { return Perform(expectedId, false); }
SnapshotData CaptureSnapshotCurrent(const std::string& expectedId, const char* operation) {
    const auto player=Current(); CheckId(player,expectedId); return Capture(player,operation);
}
std::string WriteCapturedSnapshot(const SnapshotData& snapshot) { return WriteSnapshot(snapshot); }
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string SnapshotFixtureCurrent(const std::string& expectedId, const char* operation) {
    const auto player = Current(); CheckId(player, expectedId);
    if (player.name != "EpCraftTest") throw std::runtime_error("fixture snapshot requires isolated EpCraftTest");
    return WriteSnapshot(Capture(player, operation));
}
#endif

void SaveCurrent(const std::string& expectedId) {
    const auto player = Current(); CheckId(player, expectedId); Save(player);
}
}  // namespace ep::progression
