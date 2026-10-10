#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
namespace ep::cof::rules {
constexpr int MaxFavor = 999999;
constexpr int MaxCharges = 99;
inline bool Favor(int value) { return value >= 0 && value <= MaxFavor; }
inline bool Charges(int value) { return value >= 0 && value <= MaxCharges; }
inline bool Multiplier(double value) { return std::isfinite(value) && value >= 1 && value <= 100; }
// GainFavor applies its own rank multiplier after this hook. Bound the input
// before its Int32 arithmetic; wallet saturation must not stop prophecy charging.
inline int ScaleGain(int value, double multiplier) {
    if (value <= 0 || !Multiplier(multiplier) || multiplier == 1) return value;
    return static_cast<int>(std::min<int64_t>(MaxFavor, std::llround(static_cast<double>(value) * multiplier)));
}
// GainReputation adds to its current Int32 progress before subtracting rank
// thresholds. Bound the boosted input against that addition, not the Favor cap.
inline int ScaleReputationGain(int value, double multiplier, int current) {
    if (value <= 0 || !Multiplier(multiplier) || multiplier == 1 || current < 0) return value;
    const int64_t room = static_cast<int64_t>(std::numeric_limits<int>::max()) - current;
    return static_cast<int>(std::min<int64_t>(room, std::llround(static_cast<double>(value) * multiplier)));
}
}
