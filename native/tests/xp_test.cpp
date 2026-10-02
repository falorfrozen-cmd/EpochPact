// The experience multiplier (core/xp.cpp) end to end without the game: the real module and
// the real hook engine, against t_gain, a stand-in with GainExpFromEnemyOrMote's calling
// convention and first instruction. The gate is the test's (fake_game.cpp).
// Prints one line per check and "xp_test: <passed>/<total> passed" last.

#include "../core/hook.hpp"
#include "../core/xp.hpp"
#include "fake_game.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" void t_gain(void* self, long long amount, const void* method);
extern "C" unsigned long long g_gainSelf, g_gainAmount, g_gainMethod, g_gainCalls;

namespace {

int g_total = 0, g_passed = 0;

void Check(bool ok, const char* name, const std::string& detail = {}) {
    ++g_total;
    if (ok) ++g_passed;
    std::printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", name, detail.empty() ? "" : ": ", detail.c_str());
}

bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// What the game passes: an ExperienceTracker and the method's MethodInfo.
void* const kSelf = reinterpret_cast<void*>(0x1234);
const void* const kInfo = reinterpret_cast<const void*>(0x5678);

long long Gain(long long amount) {
    t_gain(kSelf, amount, kInfo);
    return static_cast<long long>(g_gainAmount);
}

bool Installed() { return ep::hook::IsInstalled(reinterpret_cast<void*>(&t_gain)); }

int FirstGainLines() {
    int n = 0;
    for (const std::string& line : fake::log)
        if (Has(line, "first boosted gain")) ++n;
    return n;
}

}  // namespace

int main() {
    uint8_t before[16];
    std::memcpy(before, reinterpret_cast<const void*>(&t_gain), sizeof before);

    Check(ep::xp::Init(), "finds GainExpFromEnemyOrMote");
    Check(Has(ep::xp::Status(), "x1, hook out"), "starts at x1 with no hook", ep::xp::Status());
    Check(Gain(1000) == 1000, "an unhooked gain is the game's own");

    // Baseline refusals: nothing installed by any of them.
    Check(Has(ep::xp::Set(0.5), "between 1 and"), "refuses x0.5");
    Check(Has(ep::xp::Set(101), "between 1 and"), "refuses x101");
    Check(Has(ep::xp::Set(std::nan("")), "between 1 and"), "refuses NaN");
    fake::offline = false;
    std::string r = ep::xp::Set(3);
    Check(Has(r, "refused") && Has(r, "ONLINE"), "refuses in online play", r);
    fake::known = false;
    r = ep::xp::Set(3);
    Check(Has(r, "refused") && Has(r, "unknown"), "refuses when the online flag is unreadable", r);
    fake::known = true;
    fake::offline = true;
    Check(!Installed(), "no refusal installed a hook");

    // Target: x3 offline.
    r = ep::xp::Set(3);
    Check(Has(r, "xp -> x3") && Installed(), "x3 offline installs the hook", r);
    g_gainSelf = g_gainMethod = 0;
    long long got = Gain(1000);
    Check(got == 3000, "1000 experience arrives as 3000", std::to_string(got));
    Check(reinterpret_cast<void*>(g_gainSelf) == kSelf && reinterpret_cast<const void*>(g_gainMethod) == kInfo,
          "the tracker and MethodInfo reach the game unchanged");
    Gain(500);
    Check(FirstGainLines() == 1, "one first-gain line per arming", std::to_string(FirstGainLines()));

    r = ep::xp::Set(2.5);
    got = Gain(1000);
    Check(Has(r, "x2.5") && Installed() && got == 2500, "x2.5 while armed: 1000 -> 2500", std::to_string(got));
    Check(FirstGainLines() == 2, "re-arming logs the first gain again");
    Check(Gain(0) == 0 && Gain(-50) == -50, "zero and negative amounts pass unchanged");
    got = Gain(7);
    Check(got == 18, "rounds to the nearest whole point (7 x 2.5 = 17.5 -> 18)", std::to_string(got));

    fake::offline = false;  // the game went online while armed
    Check(Gain(1000) == 1000, "online while armed: the gain is the game's own");
    Check(Has(ep::xp::Status(), "refused online 1"), "the refusal is counted", ep::xp::Status());
    fake::offline = true;

    ep::xp::Set(100);
    got = Gain(4000000000000000000LL);
    Check(got == 9000000000000000000LL, "x100 saturates instead of overflowing", std::to_string(got));

    r = ep::xp::Set(1);
    Check(Has(r, "x1 (off; hook removed)") && !Installed(), "x1 removes the hook", r);
    Check(Gain(1000) == 1000, "after x1 the gain is the game's own again");
    Check(std::memcmp(before, reinterpret_cast<const void*>(&t_gain), sizeof before) == 0, "the function's bytes are back as they were");
    Check(Has(ep::xp::Set(1), "x1 (off"), "x1 again is a quiet no-op");

    std::printf("xp_test: %d/%d passed\n", g_passed, g_total);
    return g_total - g_passed;
}
