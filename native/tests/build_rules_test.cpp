#include "../core/build_rules.hpp"
#include <cstdio>
int main() {
    using namespace ep::builds::rules;
    unsigned failures=0;
    const auto check=[&](bool ok,const char* name){if(!ok)++failures;std::printf("%s %s\n",ok?"ok":"FAIL",name);};
    check(PassiveBudget(1,3,1.5)==1 && PassiveBudget(2,4,1.5)==3,"fractional passive gains carry across levels");
    check(PassiveBudget(63,55,2)==116,"quest passive points are not multiplied");
    check(PassiveBudget(65535,100,10)==65535,"passive totals do not wrap native ushort storage");
    check(Budget(20+1,2)==42,"gear skill levels contribute multiplied points");
    check(Budget(0,10)==0 && Budget(21,1)==21,"neutral and zero progress budgets");
    check(Budget(13+40,1.5,10)==89,"Weaver flat added after total multiplier");
    check(ExtraProjectiles(0,2,0)==1,"single projectile becomes two");
    check(ExtraProjectiles(2,2,1)==6,"flat and multiplier scale native total");
    check(ExtraProjectiles(40,8,32)==63,"bounded projectile count");
    check(SkillXp(0,100)==0 && SkillXp(-10,10)==-10,"no XP created for empty/negative gain");
    check(SkillXp(1000000000,100)==2147483647,"XP overflow saturates");
    check(BlessingStandard(3,0,0)==3 && BlessingStandard(3,2,0)==3,"native blessing count without a request");
    check(BlessingStandard(3,0,5)==5 && BlessingStandard(3,1,5)==4 && BlessingStandard(3,2,5)==3,"five choices in total with corruption extras");
    check(BlessingStandard(3,0,4)==4 && BlessingStandard(3,2,4)==3,"four choices never remove the game's own extras");
    check(BlessingStandard(3,2,3)==3 && BlessingStandard(3,1,3)==3,"three choices keep a higher native total");
    check(BlessingStandard(3,-1,5)==3 && BlessingStandard(-1,0,5)==-1,"invalid native counts pass through");
    check(BlessingShown(3,0)==3 && BlessingShown(3,5)==5 && BlessingShown(4,5)==5 && BlessingShown(5,3)==5 && BlessingShown(-1,5)==-1,"reward panel shows the filled count");
    for(int extra=0;extra<=2;++extra)for(int requested:{0,3,4,5})
        check(BlessingStandard(3,extra,requested)+extra==BlessingShown(3+extra,requested),"filled and shown blessing counts agree");
    // GlobalTreeData.NodeData fields as the game declares them.
    check(CopyKind(0x0e)==FieldCopy::Reference && CopyKind(0x15)==FieldCopy::Reference,"name and requirements copy the reference");
    check(CopyKind(0x05)==FieldCopy::Value,"byte ids, caps and thresholds copy their data");
    check(CopyKind(0x02)==FieldCopy::Value,"recalculatedRequirementReferences is a bool, not a reference");
    check(CopyKind(0x08)==FieldCopy::Value && CopyKind(0x0d)==FieldCopy::Value,"wider primitives copy their data");
    check(CopyKind(0x12)==FieldCopy::Reference && CopyKind(0x1d)==FieldCopy::Reference && CopyKind(0x1c)==FieldCopy::Reference,"classes, arrays and objects copy the reference");
    check(CopyKind(0x11)==FieldCopy::Unsupported && CopyKind(0x0f)==FieldCopy::Unsupported && CopyKind(0x01)==FieldCopy::Unsupported,"structs, pointers and void are refused");
    return failures?1:0;
}
