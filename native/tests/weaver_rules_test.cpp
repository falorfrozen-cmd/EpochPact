#include "../core/weaver_rules.hpp"
#include <cstdio>
#include <limits>

int main() {
    using namespace ep::weaver::rules;
    int total = 0, failed = 0;
    const auto check = [&](bool ok, const char* name) { ++total; if (!ok) { ++failed; std::printf("FAIL %s\n", name); } };

    // Weaver's Will Unique share
    check(!ForceWeaver(0, 0) && !ForceWeaver(0, 0.999), "0% never asks");
    check(ForceWeaver(100, 0) && ForceWeaver(100, 0.999999), "100% always asks");
    check(ForceWeaver(25, 0.2499) && !ForceWeaver(25, 0.25), "25% boundary");
    check(!ValidShare(-1) && !ValidShare(101) && !ValidShare(std::numeric_limits<double>::quiet_NaN()) && ValidShare(0) && ValidShare(100), "share range");
    check(!ForceWeaver(150, 0) && !ForceWeaver(std::numeric_limits<double>::infinity(), 0), "invalid shares never ask");

    // Upgrade speed
    check(UpgradeCalls(1, 0) == 1 && UpgradeCalls(1, 0.99) == 1, "x1 runs once");
    check(UpgradeCalls(3, 0) == 3 && UpgradeCalls(3, 0.99) == 3, "whole multiplier");
    check(UpgradeCalls(2.5, 0.49) == 3 && UpgradeCalls(2.5, 0.5) == 2, "fraction as a chance");
    check(UpgradeCalls(0.5, 0) == 1 && UpgradeCalls(11, 0) == 1 && UpgradeCalls(std::numeric_limits<double>::quiet_NaN(), 0) == 1, "invalid speeds run once");
    {   // the fraction's average: 2.25 -> 2.25 runs per grant
        int runs = 0; const int n = 10000;
        for (int i = 0; i < n; ++i) runs += UpgradeCalls(2.25, (i + 0.5) / n);
        check(runs == 22500, "x2.25 averages 2.25");
    }

    // Wanted affixes: upgrades
    const std::vector<Affix> item{{10, 2}, {20, 6}, {30, 1}, {40, 4}};
    {   auto held = HeldForUpgrade(item, {30});
        check(held.size() == 2 && held[0] == 0 && held[1] == 3, "unwanted affixes below T7 are held, T7 ones and the wanted one are not"); }
    check(HeldForUpgrade(item, {20}).empty(), "a wanted affix already at T7 holds nothing");
    check(HeldForUpgrade(item, {99}).empty(), "a wanted affix not on the item holds nothing");
    check(HeldForUpgrade(item, {}).empty(), "no wanted affixes hold nothing");
    {   auto held = HeldForUpgrade(item, {10, 30});
        check(held.size() == 1 && held[0] == 3, "two wanted affixes: only the unwanted one is held"); }
    check(HeldForUpgrade({{10, 6}, {30, 6}}, {10}).empty(), "everything at T7 holds nothing");

    // Wanted affixes: new affixes
    {   auto keep = KeepEligible({5, 6, 7, 8}, {6, 8, 99});
        check(!keep[0] && keep[1] && !keep[2] && keep[3], "only wanted eligible affixes stay"); }
    {   auto keep = KeepEligible({5, 6, 7}, {99});
        check(keep[0] && keep[1] && keep[2], "no wanted affix fits: the game's choice stands"); }
    {   auto keep = KeepEligible({5, 6}, {});
        check(keep[0] && keep[1], "no wanted affixes keep everything"); }
    check(KeepEligible({}, {1}).empty(), "an empty eligible list stays empty");

    std::printf("weaver rules: %d/%d passed\n", total - failed, total);
    return failed ? 1 : 0;
}
