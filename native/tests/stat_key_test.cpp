#include "../core/stat_key.hpp"
#include "../core/stat_memory.hpp"
#include <cstdio>
#include <limits>
using namespace ep::statedit;
int main() {
    int passed = 0, failed = 0;
    const auto check = [&](bool result, const char* name) {
        if (result) ++passed;
        else { ++failed; std::printf("FAIL: %s\n", name); }
    };
    int64_t n = 0; double f = 0; Mode mode{};
    check(Integer("0", 0, 133, &n) && n == 0, "SP zero");
    check(Integer("133", 0, 133, &n) && n == 133, "last SP");
    check(!Integer("134", 0, 133, &n), "undefined SP");
    check(Integer("0x2000", INT32_MIN, INT32_MAX, &n) && n == 8192, "minion bit");
    check(Integer("4097", INT32_MIN, INT32_MAX, &n) && n == 4097, "physical DoT tag combination");
    check(Integer("636", INT32_MIN, INT32_MAX, &n) && n == 636, "PlayerProperty index is not a bit mask");
    check(Integer("255", 0, 255, &n) && n == 255, "byte special max");
    check(!Integer("256", 0, 255, &n) && !Integer("-1", 0, 255, &n), "special overflow");
    check(Integer("-2147483648", INT32_MIN, INT32_MAX, &n) && n == INT32_MIN, "signed extra min");
    check(Integer("2147483647", INT32_MIN, INT32_MAX, &n) && n == INT32_MAX, "signed extra max");
    check(!Integer("2147483648", INT32_MIN, INT32_MAX, &n), "signed overflow");
    check(!Integer("1.2", 0, 255, &n) && !Integer("12oops", 0, 255, &n) && !Integer("", 0, 255, &n), "reject partial integers");
    check(!Integer("999999999999999999999", 0, 255, &n), "huge integer");
    check(Number("0.65", &f) && f == 0.65, "percent fraction");
    check(Number("10", &f) && f == 10, "1000 percent reflection");
    check(Number("-0.5", &f) && ValidValue(Mode::More, f), "less multiplier");
    check(!Number("nan", &f) && !Number("inf", &f) && !Number("1e999", &f), "nonfinite");
    check(!Number("1e40", &f), "float overflow");
    check(!Number("1e-80", &f), "silent underflow");
    check(!Number("1e-500", &f), "double underflow");
    check(!Number("0.65junk", &f) && !Number("", &f), "partial numbers");
    check(ParseMode("added", &mode) && mode == Mode::Added, "flat mode");
    check(ParseMode("increased", &mode) && mode == Mode::Increased, "increased mode");
    check(ParseMode("more", &mode) && mode == Mode::More, "more mode");
    check(!ParseMode("anything", &mode), "invalid mode");
    check(ValidValue(Mode::More, -1) && !ValidValue(Mode::More, -1.01), "more floor");
    check(!ValidValue(Mode::Increased, -2) && ValidValue(Mode::Added, -2), "negative flat versus multiplier");
    check(!ValidValue(Mode::Added, std::numeric_limits<double>::quiet_NaN()), "API nan");
    check(NormalName("Bow Attack Speed") == "bowattackspeed", "sheet name normalization");
    check(Key{1, 512, 2, 0} != Key{1, 512, 1, 0}, "ailments stay distinct");
    check(Key{98, 636, 0, 0} != Key{98, 637, 0, 0}, "player properties stay distinct");
    check(Key{58, 782, 0, 7} != Key{58, 782, 0, 8}, "ability extra tag distinct");
    check(Key{0, 8192, 0, 0} != Key{0, 0, 0, 0}, "minion scope distinct");
    check(AttributeContribution(4, 0, 1, 0) == 4, "100 percent Intelligence produces 4 additional points");
    check(AttributeContribution(8, 0, 1, 0) == 8, "equipment/base changes rebase without accumulating old bonus");
    check(AttributeContribution(4, 2, 1, 0) == 8, "flat and increased compose to 12 total");
    check(AttributeContribution(4, 0, 1, .5) == 8, "increased and more compose to 12 total");
    check(AttributeContribution(4, 0, -1, 0) == -4, "100 percent reduction removes all attribute points");
    check(AttributeContribution(4, 0, 0, 0) == 0, "neutral attribute bonus removes only the mod contribution");
    check(!std::isfinite(AttributeContribution(4, 0, 1e30, 0)), "attribute overflow refused before managed int conversion");
    // Remembered character bonuses (stat_memory.hpp)
    {
        namespace mem = ep::statedit::memory;
        mem::Characters all;
        auto& hero = all[mem::Character("6", "Hero")];
        mem::Put(hero, {{17, 0, 0, 0}, 50, 0, 0});
        mem::Put(hero, {{116, 0, 0, 0}, 0, 0.5f, 0});
        mem::Put(hero, {{0, 8192, 0, 0}, 0, 0, -0.25f});
        mem::Put(hero, {{9, 0, 0, 0}, 0, 1, 0});
        check(hero.size() == 3, "movement speed stays with the Speed setting");
        mem::Put(hero, {{17, 0, 0, 0}, 80, 0.1f, 0});
        check(hero.size() == 3 && hero[0].added == 80 && hero[0].increased == 0.1f, "a change replaces the key's entry");
        mem::Put(hero, {{116, 0, 0, 0}});
        check(hero.size() == 2, "a reset forgets the key");
        all[mem::Character("7", "Na\"me\\")] = {{{18, 0, 0, 0}, 20, 0, 0}};
        check(mem::Parse(mem::Text(all)) == all, "the file text reads back the same, also with quotes in a name");
        check(mem::Character("6", "Hero") != mem::Character("6", "Other") && mem::Character("", "Hero").empty(), "a reused slot is another character");
        const auto damaged = mem::Parse(R"({"version":1,"characters":{"6|Hero":[
            {"key":[17,0,0,0],"added":5,"increased":0,"more":0},
            {"key":[300,0,0,0],"added":1,"increased":0,"more":0},
            {"key":[1,0,256,0],"added":1,"increased":0,"more":0},
            {"key":[1,0,0,0.5],"added":1,"increased":0,"more":0},
            {"key":[2,0,0,0],"added":1,"increased":-2,"more":0},
            {"key":[3,0,0,0],"added":"1","increased":0,"more":0},
            {"key":[4,0,0,0],"added":0,"increased":0,"more":0},
            {"key":[9,0,0,0],"added":0,"increased":1,"more":0},
            {"key":[17,0,0,0],"added":9,"increased":0,"more":0}],
            "8|Empty":[], "9|Bad":{}}})");
        check(damaged.size() == 1 && damaged.at("6|Hero").size() == 1 && damaged.at("6|Hero")[0].added == 5, "damaged entries are left out, the others count");
        check(mem::Parse(R"({"version":2,"characters":{"6|Hero":[{"key":[17,0,0,0],"added":5,"increased":0,"more":0}]}})").empty(), "an unknown version is not read");
        check(mem::Parse("not json").empty() && mem::Parse("").empty(), "a broken file reads as nothing remembered");
        std::vector<mem::Bonus> full;
        for (int i = 0; i < 400; ++i) mem::Put(full, {{static_cast<uint8_t>(i % 256), i, 0, 0}, 1, 0, 0});
        check(full.size() == mem::kBonuses, "a character keeps at most 300 bonuses");
    }
    std::printf("stat_key_test: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
