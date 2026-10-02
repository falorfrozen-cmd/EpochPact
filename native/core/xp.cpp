#include "xp.hpp"

#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace ep::xp {

namespace {

using GainFn = void (*)(void* self, int64_t amount, const il2cpp::Method* method);

game::MethodRef g_target;          // ExperienceTracker.GainExpFromEnemyOrMote(long)
GainFn g_orig = nullptr;
std::atomic<double> g_mult{1.0};
std::atomic<bool> g_firstPending{false};
std::atomic<uint64_t> g_calls{0}, g_boosted{0}, g_refused{0};
std::atomic<int64_t> g_lastIn{0}, g_lastOut{0};

int64_t Scale(int64_t amount, double m) {
    const double v = static_cast<double>(amount) * m;
    if (v >= 9.0e18) return static_cast<int64_t>(9.0e18);
    return static_cast<int64_t>(std::llround(v));
}

void Detour(void* self, int64_t amount, const il2cpp::Method* method) {
    g_calls.fetch_add(1, std::memory_order_relaxed);
    const double m = g_mult.load(std::memory_order_relaxed);
    int64_t out = amount;
    if (m != 1.0 && amount > 0) {
        if (game::IsOfflinePlay()) {
            out = Scale(amount, m);
            g_boosted.fetch_add(1, std::memory_order_relaxed);
            if (g_firstPending.exchange(false))
                Log("xp: first boosted gain %lld -> %lld (x%g)", static_cast<long long>(amount), static_cast<long long>(out), m);
        } else {
            g_refused.fetch_add(1, std::memory_order_relaxed);
        }
    }
    g_lastIn = amount;
    g_lastOut = out;
    g_orig(self, out, method);
}

}  // namespace

bool Init() {
    g_target = game::FindMethod("LE.dll", "", "ExperienceTracker", "GainExpFromEnemyOrMote", 1);
    Log("xp: ExperienceTracker.GainExpFromEnemyOrMote %s", g_target ? "found" : "MISSING");
    return static_cast<bool>(g_target);
}

void* Target() { return g_target.code; }

std::string Set(double m) {
    char buf[256];
    if (!g_target) return "xp: refused: ExperienceTracker.GainExpFromEnemyOrMote was not found in this game build";
    if (!(m >= 1.0 && m <= kMax)) {
        std::snprintf(buf, sizeof buf, "xp: refused: the multiplier must be between 1 and %g", kMax);
        return buf;
    }
    if (m == 1.0) {
        g_mult = 1.0;
        std::string why;
        if (hook::IsInstalled(g_target.code) && !hook::Remove(g_target.code, &why)) return "xp: x1 set, but the hook stays: " + why;
        return "xp -> x1 (off; hook removed)";
    }
    bool known = false;
    if (!game::IsOfflinePlay(&known)) return "xp: refused: " + game::GateText();
    g_mult = m;
    g_firstPending = true;
    if (!hook::IsInstalled(g_target.code)) {
        std::string why;
        if (!hook::Install(g_target.code, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&g_orig), &why)) {
            g_mult = 1.0;
            return "xp: refused: the hook did not go in: " + why;
        }
    }
    std::snprintf(buf, sizeof buf, "xp -> x%g (hook on ExperienceTracker.GainExpFromEnemyOrMote)", m);
    return buf;
}

std::string Status() {
    char buf[320];
    std::snprintf(buf, sizeof buf, "xp: x%g, hook %s, gains %llu (boosted %llu, refused online %llu), last %lld -> %lld",
                  g_mult.load(), g_target && hook::IsInstalled(g_target.code) ? "in" : "out",
                  static_cast<unsigned long long>(g_calls.load()), static_cast<unsigned long long>(g_boosted.load()),
                  static_cast<unsigned long long>(g_refused.load()), static_cast<long long>(g_lastIn.load()),
                  static_cast<long long>(g_lastOut.load()));
    return buf;
}

}  // namespace ep::xp
