#include "research.hpp"

#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "mainthread.hpp"
#include "player.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace ep::research {

namespace {

using game::MethodRef;
using il2cpp::Method;
using Unary = void (*)(void* self, const Method*);

constexpr int kTrackers = 4, kEnemies = 16;

game::Handle g_charSelect;
game::Handle g_landing;  // LE.UI.Login.UnityUI.LandingZonePanel
game::Handle g_trackers[kTrackers];
game::Handle g_enemies[kEnemies];
std::atomic<int> g_trackerNext{0}, g_enemyNext{0};
std::atomic<uint64_t> g_trackersSeen{0}, g_enemiesSeen{0};
std::atomic<bool> g_runInBgSet{false};

MethodRef m_csOnEnable, m_csSetIndex, m_csGetIndex, m_csLoad, m_csLoading;
MethodRef m_trAwake, m_trXp, m_trLevel, m_trNext, m_trGain;
MethodRef m_kStart, m_kGive;
MethodRef m_getPlayerActor, m_agentSpeed;  // PlayerFinder.getPlayerActor, NavMeshAgent.get_speed
MethodRef m_rollRarity;                    // GenerateItems.RollRarity(int, float) -> byte
MethodRef m_switchTab;                     // CharacterSelect.SwitchOnlineOffline()
MethodRef m_lzOnEnable, m_lzPlayOffline;   // LandingZonePanel.OnOnEnable / OnPlayOfflineClicked
MethodRef m_getTransform, m_getPosition;   // Component.get_transform, Transform.get_position
MethodRef m_runInBg;                       // UnityEngine.Application.set_runInBackground
size_t g_agentOffset = 0;                  // Actor.navMeshAgent
size_t g_onlineTabOffset = 0;              // CharacterSelect.isOnlineTabShowing
size_t g_tilesOffset = 0;                  // CharacterSelect.availableCharacterTiles
Unary o_csOnEnable, o_trAwake, o_kStart, o_lzOnEnable;

void d_csOnEnable(void* self, const Method* m) {
    game::Guarded([&] { g_charSelect.Set(self); }, nullptr);  // a capture must never take the game down
    o_csOnEnable(self, m);
}
void d_lzOnEnable(void* self, const Method* m) {
    game::Guarded([&] { g_landing.Set(self); }, nullptr);
    o_lzOnEnable(self, m);
}
void d_trAwake(void* self, const Method* m) {
    game::Guarded([&] { g_trackers[g_trackerNext.fetch_add(1) % kTrackers].Set(self); }, nullptr);
    g_trackersSeen.fetch_add(1);
    o_trAwake(self, m);
}
void d_kStart(void* self, const Method* m) {
    game::Guarded([&] { g_enemies[g_enemyNext.fetch_add(1) % kEnemies].Set(self); }, nullptr);
    g_enemiesSeen.fetch_add(1);
    o_kStart(self, m);
}

void HookOrLog(const MethodRef& ref, void* detour, void** original, const char* name) {
    std::string why;
    if (!ref) Log("research: %s not found", name);
    else if (!hook::Install(ref.code, detour, original, &why)) Log("research: %s hook refused: %s", name, why.c_str());
    else Log("research: hooked %s", name);
}

// The most recent capture that is still alive (main thread only).
void* Latest(game::Handle* list, int n, const std::atomic<int>& next) {
    const int last = next.load();
    for (int i = 1; i <= n; ++i) {
        const int idx = ((last - i) % n + n) % n;
        if (void* obj = list[idx].Get()) return obj;
    }
    return nullptr;
}

template <typename R>
R Call0(const MethodRef& m, void* self) {
    return reinterpret_cast<R (*)(void*, const Method*)>(m.code)(self, m.info);
}

std::string TrackerLine(void* tr) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "level %d, experience %lld / %lld", Call0<int32_t>(m_trLevel, tr),
                  static_cast<long long>(Call0<int64_t>(m_trXp, tr)), static_cast<long long>(Call0<int64_t>(m_trNext, tr)));
    return buf;
}

std::string OnMain(std::function<std::string()> job) {
    std::string reply, why;
    if (!mainthread::Run([&] { reply = job(); }, 5000, &why)) return "refused: " + why;
    return reply;
}

struct Agent {
    void* agent = nullptr;
    float speed = -1.0f;
};

// The local player's NavMeshAgent and its current speed. Main thread only.
Agent PlayerAgent() {
    Agent a;
    if (!m_getPlayerActor) return a;
    void* actor = reinterpret_cast<void* (*)(const Method*)>(m_getPlayerActor.code)(m_getPlayerActor.info);
    if (!actor) return a;
    a.agent = g_agentOffset ? *reinterpret_cast<void* const*>(static_cast<char*>(actor) + g_agentOffset) : nullptr;
    if (a.agent && m_agentSpeed) a.speed = reinterpret_cast<float (*)(void*, const Method*)>(m_agentSpeed.code)(a.agent, m_agentSpeed.info);
    return a;
}

// CharacterSelect.isOnlineTabShowing. Main thread only.
bool OnlineTab(void* charSelect) {
    return g_onlineTabOffset && *reinterpret_cast<const uint8_t*>(static_cast<const char*>(charSelect) + g_onlineTabOffset) != 0;
}

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// The local player's world position. Main thread only.
bool PlayerPos(Vec3* out) {
    if (!m_getPlayerActor || !m_getTransform || !m_getPosition) return false;
    void* actor = reinterpret_cast<void* (*)(const Method*)>(m_getPlayerActor.code)(m_getPlayerActor.info);
    if (!actor) return false;
    void* transform = reinterpret_cast<void* (*)(void*, const Method*)>(m_getTransform.code)(actor, m_getTransform.info);
    if (!transform) return false;
    // Unity structs return through a hidden buffer: rcx = buffer, rdx = this, r8 = MethodInfo.
    reinterpret_cast<void (*)(float*, void*, const Method*)>(m_getPosition.code)(reinterpret_cast<float*>(out), transform,
                                                                                 m_getPosition.info);
    return true;
}

}  // namespace

void Init() {
    m_csOnEnable = game::FindMethod("LE.dll", "", "CharacterSelect", "OnEnable", 0);
    m_csSetIndex = game::FindMethod("LE.dll", "", "CharacterSelect", "set_SelectedCharacterIndex", 1);
    m_csGetIndex = game::FindMethod("LE.dll", "", "CharacterSelect", "get_SelectedCharacterIndex", 0);
    m_csLoad = game::FindMethod("LE.dll", "", "CharacterSelect", "LoadCharacter", 0);
    m_csLoading = game::FindMethod("LE.dll", "", "CharacterSelect", "get_IsLoadingCharacter", 0);
    m_trAwake = game::FindMethod("LE.dll", "", "ExperienceTracker", "Awake", 0);
    m_trXp = game::FindMethod("LE.dll", "", "ExperienceTracker", "get_CurrentExperience", 0);
    m_trLevel = game::FindMethod("LE.dll", "", "ExperienceTracker", "get_CurrentLevel", 0);
    m_trNext = game::FindMethod("LE.dll", "", "ExperienceTracker", "get_NextLevelExperience", 0);
    m_trGain = game::FindMethod("LE.dll", "", "ExperienceTracker", "GainExpFromEnemyOrMote", 1);
    m_kStart = game::FindMethod("LE.dll", "", "ExperienceGainedOnKill", "Start", 0);
    m_kGive = game::FindMethod("LE.dll", "", "ExperienceGainedOnKill", "GiveExp", 1);
    m_getPlayerActor = game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0);
    m_agentSpeed = game::FindMethod("UnityEngine.AIModule.dll", "UnityEngine.AI", "NavMeshAgent", "get_speed", 0);
    m_getTransform = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_transform", 0);
    m_getPosition = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_position", 0);
    m_rollRarity = game::FindMethod("LE.dll", "", "GenerateItems", "RollRarity", 2);
    m_switchTab = game::FindMethod("LE.dll", "", "CharacterSelect", "SwitchOnlineOffline", 0);
    m_lzOnEnable = game::FindMethod("LE.dll", "LE.UI.Login.UnityUI", "LandingZonePanel", "OnOnEnable", 0);
    m_lzPlayOffline = game::FindMethod("LE.dll", "LE.UI.Login.UnityUI", "LandingZonePanel", "OnPlayOfflineClicked", 0);
    m_runInBg = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Application", "set_runInBackground", 1);
    g_agentOffset = game::FieldOffset("LE.dll", "", "Actor", "navMeshAgent");
    g_onlineTabOffset = game::FieldOffset("LE.dll", "", "CharacterSelect", "isOnlineTabShowing");
    g_tilesOffset = game::FieldOffset("LE.dll", "", "CharacterSelect", "availableCharacterTiles");

    HookOrLog(m_csOnEnable, reinterpret_cast<void*>(&d_csOnEnable), reinterpret_cast<void**>(&o_csOnEnable), "CharacterSelect.OnEnable");
    HookOrLog(m_trAwake, reinterpret_cast<void*>(&d_trAwake), reinterpret_cast<void**>(&o_trAwake), "ExperienceTracker.Awake");
    HookOrLog(m_kStart, reinterpret_cast<void*>(&d_kStart), reinterpret_cast<void**>(&o_kStart), "ExperienceGainedOnKill.Start");
    HookOrLog(m_lzOnEnable, reinterpret_cast<void*>(&d_lzOnEnable), reinterpret_cast<void**>(&o_lzOnEnable), "LandingZonePanel.OnOnEnable");
}

void KeepTicking() {
    // Keep the game ticking while the window is not focused, so commands still run from the
    // owner's terminal/browser without alt-tabbing back and forth. Called after
    // mainthread::Init (EventSystem.Update must exist).
    if (!m_runInBg) return;
    std::string why;
    const bool ran = mainthread::Run([&] { reinterpret_cast<void (*)(bool, const Method*)>(m_runInBg.code)(true, m_runInBg.info); },
                                     15000, &why);
    Log("research: run in background set to true (%s)", ran ? "ok" : why.c_str());
    if (ran) g_runInBgSet.store(true);
}

void Housekeep() {
    static unsigned long lastTry = 0;
    if (g_runInBgSet.load() || !m_runInBg) return;
    const unsigned long now = GetTickCount();
    if (now - lastTry < 15000) return;
    lastTry = now;
    std::string why;
    if (mainthread::Run([&] { reinterpret_cast<void (*)(bool, const Method*)>(m_runInBg.code)(true, m_runInBg.info); }, 3000, &why)) {
        g_runInBgSet.store(true);
        Log("research: run in background is on now");
    }
}

std::string Status() {
    char buf[200];
    std::snprintf(buf, sizeof buf, "research: trackers seen %llu; kill-xp components seen %llu",
                  static_cast<unsigned long long>(g_trackersSeen.load()),
                  static_cast<unsigned long long>(g_enemiesSeen.load()));
    return buf;
}

bool Handle(const std::string& cmd, const std::vector<std::string>& args, std::string* reply) {
    if (cmd == "charsel") {
        *reply = OnMain([] {
            void* cs = g_charSelect.Get();
            if (!cs) return std::string("charsel: no live CharacterSelect");
            char buf[96];
            std::snprintf(buf, sizeof buf, "charsel: selected index %d, loading %d", Call0<int32_t>(m_csGetIndex, cs),
                          Call0<bool>(m_csLoading, cs) ? 1 : 0);
            return std::string(buf);
        });
        return true;
    }
    if (cmd == "load") {
        if (args.empty()) {
            *reply = "load: usage: load <index in the character list>";
            return true;
        }
        const int index = std::atoi(args[0].c_str());
        *reply = OnMain([index] {
            void* cs = g_charSelect.Get();
            if (!cs) return std::string("load: no live CharacterSelect (open the character list first)");
            reinterpret_cast<void (*)(void*, int32_t, const Method*)>(m_csSetIndex.code)(cs, index, m_csSetIndex.info);
            Call0<void>(m_csLoad, cs);
            return "load: LoadCharacter called for index " + std::to_string(index);
        });
        return true;
    }
    if (cmd == "xpread") {
        *reply = OnMain([] {
            void* tr = Latest(g_trackers, kTrackers, g_trackerNext);
            return tr ? "xpread: " + TrackerLine(tr) : std::string("xpread: no live ExperienceTracker");
        });
        return true;
    }
    if (cmd == "xpgain") {
        const long long amount = args.empty() ? 1000 : std::atoll(args[0].c_str());
        *reply = OnMain([amount] {
            void* tr = Latest(g_trackers, kTrackers, g_trackerNext);
            if (!tr) return std::string("xpgain: no live ExperienceTracker");
            const std::string before = TrackerLine(tr);
            // The function's own entry (what MethodInfo points at), so an installed xp hook sees the call like any other.
            reinterpret_cast<void (*)(void*, int64_t, const Method*)>(m_trGain.code)(tr, amount, m_trGain.info);
            return "xpgain " + std::to_string(amount) + ": before " + before + "; after " + TrackerLine(tr);
        });
        return true;
    }
    if (cmd == "xpkill") {
        *reply = OnMain([] {
            void* tr = Latest(g_trackers, kTrackers, g_trackerNext);
            void* kill = Latest(g_enemies, kEnemies, g_enemyNext);
            if (!tr || !kill) return std::string("xpkill: need a live ExperienceTracker and a live enemy");
            const int32_t gained = *reinterpret_cast<int32_t*>(static_cast<char*>(kill) + 0x20);  // experienceGained
            const int32_t level = *reinterpret_cast<int32_t*>(static_cast<char*>(kill) + 0x24);   // level
            const std::string before = TrackerLine(tr);
            reinterpret_cast<void (*)(void*, void*, const Method*)>(m_kGive.code)(kill, tr, m_kGive.info);
            char buf[96];
            std::snprintf(buf, sizeof buf, "xpkill (enemy experienceGained %d, level %d): before ", gained, level);
            return std::string(buf) + before + "; after " + TrackerLine(tr);
        });
        return true;
    }
    if (cmd == "enemies") {
        *reply = OnMain([] {
            int alive = 0;
            for (auto& h : g_enemies)
                if (h.Get()) ++alive;
            return "enemies: " + std::to_string(alive) + " live kill-xp components captured (of " + std::to_string(g_enemiesSeen.load()) +
                   " seen)";
        });
        return true;
    }
    if (cmd == "playoffline") {
        *reply = OnMain([] {
            void* panel = g_landing.Get();
            if (!panel) return std::string("playoffline: LandingZonePanel not captured yet (is the login screen showing?)");
            if (!m_lzPlayOffline) return std::string("playoffline: OnPlayOfflineClicked not found");
            reinterpret_cast<void (*)(void*, const Method*)>(m_lzPlayOffline.code)(panel, m_lzPlayOffline.info);
            return std::string("playoffline: LandingZonePanel.OnPlayOfflineClicked called");
        });
        return true;
    }
    if (cmd == "tab") {
        *reply = OnMain([] {
            void* cs = g_charSelect.Get();
            if (!cs) return std::string("tab: no live CharacterSelect");
            int tiles = -1;
            if (g_tilesOffset) {
                if (void* list = *reinterpret_cast<void* const*>(static_cast<char*>(cs) + g_tilesOffset))
                    tiles = *reinterpret_cast<const int32_t*>(static_cast<const char*>(list) + 0x18);  // List<T> count, as in items.cpp
            }
            char buf[128];
            std::snprintf(buf, sizeof buf, "tab: %s; character tiles %d", OnlineTab(cs) ? "online" : "offline", tiles);
            return std::string(buf);
        });
        return true;
    }
    if (cmd == "offline") {
        *reply = OnMain([] {
            void* cs = g_charSelect.Get();
            if (!cs) return std::string("offline: no live CharacterSelect");
            if (!OnlineTab(cs)) return std::string("offline: the offline tab is already showing");
            if (!m_switchTab) return std::string("offline: CharacterSelect.SwitchOnlineOffline not found");
            reinterpret_cast<void (*)(void*, const Method*)>(m_switchTab.code)(cs, m_switchTab.info);
            return std::string("offline: SwitchOnlineOffline called");
        });
        return true;
    }
    if (cmd == "speedread") {
        *reply = OnMain([] {
            const Agent a = PlayerAgent();
            char buf[320];
            if (!m_getPlayerActor) return std::string("speedread: PlayerFinder.getPlayerActor not found");
            const std::string probe = player::SpeedProbe();
            if (!a.agent) {
                std::snprintf(buf, sizeof buf, "speedread: no player NavMeshAgent; %s", probe.c_str());
                return std::string(buf);
            }
            std::snprintf(buf, sizeof buf, "speedread: player NavMeshAgent.speed %g (agent %p); %s", a.speed, a.agent, probe.c_str());
            return std::string(buf);
        });
        return true;
    }
    if (cmd == "posread") {
        *reply = OnMain([] {
            Vec3 p;
            if (!PlayerPos(&p)) return std::string("posread: no player transform (not in a zone?)");
            char buf[160];
            std::snprintf(buf, sizeof buf, "posread: %.3f %.3f %.3f", p.x, p.y, p.z);
            return std::string(buf);
        });
        return true;
    }
    if (cmd == "statprobe") {
        *reply = OnMain([] { return player::StatProbe(); });
        return true;
    }
    if (cmd == "statscan") {
        const int sp = args.empty() ? 13 : std::atoi(args[0].c_str());
        *reply = OnMain([sp] { return player::StatScan(sp); });
        return true;
    }
    if (cmd == "bg") {
        const bool on = args.empty() ? true : std::atoi(args[0].c_str()) != 0;
        *reply = OnMain([on] {
            if (!m_runInBg) return std::string("bg: Application.set_runInBackground not found");
            reinterpret_cast<void (*)(bool, const Method*)>(m_runInBg.code)(on, m_runInBg.info);
            return std::string("bg: run in background ") + (on ? "on" : "off");
        });
        return true;
    }
    if (cmd == "raritytest") {
        const int n = args.empty() ? 200 : std::atoi(args[0].c_str());
        *reply = OnMain([n] {
            if (!m_rollRarity) return std::string("raritytest: GenerateItems.RollRarity not found");
            int counts[5] = {0, 0, 0, 0, 0};
            const int rolls = n > 0 && n <= 100000 ? n : 200;
            for (int i = 0; i < rolls; ++i) {
                const uint8_t r = reinterpret_cast<uint8_t (*)(int32_t, float, const Method*)>(m_rollRarity.code)(1, 1.0f, m_rollRarity.info);
                if (r < 5) ++counts[r];
            }
            char buf[256];
            std::snprintf(buf, sizeof buf, "raritytest: %d rolls (ilvl 1): 0:%d 1:%d 2:%d 3:%d 4:%d", rolls, counts[0], counts[1], counts[2],
                          counts[3], counts[4]);
            return std::string(buf);
        });
        return true;
    }
    return false;
}

}  // namespace ep::research
