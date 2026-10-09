#include "../core/density_rules.hpp"
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
int total = 0, passed = 0;
void Check(bool ok, const char* name) { ++total; if (ok) ++passed; std::printf("%s %s\n", ok ? "ok  " : "FAIL", name); }
}
int main() {
    using namespace ep::density::rules;
    Pack regular{2.6f, .2f, true, true, false, false, false};
    const auto scaled = Evaluate(regular, 3, true);
    Check(scaled.reason == Reason::Eligible && std::fabs(scaled.mean - 7.8f) < .00001f, "normal offline pack grows x3");
    Check(std::fabs(Evaluate(regular, 2.5, true).mean - 6.5f) < .00001f, "fractional multiplier accepted");
    Check(Evaluate(regular, 3, false).reason == Reason::Offline, "online/unknown offline gate refuses scaling");
    Check(Evaluate(regular, 1, true).reason == Reason::Invalid, "off multiplier does not enter scaling");
    Check(Evaluate(regular, 0, true).reason == Reason::Invalid, "zero multiplier refused");
    Check(Evaluate(regular, -1, true).reason == Reason::Invalid, "negative multiplier refused");
    Check(Evaluate(regular, 5.1, true).reason == Reason::Invalid, "multiplier above x5 refused");
    Check(Evaluate(regular, std::numeric_limits<double>::infinity(), true).reason == Reason::Invalid, "infinite multiplier refused");
    Check(Evaluate(regular, std::numeric_limits<double>::quiet_NaN(), true).reason == Reason::Invalid, "NaN multiplier refused");
    auto p = regular; p.known = false;
    Check(Evaluate(p, 3, true).reason == Reason::Unknown, "missing ActorData fails closed");
    p = regular; p.normal = false;
    Check(Evaluate(p, 3, true).reason == Reason::Special, "boss/miniboss/minion/ally/container types excluded");
    p = regular; p.special = true;
    Check(Evaluate(p, 3, true).reason == Reason::Special, "lone boss/champion/omen/nemesis/harbinger excluded");
    p = regular; p.friendly = true;
    Check(Evaluate(p, 3, true).reason == Reason::Special, "forceGood spawners excluded");
    p = regular; p.summoned = true;
    Check(Evaluate(p, 3, true).reason == Reason::Summoned, "summoning/twinned/unknown contexts excluded");
    p = regular; p.mean = 1;
    Check(Evaluate(p, 3, true).reason == Reason::Single && Evaluate(p, 3, true).mean == 1, "single spawns unchanged");
    p.mean = 1.5f;
    Check(Evaluate(p, 3, true).reason == Reason::Single, "single-pack boundary excluded");
    p.mean = 1.51f;
    Check(Evaluate(p, 3, true).reason == Reason::Eligible, "small regular pack above boundary accepted");
    p.mean = 0;
    Check(Evaluate(p, 3, true).reason == Reason::Single, "empty pack unchanged");
    p.mean = -2;
    Check(Evaluate(p, 3, true).reason == Reason::Invalid, "negative pack mean rejected");
    p.mean = std::numeric_limits<float>::quiet_NaN();
    Check(Evaluate(p, 3, true).reason == Reason::Invalid, "NaN game field rejected");
    p.mean = std::numeric_limits<float>::infinity();
    Check(Evaluate(p, 3, true).reason == Reason::Invalid, "infinite game field rejected");
    p = regular; p.variance = -.1f;
    Check(Evaluate(p, 3, true).reason == Reason::Invalid, "negative variance rejected");
    p.variance = 1.1f;
    Check(Evaluate(p, 3, true).reason == Reason::Invalid, "variance outside expected fraction rejected");
    p.variance = std::numeric_limits<float>::quiet_NaN();
    Check(Evaluate(p, 3, true).reason == Reason::Invalid, "NaN variance rejected");
    p = regular; p.mean = 60; p.variance = 1;
    auto bounded = Evaluate(p, 5, true);
    Check(bounded.reason == Reason::Eligible && bounded.limited && bounded.mean == 64, "limit accounts for maximum random variance");
    p.mean = 64;
    Check(Evaluate(p, 5, true).reason == Reason::Limit && Evaluate(p, 5, true).mean == 64, "existing oversized pack never shrinks");
    p = regular; p.mean = (std::numeric_limits<float>::max)();
    Check(Evaluate(p, 5, true).reason == Reason::Limit, "huge finite mean passes unchanged without overflow");

    float field = regular.mean;
    int calls = 0;
    WithCount(field, scaled.mean, [&] { ++calls; Check(std::fabs(field - 7.8f) < .00001f, "game sees temporary scaled mean"); });
    Check(calls == 1 && field == regular.mean, "original runs once and field restored");
    WithCount(field, scaled.mean, [&] { ++calls; });
    Check(calls == 2 && field == regular.mean, "repeat generation does not compound");
    try { WithCount(field, scaled.mean, [] { throw std::runtime_error("game simulation"); }); }
    catch (const std::runtime_error&) {}
    Check(field == regular.mean, "exception unwind restores original field");
    int spawnerA = 0, spawnerB = 0;
    Check(!Scope::Contains(&spawnerA), "reentrancy chain starts empty");
    {
        Scope scopeA(&spawnerA);
        Check(Scope::Contains(&spawnerA) && !Scope::Contains(&spawnerB), "same-spawner reentrancy detected");
        {
            Scope scopeB(&spawnerB);
            Check(Scope::Contains(&spawnerA) && Scope::Contains(&spawnerB), "nested different spawner retains both identities");
        }
        Check(Scope::Contains(&spawnerA) && !Scope::Contains(&spawnerB), "nested scope restored");
        bool isolated = false;
        std::thread other([&] { isolated = !Scope::Contains(&spawnerA); }); other.join();
        Check(isolated, "reentrancy chain is thread-local");
    }
    Check(!Scope::Contains(&spawnerA), "scope exit clears reentrancy chain");
    std::printf("density_test: %d/%d passed\n", passed, total);
    return total - passed;
}
