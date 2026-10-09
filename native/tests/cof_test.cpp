#include "../core/cof_rules.hpp"
#include "../core/cof_tuning_rules.hpp"
#include <cstdio>
#include <limits>
int main() {
    using namespace ep::cof::rules;
    int total = 0, failed = 0;
    const auto check = [&](bool pass, const char* name) { ++total; if (!pass) { ++failed; std::printf("FAIL: %s\n", name); } };
    check(Favor(0), "zero wallet"); check(Favor(999999), "actual wallet cap");
    check(!Favor(-1), "negative wallet"); check(!Favor(1000000), "over-cap wallet");
    check(Charges(0) && Charges(99), "charge limits"); check(!Charges(-1) && !Charges(100), "charge wrapping rejected");
    check(Multiplier(1) && Multiplier(100), "multiplier limits");
    check(!Multiplier(0) && !Multiplier(101), "multiplier bounds");
    check(!Multiplier(std::numeric_limits<double>::quiet_NaN()), "NaN");
    check(!Multiplier(std::numeric_limits<double>::infinity()), "infinity");
    check(ScaleGain(25, 5) == 125, "normal gain"); check(ScaleGain(-25, 5) == -25, "loss unchanged");
    check(ScaleGain(25, 1) == 25, "x1 unchanged"); check(ScaleGain(0, 5) == 0, "zero unchanged");
    check(ScaleGain(2147483647, 100) == 999999, "large gain saturated before game arithmetic");
    check(ScaleGain(1, 1.5) == 2, "fractional rounding");
    check(ScaleReputationGain(20, 5, 0) == 100, "reputation gain");
    check(ScaleReputationGain(-20, 5, 0) == -20, "nonpositive reputation unchanged");
    check(ScaleReputationGain(20, 1, 100) == 20, "reputation x1 unchanged");
    check(ScaleReputationGain(3, 2.5, 0) == 8, "reputation rounding");
    check(ScaleReputationGain(2147483647, 100, 0) == 2147483647, "reputation multiplication saturates");
    check(ScaleReputationGain(20, 100, 2147483640) == 7, "existing reputation addition room");
    check(ScaleReputationGain(20, 5, 2147483647) == 0, "no addition room");
    check(ScaleReputationGain(20, 5, -1) == 20, "invalid progress cannot be boosted");
    check(ScaleReputationGain(20, std::numeric_limits<double>::quiet_NaN(), 0) == 20, "invalid reputation multiplier unchanged");
    namespace tuning = ep::cof::tuning::rules;
    check(tuning::Chance(0) && tuning::Chance(100), "double chance endpoints");
    check(!tuning::Chance(-1) && !tuning::Chance(101), "double chance bounds");
    check(!tuning::Chance(std::numeric_limits<double>::quiet_NaN()) && !tuning::Chance(std::numeric_limits<double>::infinity()), "double chance finite");
    check(tuning::RewardMultiplier(1) && tuning::RewardMultiplier(25), "reward multiplier endpoints");
    check(!tuning::RewardMultiplier(0) && !tuning::RewardMultiplier(25.1), "reward multiplier bounds");
    check(tuning::Probability(.4f, 3) == 1, "duplication probability saturated");
    check(tuning::RollCoefficient(2, 5) == 10, "T7 native coefficient scales");
    check(tuning::Probability(0, 100) == 0, "inactive rank bonus not invented");
    check(tuning::RollCoefficient(1.5f, 3) == 4.5f, "Exalted native factor 1.5 becomes 4.5");
    check(tuning::RollCoefficient(1.5f, 1) == 1.5f, "Exalted x1 unchanged");
    check(tuning::RewardItems(10, 3) == 30, "normal prophecy item count");
    check(tuning::RewardItems(3, 1.5) == 5, "prophecy item count rounding");
    check(tuning::RewardItems(1000000, 25) == 250, "prophecy item count bounded");
    check(tuning::RewardItems(1000, 1) == 1000, "existing reward unchanged at x1");
    check(tuning::RewardItems(0, 25) == 0 && tuning::RewardItems(-10, 25) == -10, "empty reward unchanged");
    check(tuning::ChargeInput(20, 5, 300, 0) == 100, "independent charge input");
    check(tuning::ChargeInput(3, 2.5, 1, 0) == 8, "charge input rounding");
    check(tuning::ChargeInput(0, 5, 1, 0) == 0 && tuning::ChargeInput(-5, 5, 1, 0) == -5, "charge losses unchanged");
    check(tuning::ChargeInput(INT32_MAX, 100, 200000, 100) <= 10737, "charge float and addition overflow bounded");
    check(tuning::ChargeInput(100, 100, 1, INT32_MAX) == 0, "full Int32 charge progress bounded");
    check(tuning::ChargeInput(10, 5, std::numeric_limits<double>::quiet_NaN(), 0) == 0, "invalid native charge coefficient cannot overflow");
    check(tuning::AffixContext(0) && tuning::AffixContext(4) && tuning::AffixContext(5), "normal encounter contexts supported");
    check(!tuning::AffixContext(1) && !tuning::AffixContext(2) && !tuning::AffixContext(3) && !tuning::AffixContext(999), "shop gambling and unknown contexts excluded");
    std::printf("cof: %d/%d passed\n", total-failed, total); return failed ? 1 : 0;
}
