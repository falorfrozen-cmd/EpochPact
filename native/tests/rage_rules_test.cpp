#include "../core/rage_rules.hpp"
#include <cstdio>
#include <limits>

int main() {
    using namespace ep::rage::rules;
    int total = 0, failed = 0;
    const auto check = [&](bool ok, const char* name) { ++total; if (!ok) { ++failed; std::printf("FAIL %s\n", name); } };
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();

    // Setting ranges: normal is inside, the ends are inside, outside and non-numbers are refused.
    for (const auto* r : {&kSpawn, &kHuntTime, &kCharges, &kBuffs, &kBlood})
        check(InRange(*r, r->neutral) && InRange(*r, r->low) && InRange(*r, r->high) && !InRange(*r, r->low - 0.01) && !InRange(*r, r->high + 0.01) &&
              !InRange(*r, nan) && !InRange(*r, inf), "ranges");
    check(InRange(kBlood, -1) && !InRange(kBlood, -0.5) && InRange(kBlood, 0) && InRange(kBlood, 100), "blood: the game's own (-1) or 0..100 %");
    check(BloodChance(0.5f, -1) == 0.5f && BloodChance(0.5f, 0) == 0 && BloodChance(0.5f, 100) == 1 && BloodChance(0.5f, 25) == 0.25f, "blood share as a chance");
    check(kCharges.neutral == 1 && kCharges.high == 1 && kSpawn.neutral == 1 && kSpawn.low == 1, "normal is 1 and the safe end");

    // Chances: scaled, never above 100 %, a zero or broken chance stays as it was.
    check(ScaleChance(0.05f, 1) == 0.05f && std::abs(ScaleChance(0.05f, 4) - 0.2f) < 1e-6f && ScaleChance(0.3f, 4) == 1.f && std::abs(ScaleChance(0.3f, 2) - 0.6f) < 1e-6f, "chance scale and cap");
    check(ScaleChance(0, 20) == 0 && ScaleChance(-1, 5) == -1, "zero or negative chance unchanged");
    check(std::isnan(ScaleChance(std::numeric_limits<float>::quiet_NaN(), 5)), "broken chance unchanged");

    // Rates: no cap (the game multiplies them again by each zone's own factor before rolling).
    check(ScaleRate(0.3f, 1) == 0.3f && std::abs(ScaleRate(0.3f, 4) - 1.2f) < 1e-6f && ScaleRate(0, 5) == 0 && ScaleRate(-1, 5) == -1, "rate scale without cap");

    // Durations.
    check(ScaleTime(30, 1) == 30 && ScaleTime(30, 2.5) == 75 && ScaleTime(0.5f, 4) == 2 && ScaleTime(0, 5) == 0 && ScaleTime(-2, 5) == -2, "duration scale");

    // Kill thresholds: share of the kills, rounded, at least 1 and still rising.
    const std::vector<int> charges{20, 50, 90, 140, 200};
    check(ScaleThresholds(charges, 1) == charges, "x1 keeps the game's values");
    check(ScaleThresholds(charges, 0.5) == std::vector<int>({10, 25, 45, 70, 100}), "half the kills");
    check(ScaleThresholds(charges, 0.2) == std::vector<int>({4, 10, 18, 28, 40}), "a fifth of the kills");
    check(ScaleThresholds({1, 2, 3}, 0.2) == std::vector<int>({1, 2, 3}), "at least 1 and rising");
    check(ScaleThresholds({5, 6, 7, 30}, 0.2) == std::vector<int>({1, 2, 3, 6}), "close thresholds stay apart");
    check(ScaleThresholds({}, 0.5).empty(), "empty list");
    check(ScaleThresholds({0, 10}, 0.5) == std::vector<int>({0, 5}), "a zero threshold is left alone");
    check(ScaleThresholds(charges, 0.5).size() == charges.size(), "same number of thresholds (the game draws one orb per charge)");

    std::printf("%s %d/%d rage of morditas rules: ranges, rates, chances, Blood share, durations, kill thresholds\n", failed ? "FAIL" : "PASS", total - failed, total);
    return failed ? 1 : 0;
}
