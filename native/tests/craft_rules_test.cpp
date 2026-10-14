#include "../core/craft_rules.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>
int main() {
    using namespace ep::crafting::rules;
    unsigned failures=0;
    const auto check=[&](bool ok,const char* name){if(!ok)++failures;std::printf("%s %s\n",ok?"ok":"FAIL",name);};
    const auto refused=[&](const char* input,const char* name){
        try{(void)ParseAffixes(input);check(false,name);}catch(const std::runtime_error&){check(true,name);}
    };
    check(ParseAffixes("keep").empty(),"keep adds no affixes");
    {
        const auto old=ParseAffixes("12,345");
        check(old.size()==2 && old[0].id==12 && old[0].tier==7 && old[0].roll==kItemRoll && old[1].id==345,"plain IDs keep T7 and the item-wide roll");
    }
    {
        const auto chosen=ParseAffixes("12:1:1,345:5:50,6:8:100,7:7:r");
        check(chosen.size()==4,"four chosen affixes");
        check(chosen[0].tier==1 && chosen[0].roll==1,"T1 at 1%");
        check(chosen[1].tier==5 && chosen[1].roll==50,"T5 at 50%");
        check(chosen[2].tier==8 && chosen[2].roll==100,"T8 at 100%");
        check(chosen[3].tier==7 && chosen[3].roll==kRandomRoll,"random roll");
    }
    check(ParseAffixes("1,2,3,4,5,6,7").size()==7,"seven affixes");
    refused("1,2,3,4,5,6,7,8","eight affixes are refused");
    refused("4,4","the same affix twice is refused");
    refused("4:3:10,4:5:10","the same affix with two tiers is refused");
    refused("1:8:100,2:8:r","two T8 affixes are refused");
    refused("1:0:50","tier 0 is refused");
    refused("1:9:50","tier 9 is refused");
    refused("1:7:0","roll 0% is refused");
    refused("1:7:101","roll 101% is refused");
    refused("1:7","a tier without a roll is refused");
    refused("1:7:x","a roll that is not a number or r is refused");
    refused("","an empty selection is refused");
    refused("1,,2","an empty token is refused");
    refused("65536","IDs above the game's range are refused");
    refused("-1","negative IDs are refused");
    check(RollByte(100,false,7)==255 && RollByte(50,true,7)==128 && RollByte(1,true,7)==3,"percent to roll byte");
    check(RollByte(kItemRoll,true,7)==255 && RollByte(kItemRoll,false,7)==128,"item-wide maximum and midpoint");
    check(RollByte(kRandomRoll,true,201)==201,"random roll is used as given");
    bool threw=false;try{(void)RollByte(101,true,1);}catch(const std::runtime_error&){threw=true;}
    check(threw,"invalid percent is refused");
    std::printf("%u failures\n",failures);
    return failures?1:0;
}
