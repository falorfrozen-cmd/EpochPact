#include "../core/monolith_rules.hpp"
#include <cstdio>
#include <limits>
int main() {
    using namespace ep::monolith::rules;
    int count = 0, failures = 0;
    const auto check = [&](bool pass, const char* name) { ++count; if (!pass) { ++failures; std::printf("FAIL: %s\n", name); } };
    check(Multiplier(1), "off"); check(Multiplier(100), "maximum");
    check(!Multiplier(0), "zero rejected"); check(!Multiplier(101), "oversize rejected");
    check(!Multiplier(std::numeric_limits<double>::quiet_NaN()), "NaN rejected");
    check(!Multiplier(std::numeric_limits<double>::infinity()), "infinity rejected");
    check(Corruption(0, 0, true, 50), "normal baseline");
    check(Corruption(50, 0, true, 50), "normal maximum");
    check(!Corruption(51, 0, true, 50), "normal cap enforced");
    check(!Corruption(-1, 0, true, 50), "negative rejected");
    check(!Corruption(99, 100, false, 0), "empowered floor enforced");
    check(Corruption(100, 100, false, 0), "empowered baseline");
    check(Corruption(65535, 100, false, 0), "serialization maximum");
    check(!Corruption(65536, 100, false, 0), "UInt16 wrap prevented");
    check(ScaleGain(25, 5, 0) == 125, "gain multiplication");
    check(ScaleGain(-25, 5, 100) == -25, "loss not multiplied");
    check(ScaleGain(0, 5, 100) == 0, "zero unchanged");
    check(ScaleGain(25, 1, 0) == 25, "x1 unchanged");
    check(ScaleGain(1, 1.5, 0) == 2, "fraction rounded");
    check(ScaleGain(1000000000, 100, 100) == 2147483547, "addition cannot overflow");
    check(ScaleGain(50, 100, 2147483647) == 0, "full integer cap");
    check(ScaleGain(25, std::numeric_limits<double>::quiet_NaN(), 0) == 25, "invalid gain multiplier unchanged");
    std::printf("monolith: %d/%d passed\n", count-failures, count);
    return failures ? 1 : 0;
}
