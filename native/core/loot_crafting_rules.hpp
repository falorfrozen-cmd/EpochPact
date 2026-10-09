#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace ep::lootcraft::rules {
enum class Mode { All, Filter, Quality, Materials };
struct Policy {
    Mode mode = Mode::All;
    int minimumLP = 2;
    bool t7 = true, respectFilter = true, materials = true;
    bool categories[5]{true, true, true, true, true};
    std::vector<int> affixes;
};
struct Item { bool material, unique, wantedT7, filterPass; int lp; };
inline bool Accept(const Policy& p, const Item& item) {
    if (p.mode == Mode::All) return !item.material || p.materials;
    if (p.mode == Mode::Materials) return item.material && p.materials;
    if ((p.mode == Mode::Filter || p.respectFilter) && !item.filterPass) return false;
    if (item.material) return p.materials;
    if (p.mode == Mode::Filter) return true;
    return (item.unique && item.lp >= p.minimumLP) || (p.t7 && item.wantedT7);
}
inline bool Chance(double p) { return std::isfinite(p) && (p == -1 || (p >= 0 && p <= 100)); }
inline bool Factor(double x) { return std::isfinite(x) && x >= 0 && x <= 1; }
// Scale the actual, already sampled and FP-capped loss, once. Round upward so
// a positive cost/factor cannot accidentally become free; 0 explicitly is free.
inline int RemainingFP(int old, int requested, double factor) {
    if (old < 0 || old > 63 || requested < 0 || requested > old || !Factor(factor)) return requested;
    const int loss = old - requested;
    return old - static_cast<int>(std::ceil(loss * factor));
}
inline float SealChance(float normal, int tier, double overridePercent) {
    if (!std::isfinite(normal) || normal <= 0 || normal > 1 || tier < 0 || tier > 3 ||
        overridePercent < 0 || !Chance(overridePercent)) return normal;
    return static_cast<float>(overridePercent / 100.0);
}
inline int ShardRefund(int before, int after) {
    return before >= 0 && after >= 0 && before - after == 1 ? 1 : 0;
}
}
