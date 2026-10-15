#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace ep::lootcraft::rules {
enum class Mode { All, Filter, Quality, Materials };
struct Policy {
    Mode mode = Mode::All;
    int minimumLP = 2;
    int minimumWW = 0;  // 0 = off; a Weaver's Will Unique has LP 0, so only this lets it pass a LP threshold
    bool t7 = true, respectFilter = true, materials = true;
    bool categories[6]{true, true, true, true, true, true};  // gold, potions, xp tomes, favor, bones, amber/embers/motes
    std::vector<int> affixes;
};
struct Item { bool material, unique, wantedT7, filterPass; int lp; int ww = 0; };
inline bool Accept(const Policy& p, const Item& item) {
    if (p.mode == Mode::All) return !item.material || p.materials;
    if (p.mode == Mode::Materials) return item.material && p.materials;
    if ((p.mode == Mode::Filter || p.respectFilter) && !item.filterPass) return false;
    if (item.material) return p.materials;
    if (p.mode == Mode::Filter) return true;
    const bool potential = item.lp >= p.minimumLP || (p.minimumWW > 0 && item.ww >= p.minimumWW);
    return (item.unique && potential) || (p.t7 && item.wantedT7);
}
// Weaver's Will multiplier: the rolled value times m, rounded, never below the
// roll and never above the game's maximum (a roll already above it stays).
inline int ScaleWeaversWill(int rolled, double m, int maximum) {
    if (rolled <= 0 || !std::isfinite(m) || m <= 1) return rolled;
    const double scaled = std::round(rolled * m);
    const int cap = rolled > maximum ? rolled : maximum;
    return scaled >= cap ? cap : static_cast<int>(scaled);  // m > 1, so scaled >= rolled
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
