#include "build_controls.hpp"
#include "build_rules.hpp"
#include "managed.hpp"
#include "hook.hpp"
#include "progression.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <sstream>
#include <map>

namespace ep::builds {
namespace {
using namespace managed;
using M = const il2cpp::Method*;
struct Setting { const char* key; double minimum, maximum, initial; bool integer; std::atomic<double> value;
    Setting(const char* k,double lo,double hi,double d,bool i=false):key(k),minimum(lo),maximum(hi),initial(d),integer(i),value(d){} };
Setting skillXp{"skillxp",1,100,1}, passive{"passivepoints",1,10,1}, skills{"skillpoints",1,10,1},
    weaver{"weaverpoints",1,10,1}, weaverFlat{"weaverflat",0,500,0,true}, zoom{"zoom",1,2,1}, angle{"cameraangle",0,85,0},
    projectile{"projectiles",1,8,1}, projectileFlat{"projectileflat",0,32,0,true},
    blessings{"blessingoptions",0,5,0,true}, perfectBlessings{"blessingrolls",0,1,0,true},
    summonDuration{"summonduration",1,10,1}, offhand{"offhand",0,1,0,true}, fullSets{"fullsets",0,1,0,true},
    freeSkills{"skillnodes",0,1,0,true}, freePassives{"passivenodes",0,1,0,true}, freeWeaver{"weavernodes",0,1,0,true};
std::array<Setting*,17> settings{&skillXp,&passive,&skills,&weaver,&weaverFlat,&zoom,&angle,&projectile,&projectileFlat,&blessings,&perfectBlessings,&summonDuration,&offhand,&fullSets,&freeSkills,&freePassives,&freeWeaver};
game::MethodRef getActor, offline, xpRef, skillRef, passiveRef, weaverRef, zoomRef, projectileRef, blessingRef;
game::MethodRef summonRef, slotRef, alternateSlotRef, linkedRef, invalidRef, enforceRef, mainRestrictionRef, offRestrictionRef, equipRef;
game::MethodRef constructionRef, drainRef, summonedTickRef;
game::MethodRef skillUnspentRef, passiveUnspentRef, weaverUnspentRef;
game::MethodRef requirementRef, requirementPreviewRef, passiveRequirementRef, uiRequirementRef, reachableRef,
    globalNodeRef, passiveSpendRef, passiveBulkRef, masteryRef, masteryUpdateRef, respecCostRef;
size_t actorTree=0, actorTracker=0, trackerData=0, treeActor=0, treeSkills=0, treePassive=0, treeWeaver=0,
    treeLevel=0, passiveEarned=0, weaverEarned=0, skillLevel=0, skillAdditional=0,
    zoomMin=0, zoomDefault=0, cameraAngle=0, managerActor=0, implicitRolls=0;
size_t durationOffset=0;
size_t F(const char* cls,const char* field) {
    static std::map<std::string,size_t> cache;
    const std::string key=std::string(cls)+":"+field;
    const auto found=cache.find(key);if(found!=cache.end())return found->second;
    return cache.emplace(key,Offset(cls,field)).first->second;
}
std::atomic<uint64_t> xpCalls{0}, projectileCalls{0}, blessingCalls{0}, faults{0};
std::atomic<int> lastProjectileNative{0},lastProjectileTotal{0};
using XPFn=void(*)(void*,int,bool,M); XPFn originalXp=nullptr;
using IntFn=int(*)(void*,M); IntFn originalSkill=nullptr, originalWeaver=nullptr;
using UShortFn=uint16_t(*)(void*,M);UShortFn originalSkillUnspent=nullptr, originalPassiveUnspent=nullptr, originalWeaverUnspent=nullptr;
using VoidFn=void(*)(void*,M); VoidFn originalPassive=nullptr, originalZoom=nullptr;
struct Vec3 {float x,y,z;};
using ProjectileFn=int(*)(void*,const Vec3*,const Vec3*,M); ProjectileFn originalProjectile=nullptr;
using BlessingFn=void*(*)(void*,uint8_t,int,int,int,bool,M); BlessingFn originalBlessing=nullptr;
using TickFn=void(*)(void*,float,M);TickFn originalSummon=nullptr;
TickFn originalDrain=nullptr, originalSummonedTick=nullptr;
using ConstructionFn=void*(*)(void*,void*,const Vec3*,const Vec3*,int,void*,bool,bool,void*,bool,void*,float,uint16_t,bool,const Vec3*,M);
ConstructionFn originalConstruction=nullptr;
std::atomic<uint64_t> retrofits{0},drainCalls{0};
thread_local bool retrofitting=false;
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
game::Handle testConstructed;
std::atomic<uint64_t> testConstructions{0},testExtraConstructions{0};
std::vector<std::string> testCreationKinds;
#endif
using SlotFn=bool(*)(void*,int,M);SlotFn originalSlot=nullptr,originalAlternateSlot=nullptr;
using ItemFn=bool(*)(void*,void*,M);ItemFn originalLinked=nullptr, originalMainRestriction=nullptr, originalOffRestriction=nullptr;
using EnforceFn=void(*)(void*,bool,uint64_t,M);EnforceFn originalEnforce=nullptr;
VoidFn originalInvalid=nullptr;
using EquipFn=void(*)(void*,void*,bool,bool,float,float,float,float,M);EquipFn originalEquip=nullptr;
game::Handle setStatsOwner;
std::vector<std::unique_ptr<Root>> setEntries;
std::map<std::string,std::unique_ptr<Root>> componentTypes;
thread_local bool reconcilingSets=false;
using RequirementFn=bool(*)(void*,void*,void*,void*,void*,M);RequirementFn originalRequirement=nullptr;
using RequirementPreviewFn=bool(*)(void*,void*,void*,void*,void*,void*,M);RequirementPreviewFn originalRequirementPreview=nullptr;
using PassiveRequirementFn=bool(*)(void*,void*,void*,void*,M);PassiveRequirementFn originalPassiveRequirement=nullptr;
using UIRequirementFn=bool(*)(void*,void*,void*,M);UIRequirementFn originalUIRequirement=nullptr;
using ObjectFn=void*(*)(void*,M);ObjectFn originalReachable=nullptr;
using NodeFn=void*(*)(void*,uint8_t,M);NodeFn originalGlobalNode=nullptr;
using PassiveSpendFn=bool(*)(void*,uint8_t,uint8_t,M);PassiveSpendFn originalPassiveSpend=nullptr;
using PassiveBulkFn=void(*)(void*,uint8_t,void*,M);PassiveBulkFn originalPassiveBulk=nullptr;
using MasteryFn=void*(*)(void*,void*,void*,M);MasteryFn originalMastery=nullptr;
VoidFn originalMasteryUpdate=nullptr;
using RespecCostFn=int(*)(void*,void*,void*,M);RespecCostFn originalRespecCost=nullptr;
struct PassiveValidation {void* global=nullptr;bool validating=true;std::vector<std::unique_ptr<Root>> nodes;};
thread_local PassiveValidation* passiveValidation=nullptr;
struct Hook {game::MethodRef* ref; void* detour; void** original;};

void* Actor() {
    if(!getActor || !offline || !game::IsOfflinePlay()) return nullptr;
    void* actor=reinterpret_cast<void*(*)(M)>(getActor.code)(getActor.info);
    if(!actor || !game::IsAlive(actor)) return nullptr;
    void* tracker=Get<void*>(actor,actorTracker);
    void* character=tracker?Get<void*>(tracker,trackerData):nullptr;
    return character && reinterpret_cast<bool(*)(void*,M)>(offline.code)(character,offline.info)?actor:nullptr;
}
void* Tree(void* actor) {return actor?Get<void*>(actor,actorTree):nullptr;}
bool OwnedTree(void* self) {return self && Tree(Actor())==self;}
bool OwnedSkill(void* self) {
    if(!self) return false;
    void* tree=Tree(Actor());
    if(!tree) return false;
    for(void* entry:Entries(Get<void*>(tree,treeSkills))) if(entry==self) return true;
    return false;
}
bool Checked(const std::function<void()>& work) {
    std::string why;
    if(game::Guarded(work,&why)) return true;
    ++faults; Log("build controls: %s",why.c_str()); return false;
}
bool FreeTree(void* local,void* data) {
    if(!OwnedTree(local)||!data)return false;
    if(data==Get<void*>(local,treePassive))return freePassives.value==1;
    if(data==Get<void*>(local,treeWeaver))return freeWeaver.value==1;
    if(freeSkills.value==1)for(void* skill:Entries(Get<void*>(local,treeSkills)))if(data==skill)return true;
    return false;
}
bool FreeUITree(void* tree,void* local) {
    if(!OwnedTree(local)||!tree)return false;
    const auto id=Text(Get<void*>(tree,Offset("Tree","treeID")));
    for(size_t offset:{treePassive,treeWeaver}) {void* data=Get<void*>(local,offset);if(data && Text(Get<void*>(data,Offset("LocalTreeData.TreeData","treeID")))==id)return FreeTree(local,data);}
    if(freeSkills.value==1)for(void* skill:Entries(Get<void*>(local,treeSkills)))if(skill && Text(Get<void*>(skill,Offset("LocalTreeData.TreeData","treeID")))==id)return true;
    return false;
}
bool RequirementDetour(void* self,void* global,void* node,void* data,void* localNode,M method) {bool bypass=false;Checked([&]{bypass=FreeTree(self,data);});return bypass?true:originalRequirement(self,global,node,data,localNode,method);}
bool RequirementPreviewDetour(void* self,void* global,void* node,void* data,void* localNode,void* preview,M method) {bool bypass=false;Checked([&]{bypass=FreeTree(self,data);});return bypass?true:originalRequirementPreview(self,global,node,data,localNode,preview,method);}
bool PassiveRequirementDetour(void* self,void* node,void* global,void* allocations,M method) {bool bypass=false;if(freePassives.value==1)Checked([&]{bypass=OwnedTree(self);});return bypass?true:originalPassiveRequirement(self,node,global,allocations,method);}
bool UIRequirementDetour(void* node,void* tree,void* local,M method) {bool bypass=false;Checked([&]{bypass=FreeUITree(tree,local);});return bypass?true:originalUIRequirement(node,tree,local,method);}
void* ReachableDetour(void* tree,M method) {
    void* result=originalReachable(tree,method);bool bypass=false;Checked([&]{bypass=FreeUITree(tree,Tree(Actor()));});
    if(result && bypass)Checked([&]{Root root(result);for(void* node:Entries(Get<void*>(tree,Offset("Tree","nodeList")))){
        uint8_t id=Get<uint8_t>(node,Offset("SkillTreeNode","id"));void* args[]{&id};Invoke(Method(result,"Add",1),result,args);
    }});
    return result;
}
// The authoritative passive validator contains direct mastery threshold reads.
// Return a short-lived view with those two thresholds removed, rather than
// altering the global game assets. Caps, node IDs and budget checks stay native.
void* GlobalNodeDetour(void* self,uint8_t id,M method) {
    void* native=originalGlobalNode(self,id,method);
    if(!native || !passiveValidation || !passiveValidation->validating || self!=passiveValidation->global)return native;
    void* result=native;
    Checked([&]{
        const auto* cls=game::FindClass("LE.dll","","GlobalTreeData.NodeData");
        auto root=std::make_unique<Root>(il2cpp::api().object_new(cls));
        for(const char* name:{"name","id","maxPoints","requiredMastery","masteryRequirement","requirements","recalculatedRequirementReferences"}) {
            const auto* f=il2cpp::api().class_get_field_from_name(cls,name);if(!f)throw std::runtime_error("passive validation field unavailable");
            const auto offset=il2cpp::api().field_get_offset(f);
            uint8_t zero=0;
            void* source=(!std::strcmp(name,"requiredMastery")||!std::strcmp(name,"masteryRequirement"))?static_cast<void*>(&zero):static_cast<void*>(static_cast<char*>(native)+offset);
            // il2cpp_field_set_value takes the reference itself for managed
            // reference fields; only value types take an address to their data.
            if(!std::strcmp(name,"name")||!std::strcmp(name,"requirements")||!std::strcmp(name,"recalculatedRequirementReferences"))source=Get<void*>(native,offset);
            il2cpp::api().field_set_value(root->Get(),f,source);
        }
        result=root->Get();passiveValidation->nodes.push_back(std::move(root));
    });
    return result;
}
struct PassiveScope {
    PassiveValidation state;PassiveValidation* previous=nullptr;
    explicit PassiveScope(void* self) {
        previous=passiveValidation;
        if(freePassives.value!=1)return;
        Checked([&]{
            if(!OwnedTree(self))return;
            void* data=Get<void*>(self,treePassive);if(!data)return;
            void* id=Get<void*>(data,F("LocalTreeData.TreeData","treeID"));void* args[]{id};
            Root global(Invoke(game::FindMethod("LE.dll","LE.AssetManagement","GlobalAssets","get_GlobalTreeData",0)));
            state.global=Invoke(Method(global.Get(),"getTreeData",1),global.Get(),args);passiveValidation=&state;
        });
    }
    ~PassiveScope(){passiveValidation=previous;}
};
bool PassiveSpendDetour(void* self,uint8_t cls,uint8_t id,M method) {PassiveScope scope(self);return originalPassiveSpend(self,cls,id,method);}
void PassiveBulkDetour(void* self,uint8_t cls,void* allocations,M method) {PassiveScope scope(self);originalPassiveBulk(self,cls,allocations,method);}
void* MasteryDetour(void* self,void* allocations,void* global,M method) {
    // Calculate the actual mastery totals from original definitions. A bypass must
    // not attribute Spellblade/Sorcerer/Runemaster nodes to the base class.
    const bool before=passiveValidation && passiveValidation->validating;
    if(passiveValidation)passiveValidation->validating=false;
    struct Restore{bool before;~Restore(){if(passiveValidation)passiveValidation->validating=before;}} restore{before};
    return originalMastery(self,allocations,global,method);
}
void MasteryUpdateDetour(void* self,M method) {if(passiveValidation)passiveValidation->validating=false;originalMasteryUpdate(self,method);}
int RespecCostDetour(void* self,void* allocations,void* global,M method) {if(passiveValidation)passiveValidation->validating=false;return originalRespecCost(self,allocations,global,method);}
void XpDetour(void* self,int amount,bool force,M method) {
    int adjusted=amount;
    if(skillXp.value!=1 && amount>0) Checked([&]{if(OwnedTree(self)){adjusted=rules::SkillXp(amount,skillXp.value);++xpCalls;}});
    originalXp(self,adjusted,force,method);
}
int SkillDetour(void* self,M method) {
    const int natural=originalSkill(self,method); int out=natural;
    if(skills.value!=1) Checked([&]{if(OwnedSkill(self))out=rules::Budget(natural,skills.value);});
    return out;
}
uint16_t SkillUnspentDetour(void* self,M method) {
    uint16_t result=originalSkillUnspent(self,method);
    Checked([&]{if(OwnedSkill(self)){
        const int budget=rules::Budget(originalSkill(self,skillRef.info),skills.value);
        const int allocated=Value<int>(Method(self,"numberOfPointsAllocated",0),self);
        result=static_cast<uint16_t>(std::max(0,budget-allocated));
    }});
    return result;
}
uint16_t TreeUnspent(void* self,UShortFn original,M method,size_t field,size_t earned) {
    uint16_t result=original(self,method);
    Checked([&]{void* tree=Tree(Actor());if(tree && self==Get<void*>(tree,field)) {
        const int allocated=Value<int>(Method(self,"numberOfPointsAllocated",0),self);
        result=static_cast<uint16_t>(std::max(0,int(Get<uint16_t>(self,earned))-allocated));
    }});return result;
}
uint16_t PassiveUnspentDetour(void* self,M method){return TreeUnspent(self,originalPassiveUnspent,method,treePassive,passiveEarned);}
uint16_t WeaverUnspentDetour(void* self,M method){return TreeUnspent(self,originalWeaverUnspent,method,treeWeaver,weaverEarned);}
void ApplyPassiveBudget(void* tree) {
    void* data=Get<void*>(tree,treePassive); if(!data)return;
    const auto calc=game::FindMethod("LE.dll","","LocalTreeData","calculatePassivePointsEarnt",0);
    const int natural=Value<uint8_t>(calc,tree);
    const int level=Get<uint8_t>(tree,treeLevel);
    *reinterpret_cast<uint16_t*>(static_cast<char*>(data)+passiveEarned)=static_cast<uint16_t>(rules::PassiveBudget(natural,level,passive.value));
}
void PassiveDetour(void* self,M method) {
    originalPassive(self,method);
    if(passive.value!=1) Checked([&]{if(OwnedTree(self))ApplyPassiveBudget(self);});
}
void* Weaver(void* actor) {
    void* provider=Invoke(Method(actor,"get_FactionInfo",0),actor);
    uint8_t id=3; void* result=nullptr; void* args[]{&id,&result};
    return provider && Value<bool>(Method(provider,"TryGetFaction",2),provider,args)?result:nullptr;
}
int WeaverDetour(void* self,M method) {
    const int natural=originalWeaver(self,method); int out=natural;
    if(weaver.value!=1 || weaverFlat.value!=0) Checked([&]{if(self && self==Weaver(Actor()))out=rules::Budget(natural,weaver.value,static_cast<int>(weaverFlat.value));});
    return out;
}
void ZoomDetour(void* self,M method) {
    const double factor=zoom.value; bool owned=false;
    if(factor!=1 || angle.value!=0) Checked([&]{owned=Actor()!=nullptr;});
    if(!owned){originalZoom(self,method);return;}
    // Borrow the native distance limits for this call. Do not compound the factor
    // every frame or alter the player's saved camera settings / FOV.
    float* minimum=reinterpret_cast<float*>(static_cast<char*>(self)+zoomMin);
    float* normal=reinterpret_cast<float*>(static_cast<char*>(self)+zoomDefault);
    const float oldMin=*minimum, oldDefault=*normal;
    struct Restore {float* a;float* b;float x,y;~Restore(){*a=x;*b=y;}} restore{minimum,normal,oldMin,oldDefault};
    float* pitch=reinterpret_cast<float*>(static_cast<char*>(self)+cameraAngle);
    const float previousAngle=*pitch;
    struct RestoreAngle {float* field;float before;~RestoreAngle(){*field=before;}} restoreAngle{pitch,previousAngle};
    if(angle.value!=0)*pitch=static_cast<float>(angle.value.load());
    *minimum=oldMin*static_cast<float>(factor); *normal=oldDefault*static_cast<float>(factor);
    originalZoom(self,method);
}
// ExtraProjectiles obtains the CreationReferences component in MakeExtraProjectiles.
// Read that component through the non-generic normal GetComponent(Type) method;
// never infer ownership from a nearby actor or from a global offline flag alone.
void* Component(void* self,const char* name) {
    static const il2cpp::Method* getter=nullptr;
    // Resolve the Type overload explicitly: GetComponent(string) has the same arity.
    void* it=nullptr; const auto* component=game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","Component");
    while(!getter) {const auto* m=il2cpp::api().class_get_methods(component,&it);if(!m)throw std::runtime_error("GetComponent(Type) unavailable");
        if(std::strcmp(il2cpp::api().method_get_name(m),"GetComponent") || il2cpp::api().method_get_param_count(m)!=1)continue;
        char* n=il2cpp::api().type_get_name(il2cpp::api().method_get_param(m,0));
        bool match=n && !std::strcmp(n,"System.Type"); if(n)il2cpp::api().free(n);
        if(match)getter=m;
    }
    auto found=componentTypes.find(name);
    if(found==componentTypes.end()) {
        const auto* cls=game::FindClass("LE.dll","",name);if(!cls)return nullptr;
        found=componentTypes.emplace(name,std::make_unique<Root>(il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls)))).first;
    }
    void* args[]{found->second->Get()};return Invoke(getter,self,args);
}
bool OwnedSummon(void* self);
void SummonDetour(void* self,float delta,M method) {
    // Only the expiry timer is slowed. AI, movement, attack cadence, mana upkeep,
    // unsummon-on-owner-death and the rest of the native tick stay unchanged.
    if(!self || !durationOffset){originalSummon(self,delta,method);return;}
    float* duration=reinterpret_cast<float*>(static_cast<char*>(self)+durationOffset);
    const float natural=*duration;bool owned=false;
    if(summonDuration.value!=1 && natural>0) Checked([&]{
        owned=OwnedSummon(self);
    });
    struct Restore{float* field;float before;~Restore(){*field=before;}} restore{duration,natural};
    if(owned)*duration=natural*static_cast<float>(summonDuration.value);
    originalSummon(self,delta,method);
}
bool OwnedSummon(void* self) {
    void* local=Actor();if(!local||!self)return false;
    void* summoned=Component(self,"Summoned");if(!summoned)return false;
    void* refs=Get<void*>(summoned,F("Summoned","references"));
    // Follow sub-minion ownership without accepting enemy summons or stale actors.
    for(unsigned depth=0;refs && depth<8;++depth){
        void* creator=Get<void*>(refs,F("CreationReferences","creator"));
        if(creator==local)return true;
        if(!creator||!game::IsAlive(creator))return false;
        void* parent=Component(creator,"Summoned");if(!parent)return false;
        refs=Get<void*>(parent,F("Summoned","references"));
    }return false;
}
void DrainDetour(void* self,float delta,M method) {
    bool owned=false;
    if(summonDuration.value!=1)Checked([&]{
        // This component's only tick is health decay and its acceleration.
        // Do not alter player drain, owner damage, combat hits or AI cadence.
        owned=OwnedSummon(self) && !Get<bool>(self,F("AcceleratingHealthDrain","drainCreator"));
    });
    if(owned){delta/=static_cast<float>(summonDuration.value);++drainCalls;}
    originalDrain(self,delta,method);
}
void SummonedTickDetour(void* self,float delta,M method) {
    // Age drives native temporary-minion bookkeeping. Preserve the full native
    // tick (stance updates etc.) and slow only this field, never the AI clock.
    bool owned=false;if(summonDuration.value!=1)Checked([&]{owned=OwnedSummon(self)&&Value<bool>(Method(self,"IsTemporary",0),self);});
    if(owned){auto* age=reinterpret_cast<float*>(static_cast<char*>(self)+F("Summoned","age"));
        *age-=delta*(1-1/static_cast<float>(summonDuration.value));}
    originalSummonedTick(self,delta,method);
}
bool LocalSlot(void* self) {
    if(!self)return false;
    const char* cls=il2cpp::api().class_get_name(il2cpp::api().object_get_class(self));
    if(std::strcmp(cls,"OffhandItemContainer") && std::strcmp(cls,"WeaponItemContainer"))return false;
    void* manager=Get<void*>(self,Offset("OneSlotItemContainer","containersManager"));
    void* actor=Actor();return actor && manager && Get<void*>(manager,managerActor)==actor;
}
bool SlotDetour(void* self,int type,M method) {
    bool allow=false;
    if(offhand.value==1)Checked([&]{
        if(!LocalSlot(self) || std::strcmp(il2cpp::api().class_get_name(il2cpp::api().object_get_class(self)),"OffhandItemContainer"))return;
        Root list(Invoke(game::FindMethod("LE.dll","","ItemList","get",0)));
        void* args[]{&type};allow=Value<bool>(Method(list.Get(),"isWeapon",1),list.Get(),args)||Value<bool>(Method(list.Get(),"isOffhand",1),list.Get(),args);
    });
    return allow || originalSlot(self,type,method);
}
bool AlternateSlotDetour(void* self,int type,M method) {
    bool allow=false;
    if(offhand.value==1)Checked([&]{if(LocalSlot(self)&&!std::strcmp(il2cpp::api().class_get_name(il2cpp::api().object_get_class(self)),"OffhandItemContainer")){void* args[]{&type};allow=Value<bool>(game::FindMethod("LE.dll","","ItemList","isWeapon",1),nullptr,args)||Value<bool>(game::FindMethod("LE.dll","","ItemList","isOffhand",1),nullptr,args);}});
    return allow||originalAlternateSlot(self,type,method);
}
bool LinkedDetour(void* self,void* item,M method) {bool skip=false;if(offhand.value==1)Checked([&]{skip=LocalSlot(self);});return skip?false:originalLinked(self,item,method);}
bool MainRestrictionDetour(void* self,void* item,M method) {bool skip=false;if(offhand.value==1)Checked([&]{skip=Get<void*>(self,managerActor)==Actor();});return skip?false:originalMainRestriction(self,item,method);}
bool OffRestrictionDetour(void* self,void* item,M method) {bool skip=false;if(offhand.value==1)Checked([&]{skip=Get<void*>(self,managerActor)==Actor();});return skip?false:originalOffRestriction(self,item,method);}
void InvalidDetour(void* self,M method) {bool skip=false;if(offhand.value==1)Checked([&]{skip=Get<void*>(self,managerActor)==Actor();});if(!skip)originalInvalid(self,method);}
void EnforceDetour(void* self,bool autoMove,uint64_t position,M method) {bool skip=false;if(offhand.value==1)Checked([&]{skip=LocalSlot(self);});if(!skip)originalEnforce(self,autoMove,position,method);}
void Recalculate(void* stats) {
    *reinterpret_cast<uint8_t*>(static_cast<char*>(stats)+Offset("BaseStats","statsNeedToBeUpdatedNextFrame"))=1;
    auto m=game::FindMethod("LE.dll","","BaseStats","UpdateStatsInternal",0);
    Invoke(il2cpp::api().object_get_virtual_method(stats,m.info),stats);
}
void ClearSetEntries(void* stats) {
    if(stats && setStatsOwner.Get()==stats) {
        void* list=Get<void*>(stats,Offset("Stats","stats"));
        for(auto& root:setEntries){void* args[]{root->Get()};Invoke(Method(list,"Remove",1),list,args);}
    }
    setEntries.clear();
}
void ReconcileSets(void* manager) {
    if(reconcilingSets)return;
    void* actor=Actor();if(!actor || Get<void*>(manager,Offset("ItemEquipManager","actor"))!=actor)return;
    struct Scope{Scope(){reconcilingSets=true;}~Scope(){reconcilingSets=false;}} scope;
    void* stats=Get<void*>(manager,Offset("ItemEquipManager","stats"));
    const bool changed=!setEntries.empty();
    std::vector<std::unique_ptr<Root>> prepared;
    if(fullSets.value==1) {
        void* present=Get<void*>(manager,Offset("ItemEquipManager","setsPresent"));
        // List<byte> is a value-type list; Entries() is only for object lists.
        const auto* cls=il2cpp::api().object_get_class(present);
        const int count=Get<int>(present,il2cpp::api().field_get_offset(il2cpp::api().class_get_field_from_name(cls,"_size")));
        if(count<0||count>255)throw std::runtime_error("invalid equipped set list");
        void* completion=Get<void*>(manager,Offset("ItemEquipManager","setCompletion"));
        for(int i=0;i<count;++i) {
            void* indexArgs[]{&i};uint8_t id=Value<uint8_t>(Method(present,"get_Item",1),present,indexArgs);
            // The native UpdateStats method indexes setCompletion by the
            // position in setsPresent, not by the (sparse) set ID.
            const int ownedCount=Value<int>(Method(completion,"count",1),completion,indexArgs);
            if(ownedCount<=0)continue;
            void* args[]{&id};Root entry(Invoke(game::FindMethod("LE.dll","","SetBonusesList","getEntry",1),nullptr,args));
            for(void* bonus:Entries(Get<void*>(entry.Get(),Offset("SetBonusesList.Entry","mods")))) {
                if(Get<int>(bonus,Offset("SetBonus","setRequirement"))<=ownedCount)continue; // already applied by the game
                uint8_t property=Get<uint8_t>(bonus,Offset("SetBonus","property")),special=Get<uint8_t>(bonus,Offset("SetBonus","specialTag"));
                int tags=Get<int>(bonus,Offset("SetBonus","tags")),extra=Get<int>(bonus,Offset("SetBonus","extraTag")),mode=Get<int>(bonus,Offset("SetBonus","type"));
                const float value=Get<float>(bonus,Offset("SetBonus","value"));float added=mode==0?value:0,increased=mode==1?value:0,more=mode==2?value:0;
                if(mode<0||mode>2)throw std::runtime_error("unsupported set bonus modifier mode");
                auto stat=std::make_unique<Root>(il2cpp::api().object_new(game::FindClass("LE.dll","","Stats.Stat")));
                void* ctorArgs[]{&property,&tags,&added,&increased,&more,&special,&extra};
                Invoke(Method(stat->Get(),".ctor",7),stat->Get(),ctorArgs);
                *reinterpret_cast<bool*>(static_cast<char*>(stat->Get())+Offset("Stats.Stat","dontCollapse"))=true;
                prepared.push_back(std::move(stat));
            }
        }
    }
    // Construct every replacement before touching the live list. A metadata
    // failure cannot leave half a set bonus applied or discard the previous one.
    void* list=Get<void*>(stats,Offset("Stats","stats"));
    std::vector<std::unique_ptr<Root>> previous;
    const bool sameOwner=setStatsOwner.Get()==stats;
    if(sameOwner){previous=std::move(setEntries);for(auto& root:previous){void* args[]{root->Get()};Invoke(Method(list,"Remove",1),list,args);}}
    else setEntries.clear();
    try {
        for(auto& root:prepared){void* args[]{root->Get()};Invoke(Method(list,"Add",1),list,args);}
        if(changed || !prepared.empty())Recalculate(stats);
    } catch(...) {
        for(auto& root:prepared){void* args[]{root->Get()};Invoke(Method(list,"Remove",1),list,args);}
        for(auto& root:previous){void* args[]{root->Get()};Invoke(Method(list,"Add",1),list,args);}
        setEntries=std::move(previous);Checked([&]{Recalculate(stats);});throw;
    }
    setEntries=std::move(prepared);setStatsOwner.Set(stats);
}
void EquipDetour(void* self,void* item,bool adding,bool unequipping,float global,float prefix,float suffix,float idol,M method) {
    originalEquip(self,item,adding,unequipping,global,prefix,suffix,idol,method);
    if(fullSets.value==1 || !setEntries.empty())Checked([&]{ReconcileSets(self);});
}
bool LocalCreation(void* component) {
    void* references=Component(component,"CreationReferences");
    void* actor=Actor();return actor && references && Get<void*>(references,Offset("CreationReferences","creator"))==actor;
}
int ProjectileDetour(void* self,const Vec3* start,const Vec3* end,M method) {
    const int native=originalProjectile(self,start,end,method);
    // A retrofitted component is attached to ONE already-created projectile.
    // Upstream mutators already emitted their native volley; do not count those
    // bonuses a second time when cloning this individual emission.
    const int natural=retrofitting?0:native; int out=natural;
    if(projectile.value!=1 || projectileFlat.value!=0) Checked([&]{if(LocalCreation(self)){void* info=Get<void*>(self,F("ExtraProjectiles","AbilityInfo"));
        (void)info;
        out=rules::ExtraProjectiles(natural,projectile.value,static_cast<int>(projectileFlat.value));lastProjectileNative=natural+1;lastProjectileTotal=out+1;++projectileCalls;}});
    return out;
}
void* ConstructionDetour(void* self,void* ability,const Vec3* start,const Vec3* target,int use,void* prefab,bool inherit,bool extra,void* mutator,bool onDeath,void* info,float delay,uint16_t abilityId,bool alignment,const Vec3* castPoint,M method) {
    void* result=originalConstruction(self,ability,start,target,use,prefab,inherit,extra,mutator,onDeath,info,delay,abilityId,alignment,castPoint,method);
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
    if(result && !extra && !retrofitting)testConstructed.Set(result);
    if(result){++testConstructions;if(extra)++testExtraConstructions;}
    if(result && !extra && !retrofitting)Checked([&]{
        void* t=Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","GameObject","get_transform",0),result),*m=Component(t,"AbilityMover");
        std::string row="{\"name\":"+Json(Text(Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Object","get_name",0),result)))+",\"mover\":"+Boolean(m!=nullptr)+",\"speed\":"+std::to_string(m?Get<float>(m,F("AbilityMover","speed")):0)+",\"parabolic\":"+Boolean(Component(t,"AbilityParabolicMovement")!=nullptr)+",\"curve\":"+Boolean(Component(t,"AbilityCurveMovement")!=nullptr)+",\"components\":[";
        const auto* cls=game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","Component");Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls)));bool inactive=true;void* args[]{type.Get(),&inactive};
        Root components(Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","GameObject","GetComponentsInChildren",2),result,args));const auto count=Get<uintptr_t>(components.Get(),0x18);if(count>256)throw std::runtime_error("oversize test component list");
        for(uintptr_t i=0;i<count;++i){void* c=Get<void*>(components.Get(),0x20+sizeof(void*)*i);if(!c)continue;if(row.back()!='[')row+=',';row+=Json(il2cpp::api().class_get_name(il2cpp::api().object_get_class(c)));}row+="]}";
        if(testCreationKinds.size()>=32)testCreationKinds.erase(testCreationKinds.begin());testCreationKinds.push_back(row);
    });
#endif
    if(!result || extra || retrofitting || (projectile.value==1 && projectileFlat.value==0))return result;
    Checked([&]{
        void* local=Actor();if(!local||Get<void*>(self,F("AbilityObjectConstructor","actor"))!=local||!game::IsAlive(result))return;
        // Operate on genuine moving ability objects; never duplicate actors,
        // summons, stationary areas, beams, loot, portals or scene decorations.
        // Resolve Type overload through Component(), whose overload selection
        // is explicit. A transform is always present on a live GameObject.
        void* transform=Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","GameObject","get_transform",0),result);
        if(Component(transform,"Actor")||Component(transform,"Summoned")||Component(transform,"ExtraProjectiles"))return;
        void* mover=Component(transform,"AbilityMover");
        void* parabolic=Component(transform,"AbilityParabolicMovement");
        void* curve=Component(transform,"AbilityCurveMovement");
        if(!mover && !parabolic && !curve)return;
        if(!parabolic && !curve && mover && !(Get<float>(mover,F("AbilityMover","speed"))>0))return;
        void* refs=Component(transform,"CreationReferences");
        if(!refs || Get<void*>(refs,F("CreationReferences","creator"))!=local)return;
        const auto* cls=game::FindClass("LE.dll","","ExtraProjectiles");
        void* type=il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));void* add[]{type};
        Root component(Invoke(Method(result,"AddComponent",1),result,add));
        // New component uses the game's creation and clone machinery. Only the
        // main projectile gets this component; isExtraProjectile/TLS stop recursion.
        *reinterpret_cast<float*>(static_cast<char*>(component.Get())+F("ExtraProjectiles","angle"))=10;
        struct Scope{Scope(){retrofitting=true;}~Scope(){retrofitting=false;}} scope;
        void* args[]{refs};Invoke(Method(component.Get(),"onCreation",1),component.Get(),args);++retrofits;
    });return result;
}
void MaxBlessings(void* options) {
    // Only freshly generated reward options. Existing equipped blessings / stash
    // entries are never changed by enabling the setting or reading live data.
    for(void* item:Entries(options)) {
        if(!item)continue;
        void* rolls=Get<void*>(item,implicitRolls); if(!rolls)continue;
        const uintptr_t n=Get<uintptr_t>(rolls,0x18); if(n>32)throw std::runtime_error("invalid blessing roll array");
        std::memset(static_cast<char*>(rolls)+0x20,255,n);
        Invoke(Method(item,"RebuildID",0),item);
        ++blessingCalls;
    }
}
void* BlessingDetour(void* self,uint8_t timeline,int difficulty,int count,int extra,bool lucky,M method) {
    bool owned=false; Checked([&]{owned=Actor()!=nullptr;});
    const int requested=static_cast<int>(blessings.value);
    void* result=originalBlessing(self,timeline,difficulty,owned && requested?requested:count,owned && requested?0:extra,lucky,method);
    if(result && owned && perfectBlessings.value==1) Checked([&]{Root root(result);MaxBlessings(root.Get());});
    return result;
}
Hook xpHook{&xpRef,reinterpret_cast<void*>(&XpDetour),reinterpret_cast<void**>(&originalXp)},
    skillHook{&skillRef,reinterpret_cast<void*>(&SkillDetour),reinterpret_cast<void**>(&originalSkill)},
    skillUnspentHook{&skillUnspentRef,reinterpret_cast<void*>(&SkillUnspentDetour),reinterpret_cast<void**>(&originalSkillUnspent)},
    passiveHook{&passiveRef,reinterpret_cast<void*>(&PassiveDetour),reinterpret_cast<void**>(&originalPassive)},
    weaverHook{&weaverRef,reinterpret_cast<void*>(&WeaverDetour),reinterpret_cast<void**>(&originalWeaver)},
    zoomHook{&zoomRef,reinterpret_cast<void*>(&ZoomDetour),reinterpret_cast<void**>(&originalZoom)},
    projectileHook{&projectileRef,reinterpret_cast<void*>(&ProjectileDetour),reinterpret_cast<void**>(&originalProjectile)},
    blessingHook{&blessingRef,reinterpret_cast<void*>(&BlessingDetour),reinterpret_cast<void**>(&originalBlessing)},
    summonHook{&summonRef,reinterpret_cast<void*>(&SummonDetour),reinterpret_cast<void**>(&originalSummon)},
    drainHook{&drainRef,reinterpret_cast<void*>(&DrainDetour),reinterpret_cast<void**>(&originalDrain)},
    summonedTickHook{&summonedTickRef,reinterpret_cast<void*>(&SummonedTickDetour),reinterpret_cast<void**>(&originalSummonedTick)},
    constructionHook{&constructionRef,reinterpret_cast<void*>(&ConstructionDetour),reinterpret_cast<void**>(&originalConstruction)},
    slotHook{&slotRef,reinterpret_cast<void*>(&SlotDetour),reinterpret_cast<void**>(&originalSlot)},
    alternateSlotHook{&alternateSlotRef,reinterpret_cast<void*>(&AlternateSlotDetour),reinterpret_cast<void**>(&originalAlternateSlot)},
    linkedHook{&linkedRef,reinterpret_cast<void*>(&LinkedDetour),reinterpret_cast<void**>(&originalLinked)},
    invalidHook{&invalidRef,reinterpret_cast<void*>(&InvalidDetour),reinterpret_cast<void**>(&originalInvalid)},
    enforceHook{&enforceRef,reinterpret_cast<void*>(&EnforceDetour),reinterpret_cast<void**>(&originalEnforce)},
    mainRestrictionHook{&mainRestrictionRef,reinterpret_cast<void*>(&MainRestrictionDetour),reinterpret_cast<void**>(&originalMainRestriction)},
    offRestrictionHook{&offRestrictionRef,reinterpret_cast<void*>(&OffRestrictionDetour),reinterpret_cast<void**>(&originalOffRestriction)},
    equipHook{&equipRef,reinterpret_cast<void*>(&EquipDetour),reinterpret_cast<void**>(&originalEquip)};
Hook passiveUnspentHook{&passiveUnspentRef,reinterpret_cast<void*>(&PassiveUnspentDetour),reinterpret_cast<void**>(&originalPassiveUnspent)},
    weaverUnspentHook{&weaverUnspentRef,reinterpret_cast<void*>(&WeaverUnspentDetour),reinterpret_cast<void**>(&originalWeaverUnspent)};
Hook requirementHook{&requirementRef,reinterpret_cast<void*>(&RequirementDetour),reinterpret_cast<void**>(&originalRequirement)},
    requirementPreviewHook{&requirementPreviewRef,reinterpret_cast<void*>(&RequirementPreviewDetour),reinterpret_cast<void**>(&originalRequirementPreview)},
    passiveRequirementHook{&passiveRequirementRef,reinterpret_cast<void*>(&PassiveRequirementDetour),reinterpret_cast<void**>(&originalPassiveRequirement)},
    uiRequirementHook{&uiRequirementRef,reinterpret_cast<void*>(&UIRequirementDetour),reinterpret_cast<void**>(&originalUIRequirement)},
    reachableHook{&reachableRef,reinterpret_cast<void*>(&ReachableDetour),reinterpret_cast<void**>(&originalReachable)},
    globalNodeHook{&globalNodeRef,reinterpret_cast<void*>(&GlobalNodeDetour),reinterpret_cast<void**>(&originalGlobalNode)},
    passiveSpendHook{&passiveSpendRef,reinterpret_cast<void*>(&PassiveSpendDetour),reinterpret_cast<void**>(&originalPassiveSpend)},
    passiveBulkHook{&passiveBulkRef,reinterpret_cast<void*>(&PassiveBulkDetour),reinterpret_cast<void**>(&originalPassiveBulk)},
    masteryHook{&masteryRef,reinterpret_cast<void*>(&MasteryDetour),reinterpret_cast<void**>(&originalMastery)},
    masteryUpdateHook{&masteryUpdateRef,reinterpret_cast<void*>(&MasteryUpdateDetour),reinterpret_cast<void**>(&originalMasteryUpdate)},
    respecCostHook{&respecCostRef,reinterpret_cast<void*>(&RespecCostDetour),reinterpret_cast<void**>(&originalRespecCost)};
Hook* For(const Setting& s) {
    if(&s==&skillXp)return &xpHook;
    if(&s==&skills)return &skillHook;
    if(&s==&passive)return &passiveHook;
    if(&s==&weaver || &s==&weaverFlat)return &weaverHook;
    if(&s==&zoom || &s==&angle)return &zoomHook;
    if(&s==&projectile || &s==&projectileFlat)return &projectileHook;
    if(&s==&summonDuration)return &summonHook;
    if(&s==&offhand)return &slotHook;
    if(&s==&fullSets)return &equipHook;
    if(&s==&freeSkills||&s==&freePassives||&s==&freeWeaver)return &requirementHook;
    return &blessingHook;
}
std::vector<Hook*> Hooks(const Setting& s) {
    if(&s==&projectile||&s==&projectileFlat)return {&projectileHook,&constructionHook};
    if(&s==&summonDuration)return {&summonHook,&drainHook,&summonedTickHook};
    if(&s==&offhand)return {&slotHook,&alternateSlotHook,&linkedHook,&invalidHook,&enforceHook,&mainRestrictionHook,&offRestrictionHook};
    if(&s==&skills)return {&skillHook,&skillUnspentHook};
    if(&s==&freeSkills||&s==&freePassives||&s==&freeWeaver)return {&requirementHook,&requirementPreviewHook,&passiveRequirementHook,&uiRequirementHook,&reachableHook,&globalNodeHook,&passiveSpendHook,&passiveBulkHook,&masteryHook,&masteryUpdateHook,&respecCostHook};
    return {For(s)};
}
bool Needed(Hook* h) {
    // Already allocated bonus points remain in the game's save when the session
    // ends. Keep read-only arithmetic guards: native ushort subtraction would
    // otherwise wrap below zero and grant tens of thousands of free points.
    if(h==&skillHook||h==&skillUnspentHook||h==&passiveUnspentHook||h==&weaverUnspentHook)return true;
    for(auto* s:settings)if(s->value!=s->initial)for(auto* entry:Hooks(*s))if(entry==h)return true;return false;
}
void RefreshBudgets() {
    void* actor=Actor(); void* tree=Tree(actor); if(!tree)return;
    ApplyPassiveBudget(tree);
    void* data=Get<void*>(tree,treeWeaver); void* faction=Weaver(actor);
    if(data && faction) {
        const int earned=Value<int>(Method(faction,"get_EarnedWeaverPoints",0),faction);
        *reinterpret_cast<uint16_t*>(static_cast<char*>(data)+weaverEarned)=static_cast<uint16_t>(earned);
    }
    for(const char* name:{"updatePassiveTreeIfOpen","updateOpenSkillTreeUIWithData"}) {
        auto m=game::FindMethod("LE.dll","","TreeUIManager",name,0); if(m)Invoke(m);
    }
    for(const char* field:{"characterTree","openSkillTree","weaverTree"}) {
        void* ui=game::StaticObject(game::FindStaticField("LE.dll","","TreeUIManager",field));
        if(ui && game::IsAlive(ui)){void* args[]{ui,tree};auto m=game::FindMethod("LE.dll","","TreeUIManager","UpdateAllNodeRequirements",2);if(m)Invoke(m,nullptr,args);}
    }
}
std::string State() {
    std::ostringstream out; out<<"{\"ok\":true,\"settings\":{"; bool comma=false;
    for(auto* s:settings){bool available=true;for(auto* h:Hooks(*s))available=available && static_cast<bool>(*h->ref);if(comma)out<<',';comma=true;out<<Json(s->key)<<":{\"value\":"<<s->value<<",\"available\":"<<Boolean(available)<<'}';}
    out<<"},\"counters\":{\"skillXp\":"<<xpCalls<<",\"projectiles\":"<<projectileCalls<<",\"projectileRetrofits\":"<<retrofits<<",\"summonDecayTicks\":"<<drainCalls<<",\"lastProjectileNative\":"<<lastProjectileNative<<",\"lastProjectileTotal\":"<<lastProjectileTotal<<",\"blessings\":"<<blessingCalls<<",\"faults\":"<<faults<<'}';
    void* actor=Actor();void* tree=Tree(actor);
    if(tree){void* character=Get<void*>(Get<void*>(actor,actorTracker),trackerData);
        out<<",\"player\":{\"id\":"<<Json(Text(Invoke(Method(character,"get_Id",0),character)))<<",\"name\":"<<Json(Text(Get<void*>(character,Offset("CharacterData","<CharacterName>k__BackingField","LE.Data"))))<<'}';
        void* p=Get<void*>(tree,treePassive),*w=Get<void*>(tree,treeWeaver);
        out<<",\"passivePoints\":"<<(p?Get<uint16_t>(p,passiveEarned):0)<<",\"weaverPoints\":"<<(w?Get<uint16_t>(w,weaverEarned):0)<<",\"skills\":[";
        bool next=false;void* skillList=Get<void*>(tree,treeSkills);for(void* skill:skillList?Entries(skillList):std::vector<void*>{}){if(!skill)continue;if(next)out<<',';next=true;
            out<<"{\"level\":"<<static_cast<int>(Get<uint8_t>(skill,skillLevel))<<",\"gearLevels\":"<<static_cast<int>(Get<uint8_t>(skill,skillAdditional))<<",\"maxPoints\":"<<Value<int>(Method(skill,"getMaxPoints",0),skill)<<",\"unspent\":"<<Value<uint16_t>(Method(skill,"getUnspentPoints",0),skill)<<",\"xp\":"<<Get<int>(skill,Offset("LocalTreeData.SkillTreeData","abilityXp"))<<'}';}out<<']';}
    return out.str()+'}';
}
}
bool Init() {
    try {
        getActor=game::FindMethod("LE.dll","","PlayerFinder","getPlayerActor",0);
        offline=game::FindMethod("LE.dll","LE.Data","CharacterData","get_IsOffline",0);
        actorTree=Offset("Actor","localTreeData");actorTracker=Offset("Actor","characterDataTracker");trackerData=Offset("CharacterDataTracker","charData");
        treeActor=Offset("LocalTreeData","actor");treeSkills=Offset("LocalTreeData","specialisedSkillTrees");treePassive=Offset("LocalTreeData","passiveTree");treeWeaver=Offset("LocalTreeData","weaverTree");treeLevel=Offset("LocalTreeData","characterLevel");
        passiveEarned=Offset("LocalTreeData.PassiveTreeData","pointsEarnt");weaverEarned=Offset("LocalTreeData.WeaverTreeData","<EarnedWeaverPoints>k__BackingField");
        skillLevel=Offset("LocalTreeData.SkillTreeData","level");skillAdditional=Offset("LocalTreeData.SkillTreeData","additionalMaxPointsFromStats");
        zoomMin=Offset("CameraManager","zoomMin");zoomDefault=Offset("CameraManager","zoomDefault");cameraAngle=Offset("CameraManager","cameraAngleDefault");managerActor=Offset("ItemContainersManager","actor");implicitRolls=Offset("ItemData","implicitRolls");
        xpRef=game::FindMethod("LE.dll","","LocalTreeData","ApplyAbilityXp",2);
        skillRef=game::FindMethod("LE.dll","","LocalTreeData.SkillTreeData","getMaxPoints",0);
        skillUnspentRef=game::FindMethod("LE.dll","","LocalTreeData.SkillTreeData","getUnspentPoints",0);
        passiveUnspentRef=game::FindMethod("LE.dll","","LocalTreeData.PassiveTreeData","getUnspentPoints",0);
        weaverUnspentRef=game::FindMethod("LE.dll","","LocalTreeData.WeaverTreeData","getUnspentPoints",0);
        passiveRef=game::FindMethod("LE.dll","","LocalTreeData","receiveUpdatePassivePointsEarntCommand",0);
        weaverRef=game::FindMethod("LE.dll","LE.Factions","TheWeaver","get_EarnedWeaverPoints",0);
        zoomRef=game::FindMethod("LE.dll","","CameraManager","ApplyZoom",0);
        projectileRef=game::FindMethod("LE.dll","","ExtraProjectiles","GetTotalExtraProjectiles",2);
        blessingRef=game::FindMethod("LE.dll","","GenerateItems","rollBlessingsForTimelineDifficulty",5);
        summonRef=game::FindMethod("LE.dll","","DestroyAfterDuration","OnUpdateTick",1);
        drainRef=game::FindMethod("LE.dll","","AcceleratingHealthDrain","OnUpdateTick",1);
        summonedTickRef=game::FindMethod("LE.dll","","Summoned","OnUpdateTick",1);
        constructionRef=game::FindMethod("LE.dll","","AbilityObjectConstructor","constructAbilityObject",14);
        durationOffset=Offset("DestroyAfterDuration","duration");
        slotRef=game::FindMethod("LE.dll","","OneSlotItemContainer","CanAddItemType",1);
        alternateSlotRef=game::FindMethod("LE.dll","","OneSlotItemContainerWithCharacterRequirements","alternateSufficientBaseTypeConditionFulfilled",1);
        linkedRef=game::FindMethod("LE.dll","","LinkedOneSlotItemContainer","incompatibleWithItemInLinkedSlot",1);
        invalidRef=game::FindMethod("LE.dll","","ItemContainersManager","UpdateOffHandWeaponIfInvalid",0);
        enforceRef=game::FindMethod("LE.dll","","OffhandItemContainer","EnforceWeaponLimitations",2);
        mainRestrictionRef=game::FindMethod("LE.dll","","ItemContainersManager","CannotPutItemInMainHandBecauseOfOffhand",1);
        offRestrictionRef=game::FindMethod("LE.dll","","ItemContainersManager","CannotPutItemInOffhandBecauseOfMainhand",1);
        equipRef=game::FindMethod("LE.dll","","ItemEquipManager","UpdateStats",7);
        requirementRef=game::FindMethod("LE.dll","","LocalTreeData","fulfilledRequirementExists",4);
        requirementPreviewRef=game::FindMethod("LE.dll","","LocalTreeData","fulfilledRequirementExists",5);
        passiveRequirementRef=game::FindMethod("LE.dll","","LocalTreeData","ArePassiveNodeRequirementsMet",3);
        uiRequirementRef=game::FindMethod("LE.dll","","TreeUIManager","CheckNodeRequirementsMet",3);
        reachableRef=game::FindMethod("LE.dll","","TreeUIManager","ComputeReachableNodesFromCenter",1);
        globalNodeRef=game::FindMethod("LE.dll","","GlobalTreeData.TreeData","getNode",1);
        passiveSpendRef=game::FindMethod("LE.dll","","LocalTreeData","tryToSpendPassivePoint",2);
        passiveBulkRef=game::FindMethod("LE.dll","","LocalTreeData","ReceiveBulkPassiveTreeUpdateCommand",2);
        masteryRef=game::FindMethod("LE.dll","","LocalTreeData","CalculateMasteryLevelsFromAllocation",2);
        masteryUpdateRef=game::FindMethod("LE.dll","","LocalTreeData","updateMasteryTotals",0);
        respecCostRef=game::FindMethod("LE.dll","","LocalTreeData","CalculateRespecCostForBulkUpdate",2);
        std::vector<Hook*> installed;std::string why;
        for(auto* entry:{&skillHook,&skillUnspentHook,&passiveUnspentHook,&weaverUnspentHook}) {
            if(!*entry->ref || !hook::Install(entry->ref->code,entry->detour,entry->original,&why)) {
                for(auto* old:installed)hook::Remove(old->ref->code,nullptr);
                throw std::runtime_error("point-budget guards unavailable: "+why);
            }installed.push_back(entry);
        }
        return getActor && offline;
    } catch(const std::exception& e){Log("build controls unavailable: %s",e.what());return false;}
}
std::string Read(){return Run([]{return State();});}
void ReleaseRoots(){componentTypes.clear();setEntries.clear();setStatsOwner.Reset();
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
    testConstructed.Reset();
#endif
}
std::string Set(const std::string& key,double value) {
    return Run([=]() -> std::string {
        Setting* s=nullptr;for(auto* entry:settings)if(key==entry->key)s=entry;
        if(!s)throw std::runtime_error("unknown build setting");
        if(!std::isfinite(value)||value<s->minimum||value>s->maximum||(s->integer&&std::floor(value)!=value)||(&blessings==s&&value!=0&&value<3))throw std::runtime_error("setting is outside its supported range");
        const auto hooks=Hooks(*s);
        for(auto* entry:hooks)if(!*entry->ref)throw std::runtime_error("this game build is missing the required feature method");
        if(value!=s->initial){RequireSession(4);if(!Actor())throw std::runtime_error("load an offline character first");}
        const double previous=s->value;std::string why;
        std::vector<Hook*> fresh;
        for(auto* entry:hooks)if(value!=s->initial && !hook::IsInstalled(entry->ref->code)) {
            if(!hook::Install(entry->ref->code,entry->detour,entry->original,&why)){for(auto* back:fresh)hook::Remove(back->ref->code,nullptr);throw std::runtime_error("cannot enable feature: "+why);}
            fresh.push_back(entry);
        }
        s->value=value;
        try {
            if(s==&passive||s==&skills||s==&weaver||s==&weaverFlat||s==&freeSkills||s==&freePassives||s==&freeWeaver)RefreshBudgets();
            if(s==&zoom || s==&angle){void* camera=game::StaticObject(game::FindStaticField("LE.dll","","CameraManager","instance"));if(camera)Invoke(zoomRef,camera);}
            if(s==&fullSets){void* manager=game::StaticObject(game::FindStaticField("LE.dll","","ItemContainersManager","_instance"));if(manager)ReconcileSets(Get<void*>(manager,Offset("ItemContainersManager","itemEquipManager")));}
            for(auto* entry:hooks)if(!Needed(entry)&&hook::IsInstalled(entry->ref->code)&&!hook::Remove(entry->ref->code,&why))throw std::runtime_error("cannot restore feature hook: "+why);
        }catch(...){s->value=previous;Checked([&]{if(s==&passive||s==&skills||s==&weaver||s==&weaverFlat)RefreshBudgets();});for(auto* back:fresh)if(!Needed(back))hook::Remove(back->ref->code,nullptr);throw;}
        return State();
    });
}
std::string Respec(const std::string& id,const std::string& kind) {
    if(kind!="skills"&&kind!="passives"&&kind!="weaver")return "{\"ok\":false,\"error\":\"Unknown tree respec.\"}";
    progression::SnapshotData snapshot;std::string why;game::Handle expectedActor;
    if(!mainthread::Run([&]{snapshot=progression::CaptureSnapshotCurrent(id,("respec-"+kind).c_str());expectedActor.Set(Actor());},5000,&why))return "{\"ok\":false,\"error\":"+Json(why)+"}";
    const auto backup=progression::WriteCapturedSnapshot(snapshot);
    std::vector<std::unique_ptr<Root>> dataRoots;size_t index=0;int phase=0,refunded=0;
    std::string result;
    if(!mainthread::RunSteps([&]{try{
        RequireSession(4);void* actor=Actor();void* tree=Tree(actor);if(!tree)throw std::runtime_error("Load an offline character first.");
        if(actor!=expectedActor.Get())throw std::runtime_error("The loaded actor changed. No further points were refunded.");
        void* character=Get<void*>(Get<void*>(actor,actorTracker),trackerData);
        if(Text(Invoke(Method(character,"get_Id",0),character))!=id)throw std::runtime_error("The loaded character changed.");
        if(Text(Get<void*>(character,Offset("CharacterData","<CharacterName>k__BackingField","LE.Data")))!=snapshot.name)throw std::runtime_error("The loaded character changed.");
        if(phase==0){
            if(kind=="skills"){for(void* data:Entries(Get<void*>(tree,treeSkills)))if(data)dataRoots.push_back(std::make_unique<Root>(data));}
            else {void* data=Get<void*>(tree,kind=="passives"?treePassive:treeWeaver);if(!data)throw std::runtime_error("This tree is unavailable.");dataRoots.push_back(std::make_unique<Root>(data));}
            for(auto& data:dataRoots)refunded+=Value<int>(Method(data->Get(),"numberOfPointsAllocated",0),data->Get());
            if(refunded>5000)throw std::runtime_error("Tree allocations exceed the supported bound.");
            phase=1;return false;
        }
        if(kind=="skills"&&phase==1){
            // Use the game's refund operation without its skill-level/XP penalty.
            // One point per frame keeps skill mutator updates off long frame stalls.
            while(index<dataRoots.size()){
                void* data=dataRoots[index]->Get();void* selected=nullptr;
                for(void* node:Entries(Get<void*>(data,F("LocalTreeData.TreeData","nodes"))))if(Get<uint8_t>(node,F("LocalTreeData.NodeData","pointsAllocated"))){selected=node;break;}
                if(!selected){++index;continue;}
                void* ability=Get<void*>(data,F("LocalTreeData.SkillTreeData","ability"));bool loseLevel=false;void* args[]{ability,selected,data,&loseLevel};
                Invoke(game::FindMethod("LE.dll","","LocalTreeData","respecSkillPoint",4),tree,args);return false;
            }phase=4;
        }else if(phase==1){
            void* data=dataRoots[0]->Get();Root allocation(Invoke(Method(data,"getPointsAllocated",0),data));Invoke(Method(allocation.Get(),"Clear",0),allocation.Get());
            if(kind=="passives"){void* args[]{allocation.Get()};Invoke(game::FindMethod("LE.dll","","LocalTreeData","ApplyBulkPassiveTreeUpdate",1),tree,args);}
            else {Root globals(Invoke(game::FindMethod("LE.dll","LE.AssetManagement","GlobalAssets","get_GlobalTreeData",0)));void* key=Get<void*>(data,F("LocalTreeData.TreeData","treeID"));void* a[]{key};Root global(Invoke(Method(globals.Get(),"getTreeData",1),globals.Get(),a));void* args[]{allocation.Get(),global.Get()};Invoke(game::FindMethod("LE.dll","","LocalTreeData","ApplyBulkWeaverTreeUpdate",2),tree,args);}
            phase=2;return false;
        }
        if(phase==2){if(kind=="passives")Invoke(game::FindMethod("LE.dll","","LocalTreeData","updateMasteryTotals",0),tree);phase=3;return false;}
        if(phase==3){if(kind=="passives"){bool updateOtherMutators=true;void* args[]{&updateOtherMutators};Invoke(game::FindMethod("LE.dll","","LocalTreeData","updateCharacterMutator",1),tree,args);Invoke(game::FindMethod("LE.dll","","LocalTreeData","RecalculateKnownAbilitiesAndUnequipUnlearnedOnes",0),tree);}Invoke(game::FindMethod("LE.dll","","LocalTreeData",kind=="passives"?"savePassiveTreeData":"SaveWeaverTreeData",0),tree);phase=4;return false;}
        for(auto& data:dataRoots)if(Value<int>(Method(data->Get(),"numberOfPointsAllocated",0),data->Get())!=0)throw std::runtime_error("The tree refund did not complete.");
        progression::SaveCurrent(id);RefreshBudgets();result="{\"ok\":true,\"player\":{\"id\":"+Json(id)+",\"name\":"+Json(snapshot.name)+"},\"tree\":"+Json(kind)+",\"refunded\":"+std::to_string(refunded)+",\"backup\":"+Json(backup)+"}";
    }catch(const std::exception& e){result="{\"ok\":false,\"error\":"+Json(e.what())+",\"backup\":"+Json(backup)+"}";}return true;},60000,&why))return "{\"ok\":false,\"error\":"+Json(why)+",\"backup\":"+Json(backup)+"}";
    return result;
}
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Test(const std::string& id,const std::string& action) {
    return Run([=]{
        RequireSession(4);void* actor=Actor();if(!actor)throw std::runtime_error("offline test actor required");
        void* character=Get<void*>(Get<void*>(actor,actorTracker),trackerData);
        const auto name=Text(Get<void*>(character,Offset("CharacterData","<CharacterName>k__BackingField","LE.Data")));
        if(Text(Invoke(Method(character,"get_Id",0),character))!=id || (name!="EpFeatureTest"&&name!="EpFeedbackTest"))throw std::runtime_error("feature tests require an isolated feature test clone");
        void* tree=Tree(actor);
        if(action=="creation-probe"){
            std::string why;if(!hook::Install(constructionRef.code,reinterpret_cast<void*>(&ConstructionDetour),reinterpret_cast<void**>(&originalConstruction),&why))throw std::runtime_error(why);
            return std::string("{\"ok\":true}");
        }
        if(action=="creation-counts")return "{\"ok\":true,\"total\":"+std::to_string(testConstructions)+",\"extras\":"+std::to_string(testExtraConstructions)+"}";
        if(action=="creation-kinds"){std::string out="{\"ok\":true,\"objects\":[";for(auto& row:testCreationKinds){if(out.back()!='[')out+=',';out+=row;}testCreationKinds.clear();return out+"]}";}
        if(action=="drain-probe"){
            if(!originalDrain||summonDuration.value==1)throw std::runtime_error("enable summon duration first");
            void* tracker=Get<void*>(actor,F("Actor","summonTracker")),*drain=nullptr;
            for(void* s:Entries(Get<void*>(tracker,F("SummonTracker","summons"))))if(s&&game::IsAlive(s)&&OwnedSummon(s))if(void* d=Component(s,"AcceleratingHealthDrain")){drain=d;break;}
            if(!drain)throw std::runtime_error("cast a health-decay summon first");Root root(drain);
            struct Restore {std::vector<std::pair<float*,float>> fields;~Restore(){for(auto& [p,v]:fields)*p=v;}} restore;
            for(const char* field:{"baseHealthDrain","baseHealthDrainAcceleration","healthDrainJerk","basePercentHealthDrain","basePercentHealthDrainAcceleration","percentHealthDrainJerk","baseCurrentHealthPercentDrain","baseCurrentHealthPercentDrainAcceleration","currentHealthPercentDrainJerk","drainMultiplier","healthDrain","healthDrainAcceleration","percentHealthDrain","percentHealthDrainAcceleration","currentHealthPercentDrain","currentHealthPercentDrainAcceleration"}){
                float* p=reinterpret_cast<float*>(static_cast<char*>(drain)+F("AcceleratingHealthDrain",field));restore.fields.emplace_back(p,*p);*p=0;}
            // Real owned component, initially zero drain so neither comparison
            // removes HP. Acceleration is measured after the native tick. Fields restore.
            *reinterpret_cast<float*>(static_cast<char*>(drain)+F("AcceleratingHealthDrain","drainMultiplier"))=1;
            float* acceleration=reinterpret_cast<float*>(static_cast<char*>(drain)+F("AcceleratingHealthDrain","healthDrainAcceleration"));
            float* value=reinterpret_cast<float*>(static_cast<char*>(drain)+F("AcceleratingHealthDrain","healthDrain"));
            *acceleration=10;originalDrain(drain,.25f,drainRef.info);const float native=*value;
            *value=0;*acceleration=10;DrainDetour(drain,.25f,drainRef.info);const float actual=*value;
            return "{\"ok\":true,\"native\":"+std::to_string(native)+",\"actual\":"+std::to_string(actual)+",\"factor\":"+std::to_string(summonDuration.value.load())+"}";
        }
        if(action=="summon-times"){
            std::ostringstream out;out<<"{\"ok\":true,\"summons\":[";bool comma=false;
            void* tracker=Get<void*>(actor,F("Actor","summonTracker"));
            for(void* s:Entries(Get<void*>(tracker,F("SummonTracker","summons")))){if(!s||!game::IsAlive(s))continue;
                if(comma)out<<',';comma=true;void* go=Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Component","get_gameObject",0),s);
                out<<"{\"name\":"<<Json(Text(Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Object","get_name",0),go)))<<",\"age\":"<<Get<float>(s,F("Summoned","age"))<<",\"temporary\":"<<Boolean(Value<bool>(Method(s,"IsTemporary",0),s));
                if(void* d=Component(s,"DestroyAfterDuration"))out<<",\"timerAge\":"<<Get<float>(d,F("DestroyAfterDuration","age"))<<",\"timerDuration\":"<<Get<float>(d,durationOffset);
                if(void* d=Component(s,"AcceleratingHealthDrain"))out<<",\"healthDrain\":"<<Get<float>(d,F("AcceleratingHealthDrain","healthDrain"));
                out<<'}';
            }return out.str()+"]}";
        }
        if(action.rfind("cast-",0)==0){
            uint16_t id=static_cast<uint16_t>(std::stoi(action.substr(5)));void* args[]{&id};
            const auto* ac=game::FindClass("LE.dll","","Ability");const il2cpp::Method* getter=nullptr;void* it=nullptr;
            while(auto* m=il2cpp::api().class_get_methods(ac,&it)){if(std::string(il2cpp::api().method_get_name(m))!="getAbility"||il2cpp::api().method_get_param_count(m)!=1)continue;
                char* name=il2cpp::api().type_get_name(il2cpp::api().method_get_param(m,0));bool valid=name&&std::string(name)=="System.UInt16";if(name)il2cpp::api().free(name);if(valid){getter=m;break;}}
            Root ability(Invoke(getter,nullptr,args));void* ctor=Get<void*>(actor,F("Actor","abilityObjectConstructor"));
            auto t=Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Component","get_transform",0),actor);
            Vec3 p=Value<Vec3>(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Transform","get_position",0),t),direction{0,0,1};float delay=0;int use=1;bool mutate=true;void* cast[]{ctor,&p,&direction,&delay,&use,&mutate,nullptr};
            Invoke(Method(ability.Get(),"CastAfterDelay",7),ability.Get(),cast);return State();
        }
        if(action=="last-creation"){
            void* go=testConstructed.Get();if(!go||!game::IsAlive(go))return std::string("{\"ok\":true,\"alive\":false}");
            void* t=Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","GameObject","get_transform",0),go);
            void* e=Component(t,"ExtraProjectiles"),*d=Component(t,"DestroyAfterDuration"),*s=Component(t,"Summoned"),*drain=Component(t,"AcceleratingHealthDrain");
            std::ostringstream out;out<<"{\"ok\":true,\"alive\":true,\"extraComponent\":"<<Boolean(e!=nullptr)<<",\"summoned\":"<<Boolean(s!=nullptr)<<",\"healthDecay\":"<<Boolean(drain!=nullptr);
            if(e){void* list=Get<void*>(e,F("ExtraProjectiles","newProjectiles"));out<<",\"spawnedExtras\":"<<(list?Entries(list).size():0);}
            if(d)out<<",\"duration\":"<<Get<float>(d,durationOffset)<<",\"age\":"<<Get<float>(d,F("DestroyAfterDuration","age"));
            return out.str()+"}";
        }
        if(action=="skill-budget") {
            const auto entries=Entries(Get<void*>(tree,treeSkills));if(entries.empty()||!entries[0])throw std::runtime_error("specialize a skill first");
            void* s=entries[0];Root root(s);
            auto* level=reinterpret_cast<uint8_t*>(static_cast<char*>(s)+skillLevel);
            auto* additional=reinterpret_cast<uint8_t*>(static_cast<char*>(s)+skillAdditional);
            struct Restore{uint8_t* a;uint8_t* b;uint8_t x,y;~Restore(){*a=x;*b=y;}} restore{level,additional,*level,*additional};
            *level=20;*additional=1;
            return "{\"ok\":true,\"effectiveLevel\":21,\"actualNativeBudget\":"+std::to_string(Value<int>(Method(s,"getMaxPoints",0),s))+"}";
        }
        if(action=="weaver-budget") {
            void* faction=Weaver(actor);Root root(faction);
            auto* rank=reinterpret_cast<int*>(static_cast<char*>(faction)+Offset("Faction","<Rank>k__BackingField","LE.Factions"));
            struct Restore{int* field;int old;~Restore(){*field=old;}} restore{rank,*rank};
            *rank=10;
            const IntFn native=hook::IsInstalled(weaverRef.code)?originalWeaver:reinterpret_cast<IntFn>(weaverRef.code);
            const int natural=native(faction,weaverRef.info);
            return "{\"ok\":true,\"natural\":"+std::to_string(natural)+",\"actual\":"+std::to_string(Value<int>(weaverRef,faction))+"}";
        }
        if(action=="skill-xp") {
            int gain=11;bool force=false;void* args[]{&gain,&force};Invoke(xpRef,tree,args);return State();
        }
        if(action=="passive-refresh") {Invoke(passiveRef,tree);return State();}
        if(action=="passive-probe") {
            PassiveScope scope(tree);void* data=Get<void*>(tree,treePassive);void* tid=Get<void*>(data,F("LocalTreeData.TreeData","treeID"));void* getArgs[]{tid};Root all(Invoke(game::FindMethod("LE.dll","LE.AssetManagement","GlobalAssets","get_GlobalTreeData",0)));Root global(Invoke(Method(all.Get(),"getTreeData",1),all.Get(),getArgs));uint8_t nodeId=22;void* args[]{&nodeId};Root node(Invoke(globalNodeRef,global.Get(),args));
            return "{\"ok\":true,\"scope\":"+std::string(Boolean(passiveValidation && passiveValidation->global==global.Get()))+",\"mastery\":"+std::to_string(Get<uint8_t>(node.Get(),F("GlobalTreeData.NodeData","requiredMastery")))+",\"threshold\":"+std::to_string(Get<uint8_t>(node.Get(),F("GlobalTreeData.NodeData","masteryRequirement")))+",\"unspent\":"+std::to_string(Value<uint16_t>(Method(data,"getUnspentPoints",0),data))+",\"class\":"+std::to_string(Value<uint8_t>(Method(tree,"GetCharacterClassID",0),tree))+"}";
        }
        if(action=="projectile"||action=="summon") {
            const auto* cls=game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","GameObject");Root go(il2cpp::api().object_new(cls));void* text=il2cpp::api().string_new("EpochPact isolated projectile check");void* ctor[]{text};Invoke(Method(go.Get(),".ctor",1),go.Get(),ctor);
            struct Destroy{void* object;~Destroy(){void* args[]{object};Checked([&]{Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Object","Destroy",1),nullptr,args);});}} destroy{go.Get()};
            auto add=[&](const char* name){const auto* c=game::FindClass("LE.dll","",name);void* type=il2cpp::api().type_get_object(il2cpp::api().class_get_type(c));void* args[]{type};return Invoke(Method(go.Get(),"AddComponent",1),go.Get(),args);};
            Root refs(add("CreationReferences"));
            const auto* creator=il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(refs.Get()),"creator");il2cpp::api().field_set_value(refs.Get(),creator,actor);
            if(action=="summon") {
                Root summoned(add("Summoned")),timer(add("DestroyAfterDuration")),destroyer(add("SelfDestroyer"));
                const auto set=[&](void* object,const char* cls,const char* field,void* value){const auto* f=il2cpp::api().class_get_field_from_name(game::FindClass("LE.dll","",cls),field);if(!object||!f)throw std::runtime_error(std::string("test field unavailable: ")+cls+"."+field);il2cpp::api().field_set_value(object,f,value);};
                set(summoned.Get(),"Summoned","references",refs.Get());set(timer.Get(),"DestroyAfterDuration","selfDestroyer",destroyer.Get());
                float duration=10,age=15,delta=0;bool fixed=true;set(timer.Get(),"DestroyAfterDuration","duration",&duration);set(timer.Get(),"DestroyAfterDuration","age",&age);set(timer.Get(),"DestroyAfterDuration","hasHadFixedUpdate",&fixed);
                void* args[]{&delta};Invoke(summonRef,timer.Get(),args);
                return "{\"ok\":true,\"alive\":"+std::string(Boolean(game::IsAlive(go.Get())))+",\"expired\":"+Boolean(Get<bool>(destroyer.Get(),F("SelfDestroyer","dead")))+",\"age\":"+std::to_string(Get<float>(timer.Get(),F("DestroyAfterDuration","age")))+",\"duration\":"+std::to_string(Get<float>(timer.Get(),durationOffset))+"}";
            }
            Root extras(add("ExtraProjectiles"));
            *reinterpret_cast<int*>(static_cast<char*>(extras.Get())+F("ExtraProjectiles","numberOfExtraProjectiles"))=0;
            Vec3 start{},end{0,0,0};void* args[]{&start,&end};int extra=Value<int>(projectileRef,extras.Get(),args);
            return "{\"ok\":true,\"actualTotal\":"+std::to_string(extra+1)+"}";
        }
        if(action=="camera") {
            void* camera=game::StaticObject(game::FindStaticField("LE.dll","","CameraManager","instance"));
            Invoke(zoomRef,camera);const auto pos=Get<Vec3>(camera,F("CameraManager","localPositionWithoutShake"));
            Root transform(Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Component","get_transform",0),camera));
            auto rotation=Value<Vec3>(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Transform","get_eulerAngles",0),transform.Get());
            return "{\"ok\":true,\"z\":"+std::to_string(pos.z)+",\"pitch\":"+std::to_string(rotation.x)+",\"zoomMin\":"+std::to_string(Get<float>(camera,zoomMin))+",\"zoomDefault\":"+std::to_string(Get<float>(camera,zoomDefault))+"}";
        }
        if(action=="sets") {
            void* manager=game::StaticObject(game::FindStaticField("LE.dll","","ItemContainersManager","_instance"));void* equip=Get<void*>(manager,F("ItemContainersManager","itemEquipManager")),*stats=Get<void*>(equip,F("ItemEquipManager","stats"));
            void* present=Get<void*>(equip,F("ItemEquipManager","setsPresent")),*completion=Get<void*>(equip,F("ItemEquipManager","setCompletion"));
            const int count=Get<int>(present,il2cpp::api().field_get_offset(il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(present),"_size")));
            std::ostringstream out;out<<"{\"ok\":true,\"owned\":"<<Boolean(Get<void*>(equip,F("ItemEquipManager","actor"))==actor)<<",\"present\":[";
            for(int i=0;i<count;++i){int setId=0;void* index[]{&i};setId=Value<uint8_t>(Method(present,"get_Item",1),present,index);void* idArgs[]{&setId};if(i)out<<',';out<<"{\"id\":"<<setId<<",\"byIndex\":"<<Value<int>(Method(completion,"count",1),completion,index)<<",\"byId\":"<<Value<int>(Method(completion,"count",1),completion,idArgs)<<'}';}
            out<<"],\"bonusEntries\":"<<setEntries.size()<<",\"bonuses\":[";bool next=false;
            for(auto& root:setEntries){void* s=root->Get();uint8_t sp=Get<uint8_t>(s,F("Stats.Stat","property")),special=Get<uint8_t>(s,F("Stats.Stat","specialTag"));int tags=Get<int>(s,F("Stats.Stat","tags")),extra=Get<int>(s,F("Stats.Stat","extraTag"));float zero=0;bool no=false;void* args[]{&sp,&tags,&zero,&zero,&zero,&special,&extra,&no,&no};float actual=Value<float>(Method(stats,"GetStatValue",9),stats,args);
                if(next)out<<',';next=true;out<<"{\"sp\":"<<int(sp)<<",\"tags\":"<<tags<<",\"special\":"<<int(special)<<",\"extra\":"<<extra<<",\"actual\":"<<actual<<'}';}return out.str()+"]}";
        }
        if(action=="blessings") {
            void* generator=Get<void*>(actor,F("Actor","generateItems"));uint8_t timeline=1;int difficulty=0,count=3,extra=0;bool lucky=false;
            void* args[]{&timeline,&difficulty,&count,&extra,&lucky};Root result(Invoke(blessingRef,generator,args));
            std::ostringstream out;out<<"{\"ok\":true,\"options\":[";bool comma=false;
            for(void* item:Entries(result.Get())){if(comma)out<<',';comma=true;out<<'[';void* array=Get<void*>(item,implicitRolls);const auto n=Get<uintptr_t>(array,0x18);if(n>32)throw std::runtime_error("invalid test rolls");for(uintptr_t i=0;i<n;++i){if(i)out<<',';out<<static_cast<int>(Get<uint8_t>(array,0x20+i));}out<<']';}return out.str()+"]}";
        }
        if(action=="trees") {
            std::ostringstream out;out<<"{\"ok\":true,\"trees\":[";bool comma=false;
            std::vector<void*> data=Entries(Get<void*>(tree,treeSkills));data.push_back(Get<void*>(tree,treePassive));data.push_back(Get<void*>(tree,treeWeaver));
            Root all(Invoke(game::FindMethod("LE.dll","LE.AssetManagement","GlobalAssets","get_GlobalTreeData",0)));
            for(void* d:data){if(!d)continue;void* tid=Get<void*>(d,F("LocalTreeData.TreeData","treeID"));void* args[]{tid};Root global(Invoke(Method(all.Get(),"getTreeData",1),all.Get(),args));
                if(comma)out<<',';comma=true;out<<"{\"id\":"<<Json(Text(tid))<<",\"class\":"<<Json(il2cpp::api().class_get_name(il2cpp::api().object_get_class(d)))<<",\"allocated\":"<<Value<int>(Method(d,"numberOfPointsAllocated",0),d)<<",\"nodes\":[";bool next=false;
                for(void* n:Entries(Get<void*>(global.Get(),F("GlobalTreeData.TreeData","nodes")))){if(!n)continue;if(next)out<<',';next=true;void* requirements=Get<void*>(n,F("GlobalTreeData.NodeData","requirements"));out<<"{\"id\":"<<int(Get<uint8_t>(n,F("GlobalTreeData.NodeData","id")))<<",\"name\":"<<Json(Text(Get<void*>(n,F("GlobalTreeData.NodeData","name"))))<<",\"cap\":"<<int(Get<uint8_t>(n,F("GlobalTreeData.NodeData","maxPoints")))<<",\"mastery\":"<<int(Get<uint8_t>(n,F("GlobalTreeData.NodeData","requiredMastery")))<<",\"threshold\":"<<int(Get<uint8_t>(n,F("GlobalTreeData.NodeData","masteryRequirement")))<<",\"requirements\":"<<(requirements?Entries(requirements).size():0)<<'}';}out<<"]}";
            }return out.str()+"]}";
        }
        if(action.rfind("spend-",0)==0) {
            const auto split=action.rfind('-');uint8_t node=static_cast<uint8_t>(std::stoi(action.substr(split+1)));bool accepted=false;
            if(action.rfind("spend-passive-",0)==0){uint8_t cls=Value<uint8_t>(Method(tree,"GetCharacterClassID",0),tree);void* args[]{&cls,&node};accepted=Value<bool>(game::FindMethod("LE.dll","","LocalTreeData","receiveSpendPassivePointCommand",2),tree,args);}
            else if(action.rfind("spend-weaver-",0)==0){void* args[]{&node};accepted=Value<bool>(game::FindMethod("LE.dll","","LocalTreeData","ReceiveSpendWeaverPointCommand",1),tree,args);}
            else if(action.rfind("spend-skill-",0)==0){const auto entries=Entries(Get<void*>(tree,treeSkills));void* ability=Get<void*>(entries.at(0),F("LocalTreeData.SkillTreeData","ability"));void* args[]{ability,&node};accepted=Value<bool>(game::FindMethod("LE.dll","","LocalTreeData","receiveSpendSkillPointCommand",2),tree,args);}
            else throw std::runtime_error("unknown allocation test");
            return std::string("{\"ok\":true,\"accepted\":")+Boolean(accepted)+",\"state\":"+State()+"}";
        }
        throw std::runtime_error("unknown feature test action");
    });
}
#endif
}
