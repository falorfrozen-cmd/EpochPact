#include "../core/item_tiers_text.hpp"
#include <cstdio>
#include <cstdlib>
using ep::tiers::text::StripColorTags;
using namespace ep::tiers::text;
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
    // Rule words: the game's generated "Show All ..." loses its action words; other names stay.
    Check(RuleWords("Show All Unique Items") == "Unique Items", "show all");
    Check(RuleWords("hide all Normal Items") == "Normal Items", "hide all, any case");
    Check(RuleWords("Highlight All Exalted Helmets") == "Exalted Helmets", "highlight all");
    Check(RuleWords("Show Idols") == "Idols", "show");
    Check(RuleWords("Hide all  Primalist items") == "Primalist items", "spaces after the action words");
    Check(RuleWords("Showcase gear") == "Showcase gear" && RuleWords("Show") == "Show" && RuleWords("") == "", "not an action word");
    // Rule line parts.
    const std::string open = "<color=#FF0000>", tag = "<color=#9FB4C7>Loot filter:</color>";
    Check(RuleLine(kRuleAll, 3, 0, "Uniques", open, tag) == tag + " " + open + "Rule 3 · Show · Uniques</color>", "everything");
    Check(RuleLine(kRuleName, 3, 0, "Uniques", open, tag) == open + "Uniques</color>", "name only");
    Check(RuleLine(kRuleName, 3, 0, "", open, tag) == open + "Rule 3</color>", "name only without a name shows the number");
    Check(RuleLine(kRuleNumber | kRuleName, 7, 1, "Junk", open, tag) == open + "Rule 7 · Junk</color>", "number and name");
    Check(RuleLine(kRuleKind | kRuleName, 7, 2, "Rings", open, tag) == open + "Highlight · Rings</color>", "kind and name");
    Check(RuleLine(kRuleAll, 0, -1, "", open, tag) == tag + " " + open + "no rule matched (shown by default)</color>", "no rule, with label");
    Check(RuleLine(kRuleName, 0, -1, "", open, tag) == open + "No matching rule</color>", "no rule, name only");
    Check(ValidRuleParts(15) && ValidRuleParts(8) && ValidRuleParts(2) && !ValidRuleParts(1) && !ValidRuleParts(0) && !ValidRuleParts(16), "valid parts");
    // Rule names: colour and emphasis tags pass, other tags stay text.
    Check(NameTag("<color=#FF0000>") && NameTag("<COLOR=#f00>") && NameTag("<color=red>") && NameTag("<color=\"#FF000080\">") && NameTag("<#40C0FF>"), "colour tags");
    Check(NameTag("</color>") && NameTag("<b>") && NameTag("</I>") && NameTag("<u>") && NameTag("</s>"), "emphasis tags");
    Check(!NameTag("<size=200>") && !NameTag("<color=#GG0000>") && !NameTag("<color=pink>") && !NameTag("<sprite=1>") && !NameTag("<link=x>") && !NameTag("<#12345>"), "other tags");
    {
        const auto p = NameParts("Big <color=#FF0000>red</color> <size=9>x a<b");
        Check(p.size() == 5 && !p[0].tag && p[0].text == "Big " && p[1].tag && p[2].text == "red" && p[3].tag && p[4].text == " <size=9>x a<b", "pieces");
        Check(NameParts("no tags").size() == 1 && NameParts("").empty(), "pieces without tags");
    }
    Check(KeepNameTags("<color=#FF0000>Red</color> <size=50>big</size> <b>bold</b>") == "<color=#FF0000>Red</color> big <b>bold</b>", "rule line keeps colour tags only");
    // Extra filter colours: the template itself becomes the target; its shades keep their relation.
    {
        auto near = [](Rgbf a, Rgbf b) { return std::fabs(a.r - b.r) < 0.01f && std::fabs(a.g - b.g) < 0.01f && std::fabs(a.b - b.b) < 0.01f; };
        const Rgbf red = FromRgb(0xFF0000), blue = FromRgb(0x0000FF), darkRed = FromRgb(0x800000), grey = FromRgb(0x808080);
        Check(near(Recolor(red, red, blue), blue), "template becomes the target");
        Check(near(Recolor(darkRed, red, blue), FromRgb(0x000080)), "a darker shade stays darker");
        const Rgbf g = Recolor(red, red, grey); Check(near(g, grey), "a grey target");
        Check(near(FromHsv(ToHsv(FromRgb(0x40E0D0))), FromRgb(0x40E0D0)) && near(FromHsv(ToHsv(FromRgb(0xFFFFFF))), FromRgb(0xFFFFFF)), "round trip");
        bool distinct = true;
        for (size_t i = 0; i < std::size(kExtraFilterColors); ++i) for (size_t j = 0; j < i; ++j) distinct &= kExtraFilterColors[i] != kExtraFilterColors[j];
        Check(distinct && std::size(kExtraFilterColors) == 26, "26 distinct extra colours");
    }
    std::puts("tier text: 34 checks passed");
}
