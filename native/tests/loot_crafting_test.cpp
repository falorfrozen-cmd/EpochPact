#include "../core/loot_crafting_rules.hpp"
#include "../core/category_rules.hpp"
#include <cstdio>
#include <limits>
int main() {
    using namespace ep::lootcraft::rules;
    int total=0, failed=0;
    const auto check=[&](bool ok,const char* name){++total;if(!ok){++failed;std::printf("FAIL %s\n",name);}};
    Policy p; Item lowUnique{false,true,false,true,1}, highUnique{false,true,false,true,2};
    p.mode=Mode::Quality;
    check(!Accept(p,lowUnique)&&Accept(p,highUnique),"LP boundary");
    check(!Accept(p,{false,false,false,true,4}),"LP on a non-unique cannot qualify");
    check(Accept(p,{false,false,true,true,0}),"T7 union"); p.t7=false;
    check(!Accept(p,{false,false,true,true,0}),"disabled T7");
    check(!Accept(p,{false,true,true,false,4}),"filter hides quality"); p.respectFilter=false;
    check(Accept(p,{false,true,false,false,4}),"quality without filter");
    p.mode=Mode::Filter; check(!Accept(p,{false,true,true,false,4}),"filter mode always respects filter");
    check(Accept(p,{false,false,false,true,0}),"filter accepts ordinary gear");
    p.mode=Mode::Materials; check(!Accept(p,highUnique)&&Accept(p,{true,false,false,false,0}),"materials only");
    {   // Weaver's Will multiplier: times m, rounded, at least the roll, at most the game's maximum.
        check(ScaleWeaversWill(10,1,28)==10&&ScaleWeaversWill(10,2,28)==20,"Weaver's Will x1 and x2");
        check(ScaleWeaversWill(18,2,28)==28&&ScaleWeaversWill(5,10,28)==28,"Weaver's Will stops at the maximum");
        check(ScaleWeaversWill(7,1.5,28)==11&&ScaleWeaversWill(5,1.1,28)==6,"Weaver's Will rounds");
        check(ScaleWeaversWill(0,2,28)==0,"no Weaver's Will stays none");
        check(ScaleWeaversWill(30,2,28)==30,"a roll above the maximum is left alone");
        check(ExtraLegendaryRolls(1)==0&&ExtraLegendaryRolls(2)==1&&ExtraLegendaryRolls(1000)==999,"Legendary Potential rolls: extra rolls after the first");
        check(ExtraLegendaryRolls(1.9)==0&&ExtraLegendaryRolls(5000)==999&&ExtraLegendaryRolls(std::numeric_limits<double>::quiet_NaN())==0,"Legendary Potential rolls: whole rolls, at most 1000, invalid is one roll");
        check(ScaleWeaversWill(10,std::numeric_limits<double>::quiet_NaN(),28)==10&&ScaleWeaversWill(10,0.5,28)==10,"invalid multipliers change nothing");
    }
    {   // Forging Potential on drops: times m, rounded, at most the game's 63, and the contexts left alone.
        check(ScaleForgingPotential(20,2,63)==40&&ScaleForgingPotential(32,1,63)==32,"Forging Potential x2 and x1");
        check(ScaleForgingPotential(32,4,63)==63&&ScaleForgingPotential(40,10,63)==63,"Forging Potential stops at 63");
        check(ScaleForgingPotential(25,1.5,63)==38&&ScaleForgingPotential(0,10,63)==0,"Forging Potential rounds; none stays none");
        check(ScaleForgingPotential(10,std::numeric_limits<double>::quiet_NaN(),63)==10,"invalid multiplier changes nothing");
        check(BoostsForgingPotential(0)&&BoostsForgingPotential(4)&&BoostsForgingPotential(5),"drops, Nemesis and Rage of Morditas boosted");
        check(!BoostsForgingPotential(1)&&!BoostsForgingPotential(2)&&!BoostsForgingPotential(3),"shops and gamblers keep the game's own");
    }
    {   // Item categories: the first category that fits wins.
        using namespace ep::categories::rules;
        check(Classify(23,7,false)==Unique&&Classify(21,8,false)==Unique&&Classify(0,9,false)==Unique,"Unique, Set and Legendary items are Unique & Set");
        check(Classify(21,4,true)==Exalted&&Classify(25,4,true)==Exalted,"exalted items are Exalted before their type");
        check(Classify(25,2,false)==Idol&&Classify(33,2,false)==Idol&&Classify(41,0,false)==Idol,"idols and idol altars");
        check(Classify(35,0,false)==Special&&Classify(39,0,false)==Special&&Classify(104,0,false)==Special,"lenses and non-equipment items");
        check(Classify(0,4,false)==Armour&&Classify(4,0,false)==Armour,"helmet to gloves are Armour");
        check(Classify(5,2,false)==Weapon&&Classify(19,4,false)==Weapon&&Classify(23,0,false)==Weapon&&Classify(24,0,false)==Weapon,"weapons and off-hands");
        check(Classify(20,4,false)==Jewellery&&Classify(22,0,false)==Jewellery,"amulet, ring and relic");
        check(Classify(40,0,false)==Other&&Classify(-1,0,false)==Other,"anything else is Other");
        check(Index("unique")==Unique&&Index("other")==Other&&Index("x")==-1,"category keys");
        check(TabName(Idol)=="EP Idols"&&TabName(kCount).empty(),"category tab names");
    }
    {   // Unique minimum roll: 0 leaves every roll, 100 gives the best end, in between the roll keeps its place.
        bool same=true, bounded=true, ordered=true;
        for(int r=0;r<=255;++r){
            same=same&&MinimumRoll(r,0,true)==r&&MinimumRoll(r,0,false)==r;
            const int up=MinimumRoll(r,80,true), down=MinimumRoll(r,80,false);
            bounded=bounded&&up>=204&&up>=r&&down<=51&&down<=r;
            if(r)ordered=ordered&&up>=MinimumRoll(r-1,80,true)&&down>=MinimumRoll(r-1,80,false);
        }
        check(same,"minimum roll 0 changes nothing");
        check(MinimumRoll(0,100,true)==255&&MinimumRoll(17,100,true)==255&&MinimumRoll(255,100,false)==0&&MinimumRoll(200,100,false)==0,"minimum roll 100 is the best end");
        check(bounded,"minimum roll 80: the best fifth, never worse than the game's roll");
        check(ordered,"a better game roll stays better");
        check(MinimumRoll(0,50,true)==128&&MinimumRoll(255,50,true)==255&&MinimumRoll(255,50,false)==127,"minimum roll 50 ends");
        check(MinimumRoll(10,std::numeric_limits<double>::quiet_NaN(),true)==10&&MinimumRoll(10,101,true)==10&&MinimumRoll(10,-1,true)==10,"invalid percents change nothing");
        check(ValidMinimumRoll(0)&&ValidMinimumRoll(100)&&ValidMinimumRoll(37.5)&&!ValidMinimumRoll(100.5)&&!ValidMinimumRoll(-1),"minimum roll range");
    }
    {   // Weaver's Will: a Unique with Weaver's Will has LP 0, so only the Weaver's Will threshold lets it through.
        Policy w; w.mode=Mode::Quality; w.t7=false;
        const Item weaver{false,true,false,true,0,18}, lowWeaver{false,true,false,true,0,17}, plainLP0{false,true,false,true,0,0};
        check(w.minimumWW==0&&!Accept(w,weaver),"Weaver's Will threshold off by default keeps the LP rule");
        w.minimumWW=18; check(Accept(w,weaver)&&!Accept(w,lowWeaver),"Weaver's Will boundary");
        check(!Accept(w,plainLP0),"LP 0 without Weaver's Will stays out");
        check(Accept(w,highUnique),"LP threshold still accepts in the same rule");
        check(!Accept(w,{false,false,false,true,0,28}),"Weaver's Will on a non-unique cannot qualify");
        w.respectFilter=true; check(!Accept(w,{false,true,false,false,0,28}),"filter hides a Weaver's Will Unique");
        w.minimumWW=1; w.respectFilter=false; check(Accept(w,{false,true,false,true,0,1}),"any Weaver's Will at 1");
    }
    p.materials=false; check(!Accept(p,{true,false,false,true,0}),"materials disabled");
    p.mode=Mode::All; check(Accept(p,{false,false,false,false,0}),"legacy all behavior");
    for(int old=0;old<=63;++old) for(int requested=0;requested<=old;++requested) {
        check(RemainingFP(old,requested,1)==requested,"neutral loss");
        check(RemainingFP(old,requested,0)==old,"free loss");
        const int half=RemainingFP(old,requested,.5);
        check(half>=requested&&half<=old,"scaled loss bounded");
    }
    check(RemainingFP(20,19,.01)==19,"positive fraction never rounds to free");
    check(RemainingFP(20,24,0)==24,"FP gain untouched");
    check(RemainingFP(20,-1,0)==-1,"invalid request untouched");
    check(!Factor(std::numeric_limits<double>::quiet_NaN()),"nonfinite FP factor");
    check(SealChance(.2f,0,100)==1&&SealChance(.2f,3,0)==0,"eligible sealing endpoints");
    check(SealChance(.2f,4,100)==.2f&&SealChance(0,0,100)==0,"native seal exclusions remain");
    check(SealChance(.2f,0,-1)==.2f,"normal sealing");
    check(ShardRefund(21,20)==1&&ShardRefund(21,21)==0,"actual single-shard consumption");
    check(ShardRefund(21,23)==0&&ShardRefund(21,19)==0,"no speculative refund");
    std::printf("loot/crafting: %d/%d passed\n",total-failed,total); return failed?1:0;
}
