#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../core/ingame_browser_rules.hpp"
#include "../core/ingame_preferences_rules.hpp"
#include "../core/ingame_hud_rules.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
using namespace ep::ingame;
using namespace ep::ingame::model;
J Parse(const char* text) { J j; assert(json::Parse(text, j)); return j; }
template<class F> void Refuses(F f) { bool refused = false; try { f(); } catch (const std::runtime_error&) { refused = true; } assert(refused); }
int main(int argc, char**) {
    if (argc > 1) {
        try {
            std::ostringstream text; text << std::cin.rdbuf(); J input; if (!json::Parse(text.str(),input)) throw std::runtime_error("Invalid test input.");
            J result;
            if(input["kind"].Str()=="damage")result=DamageModel(input["state"],input["metric"].Str("dps10"),static_cast<int>(input["element"].Num(-1)));
            else if(input["kind"].Str()=="loot")result=LootRows(input["entries"],input["show"]);
            else if (input["kind"].Str() == "scan") { Scan scan; for (const auto& page : input["pages"].items) scan.Accept(page); if (!scan.complete) throw std::runtime_error("Incomplete scan."); result = scan.result; }
            else result = input["kind"].Str() == "atlas" ? Atlas(input["catalog"],input["stash"]) : Groups(input["stash"]);
            std::cout << Pack(result); return 0;
        } catch (const std::exception& e) { J error = Object(); Put(error,"ok",Flag(false)); Put(error,"error",Text(e.what())); std::cout << Pack(error); return 0; }
    }
    auto page = Parse(R"({"ok":true,"player":{"id":"c1","name":"Test"},"scope":"offline","revision":"r1","total":2,"offset":0,"next":1,"items":[{"key":"a"}]})");
    Scan scan; scan.Accept(page); assert(!scan.complete && scan.offset == 1);
    auto second = page; Put(second,"offset",Number(1)); Put(second,"next",J{});
    Put(second,"items",List({Parse(R"({"key":"b"})")})); scan.Accept(second);
    assert(scan.complete && scan.result["items"].items.size() == 2 && scan.result["pages"].Num() == 2);
    for (const char* field : {"revision","scope"}) {
        Scan changed; changed.Accept(page); auto bad = second; Put(bad,field,Text("different"));
        Refuses([&] { changed.Accept(bad); }); assert(!changed.complete && changed.result.IsNull());
    }
    Scan changed; changed.Accept(page); auto bad = second;
    Put(bad,"player",Parse(R"({"id":"another"})")); Refuses([&] { changed.Accept(bad); });
    Scan incomplete; Put(page,"next",J{}); Refuses([&] { incomplete.Accept(page); });
    auto stash = Parse(R"({"ok":true,"player":{"id":"c1"},"scope":"offline","revision":"r1","items":[
        {"uniqueId":7,"kind":"Legendary","lp":4,"quantity":1,"weaversWill":0,"name":"Alpha"},
        {"uniqueId":7,"kind":"Unique","lp":2,"quantity":2,"weaversWill":0,"name":"Alpha"},
        {"uniqueId":7,"kind":"Unique","lp":4,"quantity":1,"weaversWill":18,"name":"Alpha"},
        {"uniqueId":null,"type":1,"subType":2,"name":"Boot","baseType":"Boots"}]})");
    const auto catalog = Parse(R"({"ok":true,"player":{"id":"c1"},"items":[{"id":7,"name":"Alpha"},{"id":8,"name":"Beta"}]})");
    const auto atlas = Atlas(catalog, stash);
    assert(atlas["owned"].Num() == 1 && atlas["items"].items[0]["count"].Num() == 4 && atlas["items"].items[0]["bestLP"].Num() == 2);
    assert(atlas["items"].items[1]["bestLP"].IsNull());
    const auto groups = Groups(stash); assert(groups["groups"].items.size() == 2 && groups["duplicateGroups"].Num() == 1);
    auto duplicate = catalog; duplicate.members.back().second.items.push_back(catalog["items"].items[0]); Refuses([&] { Atlas(duplicate, stash); });
    const auto echo = Parse(R"({"name":"Frozen Forest","reward":"","rewardType":5,"completed":false,"runnable":true})");
    assert(EchoMatch(echo,"unique","all","ready")); assert(!EchoMatch(echo,"unique","5","completed")); assert(!EchoMatch(echo,"gold","all","all"));
    assert(Fold("ВОЙД") == "войд");
    J roundtrip; assert(json::Parse(Pack(Parse(R"({"s":"quote\"\n\\","v":[null,true,1.125]})")),roundtrip));
    assert(roundtrip["s"].Str() == "quote\"\n\\");
    auto control = Parse(R"({"lifetime":"session","widget":"number","min":0,"max":100,"integer":false})");
    assert(ProfileSetting(control, Number(1.5))); assert(ProfileSetting(control,J{}));
    assert(!ProfileSetting(control,Number(101))); assert(!ProfileSetting(control,Text("1; campaign complete")));
    Put(control,"lifetime",Text("game_save")); assert(!ProfileSetting(control,Number(1)));
    Put(control,"lifetime",Text("actor")); Put(control,"widget",Text("toggle"));
    assert(ProfileSetting(control,Flag(true))); assert(!ProfileSetting(control,Number(.5)));
    for (auto command : {"lootmode filter", "lootmode quality", "lootmode materials", "lootcategory gold 0", "lootaffixes 1,7,18", "weaveraffixes none", "fontset family Noto%20Sans%7CBold", "fontset family game"}) {
        std::string key; const auto entry = ChoiceEntry(command,&key); assert(!key.empty() && ReplayCommand(entry) == command);
    }
    for (auto command : {"campaign complete", "lootmode filter extra", "lootmode lp", "lootcategory gold 2", "lootcategory evil 0", "lootaffixes 1,", "lootaffixes 1,1", "lootaffixes -1", "fontset family Foo%0ABar"}) {
        std::string key; assert(ChoiceEntry(command,&key).IsNull());
    }
    assert(ReplayCommand(Parse(R"({"id":"campaign_complete","args":{"value":1}})")).empty());
    const auto damage = DamageModel(Parse(R"({"ok":true,"active":true,"types":[{"name":"You","dps10":100},{"name":"Skeleton","live":4,"dps10":200}],"allies":{"dps10":200}})"));
    assert(damage["total"].Num()==300); // neither live count nor allies aggregate is added again
    const auto loot=Parse(R"([{"kind":"gold","gold":10},{"kind":"gold","gold":20},{"kind":"item","rarity":"Unique","name":"Relic"}])");
    const auto rows=LootRows(loot,Object());assert(rows.items.size()==2&&rows.items[1]["gold"].Num()==30&&rows.items[1]["count"].Num()==2);
    assert(LootCategory(Parse(R"({"kind":"material","type":101})"))=="shard");
    for (auto key : {"a:1","a:0","c:1:2"}) assert(DamageIconKey(key));
    for (auto key : {"","a:","a:1:2","c:1","c:1:","c::1","a:1\nquit","a:-1"}) assert(!DamageIconKey(key));
    auto visible=FeedShown(Parse(R"({"gold":false,"materials":false,"show":{"shard":true}})"));
    assert(!On(visible["gold"]) && !On(visible["rune"]) && On(visible["shard"]) && On(visible["normal"]));
    std::cout << "native browsers: complete snapshots, owner changes, LP, groups, filters and JSON passed\n";
}
