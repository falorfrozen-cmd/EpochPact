#pragma once
#include "cof_rules.hpp"
namespace ep::cof::tuning::rules {
constexpr int MaxItemsPerReward = 250;
inline bool Chance(double value) { return std::isfinite(value) && value >= 0 && value <= 100; }
inline bool RewardMultiplier(double value) { return std::isfinite(value) && value >= 1 && value <= 25; }
inline float Probability(float base, double multiplier) {
    return static_cast<float>(std::clamp(static_cast<double>(base) * multiplier, 0.0, 1.0));
}
inline float RollCoefficient(float base, double multiplier) {
    return static_cast<float>(base * multiplier);
}
inline int RewardItems(int base, double multiplier) {
    if (base <= 0 || multiplier == 1) return base;
    return static_cast<int>(std::min<int64_t>(MaxItemsPerReward, std::llround(base * multiplier)));
}
// Native AddFavor computes a float delta, converts it to Int32, then adds it to
// current progress. Leave float rounding room; never overflow either operation.
inline int ChargeInput(int amount, double multiplier, double factor, int progress) {
    if (amount <= 0) return amount;
    if (!std::isfinite(factor) || factor <= 0 || progress < 0) return 0;
    const double room = std::max(0.0, static_cast<double>(INT32_MAX) - progress - 4096);
    const int64_t requested = std::llround(static_cast<double>(amount) * multiplier);
    const double bound = std::min<double>(INT32_MAX, std::floor(room / factor));
    return static_cast<int>(std::min(static_cast<double>(requested), bound));
}
inline bool AffixContext(int context) { return context == 0 || context == 4 || context == 5; }
}
