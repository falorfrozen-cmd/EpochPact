#include "../core/item_tiers_text.hpp"
#include <cstdio>
#include <cstdlib>
using ep::tiers::text::StripColorTags;
void Check(bool value, const char* what) { if (!value) { std::printf("FAILED tier text: %s\n", what); std::exit(1); } }
int main() {
    // Affix lines as the game wrote them (0.1.2-dev.21 live check).
    Check(StripColorTags("<color=#ABABFFFF><size=100%>+2 Vitality\n</color>") == "<size=100%>+2 Vitality\n", "experimental");
    Check(StripColorTags("<color=#E3DE98FF><size=100%>+2 Health Regen per Second\n</color>") == "<size=100%>+2 Health Regen per Second\n", "personal");
    Check(StripColorTags("<size=100%>+23 Health\n") == "<size=100%>+23 Health\n", "line without colour");
    Check(StripColorTags("+1 to All Skills\n<color=#C0A0FF>+30 Mana</color>") == "+1 to All Skills\n+30 Mana", "two stats");
    Check(StripColorTags("<COLOR=\"red\">a</Color> <#FF00FF>b</color>") == "a b", "case and short form");
    Check(StripColorTags("Level < 50 <b>x</b> <sprite=3>") == "Level < 50 <b>x</b> <sprite=3>", "other tags and a lone <");
    Check(StripColorTags("+5 Mana <color=#FFF") == "+5 Mana <color=#FFF", "unterminated tag kept");
    Check(StripColorTags("").empty(), "empty");
    std::puts("tier text: 8 checks passed");
}
