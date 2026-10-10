#include "../core/loot_crafting_rules.hpp"
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
