#include "monolith.hpp"
#include "monolith_rules.hpp"
#include "managed.hpp"
#include "progression.hpp"
#include "hook.hpp"
#include <atomic>
#include <set>
#include <sstream>

namespace ep::monolith {
namespace {
using namespace managed;
using game::MethodRef;
MethodRef m_actor, m_offline, m_progress, m_catalog, m_timeline, m_difficulty;
MethodRef m_unlocked, m_unlock, m_tryRun, m_newRun, m_saveRuns, m_open;
MethodRef m_add, m_max, m_corruption, m_updateMod, m_updateHighest, m_updateShared;
MethodRef m_scene;
MethodRef m_requestInfo;
bool ready = false;
size_t runStability = 0;
using AddFn = void (*)(void*, int, const il2cpp::Method*);
AddFn original = nullptr;
std::atomic<double> multiplier{1};
std::atomic<uint64_t> calls{0}, boosted{0}, refused{0}, faults{0};
std::atomic<int> lastIn{0}, lastOut{0};
thread_local bool manual = false;

struct Context {
    void* actor = nullptr;
    void* data = nullptr;
    void* runs = nullptr;
    void* progress = nullptr;
    std::string id, name, scene;
};
Context Current(const std::string& expected = {}) {
    if (!ready) throw std::runtime_error("monolith metadata unavailable");
    if (!game::IsOfflinePlay()) throw std::runtime_error(game::GateText());
    RequireSession(4); // InGame; reject the character preview actor in Login.
    Context c;
    c.actor = Invoke(m_actor);
    if (!c.actor || !game::IsAlive(c.actor)) throw std::runtime_error("enter a zone with an offline character first");
    void* tracker = Get<void*>(c.actor, Offset("Actor", "characterDataTracker"));
    c.data = Get<void*>(tracker, Offset("CharacterDataTracker", "charData"));
    if (!Value<bool>(m_offline, c.data)) throw std::runtime_error("loaded character is not offline");
    c.id = Text(Invoke(Method(c.data, "get_Id", 0), c.data));
    c.name = Text(Get<void*>(c.data, Offset("CharacterData", "<CharacterName>k__BackingField", "LE.Data")));
    if (!expected.empty() && (expected.find_first_not_of("0123456789") != std::string::npos || c.id != expected))
        throw std::runtime_error("loaded offline save id changed; refresh the panel");
    c.runs = Get<void*>(c.actor, Offset("Actor", "monolithRunsManager"));
    c.progress = Invoke(m_progress);
    if (!c.runs || !game::IsAlive(c.runs) || !c.progress || !game::IsAlive(c.progress) ||
        Get<void*>(c.runs, Offset("MonolithRunsManager", "actor")) != c.actor ||
        Get<void*>(c.progress, Offset("MonolithProgressManager", "actor")) != c.actor)
        throw std::runtime_error("monolith managers do not belong to the loaded character");
    void* scene = Invoke(m_scene);
    if (scene) c.scene = Text(Get<void*>(scene, Offset("SceneDetails", "Name")));
    return c;
}
void Editable(const Context& c) {
    // Never change an active echo: its actors and reward state were created with
    // the old corruption. The game applies new values on the next echo.
    if (c.scene != "EoT" && c.scene != "MonolithHub" && c.scene != "M_Rest")
        throw std::runtime_error("use this action in End of Time, Monolith Hub or Traveler's Rest");
}
bool RestContextReady() {
    // A live Rest object can belong to the outgoing scene while its replacement
    // is still loading. Check the game's scene service as well as the object.
    void* service = Invoke(game::FindMethod("LE.dll", "LE.Services", "ServiceProvider", "get_SceneService", 0));
    if (!service || Value<bool>(Method(service, "IsLoading", 0), service) ||
        Text(Invoke(Method(service, "get_CurrentScene", 0), service)) != "M_Rest") return false;
    // SceneService can report the incoming scene before the player's full
    // transition and LoadingScreen cleanup have finished. Opening a panel
    // during that window lets the normal cleanup close it again.
    if (Value<bool>(game::FindMethod("LE.dll", "LE.UI", "LoadingScreen", "get_IsVisible", 0))) return false;
    void* transition = Invoke(game::FindMethod("LE.dll", "LE.Services", "ServiceProvider", "get_ClientTransitionService", 0));
    if (!transition || Value<bool>(Method(transition, "get_IsAnyPlayerTransitioning", 0), transition) ||
        Invoke(Method(transition, "get_ActiveTransition", 0), transition)) return false;
    void* rest = game::StaticObject(game::FindStaticField("LE.dll", "", "MonolithRestZoneManager", "instance"));
    return rest && game::IsAlive(rest);
}
void RestContextForPanel() {
    // The normal Echo start/completion flow keeps Traveler's Rest loaded.
    // Opening the panel directly in EoT without it produces an Echo that
    // cannot create its completion rewards or place the returning player.
    if (!RestContextReady())
        throw std::runtime_error("enter Traveler's Rest before opening the timeline; its return and reward context is not loaded");
}
struct Timeline { void* object; uint8_t id; std::string name; std::vector<void*> difficulties; };
std::vector<Timeline> Catalog() {
    Root list(Invoke(m_catalog));
    std::vector<Timeline> result;
    std::set<uint8_t> ids;
    for (void* t : Entries(Get<void*>(list.Get(), Offset("TimelineList", "realTimelines")))) {
        if (!t) continue;
        const auto id = Get<uint8_t>(t, Offset("MonolithTimeline", "timelineID"));
        if (!id || id == 99) continue;
        if (!ids.insert(id).second) throw std::runtime_error("duplicate timeline id");
        auto ds = Entries(Get<void*>(t, Offset("MonolithTimeline", "difficulties")));
        if (ds.empty() || ds.size() > 255) throw std::runtime_error("invalid timeline difficulties");
        for (void* d : ds) if (!d) throw std::runtime_error("missing timeline difficulty");
        result.push_back({t, id, Text(Get<void*>(t, Offset("MonolithTimeline", "displayName"))), std::move(ds)});
    }
    if (result.empty()) throw std::runtime_error("timeline catalog not loaded yet");
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return result;
}
Timeline Find(int id, int difficulty) {
    for (const auto& t : Catalog()) if (t.id == id) {
        if (difficulty < 0 || difficulty > 1 || difficulty >= static_cast<int>(t.difficulties.size()))
            throw std::runtime_error("invalid difficulty; use normal or empowered");
        return t;
    }
    throw std::runtime_error("unknown timeline id; use monolithread");
}
bool IsUnlocked(const Context& c, uint8_t id, int difficulty) {
    auto d = static_cast<uint8_t>(difficulty); void* args[]{&id, &d};
    return Value<bool>(m_unlocked, c.progress, args);
}
void CoalesceUnlockNotification(uint8_t id) {
    void* notifications = game::StaticObject(game::FindStaticField("LE.dll", "", "Notifications", "instance"));
    if (!notifications || !game::IsAlive(notifications)) return;
    void* queue = Get<void*>(notifications, Offset("Notifications", "timelineIDs"));
    if (!queue) return;
    void* args[]{&id};
    // The game queues notifications in Dictionary<TimelineID,byte>. Its Add
    // throws when normal and Empowered unlock for one timeline in the same
    // frame. Keep the final (highest difficulty) notification for that ID.
    if (Value<bool>(Method(queue, "ContainsKey", 1), queue, args))
        Invoke(Method(queue, "Remove", 1), queue, args);
}
void RefreshSelected(const Context& c, uint8_t id, int difficulty) {
    if (Get<uint8_t>(c.runs, Offset("MonolithRunsManager", "mostRecentlyRequestedTimelineID")) != id ||
        Get<int>(c.runs, Offset("MonolithRunsManager", "mostRecentlyRequestedDifficultyIndex")) != difficulty) return;
    bool specific = true; void* args[]{&id, &specific, &difficulty}; Invoke(m_requestInfo, c.runs, args);
}
void* GetRun(const Context& c, uint8_t id, int difficulty, bool create = false) {
    void* run = nullptr; void* args[]{&id, &difficulty, &run};
    if (Value<bool>(m_tryRun, c.runs, args)) {
        if (!run) throw std::runtime_error("run lookup returned an empty run");
        return run;
    }
    if (!create) return nullptr;
    void* newArgs[]{&id, &difficulty};
    // getNewRun resets an existing run, so call it only after a failed lookup.
    return Invoke(m_newRun, c.runs, newArgs);
}
void Save(const Context& c) { Invoke(m_saveRuns, c.runs); progression::SaveCurrent(c.id); }
std::string Snapshot(const Context& c, const char* action) {
    try { Invoke(m_saveRuns, c.runs); }
    catch (const std::exception& e) { throw std::runtime_error(std::string("pre-snapshot saveRuns: ") + e.what()); }
    try { return progression::SnapshotCurrent(c.id, action); }
    catch (const std::exception& e) { throw std::runtime_error(std::string("snapshot: ") + e.what()); }
}
std::string RunJson(void* run) {
    if (!run) return "null";
    void* web = Get<void*>(run, Offset("MonolithRun", "web"));
    return "{\"timeline\":" + std::to_string(Get<uint8_t>(run, Offset("MonolithRun", "timelineID"))) +
        ",\"difficulty\":" + std::to_string(Get<int>(run, Offset("MonolithRun", "difficultyIndex"))) +
        ",\"corruption\":" + std::to_string(web ? Get<int>(web, Offset("EchoWeb", "corruption")) : 0) +
        ",\"stability\":" + std::to_string(Get<int>(run, runStability)) +
        ",\"maxStability\":" + std::to_string(Value<int>(m_max, run)) + "}";
}
std::string PlayerJson(const Context& c) {
    return "{\"id\":" + Json(c.id) + ",\"name\":" + Json(c.name) + ",\"scene\":" + Json(c.scene) + "}";
}
void Detour(void* self, int amount, const il2cpp::Method* method) {
    ++calls;
    int scaled = amount;
    const double m = multiplier.load();
    if (!manual && amount > 0 && m != 1) {
        bool valid = false;
        std::string why;
        const bool checked = game::Guarded([&] {
            const auto c = Current();
            for (void* run : Entries(Get<void*>(c.runs, Offset("MonolithRunsManager", "runs")))) {
                if (run != self) continue;
                scaled = rules::ScaleGain(amount, m, Get<int>(run, runStability));
                valid = true; break;
            }
        }, &why);
        if (!checked) ++faults;
        if (valid) ++boosted; else ++refused;
    }
    lastIn = amount; lastOut = scaled;
    original(self, scaled, method); // Includes the game's own maximum stability cap.
}
struct ManualGain {
    bool before = manual;
    ManualGain() { manual = true; }
    ~ManualGain() { manual = before; }
};
template<class Work> std::string Mutation(const Context& c, const char* action, Work work) {
    const auto backup = Snapshot(c, action);
    try {
        const std::string details = work(); Save(c);
        return "{\"ok\":true,\"backup\":" + Json(backup) + "," + details + "}";
    } catch (const std::exception& e) {
        return "{\"ok\":false,\"backup\":" + Json(backup) + ",\"error\":" + Json(e.what()) + "}";
    }
}
}

bool Init() {
    std::string why;
    ready = game::Guarded([] {
        auto resolve = [](const char* cls, const char* name, int count, const char* ns = "") {
            auto m = game::FindMethod("LE.dll", ns, cls, name, count);
            if (!m) throw std::runtime_error(std::string("missing method: ") + cls + "." + name);
            return m;
        };
        m_actor = resolve("PlayerFinder", "getPlayerActor", 0);
        m_progress = resolve("PlayerFinder", "getMonolithProgressManager", 0);
        m_offline = resolve("CharacterData", "get_IsOffline", 0, "LE.Data");
        m_catalog = resolve("TimelineList", "get", 0);
        m_timeline = resolve("TimelineList", "getTimeline", 1);
        m_difficulty = resolve("TimelineList", "getTimelineDifficulty", 2);
        m_unlocked = resolve("MonolithProgressManager", "IsTimelineUnlockedForDifficulty", 2);
        m_unlock = resolve("MonolithProgressManager", "unlockTimeline", 3);
        m_corruption = resolve("MonolithProgressManager", "UpdateCorruption", 2);
        m_tryRun = resolve("MonolithRunsManager", "TryGetRun", 3);
        m_newRun = resolve("MonolithRunsManager", "getNewRun", 2);
        m_saveRuns = resolve("MonolithRunsManager", "saveRuns", 0);
        m_requestInfo = resolve("MonolithRunsManager", "requestRunInfo", 3);
        m_updateHighest = resolve("MonolithRunsManager", "updateHighestCorruptionOfAnyRun", 0);
        m_updateShared = resolve("MonolithRunsManager", "updateSharedRunData", 0);
        m_open = resolve("MonolithPanelManager", "open", 5);
        m_add = resolve("MonolithRun", "AddStability", 1);
        m_max = resolve("MonolithRun", "GetMaxStability", 0);
        m_updateMod = resolve("MonolithRun", "updateCorruptionMod", 0);
        m_scene = resolve("SceneList", "GetCurrentSceneDetails", 0);
        runStability = Offset("MonolithRun", "stability");
    }, &why);
    Log("monolith: %s%s", ready ? "metadata ready" : "unavailable: ", ready ? "" : why.c_str());
    return ready;
}
std::string Read() {
    return Run([] {
        const auto c = Current();
        std::ostringstream out;
        out << "{\"ok\":true,\"player\":" << PlayerJson(c) << ",\"editable\":"
            << Boolean(c.scene == "EoT" || c.scene == "MonolithHub" || c.scene == "M_Rest")
            << ",\"restContextReady\":" << Boolean(RestContextReady())
            << ",\"stabilityMultiplier\":" << multiplier.load() << ",\"current\":"
            << RunJson(Get<void*>(c.runs, Offset("MonolithRunsManager", "currentRun"))) << ",\"selected\":{\"timeline\":"
            << static_cast<int>(Get<uint8_t>(c.runs, Offset("MonolithRunsManager", "mostRecentlyRequestedTimelineID")))
            << ",\"difficulty\":" << Get<int>(c.runs, Offset("MonolithRunsManager", "mostRecentlyRequestedDifficultyIndex"))
            << "},\"timelines\":[";
        bool first = true;
        for (const auto& t : Catalog()) {
            if (!first) out << ','; first = false;
            out << "{\"id\":" << static_cast<int>(t.id) << ",\"name\":" << Json(t.name) << ",\"difficulties\":[";
            for (int i = 0; i < static_cast<int>(t.difficulties.size()); ++i) {
                if (i) out << ',';
                void* d = t.difficulties[i];
                const bool capped = Get<bool>(d, Offset("MonolithTimeline.Difficulty", "hasMaxCorruption"));
                out << "{\"index\":" << i << ",\"unlocked\":" << Boolean(IsUnlocked(c, t.id, i))
                    << ",\"level\":" << Get<int>(d, Offset("MonolithTimeline.Difficulty", "level"))
                    << ",\"minCorruption\":" << Get<int>(d, Offset("MonolithTimeline.Difficulty", "minimumCorruption"))
                    << ",\"maxCorruption\":" << (capped ? Get<int>(d, Offset("MonolithTimeline.Difficulty", "maximumCorruption")) : rules::kMaxStoredCorruption)
                    << ",\"maxStability\":" << Get<int>(d, Offset("MonolithTimeline.Difficulty", "maxStability"))
                    << ",\"run\":" << RunJson(GetRun(c, t.id, i)) << '}';
            }
            out << "]}";
        }
        out << "]}"; return out.str();
    });
}
namespace {
std::vector<void*> Islands(void* web) {
    void* indexed = Get<void*>(web, Offset("EchoWeb", "islands"));
    if (!indexed) throw std::runtime_error("echo collection unavailable");
    const auto* field = il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(indexed), "entries");
    if (!field) throw std::runtime_error("echo collection layout unavailable");
    return Entries(Get<void*>(indexed, il2cpp::api().field_get_offset(field)));
}
void* Web(const Context& c, uint8_t tid, int difficulty) {
    void* run = GetRun(c, tid, difficulty); // Never generate a web from a read request.
    if (!run) return nullptr;
    void* web = Get<void*>(run, Offset("MonolithRun", "web"));
    if (web && (Get<uint8_t>(web, Offset("EchoWeb", "timelineID")) != tid ||
        Get<int>(web, Offset("EchoWeb", "runDifficulty")) != difficulty))
        throw std::runtime_error("echo web identity mismatch");
    return web;
}
std::vector<int> Ints(void* list) {
    if (!list) return {};
    const auto* cls = il2cpp::api().object_get_class(list);
    const auto* sf = il2cpp::api().class_get_field_from_name(cls, "_size");
    const auto* af = il2cpp::api().class_get_field_from_name(cls, "_items");
    if (!sf || !af) throw std::runtime_error("integer list layout unavailable");
    int n = Get<int>(list, il2cpp::api().field_get_offset(sf));
    void* a = Get<void*>(list, il2cpp::api().field_get_offset(af));
    if (n < 0 || n > 8192 || !a || static_cast<uintptr_t>(n) > Get<uintptr_t>(a, 0x18))
        throw std::runtime_error("invalid echo connections");
    std::vector<int> result;
    for (int i = 0; i < n; ++i) result.push_back(Get<int>(a, 0x20 + sizeof(int) * i));
    return result;
}
}
std::string Echoes(const std::string& id, int timeline, int difficulty) {
    return Run([=]() mutable {
        if (id.empty()) throw std::runtime_error("offline save id required");
        const auto c = Current(id); const auto t = Find(timeline, difficulty);
        void* web = Web(c, t.id, difficulty);
        std::ostringstream out;
        out << "{\"ok\":true,\"player\":" << PlayerJson(c) << ",\"timeline\":" << timeline
            << ",\"difficulty\":" << difficulty << ",\"generated\":" << Boolean(web != nullptr) << ",\"echoes\":[";
        if (web) {
            Root rewards(Invoke(game::FindMethod("LE.dll", "", "DroppableRewardList", "get", 0)));
            std::set<int> seen; bool first = true;
            for (void* island : Islands(web)) {
                if (!island) continue;
                int index = Get<int>(island, Offset("EchoWebIsland", "hexIndex"));
                if (!seen.insert(index).second) throw std::runtime_error("duplicate echo index");
                struct Point { float x, y, z; };
                void* posArgs[]{&index};
                const auto p = Value<Point>(game::FindMethod("LE.dll", "", "EchoWebIslandUI", "getHexPosition", 1), nullptr, posArgs);
                int rewardIndex = Get<int>(island, Offset("EchoWebIsland", "droppableRewardIndex"));
                std::string title; int rewardType = 0;
                const bool hasReward = Get<bool>(island, Offset("EchoWebIsland", "hasDroppableReward"));
                if (hasReward) {
                    void* args[]{&rewardIndex};
                    void* reward = Invoke(Method(rewards.Get(), "getMonolithReward", 1), rewards.Get(), args);
                    if (!reward) throw std::runtime_error("echo reward unavailable");
                    title = Text(Get<void*>(reward, Offset("DroppableReward", "title")));
                    rewardType = Get<int>(reward, Offset("DroppableReward", "rewardType"));
                }
                auto tid = t.id; void* nameArgs[]{&tid, &difficulty}; void* canArgs[]{island};
                if (!first) out << ','; first = false;
                out << "{\"index\":" << index << ",\"x\":" << p.x << ",\"y\":" << p.y
                    << ",\"name\":" << Json(Text(Invoke(Method(island, "GetEchoDisplayName", 2), island, nameArgs)))
                    << ",\"reward\":" << Json(title) << ",\"rewardType\":" << rewardType
                    << ",\"kind\":" << Get<int>(island, Offset("EchoWebIsland", "islandType"))
                    << ",\"completed\":" << Boolean(Get<bool>(island, Offset("EchoWebIsland", "completed")))
                    << ",\"runnable\":" << Boolean(Value<bool>(Method(web, "islandCanBeRun", 1), web, canArgs))
                    << ",\"stability\":" << Get<int>(island, Offset("EchoWebIsland", "stabilityReward"))
                    << ",\"connections\":[";
                bool fc = true;
                for (int edge : Ints(Get<void*>(island, Offset("EchoWebIsland", "connectedHexes")))) {
                    if (!fc) out << ','; fc = false; out << edge;
                }
                out << "]}";
            }
        }
        out << "]}"; return out.str();
    });
}
std::string FocusEcho(const std::string& id, int timeline, int difficulty, int index) {
    return Run([=]() mutable {
        if (id.empty()) throw std::runtime_error("offline save id required");
        const auto c = Current(id); Editable(c); RestContextForPanel(); const auto t = Find(timeline, difficulty);
        if (!IsUnlocked(c, t.id, difficulty)) throw std::runtime_error("timeline difficulty is locked");
        void* web = Web(c, t.id, difficulty);
        if (!web) throw std::runtime_error("open this timeline normally first; no echo web exists");
        void* wanted = nullptr;
        for (void* island : Islands(web)) if (island && Get<int>(island, Offset("EchoWebIsland", "hexIndex")) == index) wanted = island;
        if (!wanted) throw std::runtime_error("echo no longer exists; refresh the navigator");
        // The backend opens and waits for the panel before focusing it. Opening
        // it again here can overlap the game's asynchronous Rest scene load.
        const auto* cls = game::FindClass("LE.dll", "", "MonolithTimelinePanelManager");
        if (!cls) throw std::runtime_error("timeline panel type unavailable");
        void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));
        bool inactive = false; void* findArgs[]{type, &inactive};
        Root objects(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, findArgs));
        auto n = Get<uintptr_t>(objects.Get(), 0x18);
        if (n > 32) throw std::runtime_error("invalid timeline panel count");
        void* panel = nullptr;
        for (uintptr_t i = 0; i < n; ++i) {
            void* obj = Get<void*>(objects.Get(), 0x20 + sizeof(void*) * i);
            if (obj && game::IsAlive(obj) && Get<void*>(obj, Offset("MonolithTimelinePanelManager", "web")) == web) {
                if (panel) throw std::runtime_error("multiple matching timeline panels");
                panel = obj;
            }
        }
        if (!panel) throw std::runtime_error("timeline panel is still opening; click Show in game again when it is ready");
        void* ui = Get<void*>(panel, Offset("MonolithTimelinePanelManager", "_echoWebUI"));
        void* islandUI = nullptr; void* lookupArgs[]{&index, &islandUI};
        if (!Value<bool>(Method(ui, "TryGetIslandUIAtIndex", 2), ui, lookupArgs) || !islandUI)
            throw std::runtime_error("this echo is not visible in the game's web yet");
        bool adjacent = false; float zoom = 1; void* focusArgs[]{web, &index, &adjacent, nullptr, &zoom};
        Invoke(Method(ui, "FocusViewOnIsland", 5), ui, focusArgs);
        if (Get<void*>(panel, Offset("MonolithTimelinePanelManager", "_selectedEcho")) != islandUI) {
            void* selectArgs[]{islandUI}; Invoke(Method(panel, "SelectEcho", 1), panel, selectArgs);
        }
        if (Get<void*>(panel, Offset("MonolithTimelinePanelManager", "_selectedEcho")) != islandUI ||
            Get<void*>(islandUI, Offset("EchoWebIslandUI", "island")) != wanted)
            throw std::runtime_error("the game did not select the requested Echo");
        bool quest = false; int selectedIndex = -1; void* selectedArgs[]{&quest, &selectedIndex};
        Invoke(Method(panel, "GetSelectedEchoInfo", 2), panel, selectedArgs);
        if (quest || selectedIndex != index) throw std::runtime_error("selected Echo readback does not match the target");
        return std::string("{\"ok\":true,\"selectionVerified\":true,\"focused\":") + std::to_string(index) + ",\"timeline\":" + std::to_string(timeline) + "}";
    });
}
std::string PanelReady(const std::string& id, int timeline, int difficulty) {
    return Run([=] {
        const auto c = Current(id); const auto t = Find(timeline, difficulty);
        bool panelReady = false;
        if (RestContextReady()) {
            void* web = Web(c, t.id, difficulty);
            const auto* cls = game::FindClass("LE.dll", "", "MonolithTimelinePanelManager");
            if (web && cls) {
                void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));
                bool inactive = false; void* args[]{type, &inactive};
                Root objects(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, args));
                const auto n = Get<uintptr_t>(objects.Get(), 0x18);
                if (n > 32) throw std::runtime_error("invalid timeline panel count");
                for (uintptr_t i = 0; i < n; ++i) {
                    void* panel = Get<void*>(objects.Get(), 0x20 + sizeof(void*) * i);
                    if (panel && game::IsAlive(panel) && Get<void*>(panel, Offset("MonolithTimelinePanelManager", "web")) == web &&
                        Get<uint8_t>(panel, Offset("MonolithTimelinePanelManager", "timelineID")) == t.id &&
                        Get<int>(panel, Offset("MonolithTimelinePanelManager", "difficultyIndex")) == difficulty)
                        panelReady = true;
                }
            }
        }
        return std::string("{\"ok\":true,\"player\":") + PlayerJson(c) + ",\"panelReady\":" + Boolean(panelReady) + "}";
    });
}
std::string Unlock(const std::string& id) {
    return Run([id] {
        if (id.empty()) throw std::runtime_error("offline save id required");
        const auto c = Current(id); Editable(c); const auto ts = Catalog();
        int pending = 0;
        for (const auto& t : ts) for (int d = 0; d < std::min(2, static_cast<int>(t.difficulties.size())); ++d)
            if (!IsUnlocked(c, t.id, d)) ++pending;
        if (!pending) return std::string("{\"ok\":true,\"unlocked\":0}");
        return Mutation(c, "monolithunlock", [&] {
            int count = 0;
            for (const auto& t : ts) for (uint8_t d = 0; d < std::min(2, static_cast<int>(t.difficulties.size())); ++d) {
                if (IsUnlocked(c, t.id, d)) continue;
                auto timeline = t.id; bool evaluate = false; void* args[]{&timeline, &d, &evaluate};
                Log("monolith: unlocking timeline %u difficulty %u", static_cast<unsigned>(timeline), static_cast<unsigned>(d));
                CoalesceUnlockNotification(timeline);
                Invoke(m_unlock, c.progress, args);
                if (!IsUnlocked(c, t.id, d)) throw std::runtime_error("timeline unlock verification failed");
                ++count;
            }
            return "\"unlocked\":" + std::to_string(count);
        });
    });
}
std::string Select(const std::string& id, int timeline, int difficulty) {
    return Run([=]() mutable {
        if (id.empty()) throw std::runtime_error("offline save id required");
        const auto c = Current(id); Editable(c); RestContextForPanel(); const auto t = Find(timeline, difficulty);
        if (!IsUnlocked(c, t.id, difficulty)) throw std::runtime_error("timeline difficulty is locked");
        return Mutation(c, "monolithselect", [&] {
            auto tid = t.id; bool specific = true, woven = false; int placed = -1;
            void* args[]{&tid, &specific, &difficulty, &woven, &placed}; Invoke(m_open, nullptr, args);
            if (Get<uint8_t>(c.runs, Offset("MonolithRunsManager", "mostRecentlyRequestedTimelineID")) != tid ||
                Get<int>(c.runs, Offset("MonolithRunsManager", "mostRecentlyRequestedDifficultyIndex")) != difficulty)
                throw std::runtime_error("timeline selection did not finish");
            return "\"selected\":" + RunJson(GetRun(c, t.id, difficulty));
        });
    });
}
std::string Corruption(const std::string& id, int timeline, int difficulty, int value) {
    return Run([=]() mutable {
        if (id.empty()) throw std::runtime_error("offline save id required");
        const auto c = Current(id); Editable(c); const auto t = Find(timeline, difficulty);
        if (!IsUnlocked(c, t.id, difficulty)) throw std::runtime_error("timeline difficulty is locked");
        void* d = t.difficulties[difficulty];
        if (!rules::Corruption(value, Get<int>(d, Offset("MonolithTimeline.Difficulty", "minimumCorruption")),
            Get<bool>(d, Offset("MonolithTimeline.Difficulty", "hasMaxCorruption")),
            Get<int>(d, Offset("MonolithTimeline.Difficulty", "maximumCorruption"))))
            throw std::runtime_error("corruption outside this difficulty's limits; use monolithread");
        return Mutation(c, "corruption", [&] {
            Root run(GetRun(c, t.id, difficulty, true));
            void* web = Get<void*>(run.Get(), Offset("MonolithRun", "web"));
            if (!web || Get<uint8_t>(web, Offset("EchoWeb", "timelineID")) != t.id)
                throw std::runtime_error("echo web does not belong to the requested timeline");
            // EchoWeb has no corruption setter. Update its primitive backing
            // field, then run the same recalculation/persistence methods as Shade.
            *reinterpret_cast<int*>(static_cast<char*>(web) + Offset("EchoWeb", "corruption")) = value;
            Invoke(m_updateMod, run.Get());
            auto tid = t.id; void* args[]{&tid, &value}; Invoke(m_corruption, c.progress, args);
            Invoke(m_updateHighest, c.runs); Invoke(m_updateShared, c.runs);
            RefreshSelected(c, t.id, difficulty);
            return "\"run\":" + RunJson(run.Get());
        });
    });
}
std::string Stability(const std::string& id, int timeline, int difficulty, int value) {
    return Run([=]() mutable {
        if (id.empty()) throw std::runtime_error("offline save id required");
        const auto c = Current(id); Editable(c); const auto t = Find(timeline, difficulty);
        if (!IsUnlocked(c, t.id, difficulty)) throw std::runtime_error("timeline difficulty is locked");
        const int maximum = Get<int>(t.difficulties[difficulty], Offset("MonolithTimeline.Difficulty", "maxStability"));
        if (value < 0 || maximum < 0 || value > maximum) throw std::runtime_error("stability outside the timeline's limits");
        return Mutation(c, "stability", [&] {
            Root run(GetRun(c, t.id, difficulty, true));
            const int current = Get<int>(run.Get(), runStability);
            if (current < 0 || current > maximum) throw std::runtime_error("invalid existing stability");
            int delta = value - current; void* args[]{&delta};
            ManualGain bypass; Invoke(m_add, run.Get(), args);
            if (Get<int>(run.Get(), runStability) != value) throw std::runtime_error("stability verification failed");
            RefreshSelected(c, t.id, difficulty);
            return "\"run\":" + RunJson(run.Get());
        });
    });
}
std::string SetMultiplier(double value) {
    return Run([value] {
        if (!ready || !rules::Multiplier(value)) throw std::runtime_error("stability multiplier must be finite and between 1 and 100");
        if (value == 1) {
            multiplier = 1; std::string why;
            if (hook::IsInstalled(m_add.code) && !hook::Remove(m_add.code, &why)) throw std::runtime_error(why);
        } else {
            (void)Current();
            if (!hook::IsInstalled(m_add.code)) {
                std::string why;
                if (!hook::Install(m_add.code, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&original), &why))
                    throw std::runtime_error(why);
            }
            multiplier = value;
        }
        return "{\"ok\":true,\"stabilityMultiplier\":" + std::to_string(value) + "}";
    });
}
std::string Status() {
    std::ostringstream out;
    out << "stability: x" << multiplier.load() << ", hook " << (m_add && hook::IsInstalled(m_add.code) ? "in" : "out")
        << ", gains " << calls.load() << ", boosted " << boosted.load() << ", refused " << refused.load()
        << ", faults " << faults.load() << ", last " << lastIn.load() << " -> " << lastOut.load();
    return out.str();
}
#ifdef EPOCHPACT_RESEARCH
std::string TestGain(const std::string& id, int timeline, int difficulty, int amount) {
    return Run([=]() mutable {
        const auto c = Current(id); Editable(c); const auto t = Find(timeline, difficulty);
        if (id.empty() || c.name != "EpMonolithTest" || amount < -10000 || amount > 10000)
            throw std::runtime_error("research gain requires the isolated EpMonolithTest character");
        if (!IsUnlocked(c, t.id, difficulty)) throw std::runtime_error("timeline difficulty is locked");
        return Mutation(c, "stability", [&] {
            Root run(GetRun(c, t.id, difficulty, true));
            void* args[]{&amount}; Invoke(m_add, run.Get(), args);
            return "\"run\":" + RunJson(run.Get());
        });
    });
}
#endif
}
