#include "../core/ingame_rules.hpp"
#include <cstdio>
#include <limits>

int main() {
    using namespace ep::ingame::rules;
    int total = 0, failed = 0;
    const auto check = [&](bool ok, const char* name) { ++total; if (!ok) { ++failed; std::printf("FAIL %s\n", name); } };
    const double nan = std::numeric_limits<double>::quiet_NaN();

    // Steps: up/down along the list, the ends stay, in-between values go to the next step that way.
    check(Step(kXpSteps, 1, true) == 2 && Step(kXpSteps, 2, true) == 3 && Step(kXpSteps, 50, true) == 100, "xp up");
    check(Step(kXpSteps, 100, true) == 100 && Step(kXpSteps, 1, false) == 1, "xp ends stay");
    check(Step(kXpSteps, 3, false) == 2 && Step(kXpSteps, 100, false) == 50, "xp down");
    check(Step(kXpSteps, 4, true) == 5 && Step(kXpSteps, 4, false) == 3, "value between steps");
    check(Step(kXpSteps, 250, false) == 100 && Step(kXpSteps, 0.5, true) == 1, "value outside the list");
    check(Step(kXpSteps, nan, true) == 1, "broken value starts at the first step");
    check(Step(kDensitySteps, 1, true) == 1.5 && Step(kDensitySteps, 1.5, true) == 2 && Step(kDensitySteps, 5, true) == 5, "density up");
    check(Step(kDensitySteps, 2, false) == 1.5 && Step(kDensitySteps, 1, false) == 1, "density down");

    check(Number(2) == "2" && Number(1.5) == "1.5" && Number(100) == "100", "number text");

    // The status lines the panel reads (the status command's real format).
    const std::string status =
        "EpochPact 0.1.2-dev.36; gate: offline play\n"
        "xp: x5, hook in, gains 12 (boosted 12, refused online 0), last 10 -> 50\n"
        "gold: x1, hooks out, boosted 0, refused online 0\n"
        "density: x1.5, hooks in, boosted 3, refused online 0; generated packs 4\n"
        "autopickup: on, hook in, scans 40, pickup calls 9\n";
    const Values v = Parse(status);
    check(v.xp == 5 && v.density == 1.5 && v.autopickup == 1 && v.offline, "status parsed");
    check(Parse("EpochPact x; gate: ONLINE play (every feature refuses)\nxp: x1, hook out\nautopickup: off, hook out").autopickup == 0, "autopickup off");
    check(!Parse("EpochPact x; gate: ONLINE play (every feature refuses)\n").offline, "online is not offline");
    check(Parse("goldxp: x9\n").xp == -1 && Parse("note xp: x9\n").xp == -1, "only a line that starts with the name");
    check(Parse("").xp == -1 && Parse("").density == -1 && Parse("").autopickup == -1 && !Parse("").offline, "empty status");
    check(Parse("xp: xbroken\n").xp == -1, "unreadable number");

    // Commands: the same text the IPC channel takes.
    check(Command(Action::XpUp, v) == "xp 10" && Command(Action::XpDown, v) == "xp 3", "xp commands");
    check(Command(Action::DensityUp, v) == "density 2" && Command(Action::DensityDown, v) == "density 1", "density commands");
    check(Command(Action::AutoPickup, v) == "autopickup 0", "auto pickup turns off when on");
    Values unread;
    check(Command(Action::XpUp, unread) == "xp 2" && Command(Action::DensityUp, unread) == "density 1.5" && Command(Action::AutoPickup, unread) == "autopickup 1", "unread values start from normal");

    check(Shown(2) == "x2" && Shown(1.5) == "x1.5" && Shown(-1) == "...", "shown values");
    check(Brief("xp: x2, hook in\nmore") == "xp: x2, hook in" && Brief(std::string(200, 'a')).size() == 90, "brief reply");

    std::printf("%s %d/%d in-game panel rules: steps, status lines, commands\n", failed ? "FAIL" : "PASS", total - failed, total);
    return failed ? 1 : 0;
}
