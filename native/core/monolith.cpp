#include "monolith.hpp"
#include "hideout.hpp"
#include "hideout_rules.hpp"
#include "ingame_window.hpp"
#include "monolith_rules.hpp"
#include "managed.hpp"
#include "progression.hpp"
#include "mutation_transaction.hpp"
#include <memory>
#include "hook.hpp"
#include <algorithm>
#include "commands.hpp"
#include <atomic>
#include <map>
#include <iterator>
#include <tuple>
#include <array>
#include <functional>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
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
MethodRef m_nextQuest, m_empowered;
bool ready = false;
size_t runStability = 0;
using AddFn = void (*)(void*, int, const il2cpp::Method*);
AddFn original = nullptr;
std::atomic<double> multiplier{1};
std::atomic<uint64_t> calls{0}, boosted{0}, refused{0}, faults{0};
std::atomic<int> lastIn{0}, lastOut{0};
thread_local bool manual = false;
// Skip quest echoes before the boss: once a Normal run's Stability is full, its quest echoes before the boss count as
// completed, so the timeline offers the boss echo next.
game::MethodRef m_objectClick, m_startQuest, m_inside, m_timelineUnlocked, m_unlockNotice, m_openBlessing, m_echoPortalUse, m_harbingerSetup, m_harbingerGuarantee, m_questWindow, m_questNotice;
bool bossTracked = false;
int bossTimeline = 0;
std::atomic<bool> bossDoors{false};
std::atomic<bool> noBlessing{false};
std::atomic<bool> questSkip{false};
std::atomic<uint64_t> questSkips{0};

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

// A timeline's quest echoes (MonolithTimeline.questZones), in the game's order: the last one is the boss
// (MonolithRun.IsBossQuestEcho). The game offers the zone whose requiredQuestCompletion equals the run's
// questCompletion (and, for a zone that requires one, its quest branch), and counts quest echo i as completed while
// questCompletion > i; finishing a quest echo adds 1 (MonolithZoneManager.OnQuestCompleted).
struct QuestZone { void* zone; int required; bool needsBranch; int branch; };
std::vector<QuestZone> QuestZones(void* timeline) {
    std::vector<QuestZone> zones;
    if (!timeline) return zones;
    for (void* z : Entries(Get<void*>(timeline, Offset("MonolithTimeline", "questZones")))) {
        if (!z) throw std::runtime_error("missing timeline quest echo");
        zones.push_back({z, Get<int>(z, Offset("MonolithTimeline.QuestZone", "requiredQuestCompletion")),
                         Get<bool>(z, Offset("MonolithTimeline.QuestZone", "requiresBranch")),
                         Get<int>(z, Offset("MonolithTimeline.QuestZone", "requiredBranch"))});
    }
    return zones;
}
// The run's quest echoes before the boss counted as completed, when it is a Normal run with full Stability whose
// next quest echo is not the boss yet. Empowered runs offer the boss alone (QuestEcho.GetAvailability). The boss must be
// the game's own next quest echo afterwards (nextQuestEcho), else nothing changes. apply false only tells whether it
// would change. Returns the quest echoes counted as completed now (0: nothing).
int SkipToBoss(void* run, bool apply) {
    if (!run || Value<bool>(m_empowered, run)) return 0;
    const int maximum = Value<int>(m_max, run);
    if (maximum <= 0 || Get<int>(run, runStability) < maximum) return 0;
    const auto zones = QuestZones(Get<void*>(run, Offset("MonolithRun", "timeline")));
    if (zones.size() < 2) return 0;
    const auto& boss = zones.back();
    // a linear route only: quest echo i needs i completed, the boss the ones before it
    if (boss.required != static_cast<int>(zones.size()) - 1) return 0;
    const size_t at = Offset("MonolithRun", "questCompletion");
    const int before = Get<int>(run, at);
    if (before < 0 || before >= boss.required) return 0;
    if (boss.needsBranch && Get<int>(run, Offset("MonolithRun", "questBranch")) != boss.branch) return 0;
    if (!apply) return boss.required - before;
    int& completion = *reinterpret_cast<int*>(static_cast<char*>(run) + at);
    completion = boss.required;
    if (Invoke(m_nextQuest, run) != boss.zone) {
        completion = before;
        throw std::runtime_error("the timeline's next quest echo is not its boss; nothing was changed");
    }
    ++questSkips;
    Log("monolith: quest echoes before the boss counted as completed (timeline %d, %d -> %d)",
        static_cast<int>(Get<uint8_t>(run, Offset("MonolithRun", "timelineID"))), before, boss.required);
    return boss.required - before;
}
std::vector<void*> PlayerRuns(const Context& c) { return Entries(Get<void*>(c.runs, Offset("MonolithRunsManager", "runs"))); }

std::string RunJson(void* run) {
    if (!run) return "null";
    void* web = Get<void*>(run, Offset("MonolithRun", "web"));
    return "{\"timeline\":" + std::to_string(Get<uint8_t>(run, Offset("MonolithRun", "timelineID"))) +
        ",\"difficulty\":" + std::to_string(Get<int>(run, Offset("MonolithRun", "difficultyIndex"))) +
        ",\"corruption\":" + std::to_string(web ? Get<int>(web, Offset("EchoWeb", "corruption")) : 0) +
        ",\"stability\":" + std::to_string(Get<int>(run, runStability)) +
        ",\"maxStability\":" + std::to_string(Value<int>(m_max, run)) +
        ",\"questCompletion\":" + std::to_string(Get<int>(run, Offset("MonolithRun", "questCompletion"))) + "}";
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
    if (questSkip.load()) {
        std::string why;
        if (!game::Guarded([&] {
                const auto c = Current();
                for (void* run : PlayerRuns(c)) if (run == self) { SkipToBoss(run, true); break; }
            }, &why)) ++faults;
    }
}
// The AddStability hook stays installed while the multiplier or the quest echo skip needs it.
void UpdateHook() {
    std::string why;
    const bool wanted = multiplier.load() != 1 || questSkip.load();
    if (wanted && !hook::IsInstalled(m_add.code)) {
        if (!hook::Install(m_add.code, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&original), &why))
            throw std::runtime_error(why);
    } else if (!wanted && hook::IsInstalled(m_add.code) && !hook::Remove(m_add.code, &why)) {
        throw std::runtime_error(why);
    }
}
struct ManualGain {
    bool before = manual;
    ManualGain() { manual = true; }
    ~ManualGain() { manual = before; }
};
template<class Work> std::string Mutation(const std::string& id, const char* action, Work work) {
    Context before;
    progression::SnapshotData snapshot;
    std::vector<std::unique_ptr<Root>> roots;
    auto sync = [](const Context& c) { Invoke(m_saveRuns, c.runs); };
    return transaction::Execute(Run, [&] {
        const auto c = Current(id);
        if (id.empty()) throw std::runtime_error("offline save id required");
        const auto early = work(c, false);
        if (!early.empty()) return early;
        before = c;
        for (void* object : {c.actor, c.data, c.runs, c.progress}) roots.push_back(std::make_unique<Root>(object));
        sync(c);
        snapshot = progression::CaptureSnapshotCurrent(id, action);
        return std::string();
    }, [&] { return progression::WriteCapturedSnapshot(snapshot); }, [&] {
        const auto c = Current(id);
        if (c.actor != before.actor ||
                c.data != before.data ||
                c.runs != before.runs ||
                c.progress != before.progress || c.name != before.name || c.scene != before.scene)
            throw std::runtime_error("character or game context changed during backup; action cancelled");
        sync(c);
        const auto now = progression::CaptureSnapshotCurrent(id, action);
        if (now.character != snapshot.character || now.stash != snapshot.stash || now.global != snapshot.global)
            throw std::runtime_error("save state changed during backup; refresh and try again");
        const auto details = work(c, true);
        Save(c);
        return "{\"ok\":true," + details + "}";
    }, Json);
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
        m_nextQuest = resolve("MonolithRun", "nextQuestEcho", 0);
        m_objectClick = resolve("WorldObjectClickListener", "ObjectClick", 2);
        m_timelineUnlocked = resolve("MonolithProgressManager", "timelineUnlocked", 2);
        m_unlockNotice = resolve("Notifications", "TimelineUnlockedNotification", 2);
        m_openBlessing = resolve("UIBase", "OpenBlessingOptions", 3);
        m_echoPortalUse = resolve("PortalLogicController", "OnAttemptUse", 1, "Networking.Multiplayer.Interactables.Portals");
        m_harbingerSetup = resolve("MonolithZoneManager", "SetUpHarbingerBossEncounter", 1);
        m_harbingerGuarantee = resolve("HarbingersProgress", "ShouldBossfightBeGuarenteedHarbinger", 2, "LE.Factions");
        m_questWindow = resolve("QuestRewardPanel", "OpenImmediate", 3);
        m_questNotice = resolve("Notifications", "ShowQuestRewardNotification", 3);
        m_startQuest = game::FindMethod("LE.dll", "LE.Gameplay.Monolith", "MonolithGameplayManager", "AttemptStartQuestAsync", 6);
        m_inside = game::FindMethod("LE.dll", "LE.Gameplay.Monolith", "MonolithGameplayManager", "IsHostPlayerInsideCurrentEcho", 0);
        if (!m_startQuest || !m_inside) throw std::runtime_error("boss echo start unavailable");
        m_empowered = resolve("MonolithRun", "get_IsEmpowered", 0);
        runStability = Offset("MonolithRun", "stability");
        (void)Offset("MonolithRun", "questCompletion");
        (void)Offset("MonolithRun", "questBranch");
        (void)Offset("MonolithTimeline", "questZones");
        (void)Offset("MonolithTimeline.QuestZone", "requiredQuestCompletion");
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
            << ",\"stabilityMultiplier\":" << multiplier.load() << ",\"questSkip\":" << Boolean(questSkip.load()) << ",\"bossDoors\":" << Boolean(bossDoors.load()) << ",\"noBlessing\":" << Boolean(noBlessing.load()) << ",\"current\":"
            << RunJson(Get<void*>(c.runs, Offset("MonolithRunsManager", "currentRun"))) << ",\"selected\":{\"timeline\":"
            << static_cast<int>(Get<uint8_t>(c.runs, Offset("MonolithRunsManager", "mostRecentlyRequestedTimelineID")))
            << ",\"difficulty\":" << Get<int>(c.runs, Offset("MonolithRunsManager", "mostRecentlyRequestedDifficultyIndex"))
            << "},\"timelines\":[";
        bool first = true;
        for (const auto& t : Catalog()) {
            if (!first) out << ','; first = false;
            // the quest echoes before the boss
            const auto zones = QuestZones(t.object);
            out << "{\"id\":" << static_cast<int>(t.id) << ",\"name\":" << Json(t.name)
                << ",\"questEchoes\":" << (zones.empty() ? 0 : static_cast<int>(zones.size()) - 1) << ",\"difficulties\":[";
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
    return Mutation(id, "monolithunlock", [id](const Context& c, bool apply) -> std::string {
        if (id.empty()) throw std::runtime_error("offline save id required");
        Editable(c); const auto ts = Catalog();
        int pending = 0;
        for (const auto& t : ts) for (int d = 0; d < std::min(2, static_cast<int>(t.difficulties.size())); ++d)
            if (!IsUnlocked(c, t.id, d)) ++pending;
        if (!pending) return std::string("{\"ok\":true,\"unlocked\":0}");
        if (!apply) return std::string();
        {
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
        }
    });
}
std::string Select(const std::string& id, int timeline, int difficulty) {
    return Mutation(id, "monolithselect", [=](const Context& c, bool apply) mutable -> std::string {
        if (id.empty()) throw std::runtime_error("offline save id required");
        Editable(c); RestContextForPanel(); const auto t = Find(timeline, difficulty);
        if (!IsUnlocked(c, t.id, difficulty)) throw std::runtime_error("timeline difficulty is locked");
        if (!apply) return std::string();
        {
            auto tid = t.id; bool specific = true, woven = false; int placed = -1;
            void* args[]{&tid, &specific, &difficulty, &woven, &placed}; Invoke(m_open, nullptr, args);
            if (Get<uint8_t>(c.runs, Offset("MonolithRunsManager", "mostRecentlyRequestedTimelineID")) != tid ||
                Get<int>(c.runs, Offset("MonolithRunsManager", "mostRecentlyRequestedDifficultyIndex")) != difficulty)
                throw std::runtime_error("timeline selection did not finish");
            return "\"selected\":" + RunJson(GetRun(c, t.id, difficulty));
        }
    });
}
std::string Corruption(const std::string& id, int timeline, int difficulty, int value) {
    return Mutation(id, "corruption", [=](const Context& c, bool apply) mutable -> std::string {
        if (id.empty()) throw std::runtime_error("offline save id required");
        Editable(c); const auto t = Find(timeline, difficulty);
        if (!IsUnlocked(c, t.id, difficulty)) throw std::runtime_error("timeline difficulty is locked");
        void* d = t.difficulties[difficulty];
        if (!rules::Corruption(value, Get<int>(d, Offset("MonolithTimeline.Difficulty", "minimumCorruption")),
            Get<bool>(d, Offset("MonolithTimeline.Difficulty", "hasMaxCorruption")),
            Get<int>(d, Offset("MonolithTimeline.Difficulty", "maximumCorruption"))))
            throw std::runtime_error("corruption outside this difficulty's limits; use monolithread");
        if (!apply) return std::string();
        {
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
        }
    });
}
std::string Stability(const std::string& id, int timeline, int difficulty, int value) {
    return Mutation(id, "stability", [=](const Context& c, bool apply) mutable -> std::string {
        if (id.empty()) throw std::runtime_error("offline save id required");
        Editable(c); const auto t = Find(timeline, difficulty);
        if (!IsUnlocked(c, t.id, difficulty)) throw std::runtime_error("timeline difficulty is locked");
        const int maximum = Get<int>(t.difficulties[difficulty], Offset("MonolithTimeline.Difficulty", "maxStability"));
        if (value < 0 || maximum < 0 || value > maximum) throw std::runtime_error("stability outside the timeline's limits");
        if (!apply) return std::string();
        {
            Root run(GetRun(c, t.id, difficulty, true));
            const int current = Get<int>(run.Get(), runStability);
            if (current < 0 || current > maximum) throw std::runtime_error("invalid existing stability");
            int delta = value - current; void* args[]{&delta};
            ManualGain bypass; Invoke(m_add, run.Get(), args);
            if (Get<int>(run.Get(), runStability) != value) throw std::runtime_error("stability verification failed");
            RefreshSelected(c, t.id, difficulty);
            return "\"run\":" + RunJson(run.Get());
        }
    });
}
std::string SetMultiplier(double value) {
    return Run([value] {
        if (!ready || !rules::Multiplier(value)) throw std::runtime_error("stability multiplier must be finite and between 1 and 100");
        if (value != 1) (void)Current();
        const double before = multiplier.load();
        multiplier = value;
        try { UpdateHook(); } catch (...) { multiplier = before; throw; }
        return "{\"ok\":true,\"stabilityMultiplier\":" + std::to_string(value) + "}";
    });
}
// Skip quest echoes before the boss on or off. Turned on, a Normal run whose Stability is already full gets it at once
// (with a recovery backup, in End of Time, Monolith Hub or Traveler's Rest); the others when their Stability fills up.
std::string SetQuestSkip(bool on) {
    const std::string set = Run([on] {
        if (!ready) throw std::runtime_error("monolith metadata unavailable");
        if (on) (void)Current();
        const bool before = questSkip.load();
        questSkip = on;
        try { UpdateHook(); } catch (...) { questSkip = before; throw; }
        return std::string("{\"ok\":true}");
    });
    if (set != "{\"ok\":true}") return set;
    int pending = 0;
    std::string id;
    bool editable = false;
    if (on) (void)Run([&] {
        const auto c = Current();
        id = c.id;
        editable = c.scene == "EoT" || c.scene == "MonolithHub" || c.scene == "M_Rest";
        for (void* run : PlayerRuns(c)) pending += SkipToBoss(run, false) > 0;
        return std::string("{}");
    });
    if (pending && editable)
        return Mutation(id, "questskip", [](const Context& c, bool apply) -> std::string {
            Editable(c);
            if (!apply) return std::string();
            int runs = 0;
            for (void* run : PlayerRuns(c)) runs += SkipToBoss(run, true) > 0;
            Invoke(m_updateShared, c.runs);
            return "\"questSkip\":true,\"runsChanged\":" + std::to_string(runs);
        });
    return "{\"ok\":true,\"questSkip\":" + std::string(Boolean(on)) + ",\"runsChanged\":0,\"waiting\":" + std::to_string(pending) + "}";
}
std::string Status() {
    std::ostringstream out;
    out << "stability: x" << multiplier.load() << ", hook " << (m_add && hook::IsInstalled(m_add.code) ? "in" : "out")
        << ", gains " << calls.load() << ", boosted " << boosted.load() << ", refused " << refused.load()
        << ", faults " << faults.load() << ", last " << lastIn.load() << " -> " << lastOut.load()
        << "; quest echo skip " << (questSkip.load() ? "on" : "off") << ", skipped " << questSkips.load();
    return out.str();
}
// ------------------------------------------------------------------------------- Boss doors and Boss Rush
// Twelve copies of the game's own Monolith rock stand in the Monolith Hub: ten start a timeline's boss echo directly, even a
// locked one (the game's own AttemptStartQuestAsync), one switches Normal and Empowered, one runs all ten in turn. A kill counts
// for whatever the game counts it for; nothing else is added. Doors are free of the timeline's Stability and quest progress: the
// game's start and its boss need those values, so they are borrowed for the echo and written back after it. Corruption stays the run's own.
// An echo refuses to start from inside an echo or outside Traveler's Rest's scene (M_Rest holds the Rest and the Hub), so a
// next boss waits for the player's own portal out, and a Forgotten Knight arrival goes to the Rest first.
namespace {
struct DoorVec3 { float x, y, z; };
struct Door {
    std::unique_ptr<Root> object,listener,label;int action;uint64_t lastClick=0;
    std::unique_ptr<Root> stationBox;
    float authoredX=0,authoredZ=0,yaw=0,size=1,lift=0;bool inHall=false,hasLook=false;
    hideout::StationBounds stationBounds{};bool stationBoundsKnown=false;
};  // action: 1-10 timeline, 0 difficulty, -1 rush
constexpr uint64_t kDoorHeldMs = 350;  // the game repeats a click while the button is held: each repeat restarts this wait
constexpr int kDifficultyDoor = 0, kRushDoor = -1, kHallDoor = -2, kHubDoor = -3, kStashStation = -4, kVendorStation = -5, kCacheStation = -6;
constexpr uint64_t kBossTickMs = 400;
std::vector<Door> doors;
#ifdef EPOCHPACT_TESTING
uint64_t hallBuildCount=0,doorBuildCount=0,doorSyncCount=0,doorPoseUpdateCount=0;
#endif
int bossDifficulty = 0, rushDifficulty = 0, rushAt = 1;
bool rushOn = false;
int pendingBoss = -1, pendingDifficulty = 0;
bool bossDied = false, bossWarped = false, goToHall = false;
std::string goToHallId,goToHallScene;uint64_t goToHallUntil=0;
uint64_t bossArrived = 0, bossTravelAt = 0, bossReadySince = 0, bossLastTick = 0, doorLabelsAt = 0;
uint64_t bossStarts = 0, bossFaults = 0;
std::string bossError;
using ObjectClickFn = void (*)(void*, void*, bool, const il2cpp::Method*);
ObjectClickFn originalObjectClick = nullptr;
#ifdef EPOCHPACT_TESTING
void TraceDoorInput(void* listener,bool click);
bool testDoorForeground=false;
#endif
int bossRunDifficulty = 0;
bool bossStarting = false;
bool bossHarbinger = false, rushHarbinger = false, pendingHarbinger = false, bossRunHarbinger = false;
bool bossKilled = false, bossPortalUsed = false, bossPortalMade = false;
int bossRequired = 0;
uint64_t bossKilledAt = 0, bossPortalLabelAt = 0;
int bossPortalLabels = 0;
using UnlockedFn = bool (*)(void*, uint8_t, uint8_t, const il2cpp::Method*);
UnlockedFn originalUnlocked = nullptr;
using UnlockNoticeFn = void (*)(uint8_t, uint8_t, const il2cpp::Method*);
UnlockNoticeFn originalUnlockNotice = nullptr;
bool UnlockedDetour(void* self, uint8_t timeline, uint8_t difficulty, const il2cpp::Method* method) {
    if ((bossStarting || bossTracked) && timeline == bossTimeline && difficulty == bossRunDifficulty) return true;
    return originalUnlocked(self, timeline, difficulty, method);
}
// Notifications.TimelineUnlockedNotification adds names to a dictionary and throws on a repeated one; the throw ends the
// game's TimelineBossDied before the loot and the completion. A door's boss goes without the note.
void UnlockNoticeDetour(uint8_t timeline, uint8_t difficulty, const il2cpp::Method* method) {
    if (bossTracked) return;
    originalUnlockNotice(timeline, difficulty, method);
}
// A portal of type Echo, which only the door's way on is here, starts a Rush's next boss. The player's own portal (Town, or CompletedEcho
// after a kill) goes where the game sends it.
constexpr uint8_t kPortalEcho = 3;
using EchoPortalFn = void (*)(void*, void*, const il2cpp::Method*);
EchoPortalFn originalEchoPortal = nullptr;
void EchoPortalDetour(void* self, void* sync, const il2cpp::Method* method) {
    if (bossTracked && self && Get<uint8_t>(self, Offset("PortalLogicController", "portalType", "Networking.Multiplayer.Interactables.Portals")) == kPortalEcho) bossPortalUsed = true;
    originalEchoPortal(self, sync, method);
}
// The game's Empowered boss echo turns into a Harbinger fight by its own rule (no Forgotten Knights rank, or the opt-in with enough
// corruption). A door's choice decides instead: the setup is left out for the boss alone, the corruption rule answers yes for the Harbinger.
using HarbingerSetupFn = void (*)(void*, void*, const il2cpp::Method*);
HarbingerSetupFn originalHarbingerSetup = nullptr;
void HarbingerSetupDetour(void* self, void* run, const il2cpp::Method* method) {
    if (bossTracked && bossRunDifficulty == 1 && !bossRunHarbinger) return;
    originalHarbingerSetup(self, run, method);
}
// The Quest Completed window of a door's boss stays closed. StatefulQuestList.onQuestCompletion has given the rewards by then:
// the window only shows them. It can open just after the way out, when the scene change is over.
uint64_t questWindowQuietUntil = 0;
uint64_t questWindowsLeftOut = 0;
bool QuestWindowQuiet() { return bossTracked || GetTickCount64() < questWindowQuietUntil; }
using QuestWindowFn = void (*)(void*, uint8_t, int, const il2cpp::Method*);
QuestWindowFn originalQuestWindow = nullptr;
void QuestWindowDetour(void* quest, uint8_t idolUnlock, int passivePoints, const il2cpp::Method* method) {
    if (QuestWindowQuiet()) { ++questWindowsLeftOut; return; }
    originalQuestWindow(quest, idolUnlock, passivePoints, method);
}
using QuestNoticeFn = void (*)(void*, uint8_t, int, const il2cpp::Method*);
QuestNoticeFn originalQuestNotice = nullptr;
void QuestNoticeDetour(void* quest, uint8_t idolUnlock, int passivePoints, const il2cpp::Method* method) {
    if (QuestWindowQuiet()) { ++questWindowsLeftOut; return; }
    originalQuestNotice(quest, idolUnlock, passivePoints, method);
}
using HarbingerGuaranteeFn = bool (*)(void*, uint8_t, int, const il2cpp::Method*);
HarbingerGuaranteeFn originalHarbingerGuarantee = nullptr;
bool HarbingerGuaranteeDetour(void* self, uint8_t timeline, int corruption, const il2cpp::Method* method) {
    if ((bossStarting || bossTracked) && bossRunHarbinger && timeline == bossTimeline) return true;
    return originalHarbingerGuarantee(self, timeline, corruption, method);
}

const il2cpp::Method* DoorSignature(const char* cls, const char* name, std::initializer_list<const char*> parameters, const char* ns = "UnityEngine", const char* image = "UnityEngine.CoreModule.dll") {
    auto& a = il2cpp::api(); auto* c = game::FindClass(image, ns, cls); void* iterator = nullptr;
    if (!c) throw std::runtime_error(std::string("no class ") + cls);
    while (auto* m = a.class_get_methods(c, &iterator)) {
        if (std::string(a.method_get_name(m)) != name || a.method_get_param_count(m) != parameters.size()) continue;
        bool match = true; unsigned i = 0;
        for (const char* expected : parameters) { char* actual = a.type_get_name(a.method_get_param(m, i++)); match &= actual && std::string(actual) == expected; if (actual) a.free(actual); }
        if (match) return m;
    }
    throw std::runtime_error(std::string("no method ") + name);
}
game::MethodRef Unity(const char* cls, const char* name, int n) { return game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", cls, name, n); }
DoorVec3 PlaceOf(void* component) {
    Root tr(Invoke(Unity("Component", "get_transform", 0), component));
    return Value<DoorVec3>(Unity("Transform", "get_position", 0), tr.Get());
}
bool AreaReady() {
    void* service = Invoke(game::FindMethod("LE.dll", "LE.Services", "ServiceProvider", "get_SceneService", 0));
    if (!service || Value<bool>(Method(service, "IsLoading", 0), service)) return false;
    if (Value<bool>(game::FindMethod("LE.dll", "LE.UI", "LoadingScreen", "get_IsVisible", 0))) return false;
    void* transition = Invoke(game::FindMethod("LE.dll", "LE.Services", "ServiceProvider", "get_ClientTransitionService", 0));
    return transition && !Value<bool>(Method(transition, "get_IsAnyPlayerTransitioning", 0), transition) && !Invoke(Method(transition, "get_ActiveTransition", 0), transition);
}
bool SampleWalkable(DoorVec3 at, float range, DoorVec3* out) {
    auto* cls = game::FindClass("UnityEngine.AIModule.dll", "UnityEngine.AI", "NavMesh");
    if (!cls) return false;
    static const il2cpp::Method* sample = nullptr;
    if (!sample) sample = DoorSignature("NavMesh", "SamplePosition", {"UnityEngine.Vector3", "UnityEngine.AI.NavMeshHit&", "System.Single", "System.Int32"}, "UnityEngine.AI", "UnityEngine.AIModule.dll");
    struct Hit { DoorVec3 position, normal; float distance; int mask; int hit; } hit{};
    int mask = -1; void* a[]{&at, &hit, &range, &mask};
    if (!Value<bool>(sample, nullptr, a)) return false;
    *out = hit.position; return true;
}
bool Warp(void* actor, DoorVec3 at) {
    void* agent = Get<void*>(actor, Offset("Actor", "navMeshAgent"));
    void* a[]{&at}; return agent && Value<bool>(Method(agent, "Warp", 1), agent, a);
}
std::string DifficultyName(int difficulty) { return difficulty ? "Empowered" : "Normal"; }
std::string ChoiceName() { return !bossDifficulty ? "Normal" : bossHarbinger ? "Empowered + Harbinger" : "Empowered"; }
std::string DoorLabel(const Door& d) {
    if (d.action == kHallDoor) return "Boss Hall";
    if (d.action == kHubDoor) return "Back to the Monolith Hub";
    if (d.action == kStashStation) return "Stash";
    if (d.action == kVendorStation) return "Vendor";
    if (d.action == kCacheStation) return "Eternity Cache";
    if (d.action == kDifficultyDoor) return "Boss doors: " + ChoiceName() + " (click to change)";
    if (d.action == kRushDoor) return "Boss Rush: " + ChoiceName() + (rushAt > 1 ? " (from boss " + std::to_string(rushAt) + ")" : "");
    for (const auto& t : Catalog()) if (t.id == d.action) return t.name + ": " + ChoiceName();
    return "Boss " + std::to_string(d.action);
}
void RefreshDoorLabels() {
    for (auto& d : doors) if (d.label && d.label->Get() && game::IsAlive(d.label->Get())) {
        void* text = il2cpp::api().string_new(DoorLabel(d).c_str()); void* a[]{text};
        // the tag falls back on its object's name when it starts, so both carry the door's name
        void* go = Invoke(Unity("Component", "get_gameObject", 0), d.label->Get());
        Invoke(Unity("Object", "set_name", 1), go, a);
        Invoke(Method(d.label->Get(), "SetHoverText", 1), d.label->Get(), a);
    }
}
void ClearDoors() {
    for (auto& d : doors) if (d.object && d.object->Get() && game::IsAlive(d.object->Get())) { void* a[]{d.object->Get()}; Invoke(Unity("Object", "Destroy", 1), nullptr, a); }
    doors.clear();
}
size_t doorsExpected = 12;
bool DoorsAlive() { return doors.size() == doorsExpected && std::all_of(doors.begin(), doors.end(), [](const Door& d) { return d.object && d.object->Get() && game::IsAlive(d.object->Get()); }); }
// The Boss Hall, a place of our own in the Hub's scene, far from the islands. Nothing of it is a copied place: the ground is a mesh made
// here in the outline we choose, in a material of the game, and the parts on it come one by one from the table below. The parts are made
// inside an inactive holder, so none of their scripts ever runs, and their scripts stay off. A NavMesh built from the ground's and the
// parts' colliders says where one can walk; clicks find the ground on the same collider. Nothing of this is saved; a scene change takes
// it away and the next visit builds it again.
constexpr DoorVec3 kHallCentre{1400.f, 1.f, 1400.f};
constexpr float kHallRock = 16.f;
constexpr DoorVec3 kHubSpot{-13.f, 2.f, 1039.f};
// Places in the hall are written as the player sees them: right and up on the screen, from the middle. Those directions are the camera's, read when the hall is built.
float hallRightX = 0.70710678f, hallRightZ = -0.70710678f, hallUpX = 0.70710678f, hallUpZ = 0.70710678f;
DoorVec3 HallPlace(float right, float up) {
    return {kHallCentre.x + right * hallRightX + up * hallUpX, kHallCentre.y, kHallCentre.z + right * hallRightZ + up * hallUpZ};
}
void HallReadCamera() {
    void* camera = Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Camera", "get_main", 0));
    if (!camera) return;
    Root tr(Invoke(Unity("Component", "get_transform", 0), camera));
    const DoorVec3 right = Value<DoorVec3>(Unity("Transform", "get_right", 0), tr.Get()), forward = Value<DoorVec3>(Unity("Transform", "get_forward", 0), tr.Get());
    const float rightLength = std::sqrt(right.x * right.x + right.z * right.z), forwardLength = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    if (rightLength < .1f || forwardLength < .1f) return;
    hallRightX = right.x / rightLength; hallRightZ = right.z / rightLength; hallUpX = forward.x / forwardLength; hallUpZ = forward.z / forwardLength;
}
std::unique_ptr<Root> hallRoot;
bool hallBroken = false;  // it could not be built in this game: the doors stay in the Hub
rules::HallMoveRequest hallMove; // 1 into the hall, 2 back to the Hub; never consumed before a successful warp
std::string hallStation;  // a station clicked: the game's panel to open, for the next tick
std::unique_ptr<Root> hallSurface, hallGroundRenderer, hallLight, hallMesh;
DoorVec3 HallCentre() { return {kHallCentre.x, kHallCentre.y + .5f, kHallCentre.z}; }
bool HallAlive() { return hallRoot && hallRoot->Get() && game::IsAlive(hallRoot->Get()); }
bool InHall(const DoorVec3& at) { return std::abs(at.x - kHallCentre.x) < 200 && std::abs(at.z - kHallCentre.z) < 200; }
void QueueHallMove(const Context& c,int direction){
    if(c.scene!="MonolithHub")throw std::runtime_error("Hideout movement requires the Monolith Hub.");
    hallMove.Set(direction,c.id,c.scene,reinterpret_cast<uintptr_t>(c.actor),GetTickCount64());
}
// A cross-scene load can take much longer than the local NavMesh warmup.
// Keep the character/scene guards, with enough time for slower storage.
void QueueHallReturn(const Context& c){goToHall=true;goToHallId=c.id;goToHallScene=c.scene;goToHallUntil=GetTickCount64()+120000;}
bool TryHallMove(const Context& c,uint64_t now){
    if(!hallMove.direction)return false;
    if(!hallMove.Valid(c.id,c.scene,reinterpret_cast<uintptr_t>(c.actor),now)){hallMove.Clear();return false;}
    if(hallMove.direction==1&&!HallAlive())return false;
    const auto to=hallMove.direction==1?HallCentre():kHubSpot;DoorVec3 snapped{};
    if(!SampleWalkable(to,6.f,&snapped)||!Warp(c.actor,snapped))return false;
    hallMove.Clear();return true;
}
DoorVec3 HallCentre();
bool HallAlive();
// ---- The hall's minimap. The Hub's map ends long before the hall, so there it is dark. The game makes maps for its generated areas with
// MinimapGenerator.GetMinimapForTriangulation; our flat ground goes in instead of a nav mesh and a MinimapInformation of our own comes out,
// all of it shown (black fog, in a texture the game can stamp into each frame, as MinimapManager.LoadFoW makes its own). Minimap.OnMapLoaded shows it while the player is in the hall and the Hub's outside.
std::vector<DoorVec3> hallFlatVertices;
std::vector<int> hallFlatIndices;
std::unique_ptr<Root> hallMapInfo, hallMapTexture, hallMapFog, hallMapHubInfo, hallMapHubScene;
bool hallMapShowing = false, hallMapFailed = false;
std::vector<void*> Minimaps() {
    std::vector<void*> out;
    Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("LE.dll", "LE.UI.Minimap", "Minimap"))));
    bool inactive = true; void* args[]{type.Get(), &inactive};
    Root found(Invoke(Unity("Object", "FindObjectsOfType", 2), nullptr, args));
    for (uintptr_t i = 0, n = found.Get() ? Get<uintptr_t>(found.Get(), 0x18) : 0; i < n && i < 16; ++i) {
        void* m = Get<void*>(found.Get(), 0x20 + 8 * i);
        if (m && game::IsAlive(m)) out.push_back(m);
    }
    return out;
}
void ShowMap(void* minimap, void* info, void* scene) {
    void* args[]{info, scene};
    Invoke(Method(minimap, "OnMapLoaded", 2), minimap, args);
}
void BakeHallMap() {
    auto& api = il2cpp::api();
    if (hallFlatIndices.empty()) throw std::runtime_error("no ground to map");
    const auto started = GetTickCount64();
    Root vertices(api.array_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Vector3"), hallFlatVertices.size()));
    std::memcpy(static_cast<char*>(vertices.Get()) + 0x20, hallFlatVertices.data(), hallFlatVertices.size() * sizeof(DoorVec3));
    Root indices(api.array_new(game::FindClass("mscorlib.dll", "System", "Int32"), hallFlatIndices.size()));
    std::memcpy(static_cast<char*>(indices.Get()) + 0x20, hallFlatIndices.data(), hallFlatIndices.size() * sizeof(int));
    Root areas(api.array_new(game::FindClass("mscorlib.dll", "System", "Int32"), hallFlatIndices.size() / 3));  // all area 0
    struct { void* vertices; void* indices; void* areas; } triangulation{vertices.Get(), indices.Get(), areas.Get()};
    const auto* make = game::FindMethod("LE.dll", "LE.UI.Minimap", "MinimapGenerator", "GetMinimapForTriangulation", 7).info;
    // the middle is what the map keeps: a List<Vector3> of one
    const auto* listClass = api.class_from_type(api.method_get_param(make, 1));
    Root required(api.object_new(listClass));
    Invoke(api.class_get_method_from_name(listClass, ".ctor", 0), required.Get());
    DoorVec3 middle = HallCentre(); void* add[]{&middle};
    Invoke(api.class_get_method_from_name(listClass, "Add", 1), required.Get(), add);
    // the game's own materials for it
    Root resourcesType(api.type_get_object(api.class_get_type(game::FindClass("LE.dll", "LE.UI.Minimap", "MinimapResources"))));
    void* ra[]{resourcesType.Get()};
    Root all(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Resources", "FindObjectsOfTypeAll", 1), nullptr, ra));
    void* resources = all.Get() && Get<uintptr_t>(all.Get(), 0x18) ? Get<void*>(all.Get(), 0x20) : nullptr;
    if (!resources) throw std::runtime_error("the game's map resources are not loaded");
    bool split = false; float separation = 0;
    void* args[]{&triangulation, required.Get(), Get<void*>(resources, Offset("MinimapResources", "MinimapWhite", "LE.UI.Minimap")), Get<void*>(resources, Offset("MinimapResources", "MinimapSdf", "LE.UI.Minimap")),
                 Get<void*>(resources, Offset("MinimapResources", "MinimapCombine", "LE.UI.Minimap")), &split, &separation};
    Root result(Invoke(game::FindMethod("LE.dll", "LE.UI.Minimap", "MinimapGenerator", "GetMinimapForTriangulation", 7), nullptr, args));
    if (!result.Get()) throw std::runtime_error("the game made no map");
    // boxed ValueTuple<RenderTexture, Bounds, int>
    void* texture = Get<void*>(result.Get(), 0x10);
    if (!texture) throw std::runtime_error("the game made no map texture");
    struct Bounds { float v[6]; };
    const Bounds bounds = Get<Bounds>(result.Get(), 0x18);
    const int downscale = Get<int>(result.Get(), 0x30);
    hallMapTexture = std::make_unique<Root>(texture);
    Root infoType(api.type_get_object(api.class_get_type(game::FindClass("LE.dll", "", "MinimapInformation"))));
    void* ia[]{infoType.Get()};
    void* info = Invoke(DoorSignature("ScriptableObject", "CreateInstance", {"System.Type"}), nullptr, ia);
    hallMapInfo = std::make_unique<Root>(info);
    auto* cls = game::FindClass("LE.dll", "", "MinimapInformation");
    const auto set = [&](const char* field, void* value) { api.field_set_value(info, api.class_get_field_from_name(cls, field), value); };
    void* black = Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Texture2D", "get_blackTexture", 0));
    int fogWidth = Value<int>(Method(texture, "get_width", 0), texture), fogHeight = Value<int>(Method(texture, "get_height", 0), texture), noDepth = 0, snorm8 = 9;  // GraphicsFormat.R8_SNorm
    Root fog(api.object_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "RenderTexture")));
    void* fa[]{&fogWidth, &fogHeight, &noDepth, &snorm8};
    Invoke(DoorSignature("RenderTexture", ".ctor", {"System.Int32", "System.Int32", "System.Int32", "UnityEngine.Experimental.Rendering.GraphicsFormat"}), fog.Get(), fa);
    bool yes = true, no = false; int trilinear = 2, clamp = 1;
    void* on[]{&yes}; void* off[]{&no}; void* filter[]{&trilinear}; void* wrap[]{&clamp};
    Invoke(Method(fog.Get(), "set_enableRandomWrite", 1), fog.Get(), on);
    Invoke(Method(fog.Get(), "set_useMipMap", 1), fog.Get(), on);
    Invoke(Method(fog.Get(), "set_autoGenerateMips", 1), fog.Get(), off);
    Invoke(Method(fog.Get(), "set_filterMode", 1), fog.Get(), filter);
    Invoke(Method(fog.Get(), "set_wrapMode", 1), fog.Get(), wrap);
    Invoke(Method(fog.Get(), "Create", 0), fog.Get());
    void* blit[]{black, fog.Get()};
    Invoke(DoorSignature("Graphics", "Blit", {"UnityEngine.Texture", "UnityEngine.RenderTexture"}), nullptr, blit);
    hallMapFog = std::make_unique<Root>(fog.Get());
    set("LiveSdf", texture); set("FoWSdf", fog.Get());
    Bounds b = bounds; set("LevelBounds", &b);
    bool runtime = true; set("Runtime", &runtime);
    int factor = downscale; set("DownscaleFactor", &factor);
    Log("boss hall: map made in %llu ms, %zu triangles", static_cast<unsigned long long>(GetTickCount64() - started), hallFlatIndices.size() / 3);
}
void HideHallMap() {
    if (hallMapShowing) {
        hallMapShowing = false;
        void* hub = hallMapHubInfo ? hallMapHubInfo->Get() : nullptr;
        void* scene = hallMapHubScene ? hallMapHubScene->Get() : nullptr;
        for (void* m : Minimaps())
            if (hallMapInfo && Get<void*>(m, Offset("Minimap", "minimapInformation", "LE.UI.Minimap")) == hallMapInfo->Get() && hub && game::IsAlive(hub)) ShowMap(m, hub, scene);
    }
}
void DropHallMap() {
    std::string why;
    if (!game::Guarded([] {
            HideHallMap();
            if (hallMapTexture && hallMapTexture->Get() && game::IsAlive(hallMapTexture->Get())) {
                Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "RenderTexture", "Release", 0), hallMapTexture->Get());
                void* gone[]{hallMapTexture->Get()}; Invoke(Unity("Object", "Destroy", 1), nullptr, gone);
            }
            if (hallMapFog && hallMapFog->Get() && game::IsAlive(hallMapFog->Get())) {
                Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "RenderTexture", "Release", 0), hallMapFog->Get());
                void* gone[]{hallMapFog->Get()}; Invoke(Unity("Object", "Destroy", 1), nullptr, gone);
            }
            if (hallMapInfo && hallMapInfo->Get() && game::IsAlive(hallMapInfo->Get())) { void* gone[]{hallMapInfo->Get()}; Invoke(Unity("Object", "Destroy", 1), nullptr, gone); }
        }, &why)) Log("boss hall: map not cleared: %s", why.c_str());
    hallMapShowing = false; hallMapFailed = false;
    hallMapInfo.reset(); hallMapTexture.reset(); hallMapFog.reset(); hallMapHubInfo.reset(); hallMapHubScene.reset();
}
// Every tick in the Hub: the map of where the player stands. A failure leaves the map as it was and the hall as it is.
void SyncHallMap(const DoorVec3& me) {
    const bool inside = HallAlive() && InHall(me);
    if (inside == hallMapShowing || hallMapFailed) return;
    std::string why;
    const bool fine = game::Guarded([&] {
        if (!inside) { HideHallMap(); return; }
        if (!hallMapInfo) BakeHallMap();
        const auto infoAt = Offset("Minimap", "minimapInformation", "LE.UI.Minimap"), sceneAt = Offset("Minimap", "sceneDetails", "LE.UI.Minimap");
        const auto minimaps = Minimaps();
        if (minimaps.empty()) return;
        for (void* m : minimaps) {
            void* hub = Get<void*>(m, infoAt); void* scene = Get<void*>(m, sceneAt);
            if (!hallMapHubInfo) { hallMapHubInfo = std::make_unique<Root>(hub); hallMapHubScene = std::make_unique<Root>(scene); }
            ShowMap(m, hallMapInfo->Get(), scene);
        }
        hallMapShowing = true;
    }, &why);
    if (!fine) { hallMapFailed = true; Log("boss hall: no map: %s", why.c_str()); }
}
void DestroyHall() {
    DropHallMap();
    hideout::Clear();
    if (HallAlive()) { void* a[]{hallRoot->Get()}; Invoke(Unity("Object", "Destroy", 1), nullptr, a); }
    if (hallMesh && hallMesh->Get() && game::IsAlive(hallMesh->Get())) { void* gone[]{hallMesh->Get()}; Invoke(Unity("Object", "Destroy", 1), nullptr, gone); }
    hallRoot.reset(); hallSurface.reset(); hallGroundRenderer.reset(); hallLight.reset(); hallMesh.reset();
}
void* NewGameObject(const char* name) {
    auto& api = il2cpp::api();
    void* object = api.object_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject"));
    void* args[]{api.string_new(name)};
    Invoke(DoorSignature("GameObject", ".ctor", {"System.String"}), object, args);
    return object;
}
void* TypeObject(const char* image, const char* ns, const char* name) {
    return il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass(image, ns, name)));
}
// A component kind under an object, on or off: a station's rock shows nothing, a station's look takes no clicks.
void SetChildrenEnabled(void* object, const char* image, const char* kind, bool enabled) {
    bool all = true; Root type(TypeObject(image, "UnityEngine", kind)); void* args[]{type.Get(), &all};
    Root list(Invoke(DoorSignature("GameObject", "GetComponentsInChildren", {"System.Type", "System.Boolean"}), object, args));
    const auto n = list.Get() ? Get<uintptr_t>(list.Get(), 0x18) : 0;
    for (uintptr_t i = 0; i < n; ++i) {
        void* c = Get<void*>(list.Get(), 0x20 + 8 * i); if (!c) continue;
        bool on = enabled; void* a[]{&on}; Invoke(Method(c, "set_enabled", 1), c, a);
    }
}
// The ground: a grid over the shape the player chose, flat inside, falling away into rock outside. The texture mirrors at each repeat, so its
// cuts never show; for that a vertex lies on every fold (the grid's step divides the period).
void HallGround(void* holder, void* material) {
    struct V2 { float u, v; };
    const auto g = hideout::Ground();
    const float reach = hideout::rules::MaxExtent(g.shape, g.size) + hideout::rules::kBand + (g.natural ? 28.f : 4.f);
    constexpr float kStep = 2.f;
    const int cells = static_cast<int>(std::ceil(reach / kStep)) * 2, side = cells + 1;
    const float origin = -kStep * (cells / 2);
    std::vector<float> distance(static_cast<size_t>(side) * side);
    for (int iz = 0; iz < side; ++iz)
        for (int ix = 0; ix < side; ++ix) {
            const auto at = hideout::rules::GridPoint(origin + ix * kStep, origin + iz * kStep, g.natural);
            distance[static_cast<size_t>(iz) * side + ix] = hideout::rules::Distance(g.shape, g.size, at.x, at.z);
        }
    std::vector<int> index(distance.size(), -1);
    std::vector<DoorVec3> v; std::vector<V2> uv; std::vector<int> ids;
    const auto vertex = [&](int ix, int iz) {
        const size_t at = static_cast<size_t>(iz) * side + ix;
        if (index[at] >= 0) return index[at];
        index[at] = static_cast<int>(v.size());
        const float a = origin + ix * kStep, b = origin + iz * kStep;
        const auto spot = hideout::rules::GridPoint(a, b, g.natural);
        // the grid is in hall coordinates: right and up
        const float worldX = spot.x * hallRightX + spot.z * hallUpX, worldZ = spot.x * hallRightZ + spot.z * hallUpZ;
        v.push_back({worldX, hideout::rules::Height(distance[at], kHallRock), worldZ});
        uv.push_back({g.u0 + hideout::rules::Mirrored(a, g.period) * (g.u1 - g.u0), g.v0 + hideout::rules::Mirrored(b, g.period) * (g.v1 - g.v0)});
        return index[at];
    };
    const float outside = -hideout::rules::kBand;
    // winding as seen from above, whichever way the camera turns the grid
    const bool flip = hallRightX * hallUpZ - hallRightZ * hallUpX < 0;
    for (int iz = 0; iz < cells; ++iz)
        for (int ix = 0; ix < cells; ++ix) {
            const float d[]{distance[static_cast<size_t>(iz) * side + ix], distance[static_cast<size_t>(iz) * side + ix + 1], distance[static_cast<size_t>(iz + 1) * side + ix], distance[static_cast<size_t>(iz + 1) * side + ix + 1]};
            if (*std::max_element(d, d + 4) < outside) continue;
            const int a0 = vertex(ix, iz), a1 = vertex(ix + 1, iz), b0 = vertex(ix, iz + 1), b1 = vertex(ix + 1, iz + 1);
            if (flip) { ids.insert(ids.end(), {a0, a1, b0, a1, b1, b0}); }
            else { ids.insert(ids.end(), {a0, b0, a1, a1, b0, b1}); }
        }
    auto& api = il2cpp::api();
    Root vertexArray(api.array_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Vector3"), v.size()));
    std::memcpy(static_cast<char*>(vertexArray.Get()) + 0x20, v.data(), v.size() * sizeof(DoorVec3));
    Root uvArray(api.array_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Vector2"), uv.size()));
    std::memcpy(static_cast<char*>(uvArray.Get()) + 0x20, uv.data(), uv.size() * sizeof(V2));
    Root indexArray(api.array_new(game::FindClass("mscorlib.dll", "System", "Int32"), ids.size()));
    std::memcpy(static_cast<char*>(indexArray.Get()) + 0x20, ids.data(), ids.size() * sizeof(int));
    Root mesh(api.object_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Mesh")));
    Invoke(DoorSignature("Mesh", ".ctor", {}), mesh.Get());
    int bits = 1; void* fa[]{&bits}; Invoke(Method(mesh.Get(), "set_indexFormat", 1), mesh.Get(), fa);  // 32 bit
    void* a1[]{vertexArray.Get()}; Invoke(DoorSignature("Mesh", "set_vertices", {"UnityEngine.Vector3[]"}), mesh.Get(), a1);
    void* a2[]{uvArray.Get()}; Invoke(DoorSignature("Mesh", "set_uv", {"UnityEngine.Vector2[]"}), mesh.Get(), a2);
    void* a3[]{indexArray.Get()}; Invoke(DoorSignature("Mesh", "set_triangles", {"System.Int32[]"}), mesh.Get(), a3);
    // The falling sides are rock, not the floor pattern stretched down a wall.
    int submeshes=2;void* sc[]{&submeshes};Invoke(Method(mesh.Get(),"set_subMeshCount",1),mesh.Get(),sc);
    std::vector<int> surface,cliffs;
    for(size_t t=0;t+2<ids.size();t+=3) {
        auto& part=v[ids[t]].y<-.001f || v[ids[t+1]].y<-.001f || v[ids[t+2]].y<-.001f?cliffs:surface;
        part.insert(part.end(),{ids[t],ids[t+1],ids[t+2]});
    }
    const auto* setTriangles=DoorSignature("Mesh","SetTriangles",{"System.Int32[]","System.Int32"});
    for(int submesh=0;submesh<2;++submesh){const auto& part=submesh?cliffs:surface;Root array(api.array_new(game::FindClass("mscorlib.dll","System","Int32"),part.size()));std::memcpy(static_cast<char*>(array.Get())+0x20,part.data(),part.size()*sizeof(int));void* a[]{array.Get(),&submesh};Invoke(setTriangles,mesh.Get(),a);}
    Invoke(Method(mesh.Get(), "RecalculateNormals", 0), mesh.Get());
    Invoke(Method(mesh.Get(), "RecalculateTangents", 0), mesh.Get());
    Invoke(Method(mesh.Get(), "RecalculateBounds", 0), mesh.Get());
    // the map's ground: the flat part alone, its cells cut along the outline so its edge is smooth
    hallFlatVertices.clear(); hallFlatIndices.clear();
    {
        constexpr float kMapStep = 1.5f;
        std::map<std::pair<int, int>, int> known;
        const auto mapVertex = [&](const hideout::rules::Point& p) {
            const std::pair<int, int> key{static_cast<int>(std::lround(p.x * 200)), static_cast<int>(std::lround(p.z * 200))};
            const auto it = known.find(key);
            if (it != known.end()) return it->second;
            const int at = static_cast<int>(hallFlatVertices.size());
            hallFlatVertices.push_back({p.x * hallRightX + p.z * hallUpX + kHallCentre.x, kHallCentre.y, p.x * hallRightZ + p.z * hallUpZ + kHallCentre.z});
            known.emplace(key, at);
            return at;
        };
        const int half = static_cast<int>(std::ceil((hideout::rules::MaxExtent(g.shape, g.size) + 3) / kMapStep));
        for (int iz = -half; iz < half; ++iz)
            for (int ix = -half; ix < half; ++ix) {
                const hideout::rules::Point corner[4]{{ix * kMapStep, iz * kMapStep}, {(ix + 1) * kMapStep, iz * kMapStep}, {(ix + 1) * kMapStep, (iz + 1) * kMapStep}, {ix * kMapStep, (iz + 1) * kMapStep}};
                float d[4];
                for (int k = 0; k < 4; ++k) d[k] = hideout::rules::Distance(g.shape, g.size, corner[k].x, corner[k].z);
                hideout::rules::Point polygon[8];
                const int n = hideout::rules::ClipCell(corner, d, polygon);
                for (int k = 1; k + 1 < n; ++k) {
                    const int p0 = mapVertex(polygon[0]), p1 = mapVertex(polygon[k]), p2 = mapVertex(polygon[k + 1]);
                    if (p0 == p1 || p1 == p2 || p0 == p2) continue;
                    if (flip) hallFlatIndices.insert(hallFlatIndices.end(), {p0, p1, p2});
                    else hallFlatIndices.insert(hallFlatIndices.end(), {p0, p2, p1});
                }
            }
    }
    hallMesh = std::make_unique<Root>(mesh.Get());  // a mesh is not freed with its object: the hall destroys it itself
    void* ground = NewGameObject("EP hall ground");
    Root parent(Invoke(Method(holder, "get_transform", 0), holder));
    Root tr(Invoke(Method(ground, "get_transform", 0), ground));
    void* pa[]{parent.Get()}; Invoke(DoorSignature("Transform", "SetParent", {"UnityEngine.Transform"}), tr.Get(), pa);
    DoorVec3 at = kHallCentre; void* place[]{&at}; Invoke(Unity("Transform", "set_position", 1), tr.Get(), place);
    const auto* add = DoorSignature("GameObject", "AddComponent", {"System.Type"});
    Root filterType(TypeObject("UnityEngine.CoreModule.dll", "UnityEngine", "MeshFilter"));
    void* ft[]{filterType.Get()}; void* filter = Invoke(add, ground, ft);
    void* m1[]{mesh.Get()}; Invoke(Method(filter, "set_sharedMesh", 1), filter, m1);
    Root rendererType(TypeObject("UnityEngine.CoreModule.dll", "UnityEngine", "MeshRenderer"));
    void* rt[]{rendererType.Get()}; void* renderer = Invoke(add, ground, rt);
    hallGroundRenderer = std::make_unique<Root>(renderer);
    Root materials(api.array_new(game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","Material"),2));
    void* cliffMaterial=hideout::CliffMaterial();if(!cliffMaterial)cliffMaterial=material;
    const auto* setValue=DoorSignature("Array","SetValue",{"System.Object","System.Int32"},"System","mscorlib.dll");
    for(int slot=0;slot<2;++slot){void* a[]{slot?cliffMaterial:material,&slot};Invoke(setValue,materials.Get(),a);}
    void* m2[]{materials.Get()}; Invoke(Method(renderer, "set_sharedMaterials", 1), renderer, m2);
    Root colliderType(TypeObject("UnityEngine.PhysicsModule.dll", "UnityEngine", "MeshCollider"));
    void* ct[]{colliderType.Get()}; void* collider = Invoke(add, ground, ct);
    if (!collider) throw std::runtime_error("no collider");
    void* m3[]{mesh.Get()}; Invoke(Method(collider, "set_sharedMesh", 1), collider, m3);
    // a light of its own, high above the middle: some grounds are dark under the Hub's sky
    void* lamp = NewGameObject("EP hall light");
    Root lampTr(Invoke(Method(lamp, "get_transform", 0), lamp));
    Invoke(DoorSignature("Transform", "SetParent", {"UnityEngine.Transform"}), lampTr.Get(), pa);
    DoorVec3 high{kHallCentre.x, kHallCentre.y + 45.f, kHallCentre.z}; void* lp[]{&high}; Invoke(Unity("Transform", "set_position", 1), lampTr.Get(), lp);
    Root lightType(TypeObject("UnityEngine.CoreModule.dll", "UnityEngine", "Light"));
    void* lt[]{lightType.Get()}; void* light = Invoke(add, lamp, lt);
    hallLight = std::make_unique<Root>(light);
    int point = 2; void* la[]{&point}; Invoke(Method(light, "set_type", 1), light, la);
    float range = reach * 2.f + 60.f; void* lr[]{&range}; Invoke(Method(light, "set_range", 1), light, lr);
    float strength = g.light; void* li[]{&strength}; Invoke(Method(light, "set_intensity", 1), light, li);
}
void HallNavMesh(void* holder) {
    void* carrier = NewGameObject("EP hall surface");
    Root parent(Invoke(Method(holder, "get_transform", 0), holder));
    Root tr(Invoke(Method(carrier, "get_transform", 0), carrier));
    void* pa[]{parent.Get()}; Invoke(DoorSignature("Transform", "SetParent", {"UnityEngine.Transform"}), tr.Get(), pa);
    DoorVec3 at = HallCentre(); void* place[]{&at}; Invoke(Unity("Transform", "set_position", 1), tr.Get(), place);
    Root surfaceType(TypeObject("Unity.AI.Navigation.dll", "Unity.AI.Navigation", "NavMeshSurface"));
    void* ta[]{surfaceType.Get()};
    void* surface = Invoke(DoorSignature("GameObject", "AddComponent", {"System.Type"}), carrier, ta);
    if (!surface) throw std::runtime_error("no NavMeshSurface");
    int collect = 1; void* a1[]{&collect}; Invoke(Method(surface, "set_collectObjects", 1), surface, a1);  // Volume
    int physics = 1; void* a2[]{&physics}; Invoke(Method(surface, "set_useGeometry", 1), surface, a2);     // colliders
    const auto g = hideout::Ground();
    const float across = (hideout::rules::MaxExtent(g.shape, g.size) + 6) * 2;
    DoorVec3 s{across, 30.f, across}; void* a3[]{&s}; Invoke(Method(surface, "set_size", 1), surface, a3);
    DoorVec3 zero{}; void* a4[]{&zero}; Invoke(Method(surface, "set_center", 1), surface, a4);
    Invoke(Method(surface, "BuildNavMesh", 0), surface);
    hallSurface = std::make_unique<Root>(surface);
}
void BuildHall() {
    DestroyHall();
    const uint64_t started = GetTickCount64();
    HallReadCamera();
    void* material = hideout::GroundMaterial();
    if (!material) throw std::runtime_error("the Hub has no ground material");
    void* holder = NewGameObject("EpochPact Boss Hall");
    hallRoot = std::make_unique<Root>(holder);
    hideout::Populate(holder);
    HallGround(holder, material);
    HallNavMesh(holder);
    DoorVec3 walk{}; if (!SampleWalkable(HallCentre(), 6.f, &walk)) throw std::runtime_error("the hall's middle is not walkable");
#ifdef EPOCHPACT_TESTING
    ++hallBuildCount;
#endif
    Log("boss hall: built in %llu ms", static_cast<unsigned long long>(GetTickCount64() - started));
}
// A station's invisible click target follows the visible model's actual bounds.
// Updating it does not replace any door or NPC instance.
void* AlignStationProxy(void* root,void* listener,const hideout::StationBounds& b,void* box){
    void* sender=Invoke(Unity("Component","get_gameObject",0),listener);
    Root tr(Invoke(Unity("GameObject","get_transform",0),sender));bool no=false;
    void* parent[]{root,&no};Invoke(DoorSignature("Transform","SetParent",{"UnityEngine.Transform","System.Boolean"}),tr.Get(),parent);
    hideout::rules::Rotation identity{0,0,0,1};DoorVec3 unit{1,1,1},foot{b.x,HallGroundY(),b.z};
    void* r[]{&identity},*u[]{&unit},*p[]{&foot};Invoke(Unity("Transform","set_rotation",1),tr.Get(),r);Invoke(Unity("Transform","set_localScale",1),tr.Get(),u);Invoke(Unity("Transform","set_position",1),tr.Get(),p);
    if(!box){Root type(TypeObject("UnityEngine.PhysicsModule.dll","UnityEngine","BoxCollider"));void* a[]{type.Get()};box=Invoke(DoorSignature("GameObject","AddComponent",{"System.Type"}),sender,a);}
    const auto scale=Value<DoorVec3>(Unity("Transform","get_lossyScale",0),tr.Get());
    DoorVec3 centre{0,(b.y-foot.y)/std::max(.001f,std::abs(scale.y)),0};
    DoorVec3 size{std::max(.35f,b.width)/std::max(.001f,std::abs(scale.x)),std::max(.35f,b.height)/std::max(.001f,std::abs(scale.y)),std::max(.35f,b.depth)/std::max(.001f,std::abs(scale.z))};
    void* c[]{&centre},*z[]{&size};Invoke(game::FindMethod("UnityEngine.PhysicsModule.dll","UnityEngine","BoxCollider","set_center",1),box,c);Invoke(game::FindMethod("UnityEngine.PhysicsModule.dll","UnityEngine","BoxCollider","set_size",1),box,z);
    *reinterpret_cast<bool*>(static_cast<char*>(listener)+Offset("WorldObjectClickListener","useOtherTransformForPathfinding"))=false;
    return box;
}
bool SameStationBounds(const hideout::StationBounds& a,const hideout::StationBounds& b){
    return a.x==b.x&&a.y==b.y&&a.z==b.z&&a.width==b.width&&a.height==b.height&&a.depth==b.depth;
}
bool SyncHallDoors(){
    if(!HallAlive()||!DoorsAlive())return false;
    const auto wanted=hideout::Doors();
    if(doors.size()!=wanted.size()+1)return false;
    std::set<int> actions;
    // Validate membership and ownership before modifying any live transform.
    for(const auto& s:wanted){
        if(!actions.insert(s.action).second)return false;
        const auto d=std::find_if(doors.begin(),doors.end(),[&](const Door& d){return d.action==s.action;});
        if(d==doors.end()||!d->inHall||d->hasLook!=s.hasLook||!d->listener||!game::IsAlive(d->listener->Get()))return false;
    }
    if(std::none_of(doors.begin(),doors.end(),[](const Door& d){return d.action==kHallDoor&&!d.inHall;}))return false;
    for(const auto& s:wanted){
        auto& d=*std::find_if(doors.begin(),doors.end(),[&](const Door& d){return d.action==s.action;});
        const auto at=HallPlace(s.right,s.up);
        const bool moved=d.authoredX!=at.x||d.authoredZ!=at.z||d.yaw!=s.yaw||d.size!=s.size||d.lift!=s.lift;
        Root tr(Invoke(Unity("GameObject","get_transform",0),d.object->Get()));
        if(moved){
            const auto p=rules::ResolveDoorPosition(true,s.hasLook,{at.x,HallGroundY(),at.z},{},false,s.lift);DoorVec3 position{p.x,p.y,p.z};void* a[]{&position};Invoke(Unity("Transform","set_position",1),tr.Get(),a);
            const float f=s.hasLook?1:s.size;DoorVec3 scale{.55f*f,.55f*f,.55f*f};void* z[]{&scale};Invoke(Unity("Transform","set_localScale",1),tr.Get(),z);
            auto q=hideout::rules::YawOnBase(s.yaw,{0,0,0,1});void* r[]{&q};Invoke(Unity("Transform","set_rotation",1),tr.Get(),r);
            d.authoredX=at.x;d.authoredZ=at.z;d.yaw=s.yaw;d.size=s.size;d.lift=s.lift;
#ifdef EPOCHPACT_TESTING
            ++doorPoseUpdateCount;
#endif
        }
        hideout::StationBounds b{};
        if(s.hasLook&&hideout::GetStationBounds(s.action,b)&&(!d.stationBoundsKnown||moved||!SameStationBounds(d.stationBounds,b))){
            void* box=AlignStationProxy(tr.Get(),d.listener->Get(),b,d.stationBox?d.stationBox->Get():nullptr);
            if(!d.stationBox)d.stationBox=std::make_unique<Root>(box);d.stationBounds=b;d.stationBoundsKnown=true;
            if(d.label){DoorVec3 offset{0,b.y+b.height/2-HallGroundY()+.5f,0};*reinterpret_cast<DoorVec3*>(static_cast<char*>(d.label->Get())+Offset("HoverTagOnMouseOver","offset"))=offset;}
        }
    }
#ifdef EPOCHPACT_TESTING
    ++doorSyncCount;
#endif
    return true;
}
// Copies of the Hub's Fall of the Outcasts rock (MonolithObject), smaller, without its quest pulses and map icon.
void BuildDoors() {
    if(SyncHallDoors())return;
    ClearDoors();
    bool hall = false;
    if (!hallBroken) {
        try { if (!HallAlive()) BuildHall(); hall = true; }
        catch (const std::exception& e) { hallBroken = true; Log("boss hall: could not be built, the doors stay in the Hub: %s", e.what()); try { DestroyHall(); } catch (...) {} }
    }
    const auto* cls = game::FindClass("LE.dll", "", "OpenMonolithPanelInteraction");
    Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls)));
    bool inactive = true; void* findArgs[]{type.Get(), &inactive};
    Root rocks(Invoke(Unity("Object", "FindObjectsOfType", 2), nullptr, findArgs));
    void* rock = nullptr;
    const auto n = rocks.Get() ? Get<uintptr_t>(rocks.Get(), 0x18) : 0;
    for (uintptr_t i = 0; i < n; ++i) {
        void* o = Get<void*>(rocks.Get(), 0x20 + 8 * i);
        if(o){void* ancestor=Invoke(Unity("Component","get_transform",0),o);bool ours=false;
            for(int depth=0;ancestor&&depth<32;++depth){void* go=Invoke(Unity("Component","get_gameObject",0),ancestor);const auto name=Text(Invoke(Unity("Object","get_name",0),go));
                if(name.rfind("EP ",0)==0||name.rfind("EpochPact ",0)==0){ours=true;break;}ancestor=Invoke(Unity("Transform","get_parent",0),ancestor);}
            if(ours)continue;}
        if (o && Get<uint8_t>(o, Offset("OpenMonolithPanelInteraction", "timelineID")) == 1 && !Get<bool>(o, Offset("OpenMonolithPanelInteraction", "isRestZoneRock")) && PlaceOf(o).z > 500) rock = o;
    }
    if (!rock) throw std::runtime_error("the Hub's rock was not found");
    Root handler(Invoke(Unity("Component", "get_transform", 0), rock));
    void* logic = Invoke(Unity("Transform", "get_parent", 0), handler.Get());
    void* source = logic ? Invoke(Unity("Transform", "get_parent", 0), logic) : nullptr;
    void* parent = source ? Invoke(Unity("Transform", "get_parent", 0), source) : nullptr;
    if (!source) throw std::runtime_error("the Hub's rock has no holder");
    void* sourceObject = Invoke(Unity("Component", "get_gameObject", 0), source);
    const auto* instantiate = DoorSignature("Object", "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform", "System.Boolean"});
    const auto* find = DoorSignature("Transform", "Find", {"System.String"});
    const auto* componentIn = DoorSignature("GameObject", "GetComponentInChildren", {"System.Type", "System.Boolean"});
    const auto* add = DoorSignature("GameObject", "AddComponent", {"System.Type"});
    Root listenerType(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("LE.dll", "", "WorldObjectClickListener"))));
    Root tagType(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("LE.dll", "", "HoverTagOnMouseOver"))));
    struct Spot { float x, z; int action; bool inHall; bool hasLook = false; float yaw=0,size=1,lift=0; };
    std::vector<Spot> spots;
    if (hall) {
        // two rows of five, the difficulty and the Rush between them, the way out behind them; the way in stands by the Hub's waypoint
        // the list is the player's; the way in stands by the Hub's waypoint
        for (const auto& door : hideout::Doors()) {
            const DoorVec3 at = HallPlace(door.right, door.up);
            spots.push_back({at.x, at.z, door.action, true, door.hasLook,door.yaw,door.size,door.lift});
        }
        spots.insert(spots.begin() + std::min<size_t>(12, spots.size()), {-7.5f, 1039, kHallDoor, false, false});
    } else {
        spots = {{-20, 1046, 1, false}, {-15, 1046, 2, false}, {-10, 1046, 3, false}, {-5, 1046, 4, false}, {-20, 1033, 5, false}, {-15, 1033, 6, false}, {-10, 1033, 7, false}, {-5, 1033, 8, false},
                 {6, 1033, 9, false}, {11, 1033, 10, false}, {-25, 1039, kDifficultyDoor, false}, {-7.5f, 1039, kRushDoor, false}};
    }
    doorsExpected = spots.size();
    std::unique_ptr<Root> hallParent;
    if (hall) hallParent = std::make_unique<Root>(Invoke(Method(hallRoot->Get(), "get_transform", 0), hallRoot->Get()));
    for (const auto& s : spots) {
        bool world = true;
        void* args[]{sourceObject, s.inHall ? hallParent->Get() : parent, &world};
        Root copy(Invoke(instantiate, nullptr, args));
        void* cloneName=il2cpp::api().string_new("EP boss door");void* rename[]{cloneName};Invoke(Unity("Object","set_name",1),copy.Get(),rename);
        Root tr(Invoke(Method(copy.Get(), "get_transform", 0), copy.Get()));
        DoorVec3 authored{s.x,s.inHall?HallGroundY():2,s.z},snapped{};
        const bool snappedToHub=!s.inHall&&SampleWalkable(authored,8.f,&snapped);
        const auto p=rules::ResolveDoorPosition(s.inHall,s.hasLook,{authored.x,authored.y,authored.z},{snapped.x,snapped.y,snapped.z},snappedToHub,s.lift);
        DoorVec3 at{p.x,p.y,p.z};
        void* place[]{&at}; Invoke(Unity("Transform", "set_position", 1), tr.Get(), place);
        const float factor=s.inHall && !s.hasLook?s.size:1;
        DoorVec3 scale{.55f*factor, .55f*factor, .55f*factor}; void* sized[]{&scale}; Invoke(Unity("Transform", "set_localScale", 1), tr.Get(), sized);
        if(s.inHall){
            // The native map instance has a -90-degree animated root beneath
            // a +90-degree import frame. Preserve the stone arch but freeze and
            // stand its separate crystal assembly up in the same doorway.
            SetChildrenEnabled(copy.Get(),"UnityEngine.AnimationModule.dll","Animator",false);
            SetChildrenEnabled(copy.Get(),"UnityEngine.AnimationModule.dll","Animation",false);
            void* path[]{il2cpp::api().string_new("Visuals/Monolith_Stone/Monolith_Stone_GEO/root")};
            if(void* crystal=Invoke(find,tr.Get(),path)) {
                hideout::rules::Rotation upright{1,0,0,0};void* rotation[]{&upright};Invoke(Unity("Transform","set_localRotation",1),crystal,rotation);
            }
            auto q=hideout::rules::YawOnBase(s.yaw,{0,0,0,1});void* r[]{&q};Invoke(Unity("Transform","set_rotation",1),tr.Get(),r);
        }
        for (const char* child : {"Pulse Holder", "DMMap_WorldIcon - Waypoint"}) {
            void* name = il2cpp::api().string_new(child); void* fa[]{name};
            void* found = Invoke(find, tr.Get(), fa);
            if (found) { void* go = Invoke(Unity("Component", "get_gameObject", 0), found); void* da[]{go}; Invoke(Unity("Object", "Destroy", 1), nullptr, da); }
        }
        bool inactiveToo = true; void* la[]{listenerType.Get(), &inactiveToo};
        void* listener = Invoke(componentIn, copy.Get(), la);
        if (!listener) throw std::runtime_error("the rock copy has no click listener");
        if (s.hasLook) {
            SetChildrenEnabled(copy.Get(), "UnityEngine.CoreModule.dll", "Renderer", false);
            SetChildrenEnabled(copy.Get(), "UnityEngine.CoreModule.dll", "Light", false);
            SetChildrenEnabled(copy.Get(), "UnityEngine.PhysicsModule.dll", "Collider", false);
            // A disabled renderer can be re-enabled by the copied rock's hover
            // VFX. Deactivate its visual branch, leaving only the click logic.
            void* path[]{il2cpp::api().string_new("Visuals")};
            if(void* visual=Invoke(find,tr.Get(),path)){
                bool no=false;void* a[]{&no};Invoke(Unity("GameObject","SetActive",1),Invoke(Unity("Component","get_gameObject",0),visual),a);
            }
        }
        void* senderObject = Invoke(Unity("Component", "get_gameObject", 0), listener);
        float labelHeight=3;void* stationBox=nullptr;hideout::StationBounds stationBounds{};bool stationBoundsKnown=false;
        if(s.hasLook&&hideout::GetStationBounds(s.action,stationBounds)){
            stationBox=AlignStationProxy(tr.Get(),listener,stationBounds,nullptr);stationBoundsKnown=true;
            labelHeight=stationBounds.y+stationBounds.height/2-HallGroundY()+.5f;
        }
        void* ta[]{tagType.Get()};
        void* tag = Invoke(add, senderObject, ta);
        Door door{std::make_unique<Root>(copy.Get()), std::make_unique<Root>(listener), tag ? std::make_unique<Root>(tag) : nullptr, s.action};
        if(stationBox)door.stationBox=std::make_unique<Root>(stationBox);
        door.authoredX=s.x;door.authoredZ=s.z;door.yaw=s.yaw;door.size=s.size;door.lift=s.lift;door.inHall=s.inHall;door.hasLook=s.hasLook;
        door.stationBounds=stationBounds;door.stationBoundsKnown=stationBoundsKnown;
        if (tag) {
            *reinterpret_cast<bool*>(static_cast<char*>(tag) + Offset("HoverTagOnMouseOver", "alwaysOn")) = !s.inHall || s.hasLook;
            DoorVec3 offset{0, labelHeight, 0}; *reinterpret_cast<DoorVec3*>(static_cast<char*>(tag) + Offset("HoverTagOnMouseOver", "offset")) = offset;
        }
        doors.push_back(std::move(door));
    }
    RefreshDoorLabels();
#ifdef EPOCHPACT_TESTING
    ++doorBuildCount;
#endif
    Log("boss doors: %zu doors in the Monolith Hub", doors.size());
}
// What a door borrows from a run. Kept in a file too, so a game closed during a boss gets its run back at the next start.
struct BorrowedRun { std::string id; int timeline, difficulty, stability, quest, branch, gaze, last, completed, lost; };
std::vector<BorrowedRun> borrowed;
std::filesystem::path BorrowedFile() { return std::filesystem::path(PluginDir()) / L"bossdoors-borrowed.txt"; }
void SaveBorrowed() {
    std::error_code ignored;
    if (borrowed.empty()) { std::filesystem::remove(BorrowedFile(), ignored); return; }
    std::ofstream out(BorrowedFile(), std::ios::trunc);
    for (const auto& b : borrowed)
        out << b.id << ' ' << b.timeline << ' ' << b.difficulty << ' ' << b.stability << ' ' << b.quest << ' ' << b.branch << ' ' << b.gaze << ' ' << b.last << ' ' << b.completed << ' ' << b.lost << '\n';
}
void LoadBorrowed() {
    borrowed.clear();
    std::ifstream in(BorrowedFile());
    BorrowedRun b;
    while (in >> b.id >> b.timeline >> b.difficulty >> b.stability >> b.quest >> b.branch >> b.gaze >> b.last >> b.completed >> b.lost) borrowed.push_back(b);
}
int& RunInt(void* run, const char* field) { return *reinterpret_cast<int*>(static_cast<char*>(run) + Offset("MonolithRun", field)); }
bool& RunFlag(void* run, const char* field) { return *reinterpret_cast<bool*>(static_cast<char*>(run) + Offset("MonolithRun", field)); }
int& RunGaze(void* web) { return *reinterpret_cast<int*>(static_cast<char*>(web) + Offset("EchoWeb", "gazeOfOrobyss")); }
void Borrow(const Context& c, void* run, int timeline, int difficulty) {
    void* web = Get<void*>(run, Offset("MonolithRun", "web"));
    borrowed.push_back({c.id, timeline, difficulty, RunInt(run, "stability"), RunInt(run, "questCompletion"), RunInt(run, "questBranch"), web ? RunGaze(web) : 0,
                        RunFlag(run, "previousEchoWasQuestEcho"), RunFlag(run, "previousQuestEchoWasCompleted"), RunFlag(run, "previousQuestAttemptLostStability")});
    SaveBorrowed();
}
// The player's runs as they were before their doors. Only outside an echo: the game reads these values during one.
void RestoreBorrowed(const Context& c) {
    bool changed = false;
    for (auto it = borrowed.begin(); it != borrowed.end();) {
        if (it->id != c.id) { ++it; continue; }
        const auto t = Find(it->timeline, it->difficulty);
        if (void* run = GetRun(c, t.id, it->difficulty)) {
            RunInt(run, "stability") = it->stability; RunInt(run, "questCompletion") = it->quest; RunInt(run, "questBranch") = it->branch;
            RunFlag(run, "previousEchoWasQuestEcho") = it->last != 0; RunFlag(run, "previousQuestEchoWasCompleted") = it->completed != 0; RunFlag(run, "previousQuestAttemptLostStability") = it->lost != 0;
            if (void* web = Get<void*>(run, Offset("MonolithRun", "web"))) RunGaze(web) = it->gaze;
            Log("boss doors: timeline %d keeps its Stability %d and quest progress %d", it->timeline, it->stability, it->quest);
        }
        it = borrowed.erase(it); changed = true;
    }
    if (changed) { SaveBorrowed(); Save(c); }
}
void DoorClicked(Door& d,void* source,bool click) {
        DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
        bool foreground=foregroundPid==GetCurrentProcessId();
#ifdef EPOCHPACT_TESTING
        foreground=foreground||testDoorForeground;
#endif
        const bool offline=game::IsOfflinePlay(),editing=hideout::Editing(),menu=ingame::view::WindowOpen();
        bool local=false;
        if(click&&source&&offline&&foreground&&!editing&&!menu){
            void* actor=Invoke(m_actor);
            local=actor&&source==Invoke(Unity("Component","get_gameObject",0),actor);
        }
        if(!rules::DoorInput(click,local,offline,foreground,editing,menu))return;
        const uint64_t clicked = GetTickCount64();
        const bool held = clicked - d.lastClick < kDoorHeldMs;
        d.lastClick = clicked;
        if (held) return;
        if (d.action == kHallDoor) { QueueHallMove(Current(),1); return; }
        if (d.action == kHubDoor) { QueueHallMove(Current(),2); return; }
        if (d.action == kStashStation) { hallStation = "stash"; return; }
        if (d.action == kVendorStation) { hallStation = "vendor"; return; }
        if (d.action == kCacheStation) { hallStation = "cache"; return; }
        if (d.action == kDifficultyDoor) {
            if (!bossDifficulty) bossDifficulty = 1;
            else if (!bossHarbinger) bossHarbinger = true;
            else { bossDifficulty = 0; bossHarbinger = false; }
            RefreshDoorLabels(); return;
        }
        if (d.action == kRushDoor) {
            if (rushDifficulty != bossDifficulty || rushHarbinger != bossHarbinger) rushAt = 1;
            rushOn = true; rushDifficulty = bossDifficulty; rushHarbinger = bossHarbinger;
            pendingBoss = rushAt; pendingDifficulty = rushDifficulty; pendingHarbinger = rushHarbinger;
        } else {
            rushOn = false; pendingBoss = d.action; pendingDifficulty = bossDifficulty; pendingHarbinger = bossHarbinger;
        }
        bossTravelAt = 0;
}
void ObjectClickDetour(void* self, void* source, bool flag, const il2cpp::Method* method) {
    Door* ours=nullptr;
    for(auto& d:doors)if(d.listener&&d.listener->Get()==self){ours=&d;break;}
    if(!ours){originalObjectClick(self,source,flag,method);return;}
    // Owned copies never fall through to the borrowed rock's interaction,
    // including when a guard fails or the local actor is not ready.
    game::Guarded([&] { DoorClicked(*ours,source,flag); }, nullptr);
#ifdef EPOCHPACT_TESTING
    if(ours)TraceDoorInput(self,flag);
#endif
}
// The boss echo through the game's own start, whatever the timeline's lock and quest progress say.
// The run as the game's start and its boss need it; its own values are borrowed. Returns the boss's quest echo.
int PrepareBossRun(const Context& c, int timeline, int difficulty) {
    const auto t = Find(timeline, difficulty);
    void* run = GetRun(c, t.id, difficulty);
    if (!run) { auto tid = t.id; void* a[]{&tid, &difficulty}; run = Invoke(Method(c.runs, "getNewRun", 2), c.runs, a); }
    if (!run) throw std::runtime_error("the timeline has no run");
    const auto zones = QuestZones(t.object);
    if (zones.empty()) throw std::runtime_error("the timeline has no boss echo");
    Borrow(c, run, timeline, difficulty);
    *reinterpret_cast<int*>(static_cast<char*>(run) + Offset("MonolithRun", "questCompletion")) = zones.back().required;
    *reinterpret_cast<int*>(static_cast<char*>(run) + runStability) = Value<int>(m_max, run);
    if (zones.back().needsBranch) *reinterpret_cast<int*>(static_cast<char*>(run) + Offset("MonolithRun", "questBranch")) = zones.back().branch;
    bossRequired = zones.back().required;
    return static_cast<int>(zones.size()) - 1;
}
void StartBoss(const Context& c, int timeline, int difficulty, bool chosenHarbinger) {
    int boss = PrepareBossRun(c, timeline, difficulty);
    const auto t = Find(timeline, difficulty);
    bossRunHarbinger = chosenHarbinger && difficulty == 1;
    void* sync = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getLocalActorSync", 0));
    void* user = Invoke(game::FindMethod("LE.dll", "LE.Networking", "PlayerStore", "get_LocalUserIdentity", 0));
    auto tid = t.id; bool harbinger = bossRunHarbinger;
    void* startArgs[]{sync, user, &tid, &difficulty, &boss, &harbinger};
    bossTimeline = timeline; bossRunDifficulty = difficulty; bossStarting = true;
    struct Started { ~Started() { bossStarting = false; } } started;
    bossTracked = true;
    Invoke(m_startQuest, nullptr, startArgs);
    ++bossStarts; bossDied = false; bossWarped = false; bossArrived = 0;
    bossKilled = false; bossPortalUsed = false; bossPortalMade = false;
    Log("boss doors: timeline %d %s%s started", timeline, DifficultyName(difficulty).c_str(), bossRunHarbinger ? " with its Harbinger" : "");
}
// Bosses a player would walk a long way to: to 8 units before the strongest fighter of the scene, or a known place.
void WalkToBoss(const Context& c, int timeline) {
    // arenas whose boss appears only when someone comes near
    struct Known { int timeline; DoorVec3 at; };
    static const Known known[]{{1, {0, 0, 6}}, {3, {-52, 0, 0}}, {4, {18, 0, 0}}, {8, {0, 0, 0}}, {9, {-25, 0, 15}}};  // 3: its boss sleeps until the guards before it fall
    for (const auto& k : known) if (k.timeline == timeline) {
        DoorVec3 snapped{};
        if (SampleWalkable(k.at, 6.f, &snapped)) Warp(c.actor, snapped);
        return;
    }
    const auto* cls = game::FindClass("LE.dll", "", "Actor");
    Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls)));
    bool inactive = true; void* args[]{type.Get(), &inactive};
    Root actors(Invoke(Unity("Object", "FindObjectsOfType", 2), nullptr, args));
    const auto me = PlaceOf(c.actor);
    void* best = nullptr; float bestHp = 20000;
    const auto n = actors.Get() ? Get<uintptr_t>(actors.Get(), 0x18) : 0;
    for (uintptr_t i = 0; i < n; ++i) {
        void* a = Get<void*>(actors.Get(), 0x20 + 8 * i);
        if (!a || a == c.actor) continue;
        void* h = Get<void*>(a, Offset("Actor", "health"));
        if (!h) continue;
        const float hp = Get<float>(h, Offset("BaseHealth", "currentHealth"));
        const std::string name = Text(Invoke(Unity("Object", "get_name", 0), a));
        if (hp > bestHp && name.find("Generator") == std::string::npos) { bestHp = hp; best = a; }
    }
    if (!best) return;
    const auto at = PlaceOf(best);
    const float dx = at.x - me.x, dz = at.z - me.z, d = std::sqrt(dx * dx + dz * dz);
    if (d <= 15) return;
    DoorVec3 target{at.x - dx / d * 8, at.y, at.z - dz / d * 8}, snapped{};
    if (SampleWalkable(target, 6.f, &snapped)) Warp(c.actor, snapped);
}
// A kill ends the run's quest (MonolithRunsManager.completeRun clears questCompletion).
bool BossRunDone(const Context& c) {
    void* run = GetRun(c, Find(bossTimeline, bossRunDifficulty).id, bossRunDifficulty);
    return !run || Get<int>(run, Offset("MonolithRun", "questCompletion")) != bossRequired;
}
// The way on: the game's portal service, as the player's own portal is made but of type Echo. It holds one portal per player,
// so the player's own portal key replaces this one and goes to town. Position: nothing given, next to the player.
void OpenBossPortal(const Context& c) {
    void* service = Invoke(game::FindMethod("LE.dll", "LE.Services", "ServiceProvider", "get_PortalService", 0));
    if (!service) throw std::runtime_error("no portal service");
    uint8_t type = kPortalEcho;
    void* sync = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getLocalActorSync", 0));
    Root scene(il2cpp::api().string_new(""));
    alignas(8) unsigned char noPosition[16]{};
    void* a[]{&type, sync, nullptr, scene.Get(), noPosition};
    Invoke(Method(service, "CreatePortalAsync", 5), service, a);
    Log("boss doors: the way on is open next to the player");
}
void LabelBossPortal() {
    const auto* cls = game::FindClass("LE.dll", "Networking.Multiplayer.Interactables.Portals", "PortalSync");
    Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls)));
    bool hidden = false; void* args[]{type.Get(), &hidden};
    Root found(Invoke(Unity("Object", "FindObjectsOfType", 2), nullptr, args));
    const auto n = found.Get() ? Get<uintptr_t>(found.Get(), 0x18) : 0;
    for (uintptr_t i = 0; i < n; ++i) {
        void* sync = Get<void*>(found.Get(), 0x20 + 8 * i);
        if (!sync || Get<uint8_t>(sync, Offset("PortalSync", "portalType", "Networking.Multiplayer.Interactables.Portals")) != kPortalEcho) continue;
        void* visual = Get<void*>(sync, Offset("PortalSync", "visualController", "Networking.Multiplayer.Interactables.Portals"));
        void* tag = visual ? Get<void*>(visual, Offset("PortalVisualController", "hoverTag", "Networking.Multiplayer.Interactables.Portals")) : nullptr;
        if (!tag) continue;
        std::string label = "Back to the boss doors";
        if (rushOn && rushAt < 10) { label = "Next boss"; for (const auto& s : Catalog()) if (s.id == rushAt + 1) label += ": " + s.name; }
        void* text = il2cpp::api().string_new(label.c_str()); void* a[]{text};
        *reinterpret_cast<void**>(static_cast<char*>(tag) + Offset("HoverTagOnMouseOver", "localizationKey")) = nullptr;
        void* tagObject = Invoke(Unity("Component", "get_gameObject", 0), tag);
        Invoke(Unity("Object", "set_name", 1), tagObject, a);
        Invoke(Method(tag, "SetHoverText", 1), tag, a);
    }
}
void BossTick() {
    const auto c = Current();
    const bool inside = Value<bool>(m_inside, nullptr);
    const uint64_t now = GetTickCount64();
    if(hallMove.direction&&!hallMove.Valid(c.id,c.scene,reinterpret_cast<uintptr_t>(c.actor),now))hallMove.Clear();
    if(goToHall&&(c.id!=goToHallId||now>=goToHallUntil||(c.scene!=goToHallScene&&c.scene!="M_Rest"&&c.scene!="MonolithHub")))goToHall=false;
    const bool ready = AreaReady();
    if (!ready) { bossReadySince = 0; return; }
    if (!bossReadySince) bossReadySince = now;
    void* health = Get<void*>(c.actor, Offset("Actor", "health"));
    const bool dead = !health || Get<float>(health, Offset("BaseHealth", "currentHealth")) <= 0;
    if (inside && bossTracked) {
        if (dead) bossDied = true;
        if (!bossArrived) bossArrived = now;
        if (!bossWarped && now - bossArrived > 1200) { bossWarped = true; WalkToBoss(c, bossTimeline); }
        if (!bossKilled && !dead && BossRunDone(c)) { bossKilled = true; bossKilledAt = now; }
        if (bossKilled && !bossPortalMade && !dead && now - bossKilledAt > 1500) { bossPortalMade = true; OpenBossPortal(c); bossPortalLabelAt = now + 1000; bossPortalLabels = 0; }
        if (bossPortalLabelAt && now >= bossPortalLabelAt) { bossPortalLabelAt = ++bossPortalLabels < 4 ? now + 1000 : 0; LabelBossPortal(); }
        return;
    }
    if (dead) {hallMove.Clear();goToHall=false;return;}
    if (!inside && bossTracked) {
        const bool killed = bossKilled || BossRunDone(c), portal = bossPortalUsed;
        bossTracked = false; bossPortalUsed = false; questWindowQuietUntil = now + 20000;
        if (bossDied) {
            if (rushOn) { rushOn = false; Log("boss doors: the rush stopped at boss %d", rushAt); }
            QueueHallReturn(c);
        } else if (portal) {
            if (rushOn && killed && rushAt < 10) { ++rushAt; pendingBoss = rushAt; pendingDifficulty = rushDifficulty; pendingHarbinger = rushHarbinger; }
            else {
                if (rushOn && killed) { rushAt = 1; Log("boss doors: the rush is complete"); }
                rushOn = false; QueueHallReturn(c);
            }
        } else {
            // a town portal: to town, nothing starts; the Rush door goes on from the next boss, or this one if it lives
            if (rushOn) { rushOn = false; if (killed) rushAt = rushAt >= 10 ? 1 : rushAt + 1; Log("boss doors: the rush waits at boss %d", rushAt); }
        }
        bossTravelAt = 0;
    }
    if (!inside && !bossTracked) RestoreBorrowed(c);
    if (inside || now - bossReadySince < 1000) return;
    if (pendingBoss > 0) {
        if (c.scene == "M_Rest" || c.scene == "MonolithHub") {
            const int t = pendingBoss; pendingBoss = -1; goToHall = false;hallMove.Clear();
            StartBoss(c, t, pendingDifficulty, pendingHarbinger);
        } else if (!bossTravelAt || now - bossTravelAt > 20000) {
            if (commands::SubmitUi("bosstravel " + c.id + " M_Rest", [](const std::string&) {})) bossTravelAt = now;
        }
        return;
    }
    if (goToHall && bossDoors.load()) {
        if (c.scene == "MonolithHub") {
            if (!DoorsAlive()) { BuildDoors(); doorLabelsAt = now + 1000; }
            // Arrival requests are retained through the delayed nav bake. An
            // unavailable hall is never replaced with an unrelated Hub warp.
            QueueHallMove(c,1);goToHall=false;TryHallMove(c,now);
        } else if (!bossTravelAt || now - bossTravelAt > 20000) {
            if (commands::SubmitUi("bosstravel " + c.id + " MonolithHub:1", [](const std::string&) {})) bossTravelAt = now;
        }
        return;
    }
    if (bossDoors.load() && c.scene == "MonolithHub") {
        if (!DoorsAlive()) { BuildDoors(); doorLabelsAt = now + 1000; }
        else if (doorLabelsAt && now >= doorLabelsAt) { doorLabelsAt = 0; RefreshDoorLabels(); }
        if(hallMove.direction)TryHallMove(c,now);
        SyncHallMap(PlaceOf(c.actor));
        if (!hallStation.empty()) {
            const std::string line = hallStation == "cache" ? "eternitycache " + c.id : "inventoryaction " + c.id + " " + hallStation;
            // the line is busy now and then: the click waits for the next tick
            if (commands::SubmitUi(line, [](const std::string&) {})) hallStation.clear();
        }
    }
}
}
// A window opened before the setting, or by a way other than the one left out above, is closed by the panel system
// (ChooseBlessingPanel cannot be closed by its usual call before a choice is made).
void CloseBlessingWindow() {
    void* manager = game::StaticObject(game::FindStaticField("LE.dll", "", "BlessingRewardPanelManager", "instance"));
    if (!manager || !game::IsAlive(manager)) return;
    void* object = Invoke(Unity("Component", "get_gameObject", 0), manager);
    if (!object || !Value<bool>(Method(object, "get_activeInHierarchy", 0), object)) return;
    const auto* cls = game::FindClass("LE.dll", "LE.UI.PanelSystem", "ChooseBlessingPanel");
    Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls)));
    bool hidden = false; void* findArgs[]{type.Get(), &hidden};
    Root found(Invoke(Unity("Object", "FindObjectsOfType", 2), nullptr, findArgs));
    if (!found.Get() || !Get<uintptr_t>(found.Get(), 0x18)) return;
    void* panel = Get<void*>(found.Get(), 0x20);
    void* ui = game::StaticObject(game::FindStaticField("LE.dll", "", "UIBase", "instance"));
    void* panels = ui ? Invoke(Method(ui, "get_PanelSystem", 0), ui) : nullptr;
    if (!panel || !panels) return;
    // it refuses until a choice is made, which this field records
    *reinterpret_cast<bool*>(static_cast<char*>(manager) + Offset("BlessingRewardPanelManager", "_confirmed")) = true;
    bool skipClosing = false; void* a[]{panel, &skipClosing};
    (void)Value<bool>(Method(panels, "ClosePanelInternal", 2), panels, a);
    Log("monolith: an open blessing window was closed");
}
void BossFrame() {
    static bool borrowedLoaded = false;
    static uint64_t blessingCheck = 0;
    if (noBlessing.load() && GetTickCount64() - blessingCheck > 150) { blessingCheck = GetTickCount64(); game::Guarded([] { CloseBlessingWindow(); }, nullptr); }
    if (!borrowedLoaded) { borrowedLoaded = true; game::Guarded([] { LoadBorrowed(); }, nullptr); }
    const bool active = bossDoors.load() || rushOn || pendingBoss >= 0 || bossTracked;
    if (!active && borrowed.empty()) return;
    const uint64_t now = GetTickCount64();
    if (now - bossLastTick < kBossTickMs) return;
    bossLastTick = now;
    if (!active) {
        // the setting is off after a restart: a run borrowed before the game closed still goes back
        game::Guarded([] { if (AreaReady()) { const auto c = Current(); if (!Value<bool>(m_inside, nullptr)) RestoreBorrowed(c); } }, nullptr);
        return;
    }
    std::string why;
    if (!game::Guarded([] { BossTick(); }, &why) && !why.empty() && why != bossError) { bossError = why; ++bossFaults; Log("boss doors: %s", why.c_str()); }
}
void HallWorld(float right, float up, float* x, float* z) { const DoorVec3 at = HallPlace(right, up); *x = at.x; *z = at.z; }
void HallLocal(float x, float z, float* right, float* up) {
    const float dx = x - kHallCentre.x, dz = z - kHallCentre.z;
    *right = dx * hallRightX + dz * hallRightZ; *up = dx * hallUpX + dz * hallUpZ;
}
float HallGroundY() { return kHallCentre.y; }
bool HallIsUp() { return HallAlive(); }
void* HallPreviewDoor() { for (const auto& d : doors) if (d.action > 0 && d.object && game::IsAlive(d.object->Get())) return d.object->Get(); return nullptr; }
void* HallDoorVisual(int action) {for(const auto& d:doors)if(d.action==action&&d.object&&game::IsAlive(d.object->Get()))return d.object->Get();return nullptr;}
void ReleaseHallRoots(){hallMove.Clear();goToHall=false;goToHallId.clear();goToHallScene.clear();goToHallUntil=0;doors.clear();hallRoot.reset();hallSurface.reset();hallGroundRenderer.reset();hallLight.reset();hallMesh.reset();hallMapInfo.reset();hallMapTexture.reset();hallMapFog.reset();hallMapHubInfo.reset();hallMapHubScene.reset();}
void HallRebuildDoors() { if (HallAlive()) { BuildDoors(); doorLabelsAt = GetTickCount64() + 1000; } }
void HallRebuildNavMesh() { if (hallSurface && hallSurface->Get() && game::IsAlive(hallSurface->Get())) Invoke(Method(hallSurface->Get(), "BuildNavMesh", 0), hallSurface->Get()); }
// All of it again: a new ground. Whoever stands there is put in the middle of the new one.
void HallRebuildAll() {
    void* actor = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0));
    const bool inside = HallAlive() && actor && game::IsAlive(actor) && InHall(PlaceOf(actor));
    ClearDoors(); DestroyHall();  // the next tick in the Hub builds it all again, and moves the player before anything else
    if (inside) QueueHallMove(Current(),1);
}
void HallSetLight(float intensity) {
    if (!hallLight || !hallLight->Get() || !game::IsAlive(hallLight->Get())) return;
    void* a[]{&intensity}; Invoke(Method(hallLight->Get(), "set_intensity", 1), hallLight->Get(), a);
}
void HallSetGroundMaterial(void* material) {
    if (!hallGroundRenderer || !hallGroundRenderer->Get() || !game::IsAlive(hallGroundRenderer->Get()) || !material) return;
    Root materials(Invoke(Method(hallGroundRenderer->Get(),"get_sharedMaterials",0),hallGroundRenderer->Get()));
    if(materials.Get() && Get<uintptr_t>(materials.Get(),0x18)>1){int slot=0;void* a[]{material,&slot};Invoke(DoorSignature("Array","SetValue",{"System.Object","System.Int32"},"System","mscorlib.dll"),materials.Get(),a);void* m[]{materials.Get()};Invoke(Method(hallGroundRenderer->Get(),"set_sharedMaterials",1),hallGroundRenderer->Get(),m);}
    else {void* a[]{material}; Invoke(Method(hallGroundRenderer->Get(), "set_sharedMaterial", 1), hallGroundRenderer->Get(), a);}
}
void HallGoIn() {
    const auto c=Current();
    if(!bossDoors.load())throw std::runtime_error("Enable Boss doors before entering the hideout.");
    if(bossTracked || pendingBoss>0)throw std::runtime_error("Finish the current boss encounter before entering the hideout.");
    if(c.scene=="MonolithHub") {if(!HallAlive())BuildDoors();QueueHallMove(c,1);TryHallMove(c,GetTickCount64());}
    else {QueueHallReturn(c);bossTravelAt=0;}
}
std::string BossDoors(bool on) {
    return Run([=]() -> std::string {
        if (!game::IsOfflinePlay()) throw std::runtime_error(game::GateText());
        std::string why;
        if (on && !hook::IsInstalled(m_objectClick.code)) {
            if (!hook::Install(m_objectClick.code, reinterpret_cast<void*>(&ObjectClickDetour), reinterpret_cast<void**>(&originalObjectClick), &why)) throw std::runtime_error(why);
            if (!hook::Install(m_timelineUnlocked.code, reinterpret_cast<void*>(&UnlockedDetour), reinterpret_cast<void**>(&originalUnlocked), &why)) throw std::runtime_error(why);
            if (!hook::Install(m_unlockNotice.code, reinterpret_cast<void*>(&UnlockNoticeDetour), reinterpret_cast<void**>(&originalUnlockNotice), &why)) throw std::runtime_error(why);
            if (!hook::Install(m_echoPortalUse.code, reinterpret_cast<void*>(&EchoPortalDetour), reinterpret_cast<void**>(&originalEchoPortal), &why)) throw std::runtime_error(why);
            if (!hook::Install(m_harbingerSetup.code, reinterpret_cast<void*>(&HarbingerSetupDetour), reinterpret_cast<void**>(&originalHarbingerSetup), &why)) throw std::runtime_error(why);
            if (!hook::Install(m_harbingerGuarantee.code, reinterpret_cast<void*>(&HarbingerGuaranteeDetour), reinterpret_cast<void**>(&originalHarbingerGuarantee), &why)) throw std::runtime_error(why);
            if (!hook::Install(m_questWindow.code, reinterpret_cast<void*>(&QuestWindowDetour), reinterpret_cast<void**>(&originalQuestWindow), &why)) throw std::runtime_error(why);
            if (!hook::Install(m_questNotice.code, reinterpret_cast<void*>(&QuestNoticeDetour), reinterpret_cast<void**>(&originalQuestNotice), &why)) throw std::runtime_error(why);
        }
        bossDoors = on;
        if (!on) {
            // nobody stays behind in a hall that goes away
            if (HallAlive()) {
                void* actor = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0));
                if (actor && game::IsAlive(actor) && InHall(PlaceOf(actor))) { DoorVec3 snapped{}; if (SampleWalkable(kHubSpot, 6.f, &snapped)) Warp(actor, snapped); }
            }
            ClearDoors(); DestroyHall(); hallMove.Clear(); rushOn = false; pendingBoss = -1; goToHall = false;
            for (const auto& m : {m_objectClick, m_timelineUnlocked, m_unlockNotice, m_echoPortalUse, m_harbingerSetup, m_harbingerGuarantee, m_questWindow, m_questNotice})
                if (hook::IsInstalled(m.code) && !hook::Remove(m.code, &why)) throw std::runtime_error(why);
        }
        return "{\"ok\":true,\"bossDoors\":" + std::string(Boolean(on)) + ",\"doors\":" + std::to_string(doors.size()) + "}";
    });
}
// The game rolls the blessing options when a timeline's boss dies and opens their panel a moment later (MonolithUIManager ->
// UIBase.OpenBlessingOptions). Only the panel is left out: the options stay unused until the next roll clears them.
namespace {
using OpenBlessingFn = void (*)(void*, uint8_t, int, int, const il2cpp::Method*);
OpenBlessingFn originalOpenBlessing = nullptr;
uint64_t blessingsSkipped = 0;
void OpenBlessingDetour(void* self, uint8_t timeline, int difficulty, int options, const il2cpp::Method* method) {
    if (noBlessing.load()) { ++blessingsSkipped; Log("monolith: blessing choice of timeline %d left out", static_cast<int>(timeline)); return; }
    originalOpenBlessing(self, timeline, difficulty, options, method);
}
}
std::string SetNoBlessing(bool on) {
    return Run([=]() -> std::string {
        if (!ready) throw std::runtime_error("monolith metadata unavailable");
        if (!game::IsOfflinePlay()) throw std::runtime_error(game::GateText());
        std::string why;
        if (on && !hook::IsInstalled(m_openBlessing.code) &&
            !hook::Install(m_openBlessing.code, reinterpret_cast<void*>(&OpenBlessingDetour), reinterpret_cast<void**>(&originalOpenBlessing), &why)) throw std::runtime_error(why);
        if (!on && hook::IsInstalled(m_openBlessing.code) && !hook::Remove(m_openBlessing.code, &why)) throw std::runtime_error(why);
        noBlessing = on;
        return std::string("{\"ok\":true,\"noBlessing\":") + Boolean(on) + "}";
    });
}
std::string BossStatus() {
    return std::string("noblessing: ") + (noBlessing.load() ? "on" : "off") + ", left out " + std::to_string(blessingsSkipped) + "\n" +
           "bossdoors: " + (bossDoors.load() ? "on" : "off") + ", doors " + std::to_string(doors.size()) + ", starts " + std::to_string(bossStarts) +
           ", rush " + (rushOn ? "on at " + std::to_string(rushAt) : std::string("off")) + ", faults " + std::to_string(bossFaults);
}
#ifdef EPOCHPACT_TESTING
// Boss Rush research, test builds only.
namespace {
#include "hideout_ui_trace.inl"
void TestDoorClick(void* listener,void* source,bool click,bool foreground=true){
    const bool previous=testDoorForeground;testDoorForeground=foreground;
    struct Restore {bool value;~Restore(){testDoorForeground=value;}} restore{previous};
    void* args[]{source,&click};Invoke(Method(listener,"ObjectClick",2),listener,args);
}
void TraceDoorInput(void* listener,bool click){
    if(!uiTraceOn)return;
    for(const auto& d:doors)if(d.listener&&d.listener->Get()==listener){
        DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
        const auto text="door "+std::to_string(d.action)+" click="+std::to_string(click)+" foreground="+std::to_string(pid==GetCurrentProcessId())+" LMB="+std::to_string((GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0);
        TraceMenu(text.c_str());break;
    }
}
struct BossVec3 { float x, y, z; };
const game::MethodRef BossStatic(const char* name, int n) { return game::FindMethod("LE.dll", "LE.Gameplay.Monolith", "MonolithGameplayManager", name, n); }
const il2cpp::Method* BossSignature(const char* cls, const char* name, std::initializer_list<const char*> parameters) {
    auto& a = il2cpp::api(); auto* c = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", cls); void* iterator = nullptr;
    while (auto* m = a.class_get_methods(c, &iterator)) {
        if (std::string(a.method_get_name(m)) != name || a.method_get_param_count(m) != parameters.size()) continue;
        bool match = true; unsigned i = 0;
        for (const char* expected : parameters) { char* actual = a.type_get_name(a.method_get_param(m, i++)); match &= actual && std::string(actual) == expected; if (actual) a.free(actual); }
        if (match) return m;
    }
    throw std::runtime_error(std::string("no method ") + name);
}
std::string BossPosition(void* component) {
    Root tr(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_transform", 0), component));
    const auto p = Value<BossVec3>(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_position", 0), tr.Get());
    return "[" + std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z) + "]";
}
std::string BossState(const Context& c) {
    std::ostringstream out;
    void* service = Invoke(game::FindMethod("LE.dll", "LE.Services", "ServiceProvider", "get_SceneService", 0));
    out << "{\"ok\":true,\"scene\":" << Json(c.scene) << ",\"loading\":" << Boolean(service && Value<bool>(Method(service, "IsLoading", 0), service))
        << ",\"sceneService\":" << Json(service ? Text(Invoke(Method(service, "get_CurrentScene", 0), service)) : "")
        << ",\"position\":" << BossPosition(c.actor);
    void* current = Get<void*>(c.runs, Offset("MonolithRunsManager", "currentRun"));
    out << ",\"currentRun\":" << RunJson(current);
    out << ",\"activeWeb\":" << Boolean(Value<bool>(BossStatic("get_HasActiveWeb", 0), nullptr))
        << ",\"insideEcho\":" << Boolean(Value<bool>(BossStatic("IsHostPlayerInsideCurrentEcho", 0), nullptr));
    return out.str() + "}";
}
}
std::string BossTest(const std::string& id, const std::vector<std::string>& args) {
    return Run([=]() mutable -> std::string {
        if (args.empty()) throw std::runtime_error("use bosstest id info|hub|state|start t d|goto scene|portal");
        const std::string action = args[0];
        const auto c = Current(id);
        if(action=="uitrace"){
            if(c.name!="EpFeedbackTest")throw std::runtime_error("Disposable EpFeedbackTest only.");
            return UiTrace(args.at(1));
        }
        if(action=="quit"){
            if(c.name!="EpFeedbackTest")throw std::runtime_error("Disposable EpFeedbackTest only.");
            ingame::view::After(250,[id]{
                const auto current=Current(id);
                if(current.name!="EpFeedbackTest")throw std::runtime_error("The disposable character changed before quit.");
                int code=0;void* a[]{&code};Invoke(Unity("Application","Quit",1),nullptr,a);
            });
            return "{\"ok\":true,\"quitScheduled\":true}";
        }
        if(action=="halllayout"){
            if(c.name!="EpFeedbackTest"||!HallAlive())throw std::runtime_error("Disposable EpFeedbackTest hideout only.");
            const auto instance=[](void* o){return o?Value<int>(Unity("Object","GetInstanceID",0),o):0;};
            std::ostringstream out;out<<"{\"ok\":true,\"hallInstance\":"<<instance(hallRoot->Get())<<",\"meshInstance\":"<<instance(hallMesh?hallMesh->Get():nullptr)<<",\"hallBuilds\":"<<hallBuildCount<<",\"doorBuilds\":"<<doorBuildCount<<",\"doorSyncs\":"<<doorSyncCount<<",\"doorPoseUpdates\":"<<doorPoseUpdateCount<<",\"movePending\":"<<hallMove.direction<<",\"doors\":[";bool first=true;
            for(const auto& d:doors){if(!first)out<<',';first=false;const auto p=PlaceOf(d.listener->Get());Root tr(Invoke(Unity("GameObject","get_transform",0),d.object->Get()));const auto world=Value<DoorVec3>(Unity("Transform","get_position",0),tr.Get());float right=0,up=0;HallLocal(world.x,world.z,&right,&up);
                out<<"{\"action\":"<<d.action<<",\"instance\":"<<instance(d.object->Get())<<",\"right\":"<<right<<",\"up\":"<<up<<",\"y\":"<<world.y<<",\"listenerX\":"<<p.x<<",\"listenerZ\":"<<p.z<<'}';}
            return out.str()+"]}";
        }
        if(action=="stationhit"){
            if(c.name!="EpFeedbackTest")throw std::runtime_error("Disposable EpFeedbackTest only.");
            const int station=std::stoi(args.at(1));
            if(station!=-4&&station!=-5&&station!=-6)throw std::runtime_error("Station fixture only.");
            for(const auto& d:doors)if(d.action==station&&d.listener&&game::IsAlive(d.listener->Get())){
                hideout::StationBounds visual{};if(!hideout::GetStationBounds(station,visual))throw std::runtime_error("Station model not loaded.");
                Root type(TypeObject("UnityEngine.PhysicsModule.dll","UnityEngine","BoxCollider"));void* a[]{type.Get()};
                void* box=Invoke(DoorSignature("Component","GetComponent",{"System.Type"}),d.listener->Get(),a);if(!box)throw std::runtime_error("Station has no aligned click box.");
                struct Bounds{DoorVec3 centre,extents;};const auto b=Value<Bounds>(game::FindMethod("UnityEngine.PhysicsModule.dll","UnityEngine","Collider","get_bounds",0),box);
                void* camera=Invoke(Unity("Camera","get_main",0));const auto origin=PlaceOf(camera);DoorVec3 direction{visual.x-origin.x,visual.y-origin.y,visual.z-origin.z};
                const float length=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);direction={direction.x/length,direction.y/length,direction.z/length};
                struct Ray{DoorVec3 origin,direction;} ray{origin,direction};alignas(16) unsigned char hit[128]{};float maxDistance=length+5;void* ra[]{&ray,hit,&maxDistance};
                const bool hits=Value<bool>(DoorSignature("Collider","Raycast",{"UnityEngine.Ray","UnityEngine.RaycastHit&","System.Single"},"UnityEngine","UnityEngine.PhysicsModule.dll"),box,ra);
                float error=std::max({std::abs(b.centre.x-visual.x),std::abs(b.centre.y-visual.y),std::abs(b.centre.z-visual.z),std::abs(2*b.extents.x-visual.width),std::abs(2*b.extents.y-visual.height),std::abs(2*b.extents.z-visual.depth)});
                if(args.size()>2&&args[2]=="click"){
                    void* player=Invoke(Unity("Component","get_gameObject",0),c.actor);TestDoorClick(d.listener->Get(),player,true);
                }
                return "{\"ok\":true,\"rayHitsVisibleModel\":"+std::string(Boolean(hits))+",\"boundsError\":"+std::to_string(error)+"}";
            }
            throw std::runtime_error("No station listener.");
        }
        if(action=="doorpose") {
            if(c.name!="EpFeedbackTest")throw std::runtime_error("Disposable EpFeedbackTest only.");
            void* object=HallDoorVisual(std::stoi(args.at(1)));if(!object)throw std::runtime_error("No such hideout door.");
            Root root(Invoke(Unity("GameObject","get_transform",0),object));
            if(args.size()>2){void* fa[]{il2cpp::api().string_new(args.at(2).c_str())};void* tr=Invoke(DoorSignature("Transform","Find",{"System.String"}),root.Get(),fa);if(!tr)throw std::runtime_error("No such child.");
                if(args.size()==6){DoorVec3 e{std::stof(args[3]),std::stof(args[4]),std::stof(args[5])};void* a[]{&e};Invoke(Unity("Transform","set_localEulerAngles",1),tr,a);}}
            std::ostringstream out;out<<"{\"ok\":true,\"tree\":[";bool first=true;
            const auto vec=[&](void* tr,const char* name){auto p=Value<DoorVec3>(Unity("Transform",name,0),tr);out<<'['<<p.x<<','<<p.y<<','<<p.z<<']';};
            std::function<void(void*,std::string,int)> walk=[&](void* tr,std::string path,int depth){if(!first)out<<',';first=false;out<<"{\"path\":"<<Json(path)<<",\"localEuler\":";vec(tr,"get_localEulerAngles");out<<",\"worldEuler\":";vec(tr,"get_eulerAngles");out<<",\"localPosition\":";vec(tr,"get_localPosition");out<<",\"localScale\":";vec(tr,"get_localScale");out<<'}';
                if(depth>=7)return;const int count=Value<int>(Unity("Transform","get_childCount",0),tr);for(int k=0;k<count&&k<40;++k){void* a[]{&k};void* child=Invoke(Unity("Transform","GetChild",1),tr,a);const auto name=Text(Invoke(Unity("Object","get_name",0),child));walk(child,path.empty()?name:path+"/"+name,depth+1);}};
            walk(root.Get(),"",0);return out.str()+"]}";
        }
        if (action == "state") return BossState(c);
        if (action == "info") {
            std::ostringstream out; out << "{\"ok\":true,\"scene\":" << Json(c.scene) << ",\"timelines\":["; bool first = true;
            for (const auto& t : Catalog()) {
                if (!first) out << ','; first = false;
                out << "{\"id\":" << int(t.id) << ",\"name\":" << Json(t.name) << ",\"difficulties\":" << t.difficulties.size() << ",\"unlocked\":[";
                for (size_t d = 0; d < t.difficulties.size(); ++d) { if (d) out << ','; out << Boolean(IsUnlocked(c, t.id, static_cast<int>(d))); }
                out << "],\"empowered\":[";
                for (size_t d = 0; d < t.difficulties.size(); ++d) { int i = static_cast<int>(d); void* a[]{&i}; if (d) out << ',';
                    out << Boolean(Value<bool>(game::FindMethod("LE.dll", "", "MonolithTimeline", "DifficultyIndexIsEmpowered", 1), nullptr, a)); }
                out << "],\"levels\":[";
                for (size_t d = 0; d < t.difficulties.size(); ++d) { if (d) out << ','; out << Get<int>(t.difficulties[d], Offset("MonolithTimeline.Difficulty", "level")); }
                out << "],\"maxStability\":[";
                for (size_t d = 0; d < t.difficulties.size(); ++d) { if (d) out << ','; out << Get<int>(t.difficulties[d], Offset("MonolithTimeline.Difficulty", "maxStability")); }
                out << "],\"restZone\":" << Json(Text(Get<void*>(t.object, Offset("MonolithTimeline", "restZone")))) << ",\"quests\":[";
                bool q = true;
                for (const auto& z : QuestZones(t.object)) { if (!q) out << ','; q = false;
                    out << "{\"required\":" << z.required << ",\"zone\":" << Json(Text(Get<void*>(z.zone, Offset("MonolithTimeline.Zone", "sceneName")))) << ",\"branch\":" << Boolean(z.needsBranch) << "}"; }
                out << "]}";
            }
            return out.str() + "]}";
        }
        if (action == "hub") {
            const auto* cls = game::FindClass("LE.dll", "", "OpenMonolithPanelInteraction");
            void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));
            bool inactive = true; void* findArgs[]{type, &inactive};
            Root objects(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, findArgs));
            std::ostringstream out; out << "{\"ok\":true,\"scene\":" << Json(c.scene) << ",\"position\":" << BossPosition(c.actor) << ",\"rocks\":[";
            const auto n = objects.Get() ? Get<uintptr_t>(objects.Get(), 0x18) : 0;
            for (uintptr_t i = 0; i < n; ++i) {
                void* o = Get<void*>(objects.Get(), 0x20 + 8 * i); if (i) out << ',';
                out << "{\"name\":" << Json(Text(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0), o)))
                    << ",\"timeline\":" << int(Get<uint8_t>(o, Offset("OpenMonolithPanelInteraction", "timelineID")))
                    << ",\"specific\":" << Boolean(Get<bool>(o, Offset("OpenMonolithPanelInteraction", "requestSpecificDifficultyIndex")))
                    << ",\"difficulty\":" << Get<int>(o, Offset("OpenMonolithPanelInteraction", "difficultyIndex"))
                    << ",\"rest\":" << Boolean(Get<bool>(o, Offset("OpenMonolithPanelInteraction", "isRestZoneRock")))
                    << ",\"position\":" << BossPosition(o) << '}';
            }
            return out.str() + "]}";
        }
        if (action == "start") {
            int timeline = std::stoi(args.at(1)), difficulty = std::stoi(args.at(2));
            const bool keepRun = args.size() > 3 && args[3] == "asis";
            const auto t = Find(timeline, difficulty);
            void* run = GetRun(c, t.id, difficulty);
            bool created = false;
            if (!run) { auto tid = t.id; void* a[]{&tid, &difficulty}; run = Invoke(Method(c.runs, "getNewRun", 2), c.runs, a); created = true; }
            if (!run) throw std::runtime_error("no run");
            const auto zones = QuestZones(t.object);
            if (zones.empty()) throw std::runtime_error("no quest echoes");
            const int boss = static_cast<int>(zones.size()) - 1;
            if (!keepRun) {
                *reinterpret_cast<int*>(static_cast<char*>(run) + Offset("MonolithRun", "questCompletion")) = zones.back().required;
                const int maximum = Value<int>(m_max, run);
                *reinterpret_cast<int*>(static_cast<char*>(run) + runStability) = maximum;
                if (zones.back().needsBranch) *reinterpret_cast<int*>(static_cast<char*>(run) + Offset("MonolithRun", "questBranch")) = zones.back().branch;
            }
            int bossIndex = boss; void* can[]{&bossIndex};
            const bool canStart = Value<bool>(Method(run, "CanStartQuestEcho", 1), run, can);
            void* sync = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getLocalActorSync", 0));
            void* user = Invoke(game::FindMethod("LE.dll", "LE.Networking", "PlayerStore", "get_LocalUserIdentity", 0));
            auto tid = t.id; bool harbinger = false;
            void* startArgs[]{sync, user, &tid, &difficulty, &bossIndex, &harbinger};
            Invoke(game::FindMethod("LE.dll", "LE.Gameplay.Monolith", "MonolithGameplayManager", "AttemptStartQuestAsync", 6), nullptr, startArgs);
            return "{\"ok\":true,\"created\":" + std::string(Boolean(created)) + ",\"canStart\":" + Boolean(canStart) + ",\"boss\":" + std::to_string(boss) + ",\"run\":" + RunJson(run) + "}";
        }
        if (action == "questecho") {
            // a timeline's first quest echo, with the zone's random encounters
            int timeline = std::stoi(args.at(1)), difficulty = std::stoi(args.at(2));
            const auto t = Find(timeline, difficulty);
            void* run = GetRun(c, t.id, difficulty);
            if (!run) { auto tid = t.id; void* a[]{&tid, &difficulty}; run = Invoke(Method(c.runs, "getNewRun", 2), c.runs, a); }
            if (!run) throw std::runtime_error("no run");
            const auto zones = QuestZones(t.object);
            if (zones.empty()) throw std::runtime_error("no quest echoes");
            *reinterpret_cast<int*>(static_cast<char*>(run) + Offset("MonolithRun", "questCompletion")) = zones.front().required;
            int index = 0; void* can[]{&index};
            const bool canStart = Value<bool>(Method(run, "CanStartQuestEcho", 1), run, can);
            void* sync = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getLocalActorSync", 0));
            void* user = Invoke(game::FindMethod("LE.dll", "LE.Networking", "PlayerStore", "get_LocalUserIdentity", 0));
            auto tid = t.id; bool harbinger = false;
            void* startArgs[]{sync, user, &tid, &difficulty, &index, &harbinger};
            Invoke(game::FindMethod("LE.dll", "LE.Gameplay.Monolith", "MonolithGameplayManager", "AttemptStartQuestAsync", 6), nullptr, startArgs);
            return "{\"ok\":true,\"canStart\":" + std::string(Boolean(canStart)) + ",\"scene\":" + Json(Text(Get<void*>(zones.front().zone, Offset("MonolithTimeline.Zone", "sceneName")))) + "}";
        }
        if (action == "mapscan") {
            // the biggest colliders and renderers inside a box: what the ground is made of
            const float x0 = std::stof(args.at(1)), z0 = std::stof(args.at(2)), half = std::stof(args.at(3));
            struct B { DoorVec3 c, e; };
            std::ostringstream out; out << "{\"ok\":true";
            for (const char* cname : {"MeshCollider", "TerrainCollider", "BoxCollider", "MeshRenderer"}) {
                const bool renderer = std::string(cname) == "MeshRenderer";
                const auto* cls = game::FindClass(renderer ? "UnityEngine.CoreModule.dll" : (std::string(cname) == "TerrainCollider" ? "UnityEngine.TerrainPhysicsModule.dll" : "UnityEngine.PhysicsModule.dll"), "UnityEngine", cname);
                if (!cls) { out << ",\"" << cname << "\":null"; continue; }
                void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));
                bool inactive = false; void* findArgs[]{type, &inactive};
                Root found(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, findArgs));
                const auto n = found.Get() ? Get<uintptr_t>(found.Get(), 0x18) : 0;
                struct Row { float area; std::string text; };
                std::vector<Row> rows; int inside = 0;
                for (uintptr_t i = 0; i < n; ++i) {
                    void* o = Get<void*>(found.Get(), 0x20 + 8 * i); if (!o) continue;
                    const B b = Value<B>(Method(o, "get_bounds", 0), o);
                    if (std::abs(b.c.x - x0) > half || std::abs(b.c.z - z0) > half) continue;
                    ++inside;
                    std::ostringstream row;
                    bool stat = false;
                    if (renderer) stat = Value<bool>(Method(o, "get_isPartOfStaticBatch", 0), o);
                    void* go = Invoke(Unity("Component", "get_gameObject", 0), o);
                    row << "{\"name\":" << Json(Text(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0), go)))
                        << ",\"c\":[" << int(b.c.x) << ',' << int(b.c.y) << ',' << int(b.c.z) << "],\"size\":[" << int(2 * b.e.x) << ',' << int(2 * b.e.y) << ',' << int(2 * b.e.z) << "],\"static\":" << Boolean(stat) << '}';
                    rows.push_back({b.e.x * b.e.z, row.str()});
                }
                std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.area > b.area; });
                out << ",\"" << cname << "\":{\"inside\":" << inside << ",\"biggest\":[";
                for (size_t i = 0; i < rows.size() && i < 16; ++i) out << (i ? "," : "") << rows[i].text;
                out << "]}";
            }
            return out.str() + "}";
        }
        if (action == "mapcopy") {
            // copies of a named object moved far away, its scripts off; x z half pick it among same-named ones
            std::string wanted = args.at(1);
            std::replace(wanted.begin(), wanted.end(), '_', ' ');
            const DoorVec3 to{std::stof(args.at(2)), std::stof(args.at(3)), std::stof(args.at(4))};
            // 1 number: all three axes; x,y,z: each its own
            float scale = 1.f, scaleY = 1.f, scaleZ = 1.f;
            if (args.size() > 5) {
                std::string s = args.at(5); std::replace(s.begin(), s.end(), ',', ' ');
                std::istringstream parts(s); parts >> scale; if (parts >> scaleY) parts >> scaleZ; else scaleY = scaleZ = scale;
            }
            const auto* cls = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Transform");
            void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));
            bool inactive = false; void* findArgs[]{type, &inactive};
            Root found(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, findArgs));
            const auto n = Get<uintptr_t>(found.Get(), 0x18);
            void* source = nullptr; DoorVec3 from{}, sourceScale{1, 1, 1}; struct Quat { float x, y, z, w; } sourceRotation{0, 0, 0, 1};
            for (uintptr_t i = 0; i < n && !source; ++i) {
                void* o = Get<void*>(found.Get(), 0x20 + 8 * i); if (!o) continue;
                void* go = Invoke(Unity("Component", "get_gameObject", 0), o);
                std::string named = Text(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0), go));
                std::replace(named.begin(), named.end(), '_', ' ');
                if (named != wanted) continue;
                from = Value<DoorVec3>(Unity("Transform", "get_position", 0), o);
                sourceRotation = Value<Quat>(Unity("Transform", "get_rotation", 0), o);
                sourceScale = Value<DoorVec3>(Unity("Transform", "get_lossyScale", 0), o);
                source = go;
            }
            if (!source) throw std::runtime_error("no such object");
            const auto* instantiate = DoorSignature("Object", "Instantiate", {"UnityEngine.Object"});
            void* ia[]{source};
            static std::vector<std::unique_ptr<Root>> keep;
            keep.push_back(std::make_unique<Root>(Invoke(instantiate, nullptr, ia)));
            void* copy = keep.back()->Get();
            Root tr(Invoke(Method(copy, "get_transform", 0), copy));
            DoorVec3 at = to; void* place[]{&at}; Invoke(Unity("Transform", "set_position", 1), tr.Get(), place);
            void* ra[]{&sourceRotation}; Invoke(Unity("Transform", "set_rotation", 1), tr.Get(), ra);
            DoorVec3 sized{sourceScale.x * scale, sourceScale.y * scaleY, sourceScale.z * scaleZ}; void* sa[]{&sized}; Invoke(Unity("Transform", "set_localScale", 1), tr.Get(), sa);
            const auto* componentsIn = DoorSignature("GameObject", "GetComponentsInChildren", {"System.Type", "System.Boolean"});
            Root behaviourType(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "MonoBehaviour"))));
            bool all = true; void* ca[]{behaviourType.Get(), &all};
            Root behaviours(Invoke(componentsIn, copy, ca));
            int off = 0; const auto m = behaviours.Get() ? Get<uintptr_t>(behaviours.Get(), 0x18) : 0;
            for (uintptr_t i = 0; i < m; ++i) {
                void* b = Get<void*>(behaviours.Get(), 0x20 + 8 * i); if (!b) continue;
                bool no = false; void* na[]{&no}; Invoke(Unity("Behaviour", "set_enabled", 1), b, na); ++off;
            }
            if (args.size() > 6 && args[6] == "hide") {
                Root rendererType(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Renderer"))));
                void* ra2[]{rendererType.Get(), &all};
                Root renderers(Invoke(componentsIn, copy, ra2));
                const auto k = renderers.Get() ? Get<uintptr_t>(renderers.Get(), 0x18) : 0;
                for (uintptr_t i = 0; i < k; ++i) {
                    void* r = Get<void*>(renderers.Get(), 0x20 + 8 * i); if (!r) continue;
                    bool no = false; void* na[]{&no}; Invoke(Method(r, "set_enabled", 1), r, na);
                }
            }
            std::ostringstream out; out << "{\"ok\":true,\"from\":[" << from.x << ',' << from.y << ',' << from.z << "],\"scriptsOff\":" << off << '}';
            return out.str();
        }
        if (action == "mapnav") {
            // a runtime NavMesh over a box from the colliders in it, and a path across it
            const DoorVec3 centre{std::stof(args.at(1)), std::stof(args.at(2)), std::stof(args.at(3))};
            const DoorVec3 size{std::stof(args.at(4)), std::stof(args.at(5)), std::stof(args.at(6))};
            const auto* surfaceClass = game::FindClass("Unity.AI.Navigation.dll", "Unity.AI.Navigation", "NavMeshSurface");
            if (!surfaceClass) throw std::runtime_error("no NavMeshSurface class");
            const auto* add = DoorSignature("GameObject", "AddComponent", {"System.Type"});
            const auto* objectClass = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
            static std::vector<std::unique_ptr<Root>> keep;
            keep.push_back(std::make_unique<Root>(il2cpp::api().object_new(objectClass)));
            void* holder = keep.back()->Get();
            void* nameArgs[]{il2cpp::api().string_new("EP map surface")};
            Invoke(DoorSignature("GameObject", ".ctor", {"System.String"}), holder, nameArgs);
            Root tr(Invoke(Method(holder, "get_transform", 0), holder));
            DoorVec3 at = centre; void* place[]{&at}; Invoke(Unity("Transform", "set_position", 1), tr.Get(), place);
            Root surfaceType(il2cpp::api().type_get_object(il2cpp::api().class_get_type(surfaceClass)));
            void* ta[]{surfaceType.Get()};
            void* surface = Invoke(add, holder, ta);
            if (!surface) throw std::runtime_error("no surface component");
            int collect = 1; void* a1[]{&collect}; Invoke(Method(surface, "set_collectObjects", 1), surface, a1);          // Volume
            int physics = args.size() > 7 ? std::stoi(args.at(7)) : 1; void* a2[]{&physics}; Invoke(Method(surface, "set_useGeometry", 1), surface, a2);  // 1 colliders, 0 render meshes
            DoorVec3 s = size; void* a3[]{&s}; Invoke(Method(surface, "set_size", 1), surface, a3);
            DoorVec3 zero{}; void* a4[]{&zero}; Invoke(Method(surface, "set_center", 1), surface, a4);
            const uint64_t t0 = GetTickCount64();
            Invoke(Method(surface, "BuildNavMesh", 0), surface);
            const uint64_t ms = GetTickCount64() - t0;
            const bool data = Invoke(Method(surface, "get_navMeshData", 0), surface) != nullptr;
            DoorVec3 walk{}; const bool middle = SampleWalkable(centre, 6.f, &walk);
            int status = -1, corners = -1;
            DoorVec3 a{centre.x - size.x / 3, centre.y, centre.z}, b{centre.x + size.x / 3, centre.y, centre.z}, sa{}, sb{};
            const bool ends = SampleWalkable(a, 6.f, &sa) && SampleWalkable(b, 6.f, &sb);
            if (ends) {
                const auto* pathClass = game::FindClass("UnityEngine.AIModule.dll", "UnityEngine.AI", "NavMeshPath");
                Root path(il2cpp::api().object_new(pathClass));
                Invoke(DoorSignature("NavMeshPath", ".ctor", {}, "UnityEngine.AI", "UnityEngine.AIModule.dll"), path.Get());
                int mask = -1; void* pa[]{&sa, &sb, &mask, path.Get()};
                Value<bool>(DoorSignature("NavMesh", "CalculatePath", {"UnityEngine.Vector3", "UnityEngine.Vector3", "System.Int32", "UnityEngine.AI.NavMeshPath"}, "UnityEngine.AI", "UnityEngine.AIModule.dll"), nullptr, pa);
                status = Value<int>(Method(path.Get(), "get_status", 0), path.Get());
            }
            std::ostringstream out; out << "{\"ok\":true,\"ms\":" << ms << ",\"data\":" << Boolean(data) << ",\"middle\":" << Boolean(middle) << ",\"at\":[" << walk.x << ',' << walk.y << ',' << walk.z << "],\"ends\":" << Boolean(ends) << ",\"pathStatus\":" << status << '}';
            return out.str();
        }
        if (action == "mapassets") {
            // every mesh in a box: name, how many, size, can it be read, one object and its layer
            const float x0 = std::stof(args.at(1)), z0 = std::stof(args.at(2)), half = std::stof(args.at(3));
            const auto* cls = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "MeshFilter");
            void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));
            bool inactive = false; void* findArgs[]{type, &inactive};
            Root found(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, findArgs));
            const auto n = found.Get() ? Get<uintptr_t>(found.Get(), 0x18) : 0;
            struct Row { int count = 0; float sx = 0, sy = 0, sz = 0; bool readable = false; std::string object, parent; int layer = 0; };
            std::map<std::string, Row> rows;
            const auto nameOf = [](void* o) { return Text(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0), o)); };
            for (uintptr_t i = 0; i < n; ++i) {
                void* f = Get<void*>(found.Get(), 0x20 + 8 * i); if (!f) continue;
                void* go = Invoke(Unity("Component", "get_gameObject", 0), f);
                Root tr(Invoke(Method(go, "get_transform", 0), go));
                const DoorVec3 at = Value<DoorVec3>(Unity("Transform", "get_position", 0), tr.Get());
                if (std::abs(at.x - x0) > half || std::abs(at.z - z0) > half) continue;
                void* mesh = Invoke(Method(f, "get_sharedMesh", 0), f);
                if (!mesh) continue;
                Row& r = rows[nameOf(mesh)];
                if (!r.count) {
                    struct B { DoorVec3 c, e; } b = Value<B>(Method(mesh, "get_bounds", 0), mesh);
                    const DoorVec3 s = Value<DoorVec3>(Unity("Transform", "get_lossyScale", 0), tr.Get());
                    r.sx = 2 * b.e.x * std::abs(s.x); r.sy = 2 * b.e.y * std::abs(s.y); r.sz = 2 * b.e.z * std::abs(s.z);
                    r.readable = Value<bool>(Method(mesh, "get_isReadable", 0), mesh);
                    r.object = nameOf(go); r.layer = Value<int>(Method(go, "get_layer", 0), go);
                    void* parent = Invoke(Unity("Transform", "get_parent", 0), tr.Get());
                    if (parent) { void* pg = Invoke(Unity("Component", "get_gameObject", 0), parent); r.parent = nameOf(pg); }
                }
                ++r.count;
            }
            std::ostringstream out; out.setf(std::ios::fixed); out.precision(1); out << "{\"ok\":true,\"meshes\":[";
            bool first = true;
            for (const auto& [k, r] : rows) {
                if (!first) out << ','; first = false;
                out << "{\"mesh\":" << Json(k) << ",\"n\":" << r.count << ",\"size\":[" << r.sx << ',' << r.sy << ',' << r.sz << "],\"read\":" << Boolean(r.readable) << ",\"object\":" << Json(r.object) << ",\"parent\":" << Json(r.parent) << ",\"layer\":" << r.layer << '}';
            }
            return out.str() + "]}";
        }
        if (action == "mapfloor") {
            // the walkable shape of a place, from the game's own NavMesh, as a hidden collider somewhere else
            const float x0 = std::stof(args.at(1)), z0 = std::stof(args.at(2)), hx = std::stof(args.at(3)), hz = std::stof(args.at(4));
            const DoorVec3 off{std::stof(args.at(5)), std::stof(args.at(6)), std::stof(args.at(7))};
            const int layer = std::stoi(args.at(8));
            // optional: stretched on x and z around a pivot, as the copies are
            const float stretchX = args.size() > 9 ? std::stof(args.at(9)) : 1.f, stretchZ = args.size() > 10 ? std::stof(args.at(10)) : 1.f;
            const float pivotX = args.size() > 11 ? std::stof(args.at(11)) : x0, pivotZ = args.size() > 12 ? std::stof(args.at(12)) : z0;
            struct Tri { void* vertices; void* indices; void* areas; };
            const Tri tri = Value<Tri>(game::FindMethod("UnityEngine.AIModule.dll", "UnityEngine.AI", "NavMesh", "CalculateTriangulation", 0));
            const auto vn = Get<uintptr_t>(tri.vertices, 0x18), in = Get<uintptr_t>(tri.indices, 0x18);
            const auto* verts = reinterpret_cast<DoorVec3*>(static_cast<char*>(tri.vertices) + 0x20);
            const auto* ids = reinterpret_cast<int*>(static_cast<char*>(tri.indices) + 0x20);
            std::vector<DoorVec3> outV; std::vector<int> outI; std::map<int, int> remap;
            const auto inside = [&](int k) { return std::abs(verts[k].x - x0) <= hx && std::abs(verts[k].z - z0) <= hz; };
            for (uintptr_t i = 0; i + 2 < in; i += 3) {
                if (!inside(ids[i]) || !inside(ids[i + 1]) || !inside(ids[i + 2])) continue;
                for (int j = 0; j < 3; ++j) {
                    auto it = remap.find(ids[i + j]);
                    if (it == remap.end()) { it = remap.emplace(ids[i + j], static_cast<int>(outV.size())).first; const DoorVec3 v = verts[ids[i + j]]; outV.push_back({pivotX + (v.x - pivotX) * stretchX + off.x, v.y + off.y, pivotZ + (v.z - pivotZ) * stretchZ + off.z}); }
                    outI.push_back(it->second);
                }
            }
            if (outI.empty()) throw std::runtime_error("no NavMesh in the box");
            auto& api = il2cpp::api();
            Root varray(api.array_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Vector3"), outV.size()));
            std::memcpy(static_cast<char*>(varray.Get()) + 0x20, outV.data(), outV.size() * sizeof(DoorVec3));
            Root iarray(api.array_new(game::FindClass("mscorlib.dll", "System", "Int32"), outI.size()));
            std::memcpy(static_cast<char*>(iarray.Get()) + 0x20, outI.data(), outI.size() * sizeof(int));
            static std::vector<std::unique_ptr<Root>> keep;
            keep.push_back(std::make_unique<Root>(api.object_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Mesh"))));
            void* mesh = keep.back()->Get();
            Invoke(DoorSignature("Mesh", ".ctor", {}), mesh);
            int bits = 1; void* fa[]{&bits}; Invoke(Method(mesh, "set_indexFormat", 1), mesh, fa);  // 32 bit
            void* va[]{varray.Get()}; Invoke(DoorSignature("Mesh", "set_vertices", {"UnityEngine.Vector3[]"}), mesh, va);
            void* ta[]{iarray.Get()}; Invoke(DoorSignature("Mesh", "set_triangles", {"System.Int32[]"}), mesh, ta);
            Invoke(Method(mesh, "RecalculateBounds", 0), mesh);
            keep.push_back(std::make_unique<Root>(api.object_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject"))));
            void* holder = keep.back()->Get();
            void* nameArgs[]{api.string_new("EP floor")};
            Invoke(DoorSignature("GameObject", ".ctor", {"System.String"}), holder, nameArgs);
            int layerValue = layer; void* la[]{&layerValue}; Invoke(Method(holder, "set_layer", 1), holder, la);
            Root colliderType(api.type_get_object(api.class_get_type(game::FindClass("UnityEngine.PhysicsModule.dll", "UnityEngine", "MeshCollider"))));
            void* ca[]{colliderType.Get()};
            void* collider = Invoke(DoorSignature("GameObject", "AddComponent", {"System.Type"}), holder, ca);
            if (!collider) throw std::runtime_error("no collider");
            void* ma[]{mesh}; Invoke(Method(collider, "set_sharedMesh", 1), collider, ma);
            std::ostringstream out; out << "{\"ok\":true,\"allVertices\":" << vn << ",\"vertices\":" << outV.size() << ",\"triangles\":" << outI.size() / 3 << '}';
            return out.str();
        }
        if (action == "mapexact") {
            // NavMesh from one layer's colliders only, with a small agent radius, so a shape taken from the game's NavMesh is not shrunk again
            const DoorVec3 centre{std::stof(args.at(1)), std::stof(args.at(2)), std::stof(args.at(3))};
            const DoorVec3 size{std::stof(args.at(4)), std::stof(args.at(5)), std::stof(args.at(6))};
            const int layer = std::stoi(args.at(7));
            const float radius = args.size() > 8 ? std::stof(args.at(8)) : 0.05f;
            auto& api = il2cpp::api();
            const auto* surfaceClass = game::FindClass("Unity.AI.Navigation.dll", "Unity.AI.Navigation", "NavMeshSurface");
            static std::vector<std::unique_ptr<Root>> keep;
            keep.push_back(std::make_unique<Root>(api.object_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject"))));
            void* holder = keep.back()->Get();
            void* nameArgs[]{api.string_new("EP exact surface")};
            Invoke(DoorSignature("GameObject", ".ctor", {"System.String"}), holder, nameArgs);
            Root tr(Invoke(Method(holder, "get_transform", 0), holder));
            DoorVec3 at = centre; void* place[]{&at}; Invoke(Unity("Transform", "set_position", 1), tr.Get(), place);
            Root surfaceType(api.type_get_object(api.class_get_type(surfaceClass)));
            void* ta[]{surfaceType.Get()};
            void* surface = Invoke(DoorSignature("GameObject", "AddComponent", {"System.Type"}), holder, ta);
            int collect = 1; void* a1[]{&collect}; Invoke(Method(surface, "set_collectObjects", 1), surface, a1);
            int physics = 1; void* a2[]{&physics}; Invoke(Method(surface, "set_useGeometry", 1), surface, a2);
            DoorVec3 s = size; void* a3[]{&s}; Invoke(Method(surface, "set_size", 1), surface, a3);
            DoorVec3 zero{}; void* a4[]{&zero}; Invoke(Method(surface, "set_center", 1), surface, a4);
            int mask = 1 << layer; void* a5[]{&mask}; Invoke(Method(surface, "set_layerMask", 1), surface, a5);
            struct BuildSettings { int type; float radius, height, slope, climb, ledge, jump, minRegion; int overrideVoxel; float voxel; int overrideTile, tile, buildHeight; unsigned workers; int preserve; uint32_t debug; };
            BuildSettings settings = Value<BuildSettings>(Method(surface, "GetBuildSettings", 0), surface);
            const float wasRadius = settings.radius, wasClimb = settings.climb;
            settings.radius = radius; settings.minRegion = 0;
            settings.overrideVoxel = 1; settings.voxel = 0.1f;
            Root sources(Invoke(Method(surface, "CollectSources", 0), surface));
            const int sourceCount = sources.Get() ? Value<int>(Method(sources.Get(), "get_Count", 0), sources.Get()) : -1;
            struct B { DoorVec3 c, e; } bounds{{0, 0, 0}, {size.x / 2, size.y / 2, size.z / 2}};
            struct Q { float x, y, z, w; } rotation{0, 0, 0, 1};
            DoorVec3 where = centre;
            void* ba[]{&settings, sources.Get(), &bounds, &where, &rotation};
            const uint64_t t0 = GetTickCount64();
            void* data = Invoke(DoorSignature("NavMeshBuilder", "BuildNavMeshData", {"UnityEngine.AI.NavMeshBuildSettings", "System.Collections.Generic.List<UnityEngine.AI.NavMeshBuildSource>", "UnityEngine.Bounds", "UnityEngine.Vector3", "UnityEngine.Quaternion"}, "UnityEngine.AI", "UnityEngine.AIModule.dll"), nullptr, ba);
            const uint64_t ms = GetTickCount64() - t0;
            if (!data) throw std::runtime_error("the NavMesh did not build");
            void* da[]{data}; Invoke(Method(surface, "set_navMeshData", 1), surface, da);
            Invoke(Method(surface, "AddData", 0), surface);
            DoorVec3 walk{}; const bool middle = SampleWalkable(centre, 6.f, &walk);
            std::ostringstream out; out << "{\"ok\":true,\"ms\":" << ms << ",\"sources\":" << sourceCount << ",\"radiusWas\":" << wasRadius << ",\"climb\":" << wasClimb << ",\"middle\":" << Boolean(middle) << ",\"at\":[" << walk.x << ',' << walk.y << ',' << walk.z << "]}";
            return out.str();
        }
        if (action == "areamats") {
            // every material in use: name, shader, one object
            const auto* cls = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "MeshRenderer");
            void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));
            bool inactive = false; void* findArgs[]{type, &inactive};
            Root found(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, findArgs));
            const auto n = found.Get() ? Get<uintptr_t>(found.Get(), 0x18) : 0;
            const auto nameOf = [](void* o) { return Text(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0), o)); };
            std::map<std::string, std::pair<std::string, std::string>> rows; std::map<std::string, int> counts;
            for (uintptr_t i = 0; i < n; ++i) {
                void* r = Get<void*>(found.Get(), 0x20 + 8 * i); if (!r) continue;
                void* m = Invoke(Method(r, "get_sharedMaterial", 0), r); if (!m) continue;
                const std::string name = nameOf(m);
                if (!counts[name]++) { void* shader = Invoke(Method(m, "get_shader", 0), m); void* go = Invoke(Unity("Component", "get_gameObject", 0), r); rows[name] = {shader ? nameOf(shader) : "", nameOf(go)}; }
            }
            std::ostringstream out; out << "{\"ok\":true,\"materials\":["; bool first = true;
            for (const auto& [k, v] : rows) { if (!first) out << ','; first = false; out << "{\"m\":" << Json(k) << ",\"n\":" << counts[k] << ",\"shader\":" << Json(v.first) << ",\"object\":" << Json(v.second) << '}'; }
            return out.str() + "]}";
        }
        if (action == "areaplate") {
            // areaplate cx y cz radius sides rock material-object [uvscale] [flat 0|1]: a ground of our own: an outline of many sides, flat on top,
            // falling away into rock at its edge, in the material of a named object
            const DoorVec3 c{std::stof(args.at(1)), std::stof(args.at(2)), std::stof(args.at(3))};
            const float radius = std::stof(args.at(4)); const int sides = std::stoi(args.at(5)); const float rock = std::stof(args.at(6));
            std::string wanted = args.at(7); std::replace(wanted.begin(), wanted.end(), '_', ' ');
            const float uvScale = args.size() > 8 ? std::stof(args.at(8)) : 0.1f;
            const auto nameOf = [](void* o) { return Text(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0), o)); };
            auto& api = il2cpp::api();
            const auto* rendererClass = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "MeshRenderer");
            void* type = api.type_get_object(api.class_get_type(rendererClass));
            bool inactive = false; void* findArgs[]{type, &inactive};
            Root found(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, findArgs));
            const auto n = found.Get() ? Get<uintptr_t>(found.Get(), 0x18) : 0;
            void* material = nullptr; int sourceLayer = 0;
            for (uintptr_t i = 0; i < n && !material; ++i) {
                void* r = Get<void*>(found.Get(), 0x20 + 8 * i); if (!r) continue;
                void* go = Invoke(Unity("Component", "get_gameObject", 0), r);
                std::string named = nameOf(go); std::replace(named.begin(), named.end(), '_', ' ');
                if (named == wanted) { material = Invoke(Method(r, "get_sharedMaterial", 0), r); sourceLayer = Value<int>(Method(go, "get_layer", 0), go); }
            }
            if (!material) throw std::runtime_error("no such object with a material");
            // rings: top from the middle out, then the edge going down and in
            std::vector<DoorVec3> v; std::vector<int> ids; struct V2 { float u, v; }; std::vector<V2> uv;
            const float pi = 3.14159265f;
            struct Ring { float r, y; };
            std::vector<Ring> rings;
            for (int i = 1; i <= 8; ++i) rings.push_back({radius * i / 8.f, 0.f});
            rings.push_back({radius * 1.01f, -rock * .15f}); rings.push_back({radius * .95f, -rock * .45f}); rings.push_back({radius * .7f, -rock * .8f}); rings.push_back({radius * .3f, -rock});
            v.push_back({0, 0, 0}); uv.push_back({0, 0});
            for (const auto& ring : rings)
                for (int s = 0; s < sides; ++s) {
                    const float angle = 2 * pi * s / sides;
                    // a little unevenness at the edge, so the outline is not a compass circle
                    const float wobble = ring.y < 0 ? 1.f + .03f * std::sin(angle * 7) + .02f * std::sin(angle * 13) : 1.f;
                    const float x = std::cos(angle) * ring.r * wobble, z = std::sin(angle) * ring.r * wobble;
                    v.push_back({x, ring.y, z}); uv.push_back({x * uvScale, (z + (ring.y < 0 ? ring.y : 0)) * uvScale});
                }
            for (int s = 0; s < sides; ++s) { ids.push_back(0); ids.push_back(1 + (s + 1) % sides); ids.push_back(1 + s); }
            for (size_t ring = 0; ring + 1 < rings.size(); ++ring)
                for (int s = 0; s < sides; ++s) {
                    const int a0 = 1 + int(ring) * sides + s, a1 = 1 + int(ring) * sides + (s + 1) % sides, b0 = a0 + sides, b1 = a1 + sides;
                    ids.push_back(a0); ids.push_back(a1); ids.push_back(b0);
                    ids.push_back(a1); ids.push_back(b1); ids.push_back(b0);
                }
            Root va(api.array_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Vector3"), v.size()));
            std::memcpy(static_cast<char*>(va.Get()) + 0x20, v.data(), v.size() * sizeof(DoorVec3));
            Root ua(api.array_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Vector2"), uv.size()));
            std::memcpy(static_cast<char*>(ua.Get()) + 0x20, uv.data(), uv.size() * sizeof(V2));
            Root ia(api.array_new(game::FindClass("mscorlib.dll", "System", "Int32"), ids.size()));
            std::memcpy(static_cast<char*>(ia.Get()) + 0x20, ids.data(), ids.size() * sizeof(int));
            Root mesh(api.object_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Mesh")));
            Invoke(DoorSignature("Mesh", ".ctor", {}), mesh.Get());
            int bits = 1; void* fa[]{&bits}; Invoke(Method(mesh.Get(), "set_indexFormat", 1), mesh.Get(), fa);
            void* a1[]{va.Get()}; Invoke(DoorSignature("Mesh", "set_vertices", {"UnityEngine.Vector3[]"}), mesh.Get(), a1);
            void* a2[]{ua.Get()}; Invoke(DoorSignature("Mesh", "set_uv", {"UnityEngine.Vector2[]"}), mesh.Get(), a2);
            void* a3[]{ia.Get()}; Invoke(DoorSignature("Mesh", "set_triangles", {"System.Int32[]"}), mesh.Get(), a3);
            Invoke(Method(mesh.Get(), "RecalculateNormals", 0), mesh.Get());
            Invoke(Method(mesh.Get(), "RecalculateTangents", 0), mesh.Get());
            Invoke(Method(mesh.Get(), "RecalculateBounds", 0), mesh.Get());
            static std::vector<std::unique_ptr<Root>> keep;
            keep.push_back(std::make_unique<Root>(api.object_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject"))));
            void* holder = keep.back()->Get();
            void* nameArgs[]{api.string_new("EP plate")};
            Invoke(DoorSignature("GameObject", ".ctor", {"System.String"}), holder, nameArgs);
            Root tr(Invoke(Method(holder, "get_transform", 0), holder));
            DoorVec3 at = c; void* place[]{&at}; Invoke(Unity("Transform", "set_position", 1), tr.Get(), place);
            int layer = sourceLayer; void* la[]{&layer}; Invoke(Method(holder, "set_layer", 1), holder, la);
            const auto* add = DoorSignature("GameObject", "AddComponent", {"System.Type"});
            Root filterType(api.type_get_object(api.class_get_type(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "MeshFilter"))));
            void* ft[]{filterType.Get()}; void* filter = Invoke(add, holder, ft);
            void* m1[]{mesh.Get()}; Invoke(Method(filter, "set_sharedMesh", 1), filter, m1);
            Root rendererType(type);
            void* rt[]{rendererType.Get()}; void* renderer = Invoke(add, holder, rt);
            void* m2[]{material}; Invoke(Method(renderer, "set_sharedMaterial", 1), renderer, m2);
            Root colliderType(api.type_get_object(api.class_get_type(game::FindClass("UnityEngine.PhysicsModule.dll", "UnityEngine", "MeshCollider"))));
            void* ct[]{colliderType.Get()}; void* collider = Invoke(add, holder, ct);
            void* m3[]{mesh.Get()}; Invoke(Method(collider, "set_sharedMesh", 1), collider, m3);
            void* shader = Invoke(Method(material, "get_shader", 0), material);
            std::ostringstream out; out << "{\"ok\":true,\"material\":" << Json(nameOf(material)) << ",\"shader\":" << Json(shader ? nameOf(shader) : "") << ",\"layer\":" << sourceLayer << ",\"vertices\":" << v.size() << '}';
            return out.str();
        }
        if (action == "assetinfo") {
            void* manager = Invoke(game::FindMethod("LE.Addressables.dll", "LE.AssetBundles", "AssetBundlesManager", "get_InstanceOrNull", 0));
            if (!manager) throw std::runtime_error("no asset manager");
            void* catalog = Invoke(Method(manager, "get_Catalog", 0), manager);
            void* lut = Invoke(Method(manager, "get_AssetPathLUT", 0), manager);
            void* directory = Invoke(game::FindMethod("LE.Addressables.dll", "LE.AssetBundles", "AssetBundlesManager", "get_BundleDirectory", 0));
            void* files = Get<void*>(catalog, game::FieldOffset("LE.Addressables.dll", "", "LEAssetBundlesCatalog", "bundleFileNames"));
            void* guids = Get<void*>(catalog, game::FieldOffset("LE.Addressables.dll", "", "LEAssetBundlesCatalog", "assetGuidToBundleId"));
            std::ostringstream out; out << "{\"ok\":true,\"dir\":" << Json(Text(directory)) << ",\"lut\":" << Boolean(lut != nullptr) << ",\"files\":" << (files ? Get<uintptr_t>(files, 0x18) : 0)
                << ",\"guids\":" << (guids ? Value<int>(Method(guids, "get_Count", 0), guids) : -1) << ",\"assetBundles\":" << Value<int>(Method(catalog, "get_AssetBundleCount", 0), catalog) << ",\"first\":[";
            for (uintptr_t i = 0; files && i < 5 && i < Get<uintptr_t>(files, 0x18); ++i) out << (i ? "," : "") << Json(Text(Get<void*>(files, 0x20 + 8 * i)));
            return out.str() + "]}";
        }
        if (action == "assetnames") {
            // the names inside bundles, opened and closed again in the same call
            const size_t from = std::stoul(args.at(1)), count = std::stoul(args.at(2));
            void* manager = Invoke(game::FindMethod("LE.Addressables.dll", "LE.AssetBundles", "AssetBundlesManager", "get_InstanceOrNull", 0));
            void* catalog = Invoke(Method(manager, "get_Catalog", 0), manager);
            const std::string directory = Text(Invoke(game::FindMethod("LE.Addressables.dll", "LE.AssetBundles", "AssetBundlesManager", "get_BundleDirectory", 0)));
            void* files = Get<void*>(catalog, game::FieldOffset("LE.Addressables.dll", "", "LEAssetBundlesCatalog", "bundleFileNames"));
            const auto total = Get<uintptr_t>(files, 0x18);
            const auto load = game::FindMethod("UnityEngine.AssetBundleModule.dll", "UnityEngine", "AssetBundle", "LoadFromFile", 1);
            const uint64_t t0 = GetTickCount64();
            std::ostringstream out; out << "{\"ok\":true,\"total\":" << total << ",\"rows\":["; bool first = true; int opened = 0, already = 0;
            for (size_t i = from; i < from + count && i < total; ++i) {
                const std::string name = Text(Get<void*>(files, 0x20 + 8 * i));
                void* path[]{il2cpp::api().string_new((directory + name).c_str())};
                void* opened1 = Invoke(load, nullptr, path);
                if (!opened1) { ++already; continue; }
                Root bundle(opened1);
                ++opened;
                Root names(Invoke(Method(bundle.Get(), "GetAllAssetNames", 0), bundle.Get()));
                if (!first) out << ','; first = false;
                out << "{\"f\":" << Json(name) << ",\"n\":[";
                const auto k = names.Get() ? Get<uintptr_t>(names.Get(), 0x18) : 0;
                for (uintptr_t j = 0; j < k && j < 12; ++j) out << (j ? "," : "") << Json(Text(Get<void*>(names.Get(), 0x20 + 8 * j)));
                out << "],\"c\":" << k << '}';
                bool unloadAll = false; void* ua[]{&unloadAll}; Invoke(Method(bundle.Get(), "Unload", 1), bundle.Get(), ua);
            }
            out << "],\"opened\":" << opened << ",\"inUse\":" << already << ",\"ms\":" << GetTickCount64() - t0 << '}';
            return out.str();
        }
        if (action == "assetprefabs") {
            // assetprefabs from count | loaded: each prefab's Guid, its real name and what it is made of
            auto& api = il2cpp::api();
            const bool loadedOnes = args.at(1) == "loaded";
            const size_t from = loadedOnes ? std::stoul(args.at(2)) : std::stoul(args.at(1)), count = loadedOnes ? std::stoul(args.at(3)) : std::stoul(args.at(2));
            void* manager = Invoke(game::FindMethod("LE.Addressables.dll", "LE.AssetBundles", "AssetBundlesManager", "get_InstanceOrNull", 0));
            void* catalog = Invoke(Method(manager, "get_Catalog", 0), manager);
            const std::string directory = Text(Invoke(game::FindMethod("LE.Addressables.dll", "LE.AssetBundles", "AssetBundlesManager", "get_BundleDirectory", 0)));
            void* files = Get<void*>(catalog, game::FieldOffset("LE.Addressables.dll", "", "LEAssetBundlesCatalog", "bundleFileNames"));
            const auto total = Get<uintptr_t>(files, 0x18);
            const auto load = game::FindMethod("UnityEngine.AssetBundleModule.dll", "UnityEngine", "AssetBundle", "LoadFromFile", 1);
            Root goType(api.type_get_object(api.class_get_type(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject"))));
            const auto* childrenOf = DoorSignature("GameObject", "GetComponentsInChildren", {"System.Type", "System.Boolean"});
            struct Kind { const char* key; std::unique_ptr<Root> type; };
            std::vector<Kind> kinds;
            const auto addKind = [&](const char* key, const char* image, const char* cls) { if (const auto* c = game::FindClass(image, "UnityEngine", cls)) kinds.push_back({key, std::make_unique<Root>(api.type_get_object(api.class_get_type(c)))}); };
            addKind("m", "UnityEngine.CoreModule.dll", "MeshRenderer"); addKind("s", "UnityEngine.CoreModule.dll", "SkinnedMeshRenderer"); addKind("p", "UnityEngine.ParticleSystemModule.dll", "ParticleSystem");
            addKind("l", "UnityEngine.CoreModule.dll", "Light"); addKind("c", "UnityEngine.PhysicsModule.dll", "Collider"); addKind("b", "UnityEngine.CoreModule.dll", "MonoBehaviour");
            const uint64_t t0 = GetTickCount64();
            std::ostringstream out; out << "{\"ok\":true,\"total\":" << total << ",\"rows\":["; bool first = true; int opened = 0, already = 0, assets = 0;
            size_t scanIndex = 0;
            const auto scan = [&](void* bundle) {
                Root names(Invoke(Method(bundle, "GetAllAssetNames", 0), bundle));
                const auto k = names.Get() ? Get<uintptr_t>(names.Get(), 0x18) : 0;
                for (uintptr_t j = 0; j < k; ++j) {
                    void* nameObject = Get<void*>(names.Get(), 0x20 + 8 * j);
                    ++assets;
                    void* la[]{nameObject, goType.Get()};
                    void* prefab = Invoke(DoorSignature("AssetBundle", "LoadAsset", {"System.String", "System.Type"}, "UnityEngine", "UnityEngine.AssetBundleModule.dll"), bundle, la);
                    if (!prefab) continue;
                    if (!first) out << ','; first = false;
                    out << "{\"g\":" << Json(Text(nameObject)) << ",\"f\":" << scanIndex << ",\"n\":" << Json(Text(Invoke(Unity("Object", "get_name", 0), prefab)));
                    for (const auto& kind : kinds) {
                        bool all = true; void* ca[]{kind.type->Get(), &all};
                        void* list = Invoke(childrenOf, prefab, ca);
                        const auto c = list ? Get<uintptr_t>(list, 0x18) : 0;
                        if (c) out << ",\"" << kind.key << "\":" << c;
                    }
                    out << '}';
                }
            };
            if (loadedOnes) {
                Root loaded(Invoke(game::FindMethod("UnityEngine.AssetBundleModule.dll", "UnityEngine", "AssetBundle", "GetAllLoadedAssetBundles_Native", 0)));
                const auto n = loaded.Get() ? Get<uintptr_t>(loaded.Get(), 0x18) : 0;
                for (size_t i = from; i < from + count && i < n; ++i) if (void* bundle = Get<void*>(loaded.Get(), 0x20 + 8 * i)) { ++opened; scan(bundle); }
                out << "],\"loadedTotal\":" << n;
            } else {
                for (size_t i = from; i < from + count && i < total; ++i) {
                    const std::string name = Text(Get<void*>(files, 0x20 + 8 * i));
                    void* path[]{api.string_new((directory + name).c_str())};
                    scanIndex = i;
                    void* opened1 = Invoke(load, nullptr, path);
                    if (!opened1) {
                        // the game has it open: its own entry knows the bundle
                        ++already;
                        const uint16_t begin = Value<uint16_t>(Method(Invoke(Method(manager, "get_Catalog", 0), manager), "get_AllBundleIdsBegin", 0), Invoke(Method(manager, "get_Catalog", 0), manager));
                        uint16_t id = static_cast<uint16_t>(begin + i); void* ia[]{&id};
                        void* entry = Invoke(Method(manager, "GetAssetBundleEntry", 1), manager, ia);
                        void* inUse = entry ? Get<void*>(entry, game::FieldOffset("LE.Addressables.dll", "LE.AssetBundles", "BundleEntry", "assetBundle")) : nullptr;
                        if (inUse) scan(inUse);
                        continue;
                    }
                    Root bundle(opened1);
                    ++opened;
                    scan(bundle.Get());
                    bool unloadAll = true; void* ua[]{&unloadAll}; Invoke(Method(bundle.Get(), "Unload", 1), bundle.Get(), ua);
                }
                out << "]";
            }
            out << ",\"opened\":" << opened << ",\"inUse\":" << already << ",\"assets\":" << assets << ",\"ms\":" << GetTickCount64() - t0 << '}';
            return out.str();
        }
        if (action == "assetid") {
            // how a file of the catalog is named as a bundle id
            void* manager = Invoke(game::FindMethod("LE.Addressables.dll", "LE.AssetBundles", "AssetBundlesManager", "get_InstanceOrNull", 0));
            void* catalog = Invoke(Method(manager, "get_Catalog", 0), manager);
            void* files = Get<void*>(catalog, game::FieldOffset("LE.Addressables.dll", "", "LEAssetBundlesCatalog", "bundleFileNames"));
            const uint16_t begin = Value<uint16_t>(Method(Invoke(Method(manager, "get_Catalog", 0), manager), "get_AllBundleIdsBegin", 0), Invoke(Method(manager, "get_Catalog", 0), manager));
            std::ostringstream out; out << "{\"ok\":true,\"begin\":" << begin << ",\"rows\":[";
            for (int i = 0; i < 3; ++i) {
                uint16_t id = static_cast<uint16_t>(begin + i); void* a[]{&id};
                void* name = Invoke(Method(manager, "GetBundleNameForDiagnostic", 1), manager, a);
                out << (i ? "," : "") << "[" << Json(Text(Get<void*>(files, 0x20 + 8 * i))) << "," << Json(name ? Text(name) : "") << "]";
            }
            return out.str() + "]}";
        }
        if (action == "assetopen" || action == "assettake") {
            // assetopen <file index>: the game's manager opens a bundle and all it depends on. assettake <hex> x y z [scale]: its prefab placed.
            static std::unique_ptr<Root> arrayRef; static uint16_t openId = 0;
            void* manager = Invoke(game::FindMethod("LE.Addressables.dll", "LE.AssetBundles", "AssetBundlesManager", "get_InstanceOrNull", 0));
            if (action == "assetopen") {
                const uint16_t begin = Value<uint16_t>(Method(Invoke(Method(manager, "get_Catalog", 0), manager), "get_AllBundleIdsBegin", 0), Invoke(Method(manager, "get_Catalog", 0), manager));
                openId = static_cast<uint16_t>(begin + std::stoul(args.at(1)));
                Root ids(il2cpp::api().array_new(game::FindClass("LE.Addressables.dll", "LE.AssetBundles", "BundleId"), 1));
                *reinterpret_cast<uint16_t*>(static_cast<char*>(ids.Get()) + 0x20) = openId;
                void* file = il2cpp::api().string_new("epochpact"); int line = 1; void* ca[]{ids.Get(), file, &line};
                arrayRef = std::make_unique<Root>(Invoke(game::FindMethod("LE.Addressables.dll", "LE.AssetBundles", "AssetBundlesManager", "CreateBundleIdArrayLoadRef", 3), nullptr, ca));
                return "{\"ok\":true,\"id\":" + std::to_string(openId) + ",\"status\":" + std::to_string(Value<int>(Method(arrayRef->Get(), "get_Status", 0), arrayRef->Get())) + "}";
            }
            if (!arrayRef) throw std::runtime_error("nothing is open");
            const int status = Value<int>(Method(arrayRef->Get(), "get_Status", 0), arrayRef->Get());
            std::ostringstream out; out << "{\"ok\":true,\"status\":" << status;
            if (status == 2 && args.size() > 1) {
                uint16_t id = openId; void* ia[]{&id};
                void* entry = Invoke(Method(manager, "GetAssetBundleEntry", 1), manager, ia);
                void* bundle = entry ? Get<void*>(entry, game::FieldOffset("LE.Addressables.dll", "LE.AssetBundles", "BundleEntry", "assetBundle")) : nullptr;
                if (!bundle) throw std::runtime_error("the bundle is not there");
                Root goType(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject"))));
                void* la[]{il2cpp::api().string_new(args.at(1).c_str()), goType.Get()};
                void* prefab = Invoke(DoorSignature("AssetBundle", "LoadAsset", {"System.String", "System.Type"}, "UnityEngine", "UnityEngine.AssetBundleModule.dll"), bundle, la);
                if (!prefab) throw std::runtime_error("no such prefab in the bundle");
                out << ",\"name\":" << Json(Text(Invoke(Unity("Object", "get_name", 0), prefab)));
                if (args.size() > 4) {
                    void* holder = NewGameObject("EP placed");
                    bool off = false; void* oa[]{&off}; Invoke(Method(holder, "SetActive", 1), holder, oa);
                    Root parent(Invoke(Method(holder, "get_transform", 0), holder));
                    bool world = true; void* pa[]{prefab, parent.Get(), &world};
                    void* copy = Invoke(DoorSignature("Object", "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform", "System.Boolean"}), nullptr, pa);
                    Root tr(Invoke(Method(copy, "get_transform", 0), copy));
                    DoorVec3 at{std::stof(args.at(2)), std::stof(args.at(3)), std::stof(args.at(4))}; void* wa[]{&at}; Invoke(Unity("Transform", "set_position", 1), tr.Get(), wa);
                    if (args.size() > 5) { const float s = std::stof(args.at(5)); DoorVec3 sized{s, s, s}; void* sa[]{&sized}; Invoke(Unity("Transform", "set_localScale", 1), tr.Get(), sa); }
                    SetChildrenEnabled(copy, "UnityEngine.CoreModule.dll", "MonoBehaviour", false);
                    bool on = true; void* na[]{&on}; Invoke(Method(holder, "SetActive", 1), holder, na);
                    // the bundle stays open while its copy lives
                    static std::vector<std::unique_ptr<Root>> keep; keep.push_back(std::make_unique<Root>(arrayRef->Get()));
                    out << ",\"placed\":true";
                }
            }
            return out.str() + "}";
        }
        if (action == "assetload" || action == "assetplace") {
            // through the game's own loader, with what the prefab depends on: assetload <hex guid>; assetplace x y z [scale]
            static std::unique_ptr<Root> softRef, loadRef;
            if (action == "assetload") {
                void* encounter = Invoke(game::FindMethod("LE.dll", "", "RandomEncounterData", "get", 0));
                void* sample = encounter ? Get<void*>(encounter, Offset("RandomEncounterData", "nemesisEncounterGameplayPrefabSoftRef")) : nullptr;
                if (!sample) throw std::runtime_error("no soft reference to copy");
                struct G { uint64_t a, b; };
                void* hex[]{il2cpp::api().string_new(args.at(1).c_str())};
                G guid = Value<G>(game::FindMethod("LE.Addressables.dll", "LE.AssetBundles", "Guid", "FromHex", 1), nullptr, hex);
                void* sub = il2cpp::api().string_new("");
                void* ca[]{&guid, sub};
                softRef = std::make_unique<Root>(Invoke(Method(sample, "CreateNewOfSameType", 2), sample, ca));
                void* file = il2cpp::api().string_new("epochpact"); int line = 1; void* la[]{file, &line};
                loadRef = std::make_unique<Root>(Invoke(Method(softRef->Get(), "CreateLoadRefTypelessImpl", 2), softRef->Get(), la));
                return "{\"ok\":true,\"guid\":[" + std::to_string(guid.a) + "," + std::to_string(guid.b) + "],\"status\":" + std::to_string(Value<int>(Method(loadRef->Get(), "get_Status", 0), loadRef->Get())) + "}";
            }
            if (!loadRef) throw std::runtime_error("nothing is loading");
            const int status = Value<int>(Method(loadRef->Get(), "get_Status", 0), loadRef->Get());
            void* asset = Invoke(Method(loadRef->Get(), "get_TypelessAssetOrNull", 0), loadRef->Get());
            std::ostringstream out; out << "{\"ok\":true,\"status\":" << status << ",\"loaded\":" << Boolean(asset != nullptr);
            if (asset) {
                out << ",\"name\":" << Json(Text(Invoke(Unity("Object", "get_name", 0), asset)));
                if (args.size() > 3) {
                    void* ia[]{asset};
                    void* holder = NewGameObject("EP placed");
                    bool off = false; void* oa[]{&off}; Invoke(Method(holder, "SetActive", 1), holder, oa);
                    Root parent(Invoke(Method(holder, "get_transform", 0), holder));
                    bool world = true; void* pa[]{asset, parent.Get(), &world};
                    void* copy = Invoke(DoorSignature("Object", "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform", "System.Boolean"}), nullptr, pa);
                    Root tr(Invoke(Method(copy, "get_transform", 0), copy));
                    DoorVec3 at{std::stof(args.at(1)), std::stof(args.at(2)), std::stof(args.at(3))}; void* wa[]{&at}; Invoke(Unity("Transform", "set_position", 1), tr.Get(), wa);
                    if (args.size() > 4) { const float s = std::stof(args.at(4)); DoorVec3 sized{s, s, s}; void* sa[]{&sized}; Invoke(Unity("Transform", "set_localScale", 1), tr.Get(), sa); }
                    SetChildrenEnabled(copy, "UnityEngine.CoreModule.dll", "MonoBehaviour", false);
                    bool on = true; void* na[]{&on}; Invoke(Method(holder, "SetActive", 1), holder, na);
                    out << ",\"placed\":true";
                }
            }
            return out.str() + "}";
        }
        if (action == "objtree") {
            // objtree <class>: where objects with that script stand, their parents, the kinds of things under the top one asked for
            const std::string wanted = args.at(1);
            const int up = args.size() > 2 ? std::stoi(args.at(2)) : 3;
            const auto* cls = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "MonoBehaviour");
            void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));
            bool inactive = true; void* findArgs[]{type, &inactive};
            Root objects(Invoke(Unity("Object", "FindObjectsOfType", 2), nullptr, findArgs));
            const auto n = Get<uintptr_t>(objects.Get(), 0x18);
            std::ostringstream out; out << "{\"ok\":true,\"found\":["; bool first = true; int shown = 0;
            const auto nameOf = [](void* o) { return Text(Invoke(Unity("Object", "get_name", 0), o)); };
            const auto kindsUnder = [&](void* go, std::ostringstream& to) {
                to << '{';
                bool f2 = true;
                for (const char* kind : {"MeshRenderer", "SkinnedMeshRenderer", "Animator", "Collider", "Light"}) {
                    const auto* kc = game::FindClass(std::string(kind) == "Animator" ? "UnityEngine.AnimationModule.dll" : std::string(kind) == "Collider" ? "UnityEngine.PhysicsModule.dll" : "UnityEngine.CoreModule.dll", "UnityEngine", kind);
                    if (!kc) continue;
                    bool all = true; Root kt(il2cpp::api().type_get_object(il2cpp::api().class_get_type(kc))); void* ka[]{kt.Get(), &all};
                    Root list(Invoke(DoorSignature("GameObject", "GetComponentsInChildren", {"System.Type", "System.Boolean"}), go, ka));
                    if (!f2) to << ','; f2 = false;
                    to << '"' << kind << "\":" << (list.Get() ? Get<uintptr_t>(list.Get(), 0x18) : 0);
                }
                to << '}';
            };
            for (uintptr_t i = 0; i < n && shown < 12; ++i) {
                void* o = Get<void*>(objects.Get(), 0x20 + 8 * i); if (!o) continue;
                if (std::string(il2cpp::api().class_get_name(il2cpp::api().object_get_class(o))).find(wanted) == std::string::npos) continue;
                ++shown;
                if (!first) out << ','; first = false;
                out << "{\"class\":" << Json(il2cpp::api().class_get_name(il2cpp::api().object_get_class(o))) << ",\"at\":" << BossPosition(o) << ",\"chain\":[";
                void* tr = Invoke(Unity("Component", "get_transform", 0), o);
                for (int k = 0; k <= up && tr; ++k) {
                    void* go = Invoke(Unity("Component", "get_gameObject", 0), tr);
                    if (k) out << ',';
                    out << "{\"name\":" << Json(nameOf(go)) << ",\"kinds\":"; kindsUnder(go, out); out << '}';
                    tr = Invoke(Unity("Transform", "get_parent", 0), tr);
                }
                out << "]}";
            }
            return out.str() + "]}";
        }
        if (action == "mapclick") {
            // the game's own test of a point on the ground, as for a click (camera ray, its layer mask)
            DoorVec3 at{std::stof(args.at(1)), std::stof(args.at(2)), std::stof(args.at(3))};
            void* a[]{&at};
            const DoorVec3 hit = Value<DoorVec3>(game::FindMethod("LE.dll", "", "MouseManager", "WalkablePoint", 1), nullptr, a);
            std::ostringstream out; out << "{\"ok\":true,\"asked\":[" << at.x << ',' << at.y << ',' << at.z << "],\"point\":[" << hit.x << ',' << hit.y << ',' << hit.z << "]}";
            return out.str();
        }
        if (action == "mapmove") {
            // the player's own NavMesh agent walks to a place
            DoorVec3 to{std::stof(args.at(1)), std::stof(args.at(2)), std::stof(args.at(3))}, snapped{};
            if (SampleWalkable(to, 6.f, &snapped)) to = snapped;
            const auto* agentClass = game::FindClass("UnityEngine.AIModule.dll", "UnityEngine.AI", "NavMeshAgent");
            Root agentType(il2cpp::api().type_get_object(il2cpp::api().class_get_type(agentClass)));
            void* go = Invoke(Unity("Component", "get_gameObject", 0), c.actor);
            bool all = true; void* ca[]{agentType.Get(), &all};
            void* agent = Invoke(DoorSignature("GameObject", "GetComponentInChildren", {"System.Type", "System.Boolean"}), go, ca);
            if (!agent) throw std::runtime_error("no agent");
            const bool onMesh = Value<bool>(Method(agent, "get_isOnNavMesh", 0), agent);
            void* ma[]{&to};
            const bool started = onMesh ? Value<bool>(Method(agent, "SetDestination", 1), agent, ma) : false;
            std::ostringstream out; out << "{\"ok\":true,\"onMesh\":" << Boolean(onMesh) << ",\"started\":" << Boolean(started) << ",\"to\":[" << to.x << ',' << to.y << ',' << to.z << "],\"me\":" << BossPosition(c.actor) << '}';
            return out.str();
        }
        if (action == "scan") {
            // every active MonoBehaviour by class; with a filter, those classes' objects and places
            const auto* cls = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "MonoBehaviour");
            void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));
            bool inactive = args.size() > 2 && args[2] == "all"; void* findArgs[]{type, &inactive};
            Root objects(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, findArgs));
            const auto n = Get<uintptr_t>(objects.Get(), 0x18);
            std::map<std::string, int> counts;
            std::string filter = args.size() > 1 ? args[1] : "";
            std::ostringstream listed; bool first = true;
            for (uintptr_t i = 0; i < n; ++i) {
                void* o = Get<void*>(objects.Get(), 0x20 + 8 * i); if (!o) continue;
                const std::string name = il2cpp::api().class_get_name(il2cpp::api().object_get_class(o));
                ++counts[name];
                if (!filter.empty() && filter != "-" && name.find(filter) != std::string::npos) {
                    if (!first) listed << ','; first = false;
                    listed << "{\"class\":" << Json(name) << ",\"name\":" << Json(Text(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0), o)))
                           << ",\"at\":" << BossPosition(o) << '}';
                }
            }
            std::ostringstream out; out << "{\"ok\":true,\"total\":" << n << ",\"listed\":[" << listed.str() << "],\"classes\":{"; first = true;
            for (const auto& [k, v] : counts) { if (!first) out << ','; first = false; out << Json(k) << ':' << v; }
            return out.str() + "}}";
        }
        if (action == "actors") {
            const auto* cls = game::FindClass("LE.dll", "", "Actor");
            void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));
            bool inactive = true; void* findArgs[]{type, &inactive};
            Root objects(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, findArgs));
            const auto n = Get<uintptr_t>(objects.Get(), 0x18);
            std::ostringstream out; out << "{\"ok\":true,\"me\":" << BossPosition(c.actor) << ",\"actors\":[";
            for (uintptr_t i = 0; i < n; ++i) { void* o = Get<void*>(objects.Get(), 0x20 + 8 * i); if (!o) continue; if (i) out << ',';
                void* h = Get<void*>(o, Offset("Actor", "health"));
                out << "{\"name\":" << Json(Text(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0), o)))
                    << ",\"at\":" << BossPosition(o) << ",\"hp\":" << (h ? Get<float>(h, Offset("BaseHealth", "currentHealth")) : -1.f)
                    << ",\"active\":" << Boolean(Value<bool>(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Behaviour", "get_isActiveAndEnabled", 0), o)) << '}'; }
            return out.str() + "]}";
        }
        if (action == "warp") {
            BossVec3 at{std::stof(args.at(1)), std::stof(args.at(2)), std::stof(args.at(3))};
            void* agent = Get<void*>(c.actor, Offset("Actor", "navMeshAgent"));
            void* a[]{&at};
            const bool ok = Value<bool>(Method(agent, "Warp", 1), agent, a);
            return "{\"ok\":" + std::string(Boolean(ok)) + ",\"at\":" + BossPosition(c.actor) + "}";
        }
        if (action == "interact") {
            // the rock nearest the player answers as a click does (its EventSender's listener chain ends here)
            const auto* cls = game::FindClass("LE.dll", "", "OpenMonolithPanelInteraction");
            void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));
            bool inactive = false; void* findArgs[]{type, &inactive};
            Root objects(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, findArgs));
            const auto n = objects.Get() ? Get<uintptr_t>(objects.Get(), 0x18) : 0;
            const auto where = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_position", 0);
            const auto getTr = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_transform", 0);
            const auto me = Value<BossVec3>(where, Invoke(getTr, c.actor));
            void* best = nullptr; float bestD = 1e18f;
            for (uintptr_t i = 0; i < n; ++i) { void* o = Get<void*>(objects.Get(), 0x20 + 8 * i); if (!o) continue;
                const auto at = Value<BossVec3>(where, Invoke(getTr, o)); const float d = (at.x - me.x) * (at.x - me.x) + (at.z - me.z) * (at.z - me.z);
                if (d < bestD) { bestD = d; best = o; } }
            if (!best) throw std::runtime_error("no rock");
            void* go = Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_gameObject", 0), c.actor);
            void* a[]{go}; Invoke(Method(best, "Interaction", 1), best, a);
            return "{\"ok\":true,\"timeline\":" + std::to_string(int(Get<uint8_t>(best, Offset("OpenMonolithPanelInteraction", "timelineID")))) + ",\"distance2\":" + std::to_string(bestD) + "}";
        }
        if (action == "doorclick") {
            // door n (spot order) clicked, through the click listener's own method; doorstate tells where things are
            const size_t index = std::stoul(args.at(1));
            if (index >= doors.size()) throw std::runtime_error("no such door");
            void* go = Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_gameObject", 0), c.actor);
            const std::string kind=args.size()>2?args[2]:"click";
            if(kind!="click"&&kind!="nonclick"&&kind!="foreign"&&kind!="null"&&kind!="background")throw std::runtime_error("Unknown door input test.");
            if(kind=="foreign")go=doors[index].object->Get();
            if(kind=="null")go=nullptr;
            TestDoorClick(doors[index].listener->Get(),go,kind!="nonclick",kind!="background");
            return "{\"ok\":true,\"pending\":" + std::to_string(pendingBoss) + ",\"rush\":" + std::string(Boolean(rushOn)) + ",\"difficulty\":" + std::to_string(bossDifficulty) + "}";
        }
        if (action == "suicide") {
            void* health = Get<void*>(c.actor, Offset("Actor", "health"));
            *reinterpret_cast<bool*>(static_cast<char*>(health) + Offset("BaseHealth", "damageable")) = true;
            float hit = Get<float>(health, Offset("BaseHealth", "currentHealth")) + 1000.f; void* a[]{&hit};
            (void)Value<float>(Method(health, "HealthDamage", 1), health, a);
            return std::string("{\"ok\":true}");
        }
        if (action == "runread") {
            const int timeline = std::stoi(args.at(1)), difficulty = std::stoi(args.at(2));
            void* run = GetRun(c, Find(timeline, difficulty).id, difficulty);
            if (!run) return std::string("{\"ok\":true,\"run\":null}");
            void* web = Get<void*>(run, Offset("MonolithRun", "web"));
            std::ostringstream out;
            out << "{\"ok\":true,\"run\":{\"stability\":" << RunInt(run, "stability") << ",\"quest\":" << RunInt(run, "questCompletion") << ",\"branch\":" << RunInt(run, "questBranch")
                << ",\"corruption\":" << (web ? Get<int>(web, Offset("EchoWeb", "corruption")) : 0) << ",\"gaze\":" << (web ? RunGaze(web) : 0)
                << ",\"last\":" << Boolean(RunFlag(run, "previousEchoWasQuestEcho")) << ",\"completed\":" << Boolean(RunFlag(run, "previousQuestEchoWasCompleted")) << ",\"lost\":" << Boolean(RunFlag(run, "previousQuestAttemptLostStability"))
                << "},\"borrowed\":" << borrowed.size() << "}";
            return out.str();
        }
        if (action == "rushfrom") { rushAt = std::stoi(args.at(1)); return std::string("{\"ok\":true}"); }
        if (action == "doorstate") {
            std::ostringstream out; out << "{\"ok\":true,\"doors\":[";
            for (size_t i = 0; i < doors.size(); ++i) { if (i) out << ',';
                out << "{\"action\":" << doors[i].action << ",\"label\":" << Json(DoorLabel(doors[i])) << ",\"alive\":" << Boolean(doors[i].object->Get() && game::IsAlive(doors[i].object->Get())) << ",\"at\":" << (doors[i].listener->Get() && game::IsAlive(doors[i].listener->Get()) ? BossPosition(doors[i].listener->Get()) : std::string("null")) << "}"; }
            out << "],\"hall\":" << Boolean(HallAlive()) << ",\"hallBroken\":" << Boolean(hallBroken) << ",\"pending\":" << pendingBoss << ",\"rush\":" << Boolean(rushOn) << ",\"rushAt\":" << rushAt << ",\"harbinger\":" << Boolean(bossHarbinger) << ",\"runHarbinger\":" << Boolean(bossRunHarbinger) << ",\"killed\":" << Boolean(bossKilled) << ",\"portalMade\":" << Boolean(bossPortalMade) << ",\"tracked\":" << Boolean(bossTracked) << ",\"goToHall\":" << Boolean(goToHall) << ",\"error\":" << Json(bossError) << "}";
            return out.str();
        }
        if (action == "navscan") {
            // navscan cx cy cz half step: each row of a square, # walkable within 1 unit, . not
            const float cx = std::stof(args.at(1)), cy = std::stof(args.at(2)), cz = std::stof(args.at(3)), half = std::stof(args.at(4)), step = std::stof(args.at(5));
            auto* cls = game::FindClass("UnityEngine.AIModule.dll", "UnityEngine.AI", "NavMesh");
            if (!cls) throw std::runtime_error("no NavMesh class");
            const il2cpp::Method* sample = nullptr; void* iterator = nullptr; auto& api = il2cpp::api();
            while (auto* m = api.class_get_methods(cls, &iterator)) if (std::string(api.method_get_name(m)) == "SamplePosition" && api.method_get_param_count(m) == 4) {
                char* last = api.type_get_name(api.method_get_param(m, 3)); const bool isInt = last && std::string(last) == "System.Int32"; if (last) api.free(last); if (isInt) sample = m; }
            if (!sample) throw std::runtime_error("no SamplePosition");
            struct Hit { BossVec3 position, normal; float distance; int mask; int hit; } hit{};
            std::ostringstream out; out << "{\"ok\":true,\"rows\":[";
            bool firstRow = true;
            for (float z = cz + half; z >= cz - half; z -= step) {
                std::string row;
                for (float x = cx - half; x <= cx + half; x += step) {
                    BossVec3 at{x, cy, z}; float range = 6.f; int mask = -1; void* a2[]{&at, &hit, &range, &mask};
                    const bool ok = Value<bool>(sample, nullptr, a2);
                    row += ok && std::abs(hit.position.x - x) < 0.8f && std::abs(hit.position.z - z) < 0.8f ? '#' : '.';
                }
                if (!firstRow) out << ','; firstRow = false; out << Json(row);
            }
            return out.str() + "]}";
        }
        if (action == "god") {
            void* health = Get<void*>(c.actor, Offset("Actor", "health"));
            *reinterpret_cast<bool*>(static_cast<char*>(health) + Offset("BaseHealth", "damageable")) = false;
            void* protection = Get<void*>(c.actor, Offset("Actor", "protection"));
            if (protection) *reinterpret_cast<float*>(static_cast<char*>(protection) + Offset("PrecalculatedStatsHolder", "untaggedDamageTakenModifier")) = 0.f;
            return "{\"ok\":true,\"hp\":" + std::to_string(Get<float>(health, Offset("BaseHealth", "currentHealth"))) + "}";
        }
        if (action == "blessing") {
            void* panel = game::StaticObject(game::FindStaticField("LE.dll", "", "BlessingRewardPanelManager", "instance"));
            if (!panel) return std::string("{\"ok\":true,\"panel\":false}");
            const auto* obj = Invoke(Method(panel, "get_gameObject", 0), panel);
            bool open = obj && Value<bool>(Method(const_cast<void*>(static_cast<const void*>(obj)), "get_activeInHierarchy", 0), const_cast<void*>(static_cast<const void*>(obj)));
            if (open && args.size() > 1) Invoke(Method(panel, args[1].c_str(), 0), panel);
            return "{\"ok\":true,\"panel\":true,\"open\":" + std::string(Boolean(open)) + "}";
        }
        if (action == "rocktree" || action == "door") {
            const auto* cls = game::FindClass("LE.dll", "", "OpenMonolithPanelInteraction");
            void* type = il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));
            bool inactive = true; void* findArgs[]{type, &inactive};
            Root objects(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, findArgs));
            void* rock = nullptr;
            const auto n = objects.Get() ? Get<uintptr_t>(objects.Get(), 0x18) : 0;
            const int wanted = args.size() > 1 ? std::stoi(args[1]) : 1;
            for (uintptr_t i = 0; i < n; ++i) { void* o = Get<void*>(objects.Get(), 0x20 + 8 * i);
                if (o && int(Get<uint8_t>(o, Offset("OpenMonolithPanelInteraction", "timelineID"))) == wanted && !Get<bool>(o, Offset("OpenMonolithPanelInteraction", "isRestZoneRock")) &&
                    Text(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0), o)) == "EventHandler") rock = o; }
            if (!rock) throw std::runtime_error("no such rock");
            const auto getName = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
            const auto getTransform = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_transform", 0);
            const auto parentOf = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
            const auto childCount = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_childCount", 0);
            const auto childAt = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "GetChild", 1);
            const auto compType = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Component");
            Root compObject(il2cpp::api().type_get_object(il2cpp::api().class_get_type(compType)));
            void* tr = Invoke(getTransform, rock);
            std::vector<void*> chain;
            for (void* at = tr; at; at = Invoke(parentOf, at)) chain.push_back(at);
            if (action == "rocktree") {
                std::ostringstream out; out << "{\"ok\":true,\"up\":[";
                for (size_t i = 0; i < chain.size(); ++i) { if (i) out << ','; out << Json(Text(Invoke(getName, chain[i]))); }
                out << "],\"tree\":[";
                bool first = true;
                std::function<void(void*, int)> walk = [&](void* at, int depth) {
                    void* go = Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_gameObject", 0), at);
                    std::string comps;
                    bool inc = true; void* compArgs[]{compObject.Get(), &inc};
                    Root list(Invoke(BossSignature("GameObject", "GetComponents", {"System.Type"}), go, std::array<void*, 1>{compObject.Get()}.data()));
                    if (list.Get()) { const auto m = Get<uintptr_t>(list.Get(), 0x18); for (uintptr_t i = 0; i < m; ++i) { void* cc = Get<void*>(list.Get(), 0x20 + 8 * i); if (cc) comps += std::string(il2cpp::api().class_get_name(il2cpp::api().object_get_class(cc))) + " "; } }
                    (void)compArgs;
                    if (!first) out << ','; first = false;
                    out << "{\"d\":" << depth << ",\"n\":" << Json(Text(Invoke(getName, at))) << ",\"c\":" << Json(comps) << ",\"at\":" << BossPosition(at) << "}";
                    if (depth >= 5) return;
                    const int kids = Value<int>(childCount, at);
                    for (int i = 0; i < kids && i < 40; ++i) { int idx = i; void* a[]{&idx}; walk(Invoke(childAt, at, a), depth + 1); }
                };
                const size_t rootIndex = args.size() > 2 ? static_cast<size_t>(std::stoi(args[2])) : 1;
                walk(chain[(std::min)(rootIndex, chain.size() - 1)], 0);
                return out.str() + "]}";
            }
            // door: a copy of the rock's holder (chain[level]) at the player's place + an offset
            const size_t level = std::stoi(args.at(2));
            BossVec3 at{std::stof(args.at(3)), std::stof(args.at(4)), std::stof(args.at(5))};
            void* source = chain.at(level);
            void* sourceGo = Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_gameObject", 0), source);
            void* parent = level + 1 < chain.size() ? chain[level + 1] : nullptr;
            bool world = true; void* instantiateArgs[]{sourceGo, parent, &world};
            Root copy(Invoke(BossSignature("Object", "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform", "System.Boolean"}), nullptr, instantiateArgs));
            Root copyTr(Invoke(Method(copy.Get(), "get_transform", 0), copy.Get()));
            void* posArgs[]{&at}; Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "set_position", 1), copyTr.Get(), posArgs);
            const auto after = Value<BossVec3>(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_position", 0), copyTr.Get());
            // the copy's own rock remembers its timeline; ours is told apart later by its place
            return "{\"ok\":true,\"copy\":" + Json(Text(Invoke(getName, copy.Get()))) + ",\"at\":[" + std::to_string(after.x) + "," + std::to_string(after.y) + "," + std::to_string(after.z) + "]}";
        }
        if (action == "portals" || action == "portaluse" || action == "townuse") {
            // the portals here; use: the way on, or another one, by the call a click makes
            const auto* cls = game::FindClass("LE.dll", "Networking.Multiplayer.Interactables.Portals", "PortalSync");
            Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls)));
            bool hidden = false; void* findArgs[]{type.Get(), &hidden};
            Root found(Invoke(Unity("Object", "FindObjectsOfType", 2), nullptr, findArgs));
            const auto n = found.Get() ? Get<uintptr_t>(found.Get(), 0x18) : 0;
            std::ostringstream out; out << "{\"ok\":true,\"portals\":["; void* use = nullptr;
            for (uintptr_t i = 0; i < n; ++i) {
                void* p = Get<void*>(found.Get(), 0x20 + 8 * i);
                if (!p) continue;
                const int kind = Get<uint8_t>(p, Offset("PortalSync", "portalType", "Networking.Multiplayer.Interactables.Portals"));
                const auto at = PlaceOf(p);
                out << (i ? "," : "") << "{\"type\":" << kind << ",\"at\":[" << at.x << ',' << at.y << ',' << at.z << "]}";
                if ((action == "portaluse") == (kind == kPortalEcho)) use = p;
            }
            out << "]";
            if (action != "portals") { if (!use) throw std::runtime_error("no such portal"); Invoke(Method(use, "SendAttemptUse", 0), use); out << ",\"used\":true"; }
            return out.str() + "}";
        }
        if (action == "questwindow") {
            const auto* cls = game::FindClass("LE.dll", "", "QuestRewardPanel");
            Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls)));
            bool hidden = false; void* findArgs[]{type.Get(), &hidden};
            Root found(Invoke(Unity("Object", "FindObjectsOfType", 2), nullptr, findArgs));
            void* gold = Get<void*>(c.actor, Offset("Actor", "goldTracker"));
            return "{\"ok\":true,\"open\":" + std::to_string(found.Get() ? Get<uintptr_t>(found.Get(), 0x18) : 0) + ",\"leftOut\":" + std::to_string(questWindowsLeftOut) +
                   ",\"gold\":" + std::to_string(gold ? Get<int>(gold, Offset("GoldTracker", "value")) : -1) + "}";
        }
        if (action == "portallabel") {
            const auto* cls = game::FindClass("LE.dll", "Networking.Multiplayer.Interactables.Portals", "PortalSync");
            Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls)));
            bool hidden = false; void* findArgs[]{type.Get(), &hidden};
            Root found(Invoke(Unity("Object", "FindObjectsOfType", 2), nullptr, findArgs));
            std::string text;
            const auto n = found.Get() ? Get<uintptr_t>(found.Get(), 0x18) : 0;
            for (uintptr_t i = 0; i < n; ++i) {
                void* sync = Get<void*>(found.Get(), 0x20 + 8 * i);
                void* visual = sync ? Get<void*>(sync, Offset("PortalSync", "visualController", "Networking.Multiplayer.Interactables.Portals")) : nullptr;
                void* tag = visual ? Get<void*>(visual, Offset("PortalVisualController", "hoverTag", "Networking.Multiplayer.Interactables.Portals")) : nullptr;
                if (tag) text = Text(Get<void*>(tag, Offset("HoverTagOnMouseOver", "hoverText")));
            }
            return "{\"ok\":true,\"label\":" + Json(text) + "}";
        }
        if (action == "townopen") {
            void* input = game::StaticObject(game::FindStaticField("LE.dll", "", "EpochInputManager", "instance"));
            void* state = Invoke(Method(input, "get_playerAbilityState", 0), input);
            DoorVec3 at = PlaceOf(c.actor); at.z += 4; bool flag = false; void* a[]{&at, &flag};
            Invoke(Method(state, "UsePortalCommand", 2), state, a);
            return std::string("{\"ok\":true}");
        }
        if (action == "portal") {
            void* sync = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getLocalActorSync", 0));
            void* a[]{sync}; Invoke(BossStatic("ProcessEchoPortalInteraction", 1), nullptr, a);
            return BossState(c);
        }
        throw std::runtime_error("unknown bosstest action");
    });
}
#endif
#ifdef EPOCHPACT_RESEARCH
std::string TestGain(const std::string& id, int timeline, int difficulty, int amount) {
    return Mutation(id, "stability", [=](const Context& c, bool apply) mutable -> std::string {
        Editable(c); const auto t = Find(timeline, difficulty);
        if (id.empty() || c.name != "EpMonolithTest" || amount < -10000 || amount > 10000)
            throw std::runtime_error("research gain requires the isolated EpMonolithTest character");
        if (!IsUnlocked(c, t.id, difficulty)) throw std::runtime_error("timeline difficulty is locked");
        if (!apply) return std::string();
        {
            Root run(GetRun(c, t.id, difficulty, true));
            void* args[]{&amount}; Invoke(m_add, run.Get(), args);
            return "\"run\":" + RunJson(run.Get());
        }
    });
}
#endif
}
