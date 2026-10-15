// Weaver's Will rules shared by weaver.cpp and its tests: no game types here.
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace ep::weaver::rules {

constexpr int MaximumAffixes = 64;
constexpr int TopTier = 6;  // stored zero-based: 6 is T7, the highest tier a Weaver's Will upgrade reaches

// Weaver's Will Unique share, percent of the Uniques rolled without a required legendary type.
inline bool ValidShare(double percent) { return std::isfinite(percent) && percent >= 0 && percent <= 100; }
inline bool ForceWeaver(double percent, double u) { return ValidShare(percent) && percent > 0 && u * 100.0 < percent; }

// Upgrade speed: every upgrade the game grants runs floor(m) times, plus once more with the fraction's chance.
inline bool ValidSpeed(double m) { return std::isfinite(m) && m >= 1 && m <= 10; }
inline int UpgradeCalls(double m, double u) {
    if (!ValidSpeed(m)) return 1;
    const double whole = std::floor(m);
    return static_cast<int>(whole) + (u < m - whole ? 1 : 0);
}

struct Affix { int id; int tier; };

inline bool Wanted(const std::vector<int>& wanted, int id) {
    for (int w : wanted) if (w == id) return true;
    return false;
}

// The affixes to hold at the top tier while the game picks one to upgrade: every affix that
// is not wanted and could still be upgraded, but only when a wanted one can be upgraded too.
// Otherwise nothing is held and the game's own choice stands.
inline std::vector<std::size_t> HeldForUpgrade(const std::vector<Affix>& affixes, const std::vector<int>& wanted) {
    std::vector<std::size_t> held;
    bool wantedBelowTop = false;
    for (const auto& a : affixes) if (a.tier < TopTier && Wanted(wanted, a.id)) wantedBelowTop = true;
    if (!wantedBelowTop) return held;
    for (std::size_t i = 0; i < affixes.size(); ++i)
        if (affixes[i].tier < TopTier && !Wanted(wanted, affixes[i].id)) held.push_back(i);
    return held;
}

// Which new affixes the game may add: the wanted ones among the eligible, or all of them when
// none of the wanted ones fits this item.
inline std::vector<bool> KeepEligible(const std::vector<int>& eligible, const std::vector<int>& wanted) {
    std::vector<bool> keep(eligible.size(), true);
    bool any = false;
    for (int id : eligible) if (Wanted(wanted, id)) any = true;
    if (!any || wanted.empty()) return keep;
    for (std::size_t i = 0; i < eligible.size(); ++i) keep[i] = Wanted(wanted, eligible[i]);
    return keep;
}

}  // namespace ep::weaver::rules
