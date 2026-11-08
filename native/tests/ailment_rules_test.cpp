#include "../core/ailment_rules.hpp"
#include <cstdio>
#include <limits>

int main() {
    using namespace ep::ailments::rules;
    int total = 0, failed = 0;
    const auto check = [&](bool ok, const char* name) { ++total; if (!ok) { ++failed; std::printf("FAIL %s\n", name); } };
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();

    const Range* ranges[]{&kStacks, &kDuration};
    for (const Range* r : ranges)
        check(InRange(*r, r->neutral) && InRange(*r, r->low) && InRange(*r, r->high) && !InRange(*r, r->low - 0.01) && !InRange(*r, r->high + 0.01) &&
              !InRange(*r, nan) && !InRange(*r, inf), "ranges");
    check(kStacks.neutral == 1 && kStacks.high == 30 && kDuration.neutral == 1 && kDuration.high == 10, "normal is 1, stacks up to 30x, duration up to 10x");

    // Stack caps: Time Rot 12 -> 24 .. 360; normal leaves it; no cap stays no cap.
    check(ScaleStacks(12, 1) == 12 && ScaleStacks(12, 2) == 24 && ScaleStacks(12, 30) == 360, "Time Rot 12 times 2 and 30");
    check(ScaleStacks(4, 1.5) == 6 && ScaleStacks(1, 30) == 30 && ScaleStacks(3, 2.5) == 8, "rounded");
    check(ScaleStacks(0, 30) == 0 && ScaleStacks(-1, 30) == -1 && ScaleStacks(kNoCap, 30) == kNoCap && ScaleStacks(2147483647, 30) == 2147483647, "no cap left alone");
    check(ScaleStacks(999, 30) == 29970 && ScaleStacks(12, nan) == 12 && ScaleStacks(12, 0.5) == 12, "large cap, broken or lower multiplier");
    check(HasCap(12) && HasCap(1) && !HasCap(0) && !HasCap(kNoCap) && !HasCap(-5), "what counts as a cap");

    // Durations: 3 s -> 4.5 s, 6 s, 15 s; no duration stays none.
    check(ScaleDuration(3, 1) == 3 && ScaleDuration(3, 1.5f) == 4.5f && ScaleDuration(3, 2) == 6 && ScaleDuration(3, 5) == 15 && ScaleDuration(3, 10) == 30, "duration scale");
    check(ScaleDuration(0, 5) == 0 && ScaleDuration(-1, 5) == -1 && ScaleDuration(std::numeric_limits<float>::infinity(), 5) == std::numeric_limits<float>::infinity(), "no duration left alone");
    check(ScaleDuration(3, nan) == 3 && ScaleDuration(3, 0.5) == 3, "broken or lower multiplier");

    std::printf("ailment rules: %d/%d passed\n", total - failed, total);
    return failed ? 1 : 0;
}
