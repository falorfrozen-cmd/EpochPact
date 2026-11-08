#pragma once
#include <cmath>
// Ailment stack cap and duration multipliers: pure rules, tested in tests/ailment_rules_test.cpp.
namespace ep::ailments::rules {
struct Range { double low, high, neutral; };
constexpr Range kStacks{1, 30, 1};    // the most stacks an ailment can have, times this
constexpr Range kDuration{1, 10, 1};  // an ailment's base duration, times this
constexpr int kNoCap = 1000;          // a maximum this high (or 0 and below) is no real cap: left alone
constexpr int kMostStacks = 100000;
inline bool InRange(const Range& r, double v) { return std::isfinite(v) && v >= r.low && v <= r.high; }
// A hard cap of the game's, multiplied; an ailment without one keeps what it has.
inline int ScaleStacks(int original, double times) {
    if (original <= 0 || original >= kNoCap || !std::isfinite(times) || times <= 1) return original;
    const double scaled = std::round(original * times);
    return scaled >= kMostStacks ? kMostStacks : static_cast<int>(scaled);
}
inline bool HasCap(int original) { return original > 0 && original < kNoCap; }
// A positive, finite base duration, multiplied; none (0) or a broken one stays as it was.
inline float ScaleDuration(float original, double times) {
    if (!std::isfinite(original) || original <= 0 || !std::isfinite(times) || times <= 1) return original;
    return static_cast<float>(original * times);
}
}  // namespace ep::ailments::rules
