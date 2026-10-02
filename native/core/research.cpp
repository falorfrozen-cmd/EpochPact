#include "research.hpp"

#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "mainthread.hpp"

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
game::Handle g_trackers[kTrackers];
game::Handle g_enemies[kEnemies];
std::atomic<int> g_trackerNext{0}, g_enemyNext{0};
std::atomic<uint64_t> g_trackersSeen{0}, g_enemiesSeen{0};

MethodRef m_csOnEnable, m_csSetIndex, m_csGetIndex, m_csLoad, m_csLoading;
MethodRef m_trAwake, m_trXp, m_trLevel, m_trNext, m_trGain;
MethodRef m_kStart, m_kGive;
Unary o_csOnEnable, o_trAwake, o_kStart;

void d_csOnEnable(void* self, const Method* m) {
    game::Guarded([&] { g_charSelect.Set(self); }, nullptr);  // a capture must never take the game down
    o_csOnEnable(self, m);
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

    HookOrLog(m_csOnEnable, reinterpret_cast<void*>(&d_csOnEnable), reinterpret_cast<void**>(&o_csOnEnable), "CharacterSelect.OnEnable");
    HookOrLog(m_trAwake, reinterpret_cast<void*>(&d_trAwake), reinterpret_cast<void**>(&o_trAwake), "ExperienceTracker.Awake");
    HookOrLog(m_kStart, reinterpret_cast<void*>(&d_kStart), reinterpret_cast<void**>(&o_kStart), "ExperienceGainedOnKill.Start");
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
    return false;
}

}  // namespace ep::research
