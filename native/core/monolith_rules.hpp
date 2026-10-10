#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace ep::monolith::rules {
constexpr int kMaxStoredCorruption = 65535; // ProgressManager serializes UInt16.
constexpr double kMaxMultiplier = 100.0;
inline bool Multiplier(double value) { return std::isfinite(value) && value >= 1 && value <= kMaxMultiplier; }
inline bool Corruption(int value, int minimum, bool capped, int maximum) {
    return minimum >= 0 && value >= minimum && value <= kMaxStoredCorruption && (!capped || value <= maximum);
}
inline int ScaleGain(int amount, double multiplier, int current) {
    if (amount <= 0 || !Multiplier(multiplier) || current < 0) return amount;
    const auto room = static_cast<int64_t>(std::numeric_limits<int>::max()) - current;
    return static_cast<int>(std::min(room, static_cast<int64_t>(std::llround(static_cast<double>(amount) * multiplier))));
}
}
