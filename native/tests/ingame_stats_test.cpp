// build.bat uses /DNDEBUG for optimized runtime code. Fixture assertions must still run.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../core/ingame_stat_rules.hpp"
#include <cassert>
#include <iostream>
using namespace ep::ingame;
int main() {
    json::Value e;
    assert(json::Parse(R"({"key":{"sp":9,"tags":8192,"special":0,"extra":0},"modes":["increased","more"],"units":{"increased":{"scale":100,"minimum":-100,"maximum":3.4e40},"more":{"scale":100,"minimum":-100,"maximum":3.4e40}}})", e));
    std::string command;
    assert(stats::Key(e) == "statraw 9 8192 0 0");
    assert(stats::Command(e, "increased", "50", &command) && command == "statraw 9 8192 0 0 increased 0.5");
    assert(stats::Command(e, "more", "-100", &command) && command == "statraw 9 8192 0 0 more -1");
    assert(!stats::Command(e, "added", "1", &command));
    for (const char* bad : {"-101", "nan", "inf", "1e309", "", "5 more 100", "50x"}) assert(!stats::Command(e, "more", bad, &command));
    auto value = stats::Contributions("gameValue=44 EpochPact added=1.5 increased=0.5 more=-0.25 attached=1");
    assert(value.known && value.attached && value.values[0] == 1.5 && value.values[1] == 0.5 && value.game == "44");
    value = stats::Contributions("SP=9 (Movespeed) tags=0 special=0 extra=0: exact-key sum=2 applicableMore=0");
    assert(value.known && value.attached && value.values[0] == 0);
    assert(!stats::Contributions("error: not ready").known);
    assert(!stats::Contributions("statraw applicableMore=0 EpochPact added=nan increased=0 more=0 attached=1").known);
    std::cout << "ingame stats: validation, units, exact keys and native reply parsing passed\n";
}
