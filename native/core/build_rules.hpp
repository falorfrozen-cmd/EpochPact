#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace ep::builds::rules {
// Derive budgets from natural progress, never from the last modified budget.
// Flooring the total carries fractional gains across levels without a saved counter.
inline int Budget(int natural, double multiplier, int flat = 0, int limit = 65535) {
    if (natural < 0 || !std::isfinite(multiplier) || multiplier < 1 || flat < 0) return natural;
    const double value = std::floor(static_cast<double>(natural) * multiplier) + flat;
    return static_cast<int>(std::clamp(value, 0.0, static_cast<double>(limit)));
}
inline int PassiveBudget(int natural, int level, double multiplier) {
    const int fromLevels = std::clamp(level - 2, 0, natural);
    return std::min(65535, Budget(fromLevels, multiplier) + (natural - fromLevels));
}
inline int SkillXp(int gain, double multiplier) {
    return gain <= 0 ? gain : Budget(gain, multiplier, 0, std::numeric_limits<int>::max());
}
inline int ExtraProjectiles(int nativeExtra, double multiplier, int flat) {
    // At least one projectile already exists. Scale the total, not only the extras.
    return nativeExtra < 0 ? nativeExtra : std::max(0, Budget(nativeExtra + 1, multiplier, flat, 64) - 1);
}
}
