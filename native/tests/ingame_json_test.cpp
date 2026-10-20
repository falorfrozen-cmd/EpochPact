#include "../core/ingame_json.hpp"
#include <cstdio>

int main() {
    using namespace ep::ingame::json;
    int total = 0, failed = 0;
    const auto check = [&](bool ok, const char* name) { ++total; if (!ok) { ++failed; std::printf("FAIL %s\n", name); } };

    Value v;
    check(Parse(R"({"ok":true,"settings":{"zoom":{"value":1.5},"name":"x"},"list":[1,2,3],"none":null})", v), "object parses");
    check(v["ok"].IsBool() && v["ok"].boolean, "bool member");
    check(v.Path("settings.zoom.value") && v.Path("settings.zoom.value")->Num() == 1.5, "dotted path");
    check(!v.Path("settings.missing.value") && !v.Path("list.0"), "missing path is null, no array indexes");
    check(v["list"].items.size() == 3 && v["list"].items[2].Num() == 3, "array");
    check(v["none"].IsNull() && v["nothing"].IsNull(), "null and absent");
    check(v["settings"]["name"].Str() == "x" && v["ok"].Str("fallback") == "fallback", "strings and fallbacks");

    check(Parse(R"("caf\u00e9 \u00d7 \u2212 \ud83d\ude00 \"q\" \\ \/ \n")", v) && v.text == "caf\xC3\xA9 \xC3\x97 \xE2\x88\x92 \xF0\x9F\x98\x80 \"q\" \\ / \n", "escapes and UTF-8");
    check(Parse("\"T\xC3\xBCrk\xC3\xA7" "e \xD1\x80\xD1\x83\xD1\x81\"", v) && v.text == "T\xC3\xBCrk\xC3\xA7" "e \xD1\x80\xD1\x83\xD1\x81", "raw UTF-8 passes through");
    check(Parse("[-1.5e2, 0, 12.25]", v) && v.items[0].Num() == -150 && v.items[2].Num() == 12.25, "numbers");
    check(Parse(" {} ", v) && v.IsObject() && v.members.empty() && Parse("[]", v) && v.items.empty(), "empty containers");

    std::string why;
    check(!Parse("{\"a\":1,}", v, &why) && !why.empty(), "trailing comma refused with a reason");
    check(!Parse("{\"a\" 1}", v) && !Parse("[1 2]", v) && !Parse("tru", v) && !Parse("\"open", v), "broken texts refused");
    check(!Parse("{} x", v) && !Parse("", v) && !Parse("-", v) && !Parse("01x", v), "trailing text and bad numbers refused");
    check(!Parse("\"\\ud83d\"", v) && !Parse("\"\\udc00\"", v) && !Parse("\"\\x\"", v), "bad escapes refused");
    check(!Parse("\"a\x01\"", v), "control character refused");
    std::string deep(100, '[');
    deep += std::string(100, ']');
    check(!Parse(deep, v, &why) && why.find("deep") != std::string::npos, "nesting limit");

    std::printf("%s %d/%d in-game JSON reader: objects, paths, escapes, numbers, refusals\n", failed ? "FAIL" : "PASS", total - failed, total);
    return failed ? 1 : 0;
}
