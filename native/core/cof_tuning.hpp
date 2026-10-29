#pragma once
#include <atomic>
#include <cmath>
#include <functional>
#include <string>
namespace ep::cof::tuning {
// The factor EpochPact's own experience and Favor multipliers added to the Favor gained on this thread right now.
// Prophecy progress comes from gained Favor; it is divided by this, so the Prophecy charge speed alone decides how
// fast a prophecy fills (player report 2026-10-29: experience x100 filled prophecies at once).
inline thread_local double favorBoost = 1.0;
struct FavorBoost {
    double saved;
    explicit FavorBoost(double factor) : saved(favorBoost) { if (std::isfinite(factor) && factor > 0) favorBoost *= factor; }
    ~FavorBoost() { favorBoost = saved; }
    FavorBoost(const FavorBoost&) = delete;
    FavorBoost& operator=(const FavorBoost&) = delete;
};
// The experience and Favor multipliers as set (xp.cpp, cof.cpp): the prophecy hook is needed while either is above 1.
// Header-only, so those modules need no link to this one; Init points the observer at the hook's reconciliation.
inline std::atomic<double> xpSetting{1.0}, favorSetting{1.0};
inline void (*boostsObserver)() = nullptr;
inline void BoostSettingChanged() { if (boostsObserver) boostsObserver(); }
bool Init();
std::string Set(const std::string& setting, double value);
std::string Read();
std::string Status();
void WithEnemyLoot(bool enemyDeath, const std::function<void()>& original);
}
