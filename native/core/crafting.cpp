#include "crafting.hpp"
#include "managed.hpp"
#include "hook.hpp"
#include "craft_rules.hpp"
#include "loot_crafting_rules.hpp"
#include "progression.hpp"
#include "workshop.hpp"
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
#include "stat_editor.hpp"
#endif
#include <atomic>
#include <cstring>
#include <memory>
#include <sstream>
#include <charconv>
#include <map>
#include <random>
#include <set>
#include <exception>

namespace ep::crafting {
namespace {
using namespace managed;
using namespace lootcraft::rules;
std::atomic<double> fpFactor{1}, hope{-1}, despair{-1};
std::atomic<bool> preserveShards{false};
std::atomic<bool> preserveRunes{false}, preserveGlyphs{false}, bypassLevel{false};
std::atomic<bool> allowT7{false};
std::set<int> sevenTierAffixes;
std::atomic<uint64_t> refundedRunes{0}, refundedGlyphs{0}, levelChecks{0};
std::atomic<uint64_t> crafts{0}, fpChanges{0}, hopeChanges{0}, sealChanges{0}, refunded{0}, faults{0};
std::atomic<int> lastNormalLoss{0}, lastEffectiveLoss{0};
bool ready=false;
size_t managerActor, managerCraft, managerLoaded, managerStorage, forgeMain, forgeSupport, forgeModifier;
size_t actorTracker, trackerData, forgingPotential, affixList, affixId, affixTier, affixRoll, itemSubtype, itemRarity;
size_t itemType, entryData, entryQuantity;
const il2cpp::Field* managerInstance; const il2cpp::Field* craftingRng;
game::MethodRef getActor, offline, forge, cost, setFP, seal, roll, minCost, maxCost, shardCheck, glyphCheck, getItem;
game::MethodRef capability, requiredLevel, itemConstructor, maxCraftTier, addTier, affixStats, shardCost, rebuild, rarityFromAffixes, randomByte;
using MaxTierFn=bool(*)(void*,int,const il2cpp::Method*);
using AddTierFn=bool(*)(void*,int,bool,bool,bool,bool,bool,bool,void**,void*,const il2cpp::Method*);
MaxTierFn originalMaxTier=nullptr; AddTierFn originalAddTier=nullptr;
using ItemConstructorFn=void(*)(void*,void*,const il2cpp::Method*);
ItemConstructorFn originalItemConstructor=nullptr;
using ForgeFn=void (*)(void*,const il2cpp::Method*);
using CostFn=void (*)(void*,int,int,bool,int*,const il2cpp::Method*);
using SetFPFn=void (*)(void*,int,const il2cpp::Method*);
using SealFn=float (*)(void*,int,const il2cpp::Method*);
using RollFn=bool (*)(void*,float,const il2cpp::Method*);
ForgeFn originalForge=nullptr; CostFn originalCost=nullptr; SetFPFn originalSetFP=nullptr;
SealFn originalSeal=nullptr; RollFn originalRoll=nullptr;
using CapabilityFn=bool (*)(void*,void**,bool*,bool*,void**,const il2cpp::Method*);
using LevelFn=int (*)(void*,int,int,const il2cpp::Method*);
CapabilityFn originalCapability=nullptr; LevelFn originalLevel=nullptr;
struct Context { void* actor; void* manager; void* craft; void* main; void* modifier; void* support; void* storage; std::string id; std::string name; };
bool Checked(const std::function<void()>& work);
void ItemConstructorDetour(void* self,void* bytes,const il2cpp::Method* method) {
    if(game::IsOfflinePlay())Checked([]{workshop::Ensure();});
    originalItemConstructor(self,bytes,method);
    // Unique/Set loading normally replaces the encoded base with the template's
    // base. Custom workshop items retain the v6 header's original equipment
    // base. Ordinary items have matching headers and are untouched; without
    // EpochPact the game still loads the item as its normal template base.
    if(!self||!bytes||!game::IsOfflinePlay())return;
    Checked([&]{
        const auto n=Get<uintptr_t>(bytes,0x18);if(n<23||n>1024||Get<uint8_t>(bytes,0x20)!=6)return;
        const auto rarity=Get<uint8_t>(self,itemRarity),type=Get<uint8_t>(bytes,0x25),subtype=Get<uint8_t>(bytes,0x26);
        if((rarity!=8&&rarity!=9)||type>=25)return;
        *reinterpret_cast<uint8_t*>(static_cast<char*>(self)+itemType)=type;
        *reinterpret_cast<uint16_t*>(static_cast<char*>(self)+itemSubtype)=subtype;
    });
}
Context Current() {
    if(!ready || !game::IsOfflinePlay()) throw std::runtime_error("offline crafting metadata/session unavailable");
    RequireSession(4);
    void* actor=Invoke(getActor);
    if(!actor || !game::IsAlive(actor)) throw std::runtime_error("enter an offline character first");
    void* character=Get<void*>(Get<void*>(actor,actorTracker),trackerData);
    if(!Value<bool>(offline,character)) throw std::runtime_error("loaded character is not offline");
    void* manager=game::StaticObject(managerInstance);
    if(!manager || !Get<bool>(manager,managerLoaded) || Get<void*>(manager,managerActor)!=actor)
        throw std::runtime_error("local inventory not ready");
    void* craft=Get<void*>(manager,managerCraft);
    if(!craft || Get<void*>(craft,Offset("CraftingManager","actor"))!=actor) throw std::runtime_error("crafting ownership mismatch");
    const auto item=[&](size_t field) { void* container=Get<void*>(craft,field); return Invoke(getItem,container); };
    return {actor,manager,craft,item(forgeMain),item(forgeModifier),item(forgeSupport),Get<void*>(manager,managerStorage),
        Text(Invoke(Method(character,"get_Id",0),character)),Text(Get<void*>(character,Offset("CharacterData","<CharacterName>k__BackingField","LE.Data")))};
}
bool Glyph(void* item,int subtype) { return item && Value<bool>(glyphCheck,item) && Get<uint16_t>(item,itemSubtype)==subtype; }
int Count(void* storage,int id) { bool include=true; void* args[]{&id,&include}; return Value<int>(Method(storage,"GetCountOfShard",2),storage,args); }
// Count storage plus the selected forge slot: the normal forge automatically
// moves the next material into the slot. Slot-only counts would duplicate it.
int MaterialCount(const Context& c,int type,int subtype) {
    int total=0;
    const auto count=[&](void* container) {
        for(void* entry:Entries(Invoke(Method(container,"GetContent",0),container))) {
            void* data=Get<void*>(entry,entryData);
            if(data && Get<uint8_t>(data,itemType)==type && Get<uint16_t>(data,itemSubtype)==subtype) {
                const int quantity=Get<int>(entry,entryQuantity);
                if(quantity<0 || quantity>100000000 || total>100000000-quantity) throw std::runtime_error("invalid material count");
                total+=quantity;
            }
        }
    };
    void* materials=Invoke(Method(c.manager,"get_materials",0),c.manager);
    for(void* container:Entries(Invoke(Method(materials,"get_Containers",0),materials))) count(container);
    count(Get<void*>(c.craft,forgeModifier)); count(Get<void*>(c.craft,forgeSupport));
    return total;
}
int MaterialCount(const Context& c,void* item) {
    return item?MaterialCount(c,Get<uint8_t>(item,itemType),Get<uint16_t>(item,itemSubtype)):0;
}
struct Scope {
    Context c{}; bool owned=false, hopeUsed=false;
    double factor=1, hopePercent=-1, sealPercent=-1;
    int shard=-1, before=-1;
    int runeBefore=-1, glyphBefore=-1;
    std::vector<std::unique_ptr<Root>> roots;
    void Pin(void* obj) { if(obj) roots.push_back(std::make_unique<Root>(obj)); }
};
thread_local Scope* active=nullptr;
thread_local void* costItem=nullptr;
thread_local void* capabilityItem=nullptr;
thread_local bool extendedCapability=false;
struct Binding { Scope* previous; explicit Binding(Scope* s):previous(active) {active=s;} ~Binding(){active=previous;} };
struct CostBinding { void* previous; explicit CostBinding(void* i):previous(costItem){costItem=i;} ~CostBinding(){costItem=previous;} };
struct CapabilityBinding { void* previous; explicit CapabilityBinding(void* i):previous(capabilityItem){capabilityItem=i;} ~CapabilityBinding(){capabilityItem=previous;} };
bool Checked(const std::function<void()>& work) {
    std::string why; if(game::Guarded(work,&why)) return true;
    ++faults; Log("crafting: refused preparation: %s",why.c_str()); return false;
}
void ForgeDetour(void* self,const il2cpp::Method* method) {
    if(active) { originalForge(self,method); return; }
    Scope scope;
    if(!Checked([&] {
        scope.c=Current(); if(scope.c.craft!=self || !scope.c.main) return;
        for(void* p:{scope.c.actor,self,scope.c.main,scope.c.modifier,scope.c.support,scope.c.storage}) scope.Pin(p);
        scope.factor=fpFactor.load(); scope.hopePercent=hope.load(); scope.sealPercent=despair.load();
        if(preserveShards.load() && scope.c.modifier && Value<bool>(shardCheck,scope.c.modifier)) {
            scope.shard=Get<uint16_t>(scope.c.modifier,itemSubtype); scope.before=Count(scope.c.storage,scope.shard);
        }
        if(preserveRunes.load() && scope.c.modifier && Get<uint8_t>(scope.c.modifier,itemType)==102)
            scope.runeBefore=MaterialCount(scope.c,scope.c.modifier);
        if(preserveGlyphs.load() && scope.c.support && Value<bool>(glyphCheck,scope.c.support))
            scope.glyphBefore=MaterialCount(scope.c,scope.c.support);
        scope.owned=true;
    })) scope.owned=false;
    Binding bind(&scope);
    originalForge(self,method); // Execute precisely one normal craft, including all native eligibility checks.
    if(!scope.owned) return;
    ++crafts;
    if(scope.shard>=0) Checked([&] {
        const auto now=Current(); if(now.actor!=scope.c.actor || now.craft!=self || now.storage!=scope.c.storage) return;
        const int refund=ShardRefund(scope.before,Count(now.storage,scope.shard));
        if(refund) { int id=scope.shard, amount=refund; bool notify=false; void* args[]{&id,&amount,&notify};
            Invoke(Method(now.storage,"AddShard",3),now.storage,args); refunded+=refund;
            Invoke(Method(now.manager,"UpdateCraftingSlotManagerItemInfo",0),now.manager);
        }
    });
    if(scope.runeBefore>=0 || scope.glyphBefore>=0) Checked([&] {
        const auto now=Current();
        if(now.actor!=scope.c.actor || now.manager!=scope.c.manager || now.craft!=self) return;
        const auto refund=[&](void* item,int before,std::atomic<uint64_t>& counter) {
            if(before<0 || !ShardRefund(before,MaterialCount(now,item))) return;
            void* materials=Invoke(Method(now.manager,"get_materials",0),now.manager);
            int amount=1, context=0; bool notify=false; void* args[]{item,&amount,&context,&notify};
            // Resolve the overload by its fourth parameter (the other overload
            // takes a managed out reference, not a bool).
            const auto* cls=il2cpp::api().object_get_class(materials); void* iter=nullptr;
            const il2cpp::Method* add=nullptr;
            while(const auto* m=il2cpp::api().class_get_methods(cls,&iter)) {
                if(std::strcmp(il2cpp::api().method_get_name(m),"TryAddItem") || il2cpp::api().method_get_param_count(m)!=4) continue;
                char* name=il2cpp::api().type_get_name(il2cpp::api().method_get_param(m,3));
                const bool match=name && std::strcmp(name,"System.Boolean")==0;
                if(name) il2cpp::api().free(name); if(match) {add=m;break;}
            }
            if(!add || !Value<bool>(add,materials,args)) throw std::runtime_error("normal material refund refused");
            ++counter;
        };
        refund(scope.c.modifier,scope.runeBefore,refundedRunes);
        refund(scope.c.support,scope.glyphBefore,refundedGlyphs);
        Invoke(Method(now.manager,"UpdateCraftingSlotManagerItemInfo",0),now.manager);
    });
}
bool ExtendedSelection(const Context& c,int id){
    return allowT7.load() && sevenTierAffixes.contains(id) && c.main && c.modifier &&
        Value<bool>(shardCheck,c.modifier) && Get<uint16_t>(c.modifier,itemSubtype)==id &&
        (!c.support || Glyph(c.support,0)); // Other glyphs retain their native tier limits.
}
bool MaxTierDetour(void* item,int id,const il2cpp::Method* method){
    const bool normal=originalMaxTier(item,id,method);
    if(!normal || !extendedCapability || capabilityItem!=item || !game::IsOfflinePlay())return normal;
    int tier=-1;
    if(!Checked([&]{void* args[]{&id};tier=Value<int>(Method(item,"GetAffixTier",1),item,args);} ))return normal;
    return tier<4 || tier>=6;
}
bool AddTierDetour(void* item,int id,bool free,bool reroll,bool preserve,bool sealAffix,bool experimental,bool unpredictable,void** stats,void* results,const il2cpp::Method* method){
    bool handled=false,success=false;
    // T1–T5 and every unrelated crafting operation use the original method.
    // Only the two new upgrades use the game's stat-delta, RNG, FP and serializer APIs.
    const bool prepared=Checked([&]{
        if(!active || !active->owned || active->c.main!=item || !stats || !results || reroll || experimental || unpredictable || !game::IsOfflinePlay() || !ExtendedSelection(active->c,id))return;
        void* selected=nullptr;
        for(void* a:Entries(Invoke(Method(item,"GetNonSealedAffixes",0),item)))if(a&&Get<uint16_t>(a,affixId)==id){selected=a;break;}
        if(!selected)return;
        const int tier=Get<uint8_t>(selected,affixTier);
        if(tier<4 || tier>=6 || Get<uint8_t>(item,forgingPotential)==0)return;
        Root affix(selected),rng(game::StaticObject(craftingRng));
        if(!rng.Get())throw std::runtime_error("crafting RNG unavailable");
        uint8_t newRoll=Get<uint8_t>(selected,affixRoll);
        if(!preserve)newRoll=Value<uint8_t>(randomByte,rng.Get());
        handled=true; // Never fall back/retry after the first possible mutation.
        bool subtract=true;void* beforeArgs[]{selected,&subtract,stats};Invoke(affixStats,item,beforeArgs);
        *reinterpret_cast<uint8_t*>(static_cast<char*>(selected)+affixTier)=static_cast<uint8_t>(tier+1);
        *reinterpret_cast<uint8_t*>(static_cast<char*>(selected)+affixRoll)=newRoll;
        subtract=false;void* afterArgs[]{selected,&subtract,stats};Invoke(affixStats,item,afterArgs);
        int lost=0,next=tier+1;bool insight=false;void* costArgs[]{&next,&free,&insight,&lost};Invoke(shardCost,item,costArgs);
        // AddAffixTierResults is a managed value type: offsets exclude its boxed header.
        std::memset(results,0,12);std::memcpy(results,&lost,4);
        static_cast<uint8_t*>(results)[5]=1;int rerolled=-1;std::memcpy(static_cast<char*>(results)+8,&rerolled,4);
        Invoke(rarityFromAffixes,item);
        // The native normal/magic/rare helper counts affixes, not exalted tiers.
        // Promote ordinary gear with a newly crafted T6/T7; keep all other rarity types.
        if(Get<uint8_t>(item,itemRarity)<=2)*reinterpret_cast<uint8_t*>(static_cast<char*>(item)+itemRarity)=4;
        Invoke(rebuild,item);success=true;
    });
    if(handled)return prepared&&success;
    return originalAddTier(item,id,free,reroll,preserve,sealAffix,experimental,unpredictable,stats,results,method);
}
bool CapabilityDetour(void* self,void** error,bool* warning,bool* green,void** success,const il2cpp::Method* method) {
    void* item=nullptr;
    bool extended=false;
    if(bypassLevel.load()||allowT7.load()) Checked([&] {const auto c=Current(); if(c.craft==self){item=c.main;if(c.modifier)extended=ExtendedSelection(c,Get<uint16_t>(c.modifier,itemSubtype));}});
    CapabilityBinding bind(item);
    const bool previous=extendedCapability;extendedCapability=extended;
    struct Restore {bool value;~Restore(){extendedCapability=value;}} restore{previous};
    return originalCapability(self,error,warning,green,success,method);
}
int LevelDetour(void* item,int affix,int tier,const il2cpp::Method* method) {
    const int normal=originalLevel(item,affix,tier,method);
    if(bypassLevel.load() && capabilityItem==item && item && normal>0 && game::IsOfflinePlay()) {++levelChecks;return 0;}
    return normal;
}
void CostDetour(void* item,int minimum,int maximum,bool free,int* lost,const il2cpp::Method* method) {
    CostBinding bind(active && active->owned && active->c.main==item ? item : nullptr);
    originalCost(item,minimum,maximum,free,lost,method);
}
void SetFPDetour(void* item,int requested,const il2cpp::Method* method) {
    if(active && active->owned && costItem==item && active->c.main==item && active->factor!=1) {
        const int previous=Get<uint8_t>(item,forgingPotential);
        const int changed=RemainingFP(previous,requested,active->factor);
        lastNormalLoss=previous-requested; lastEffectiveLoss=previous-changed;
        if(changed!=requested) { requested=changed; ++fpChanges; }
    }
    originalSetFP(item,requested,method);
}
float SealDetour(void* item,int tier,const il2cpp::Method* method) {
    const float normal=originalSeal(item,tier,method);
    float result=normal;
    if(active && active->owned && active->c.main==item) result=SealChance(normal,tier,active->sealPercent);
    // Normal forge UI also queries this getter outside Forge. Keep its shown chance
    // consistent, while restricting the override to the actual local forge item.
    else if(!active && despair.load()>=0) Checked([&] { const auto c=Current(); if(c.main==item) result=SealChance(normal,tier,despair.load()); });
    if(result!=normal) ++sealChanges;
    return result;
}
bool RollDetour(void* rng,float probability,const il2cpp::Method* method) {
    if(active && active->owned && !active->hopeUsed && active->hopePercent>=0 && probability==0.25f &&
        rng==game::StaticObject(craftingRng) && Glyph(active->c.support,0)) {
        active->hopeUsed=true; probability=static_cast<float>(active->hopePercent/100); ++hopeChanges;
    }
    return originalRoll(rng,probability,method);
}
struct Hook { game::MethodRef m; void* detour; void** original; };
std::vector<Hook> Hooks() {
    std::vector<Hook> result;
    if(fpFactor.load()!=1 || hope.load()>=0 || despair.load()>=0 || preserveShards.load() || preserveRunes.load() || preserveGlyphs.load() || allowT7.load())
        result.push_back({forge,reinterpret_cast<void*>(&ForgeDetour),reinterpret_cast<void**>(&originalForge)});
    if(fpFactor.load()!=1) {
        result.push_back({cost,reinterpret_cast<void*>(&CostDetour),reinterpret_cast<void**>(&originalCost)});
        result.push_back({setFP,reinterpret_cast<void*>(&SetFPDetour),reinterpret_cast<void**>(&originalSetFP)});
    }
    if(despair.load()>=0) result.push_back({seal,reinterpret_cast<void*>(&SealDetour),reinterpret_cast<void**>(&originalSeal)});
    if(hope.load()>=0) result.push_back({roll,reinterpret_cast<void*>(&RollDetour),reinterpret_cast<void**>(&originalRoll)});
    if(bypassLevel.load()||allowT7.load())
        result.push_back({capability,reinterpret_cast<void*>(&CapabilityDetour),reinterpret_cast<void**>(&originalCapability)});
    if(bypassLevel.load()) {
        result.push_back({requiredLevel,reinterpret_cast<void*>(&LevelDetour),reinterpret_cast<void**>(&originalLevel)});
    }
    if(allowT7.load()){
        result.push_back({maxCraftTier,reinterpret_cast<void*>(&MaxTierDetour),reinterpret_cast<void**>(&originalMaxTier)});
        result.push_back({addTier,reinterpret_cast<void*>(&AddTierDetour),reinterpret_cast<void**>(&originalAddTier)});
    }
    return result;
}
void Reconcile() {
    const auto required=Hooks(); std::vector<void*> installed; std::string why;
    try {
        for(const auto& h:required) if(!hook::IsInstalled(h.m.code)) {
            if(!hook::Install(h.m.code,h.detour,h.original,&why)) throw std::runtime_error(why);
            installed.push_back(h.m.code);
        }
        for(const auto& m:{forge,cost,setFP,seal,roll,capability,requiredLevel,maxCraftTier,addTier}) if(std::none_of(required.begin(),required.end(),[&](const Hook& h){return h.m.code==m.code;}) &&
            hook::IsInstalled(m.code) && !hook::Remove(m.code,&why)) throw std::runtime_error(why);
    } catch(...) { for(void* ptr:installed) hook::Remove(ptr,nullptr); throw; }
}
game::MethodRef Resolve(const char* cls,const char* method,int n,const char* ns="") {
    auto m=game::FindMethod("LE.dll",ns,cls,method,n);
    if(!m) throw std::runtime_error(std::string("missing crafting method: ")+cls+'.'+method); return m;
}
std::string Settings() {
    std::ostringstream out;
    out << "{\"ok\":true,\"ready\":" << Boolean(ready) << ",\"fpFactor\":" << fpFactor.load()
        << ",\"hopePercent\":" << (hope.load()<0?"null":std::to_string(hope.load()))
        << ",\"despairPercent\":" << (despair.load()<0?"null":std::to_string(despair.load()))
        << ",\"preserveShards\":" << Boolean(preserveShards.load())
        << ",\"preserveRunes\":" << Boolean(preserveRunes.load()) << ",\"preserveGlyphs\":" << Boolean(preserveGlyphs.load())
        << ",\"allowT7\":" << Boolean(allowT7.load())
        << ",\"bypassLevel\":" << Boolean(bypassLevel.load()) << ",\"hooks\":{\"forge\":" << Boolean(hook::IsInstalled(forge.code))
        << ",\"cost\":" << Boolean(hook::IsInstalled(cost.code)) << ",\"setFP\":" << Boolean(hook::IsInstalled(setFP.code))
        << ",\"seal\":" << Boolean(hook::IsInstalled(seal.code)) << ",\"rng\":" << Boolean(hook::IsInstalled(roll.code))
        << ",\"capability\":" << Boolean(hook::IsInstalled(capability.code)) << ",\"level\":" << Boolean(hook::IsInstalled(requiredLevel.code))
        << "},\"telemetry\":{\"refundedRunes\":" << refundedRunes.load() << ",\"refundedGlyphs\":" << refundedGlyphs.load()
        << ",\"levelChecks\":" << levelChecks.load() << ",\"crafts\":" << crafts.load() << ",\"fpChanges\":" << fpChanges.load()
        << ",\"hopeRolls\":" << hopeChanges.load() << ",\"sealChanges\":" << sealChanges.load()
        << ",\"refundedShards\":" << refunded.load() << ",\"lastNormalFPLoss\":" << lastNormalLoss.load()
        << ",\"lastEffectiveFPLoss\":" << lastEffectiveLoss.load() << ",\"faults\":" << faults.load() << "}}";
    return out.str();
}
std::string Preview() {
    const auto c=Current(); std::ostringstream out;
    out << "{\"player\":{\"id\":" << Json(c.id) << ",\"name\":" << Json(c.name) << "},\"hasItem\":" << Boolean(c.main!=nullptr);
    void* error=nullptr; void* success=nullptr; bool warning=false, green=false;
    void* args[]{&error,&warning,&green,&success};
    const bool can=Value<bool>(Method(c.craft,"CheckForgeCapability",4),c.craft,args);
    out << ",\"canForge\":" << Boolean(can) << ",\"canCustom\":" << Boolean(c.main && Value<bool>(Method(c.main,"isEquipment",0),c.main) && Get<uint8_t>(c.main,itemType)<25) << ",\"message\":" << Json(Text(error)) << ",\"successMessage\":" << Json(Text(success));
    if(c.main) {
        const int fp=Get<uint8_t>(c.main,forgingPotential);
        out << ",\"forgingPotential\":" << fp << ",\"affixes\":[";
        bool first=true;
        if(void* list=Get<void*>(c.main,affixList)) for(void* a:Entries(list)) if(a) {
            if(!first) out<<','; first=false;
            out << "{\"id\":" << Get<uint16_t>(a,affixId) << ",\"tier\":" << (Get<uint8_t>(a,affixTier)+1) << '}';
        }
        out << ']';
        out << ",\"sealedAffixCount\":" << Value<int>(Method(c.main,"get_SealedAffixCount",0),c.main);
        if(c.modifier && Value<bool>(shardCheck,c.modifier)) {
            int id=Get<uint16_t>(c.modifier,itemSubtype);
            void* tierArgs[]{&id}; const int tier=Value<int>(Method(c.main,"GetAffixTier",1),c.main,tierArgs);
            out << ",\"selectedAffix\":" << id << ",\"shardsAvailable\":" << Count(c.storage,id);
            if(!Glyph(c.support,4)) { // Glyph of Insight has a separate, item-dependent formula.
                int next=tier<0?0:tier+1; int rarity=Get<uint8_t>(c.main,itemRarity);
                void* lowerArgs[]{&next}; void* upperArgs[]{&next,&rarity};
                const int low=Value<int>(minCost,nullptr,lowerArgs), high=Value<int>(maxCost,nullptr,upperArgs)-1;
                out << ",\"costRange\":{\"nativeMinimum\":" << std::min(fp,low) << ",\"nativeMaximum\":" << std::min(fp,high)
                    << ",\"effectiveMinimum\":" << fp-RemainingFP(fp,std::max(0,fp-low),fpFactor.load())
                    << ",\"effectiveMaximum\":" << fp-RemainingFP(fp,std::max(0,fp-high),fpFactor.load()) << '}';
            }
            if(Glyph(c.support,3) && tier>=0) {
                void* sealArgs[]{const_cast<int*>(&tier)}; const float normal=hook::IsInstalled(seal.code) ? originalSeal(c.main,tier,seal.info) : Value<float>(seal,c.main,sealArgs);
                out << ",\"sealChance\":{\"nativePercent\":" << normal*100 << ",\"effectivePercent\":" << SealChance(normal,tier,despair.load())*100 << '}';
            }
        }
        if(Glyph(c.support,0)) out << ",\"hopeChance\":{\"nativePercent\":25,\"effectivePercent\":" << (hope.load()<0?25:hope.load()) << '}';
    }
    out << ",\"exactOutcomeKnown\":false}"; return out.str();
}
}
bool Init() {
    try{workshop::Load();}catch(const std::exception& e){Log("workshop recipe read failed: %s",e.what());return false;}
    std::string why; ready=game::Guarded([] {
        getActor=Resolve("PlayerFinder","getPlayerActor",0); offline=Resolve("CharacterData","get_IsOffline",0,"LE.Data");
        forge=Resolve("CraftingManager","Forge",0); cost=Resolve("ItemData","applyForgingPotentialCost",4);
        setFP=Resolve("ItemData","SetForgingPotential",1); seal=Resolve("ItemData","getChanceToSealAffix",1);
        roll=game::FindMethod("LE.Core.dll","LE.Core","RngElement","Roll",1);
        if(!roll) throw std::runtime_error("missing LE.Core.dll RNG method");
        minCost=Resolve("ItemData","getMinForgingPotentialCostFromTier",1);
        maxCost=Resolve("ItemData","getMaxForgingPotentialCostFromTier",2); shardCheck=Resolve("ItemData","IsAffixShard",0);
        capability=Resolve("CraftingManager","CheckForgeCapability",4);
        requiredLevel=Resolve("ItemData","CalculateLevelRequirementAfterShard",2);
        maxCraftTier=Resolve("ItemData","AffixIsAtMaxCraftableTierOrAbove",1);addTier=Resolve("ItemData","AddAffixTier",9);
        affixStats=Resolve("ItemData","AddAffixValueToStatList",3);shardCost=Resolve("ItemData","applyForgingPotentialCostFromShard",4);
        rebuild=Resolve("ItemData","RebuildID",0);rarityFromAffixes=Resolve("ItemData","setRarityFromAffixesForNormalMagicOrRareItem",0);
        randomByte=game::FindMethod("LE.Core.dll","LE.Core","RngElement","RandomByte",0);
        if(!randomByte)throw std::runtime_error("crafting byte RNG unavailable");
        glyphCheck=Resolve("ItemData","IsGlyph",0); getItem=Resolve("OneSlotItemContainer","getItem",0);
        managerInstance=game::FindStaticField("LE.dll","","ItemContainersManager","_instance");
        craftingRng=game::FindStaticField("LE.dll","","CraftingManager","rng");
        if(!managerInstance||!craftingRng) throw std::runtime_error("crafting singleton/RNG unavailable");
        managerActor=Offset("ItemContainersManager","actor"); managerCraft=Offset("ItemContainersManager","craftingManager");
        managerLoaded=Offset("ItemContainersManager","fullyInitialisedAndLoaded"); managerStorage=Offset("ItemContainersManager","shardStorage");
        forgeMain=Offset("CraftingManager","main"); forgeSupport=Offset("CraftingManager","support"); forgeModifier=Offset("CraftingManager","modifier");
        actorTracker=Offset("Actor","characterDataTracker"); trackerData=Offset("CharacterDataTracker","charData");
        forgingPotential=Offset("ItemData","forgingPotential"); affixList=Offset("ItemData","affixes");
        affixId=Offset("ItemAffix","affixId"); affixTier=Offset("ItemAffix","affixTier"); affixRoll=Offset("ItemAffix","affixRoll");
        itemSubtype=Offset("ItemData","subType"); itemRarity=Offset("ItemData","rarity");
        itemType=Offset("ItemData","itemType"); entryData=Offset("ItemContainerEntry","data"); entryQuantity=Offset("ItemContainerEntry","quantity");
        const auto* cls=game::FindClass("LE.dll","","ItemData");void* iter=nullptr;
        while(const auto* m=il2cpp::api().class_get_methods(cls,&iter)) {
            if(std::strcmp(il2cpp::api().method_get_name(m),".ctor")||il2cpp::api().method_get_param_count(m)!=1)continue;
            char* name=il2cpp::api().type_get_name(il2cpp::api().method_get_param(m,0));const bool match=name&&!std::strcmp(name,"System.Byte[]");if(name)il2cpp::api().free(name);
            if(match){itemConstructor.info=m;itemConstructor.code=*reinterpret_cast<void* const*>(m);break;}
        }
        std::string failure;
        if(!itemConstructor||!hook::Install(itemConstructor.code,reinterpret_cast<void*>(&ItemConstructorDetour),reinterpret_cast<void**>(&originalItemConstructor),&failure))throw std::runtime_error("custom equipment reader unavailable: "+failure);
    },&why);
    Log("crafting: %s%s",ready?"metadata ready":"unavailable: ",ready?"":why.c_str()); return ready;
}
std::string Read() {
    return Run([] {
        std::string preview;
        try { preview=",\"preview\":"+Preview(); } catch(const std::exception& e) { preview=",\"preview\":null,\"previewError\":"+Json(e.what()); }
        // Preview may call the scoped level hook. Serialize counters afterwards
        // so the diagnostic response describes this read, not the previous one.
        std::string result=Settings(); result.pop_back();
        const auto version=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Application","get_version",0);
        if(version) result+=",\"gameVersion\":"+Json(Text(Invoke(version)));
        result+=preview;
        return result+'}';
    });
}
std::string Set(const std::string& key,double value) {
    return Run([=] {
        if(!ready) throw std::runtime_error("crafting metadata unavailable");
        const double oldFP=fpFactor.load(),oldHope=hope.load(),oldDespair=despair.load(); const bool oldShards=preserveShards.load();
        const bool oldRunes=preserveRunes.load(),oldGlyphs=preserveGlyphs.load(),oldLevel=bypassLevel.load(),oldT7=allowT7.load();
        if(key=="reset") { fpFactor=1; hope=-1; despair=-1; preserveShards=false; preserveRunes=false; preserveGlyphs=false; bypassLevel=false; allowT7=false; }
        else {
            if(key=="fp" && !Factor(value)) throw std::runtime_error("FP cost factor 0..1");
            if((key=="hope"||key=="despair") && !Chance(value)) throw std::runtime_error("glyph chance 0..100/reset");
            const bool toggle=key=="shards"||key=="runes"||key=="glyphs"||key=="level"||key=="t7";
            if(toggle && value!=0 && value!=1) throw std::runtime_error("crafting toggles use 0 or 1");
            if(key!="fp"&&key!="hope"&&key!="despair"&&!toggle) throw std::runtime_error("unknown crafting setting");
            if((key=="fp"&&value!=1)||((key=="hope"||key=="despair")&&value>=0)||(toggle&&value==1)) (void)Current();
            if(key=="t7"&&value==1){
                std::set<int> known;Root catalog(Invoke(Resolve("AffixList","get",0)));
                for(void* a:Entries(Invoke(Method(catalog.Get(),"get_AllAffixes",0),catalog.Get())))if(a){
                    const int id=Get<int>(a,Offset("AffixList.Affix","affixId"));
                    if(!workshop::Owns(id)&&Entries(Get<void*>(a,Offset("AffixList.Affix","tiers"))).size()>=7)known.insert(id);
                }
                if(known.empty())throw std::runtime_error("seven-tier affix catalog unavailable");sevenTierAffixes=std::move(known);
            }
            if(key=="fp") fpFactor=value; else if(key=="hope") hope=value; else if(key=="despair") despair=value;
            else if(key=="shards") preserveShards=value!=0; else if(key=="runes") preserveRunes=value!=0;
            else if(key=="glyphs") preserveGlyphs=value!=0; else if(key=="t7")allowT7=value!=0;else bypassLevel=value!=0;
        }
        try { Reconcile(); } catch(...) { fpFactor=oldFP; hope=oldHope; despair=oldDespair; preserveShards=oldShards;
            preserveRunes=oldRunes; preserveGlyphs=oldGlyphs; bypassLevel=oldLevel;allowT7=oldT7;
            try {Reconcile();} catch(...) {Log("crafting rollback failed");} throw; }
        return Settings();
    });
}
namespace {
void SyncItems(const Context& c) {
        void* character=Get<void*>(Get<void*>(c.actor,actorTracker),trackerData);
        const auto* cls=il2cpp::api().object_get_class(character);void* iter=nullptr;const il2cpp::Method* saveItems=nullptr;
        while(const auto* m=il2cpp::api().class_get_methods(cls,&iter)) {
            if(std::strcmp(il2cpp::api().method_get_name(m),"SaveItems") || il2cpp::api().method_get_param_count(m)!=1) continue;
            char* name=il2cpp::api().type_get_name(il2cpp::api().method_get_param(m,0));
            const bool match=name && std::strcmp(name,"ItemContainersManager")==0;
            if(name) il2cpp::api().free(name);if(match) {saveItems=m;break;}
        }
        if(!saveItems) throw std::runtime_error("forge item backup serializer unavailable");
        void* saveArgs[]{c.manager};Invoke(saveItems,character,saveArgs);
}
void RequireForge(const Context& c) {
    void* error=nullptr; void* success=nullptr; bool warning=false,green=false;
    void* args[]{&error,&warning,&green,&success};
    if(!Value<bool>(capability,c.craft,args))throw std::runtime_error("normal forge refuses: "+Text(error));
}
}
std::string Forge(const std::string& id) {
    Context prepared{}; progression::SnapshotData snapshot;
    std::vector<std::unique_ptr<Root>> roots;
    const auto captured=Run([&] {
        prepared=Current();
        if(id.empty() || id!=prepared.id)throw std::runtime_error("loaded offline character changed; refresh the forge");
        RequireForge(prepared);
        for(void* p:{prepared.actor,prepared.manager,prepared.craft,prepared.main,prepared.modifier,prepared.support,prepared.storage})
            if(p)roots.push_back(std::make_unique<Root>(p));
        SyncItems(prepared);
        snapshot=progression::CaptureSnapshotCurrent(id,"forge-one-item");
        return std::string("{\"ok\":true}");
    });
    if(captured!="{\"ok\":true}")return captured;
    std::string backup;
    try { backup=progression::WriteCapturedSnapshot(snapshot); }
    catch(const std::exception& error) {return std::string("{\"ok\":false,\"error\":")+Json(error.what())+"}";}
    auto result=Run([&] {
        const auto c=Current();
        if(c.id!=prepared.id || c.name!=prepared.name || c.actor!=prepared.actor || c.manager!=prepared.manager ||
           c.craft!=prepared.craft || c.main!=prepared.main || c.modifier!=prepared.modifier || c.support!=prepared.support)
            throw std::runtime_error("forge selection or character changed while preparing the backup; no craft applied; refresh live");
        SyncItems(c);
        const auto now=progression::CaptureSnapshotCurrent(id,"forge-one-item");
        if(now.character!=snapshot.character || now.stash!=snapshot.stash || now.global!=snapshot.global)
            throw std::runtime_error("character or item data changed while preparing the backup; no craft applied; refresh live");
        RequireForge(c);
        const auto faultsBefore=faults.load();
        Invoke(forge,c.craft); // Precisely one craft; never retry this operation.
        if(faults.load()!=faultsBefore)throw std::runtime_error("crafting hook failed; the craft may have completed; do not repeat it automatically");
        return std::string("{\"ok\":true,\"crafting\":")+Settings()+",\"preview\":"+Preview()+"}";
    });
    // Even a timeout/fault after the commit marker retains recovery. Managed
    // failure does not establish that the original Forge made no changes.
    result.pop_back();return result+",\"backup\":"+Json(backup)+"}";
}
namespace {
void* CloneItem(void* source) {
    const auto* cls=game::FindClass("LE.dll","","ItemDataUnpacked");
    Root copy(il2cpp::api().object_new(cls));const il2cpp::Method* ctor=nullptr;void* iter=nullptr;
    while(const auto* m=il2cpp::api().class_get_methods(cls,&iter)) {
        if(std::strcmp(il2cpp::api().method_get_name(m),".ctor")||il2cpp::api().method_get_param_count(m)!=1)continue;
        char* name=il2cpp::api().type_get_name(il2cpp::api().method_get_param(m,0));const bool match=name && !std::strcmp(name,"System.Byte[]");if(name)il2cpp::api().free(name);if(match){ctor=m;break;}
    }
    void* bytes=Get<void*>(source,Offset("ItemData","id"));void* args[]{bytes};Invoke(ctor,copy.Get(),args);return copy.Get();
}
void Assign(void* item,const char* name,void* value) {
    const auto* f=il2cpp::api().class_get_field_from_name(game::FindClass("LE.dll","","ItemData"),name);
    if(!f)throw std::runtime_error("custom item field unavailable");il2cpp::api().field_set_value(item,f,value);
}
// Affix choices for a custom item (craft_rules.hpp), checked against the
// game's catalog: the affix exists and has the chosen tier.
std::vector<rules::AffixChoice> CustomAffixes(const std::string& input) {
    auto choices=rules::ParseAffixes(input);if(choices.empty())return choices;
    Root affixes(Invoke(Resolve("AffixList","get",0)));Root list(Invoke(Method(affixes.Get(),"get_AllAffixes",0),affixes.Get()));std::map<int,size_t> tiers;
    for(void* a:Entries(list.Get()))if(a){const int id=Get<int>(a,Offset("AffixList.Affix","affixId"));if(!workshop::Owns(id))tiers[id]=Entries(Get<void*>(a,Offset("AffixList.Affix","tiers"))).size();}
    for(const auto& c:choices){
        const auto known=tiers.find(c.id);if(known==tiers.end())throw std::runtime_error("affix is not in this game's catalog");
        if(static_cast<size_t>(c.tier)>known->second)throw std::runtime_error("this affix has no T"+std::to_string(c.tier)+" in the game's catalog");
    }
    return choices;
}
void* CustomItem(void* original,int templateId,const std::string& input,bool perfect,int recipe=-1) {
    if(!original||!Value<bool>(Method(original,"isEquipment",0),original)||Get<uint8_t>(original,itemType)>=25)throw std::runtime_error("select a normal equipment item in the game forge");
    if(templateId<-1||templateId>65535)throw std::runtime_error("invalid Unique/Set template ID");
    Root copy(CloneItem(original));auto choices=CustomAffixes(input);
    void* list=Get<void*>(copy.Get(),affixList);
    if(recipe>=0){
        if(input=="keep")for(void* a:Entries(list)){const int id=Get<uint16_t>(a,affixId);if(!workshop::Owns(id))choices.push_back({id,7,rules::kItemRoll});}
        if(choices.size()>6)throw std::runtime_error("mixed modifier recipe uses one affix slot; select up to six regular affixes");
        choices.push_back({recipe,7,rules::kItemRoll});
    }
    int primordial=-1;size_t regular=0;
    if(input!="keep"||recipe>=0) {
        Invoke(Method(list,"Clear",0),list);
        // The old affixes are gone, sealed ones included.
        uint8_t no=0;for(const char* sealedFlag:{"hasSealedRegularAffix","hasSealedPrimordialAffix","hasSealedAffixFromCorruption"})Assign(copy.Get(),sealedFlag,&no);
        std::mt19937 random(std::random_device{}());
        for(const auto& choice:choices) {
            uint16_t id=static_cast<uint16_t>(choice.id);uint8_t tier=static_cast<uint8_t>(choice.tier-1),affixRoll=rules::RollByte(choice.roll,perfect,static_cast<uint8_t>(random()&0xff)),extra=0;int sealed=0;
            Root affix(il2cpp::api().object_new(game::FindClass("LE.dll","","ItemAffix")));void* args[]{&id,&tier,&affixRoll,&extra,&sealed};Invoke(Method(affix.Get(),".ctor",5),affix.Get(),args);
            void* add[]{affix.Get()};Invoke(Method(list,"Add",1),list,add);
            if(choice.tier==rules::kPrimordialTier)primordial=choice.id;
        }
        // T8 is the game's primordial tier: the game's own method seals the
        // affix (tier index 7, sealed type 2) and marks the item primordial,
        // keeping the chosen roll. Without a T8 the item is not primordial.
        regular=choices.size()-(primordial>=0?1:0);
        if(primordial>=0)for(void* a:Entries(list))if(a&&Get<uint16_t>(a,affixId)==primordial){
            bool keepRoll=true;void* args[]{a,&keepRoll};Invoke(Method(copy.Get(),"MakeAffixSealedPrimordialAffix",2),copy.Get(),args);break;
        }
        list=Get<void*>(copy.Get(),affixList);
    } else if(perfect)for(void* a:Entries(list))if(a)*reinterpret_cast<uint8_t*>(static_cast<char*>(a)+Offset("ItemAffix","affixRoll"))=255;
    // An item with a sealed primordial affix gets its rarity from the game
    // (ItemData.setValuesFromSerialisation): the regular affixes, at most 4.
    uint16_t unique=templateId<0?0:static_cast<uint16_t>(templateId);uint8_t rarity=primordial>=0?static_cast<uint8_t>(std::min<size_t>(regular,4)):4;
    if(templateId>=0) {
        void* args[]{&unique};void* found=Invoke(Resolve("UniqueList","getUnique",1),nullptr,args);if(!found)throw std::runtime_error("Unique/Set template is unavailable in this game version");Root entry(found);
        if(Get<uint16_t>(entry.Get(),Offset("UniqueList.Entry","uniqueID"))!=unique||Get<bool>(entry.Get(),Offset("UniqueList.Entry","hideFromPlayers")))throw std::runtime_error("select a public Unique/Set template");
        rarity=Get<bool>(entry.Get(),Offset("UniqueList.Entry","isSetItem"))?8:9;
        // The native item v6 serializer writes eight unique roll bytes,
        // including unused slots. A shorter array cannot be saved or reloaded.
        uintptr_t length=8;for(void* mod:Entries(Get<void*>(entry.Get(),Offset("UniqueList.Entry","mods"))))if(mod&&Get<bool>(mod,Offset("UniqueItemMod","canRoll"))&&Get<uint8_t>(mod,Offset("UniqueItemMod","rollID"))>=length)throw std::runtime_error("template roll layout exceeds the native item format");
        Root rolls(il2cpp::api().array_new(game::FindClass("mscorlib.dll","System","Byte"),length));if(length)std::memset(static_cast<char*>(rolls.Get())+0x20,perfect?255:128,length);
        Assign(copy.Get(),"uniqueRolls",rolls.Get());
    }
    Assign(copy.Get(),"uniqueID",&unique);Assign(copy.Get(),"rarity",&rarity);
    uint8_t zero=0;for(const char* field:{"legendaryPotential","weaversWill","weaversTouch"})Assign(copy.Get(),field,&zero);
    if(perfect){void* rolls=Get<void*>(copy.Get(),Offset("ItemData","implicitRolls"));if(rolls){const auto n=Get<uintptr_t>(rolls,0x18);if(n>32)throw std::runtime_error("invalid implicit roll data");std::memset(static_cast<char*>(rolls)+0x20,255,n);}}
    Invoke(Method(copy.Get(),"RebuildID",0),copy.Get());
    // Prove the native save format can round-trip the proposed item before a
    // live container is modified. No sidecar-only or invalid affix encoding.
    Root loaded(CloneItem(copy.Get()));
    if(Get<uint8_t>(loaded.Get(),itemType)!=Get<uint8_t>(original,itemType)||Get<uint16_t>(loaded.Get(),itemSubtype)!=Get<uint16_t>(original,itemSubtype)||Get<uint8_t>(loaded.Get(),itemRarity)!=rarity||Get<uint16_t>(loaded.Get(),Offset("ItemData","uniqueID"))!=unique||Entries(Get<void*>(loaded.Get(),affixList)).size()!=Entries(list).size())throw std::runtime_error("custom item round-trip mismatch: type "+std::to_string(Get<uint8_t>(loaded.Get(),itemType))+"/"+std::to_string(Get<uint8_t>(original,itemType))+", subtype "+std::to_string(Get<uint16_t>(loaded.Get(),itemSubtype))+"/"+std::to_string(Get<uint16_t>(original,itemSubtype))+", rarity "+std::to_string(Get<uint8_t>(loaded.Get(),itemRarity))+"/"+std::to_string(rarity)+", template "+std::to_string(Get<uint16_t>(loaded.Get(),Offset("ItemData","uniqueID")))+"/"+std::to_string(unique)+", affixes "+std::to_string(Entries(Get<void*>(loaded.Get(),affixList)).size())+"/"+std::to_string(Entries(list).size())+"; nothing was applied");
    const auto before=Entries(list),after=Entries(Get<void*>(loaded.Get(),affixList));
    for(size_t i=0;i<before.size();++i)for(const auto offset:{affixId,affixTier,Offset("ItemAffix","affixRoll")}) {
        const bool same=offset==affixId?Get<uint16_t>(before[i],offset)==Get<uint16_t>(after[i],offset):Get<uint8_t>(before[i],offset)==Get<uint8_t>(after[i],offset);
        if(!same)throw std::runtime_error("the game changed a custom affix during round-trip; nothing was applied");
    }
    const auto sealedType=Offset("ItemAffix","sealedAffixType"),primordialFlag=Offset("ItemData","hasSealedPrimordialAffix");
    for(size_t i=0;i<before.size();++i)if(Get<int>(before[i],sealedType)!=Get<int>(after[i],sealedType))throw std::runtime_error("the game changed a sealed custom affix during round-trip; nothing was applied");
    if(Get<bool>(copy.Get(),primordialFlag)!=Get<bool>(loaded.Get(),primordialFlag))throw std::runtime_error("the game changed the primordial state during round-trip; nothing was applied");
    return loaded.Get();
}
}
std::string Custom(const std::string& id,int templateId,const std::string& affixes,bool perfect,const std::string& modifiers) {
    Context prepared{};progression::SnapshotData snapshot;std::vector<std::unique_ptr<Root>> roots;std::unique_ptr<Root> replacement;
    const auto captured=Run([&]{prepared=Current();if(id!=prepared.id)throw std::runtime_error("character changed; refresh the forge");
        int recipe=-1;if(!modifiers.empty()&&modifiers!="none"){
            if(!prepared.main)throw std::runtime_error("select a forge item first");
            Root itemList(Invoke(Resolve("ItemList","get",0)));int type=Get<uint8_t>(prepared.main,itemType),sub=Get<uint16_t>(prepared.main,itemSubtype);void* args[]{&type,&sub};
            recipe=workshop::Recipe(modifiers,perfect,Value<float>(Method(itemList.Get(),"getAffixEffectModifier",2),itemList.Get(),args));
        }
        replacement=std::make_unique<Root>(CustomItem(prepared.main,templateId,affixes,perfect,recipe));for(void* p:{prepared.actor,prepared.manager,prepared.craft,prepared.main})roots.push_back(std::make_unique<Root>(p));SyncItems(prepared);snapshot=progression::CaptureSnapshotCurrent(id,"custom-forge-item");return std::string("{\"ok\":true}");});
    if(captured!="{\"ok\":true}")return captured;
    std::string backup;try{backup=progression::WriteCapturedSnapshot(snapshot);}catch(const std::exception& e){return "{\"ok\":false,\"error\":"+Json(e.what())+"}";}
    auto result=Run([&]{
        auto c=Current();if(c.id!=prepared.id||c.name!=prepared.name||c.actor!=prepared.actor||c.main!=prepared.main||c.craft!=prepared.craft)throw std::runtime_error("forge selection changed while making the backup; nothing applied");
        SyncItems(c);const auto now=progression::CaptureSnapshotCurrent(id,"custom-forge-item");if(now.character!=snapshot.character||now.stash!=snapshot.stash||now.global!=snapshot.global)throw std::runtime_error("save data changed while making the backup; nothing applied");
        void* container=Get<void*>(c.craft,forgeMain);const auto entries=Entries(Invoke(Method(container,"GetContent",0),container));if(entries.size()!=1)throw std::runtime_error("expected one forge item");Root entry(entries[0]);int amount=1,context=0;void* remove[]{entry.Get(),&amount,&context};
        if(!Value<bool>(Method(container,"TryRemoveItem",3),container,remove))throw std::runtime_error("normal forge removal refused");
        bool committed=false;
        try {void* add[]{replacement->Get(),&amount,&context};if(!Value<bool>(Method(container,"TryAddItem",3),container,add))throw std::runtime_error("normal forge insertion refused");committed=true;SyncItems(Current());progression::SaveCurrent(id);}
        catch(...) {
            const auto failure=std::current_exception();
            try {
                if(committed){auto current=Entries(Invoke(Method(container,"GetContent",0),container));
                    if(current.size()!=1 || Get<void*>(current[0],entryData)!=replacement->Get())throw std::runtime_error("forge selection changed during rollback");
                    void* undo[]{current[0],&amount,&context};
                    if(!Value<bool>(Method(container,"TryRemoveItem",3),container,undo))throw std::runtime_error("replacement removal refused");
                }
                void* restore[]{prepared.main,&amount,&context};
                if(!Value<bool>(Method(container,"TryAddItem",3),container,restore))throw std::runtime_error("original item insertion refused");
                SyncItems(Current());progression::SaveCurrent(id);
            } catch(const std::exception& e){Log("custom forge rollback incomplete: %s; recovery backup: %s",e.what(),backup.c_str());}
            catch(...){Log("custom forge rollback incomplete; recovery backup: %s",backup.c_str());}
            std::rethrow_exception(failure);
        }
        return "{\"ok\":true,\"preview\":"+Preview()+"}";
    });result.pop_back();return result+",\"backup\":"+Json(backup)+"}";
}
namespace {
struct MaterialInfo { int type, id, count; std::string name; };
struct MaterialOwner { void* tracker; void* stash; void* container; void* materials; };
MaterialOwner Owner(const Context& c) {
    void* tracker=Get<void*>(c.actor,Offset("Actor","globalDataTracker"));
    void* stash=Get<void*>(tracker,Offset("GlobalDataTracker","<stash>k__BackingField"));
    void* holder=Get<void*>(c.manager,Offset("ItemContainersManager","stash"));
    void* container=Invoke(Method(holder,"get_ActiveContainer",0),holder);
    void* materials=Invoke(Method(c.manager,"get_materials",0),c.manager);
    if(c.storage!=tracker || !stash || Get<void*>(container,Offset("StashItemContainer","<LinkedStash>k__BackingField"))!=stash ||
       Invoke(Method(container,"get_Materials",0),container)!=materials)
        throw std::runtime_error("crafting material stash ownership could not be verified");
    return {tracker,stash,container,materials};
}
void SyncMaterials(const Context& c) {
    const auto owner=Owner(c);void* args[]{owner.container};Invoke(Method(owner.tracker,"SaveMaterials",1),owner.tracker,args);
    void* dirty[]{owner.stash};Invoke(Resolve("Stash","MarkDirty",1,"LE.Data"),nullptr,dirty);
    SyncItems(c);
}
int SafeCount(int count) {
    if(count<0 || count>100000000)throw std::runtime_error("invalid crafting material count");return count;
}
std::vector<MaterialInfo> MaterialCatalog(const Context& c) {
    std::vector<MaterialInfo> out;std::set<std::pair<int,int>> seen;
    const auto append=[&](int type,int id,std::string name) {
        if(id<0 || id>65535 || !seen.emplace(type,id).second || name.empty())throw std::runtime_error("invalid material catalog entry");
        out.push_back({type,id,SafeCount(type==101?Count(c.storage,id):MaterialCount(c,type,id)),std::move(name)});
    };
    Root affixes(Invoke(Resolve("AffixList","get",0)));
    for(void* a:Entries(Invoke(Method(affixes.Get(),"get_AllAffixes",0),affixes.Get()))) {
        if(a && !workshop::Owns(Get<int>(a,Offset("AffixList.Affix","affixId"))) && Value<bool>(Method(a,"get_HasShard",0),a))
            append(101,Get<int>(a,Offset("AffixList.Affix","affixId")),Text(Invoke(Method(a,"getAffixDisplayName",0),a)));
    }
    Root items(Invoke(Resolve("ItemList","get",0)));
    for(int type:{102,103}) {
        // GetAllSubTypesIDs covers equipment only; non-equipment materials
        // have their own catalog and must not silently disappear from stock.
        void* args[]{&type};Root list(Invoke(Method(items.Get(),"GetNonEquipmentSubItems",1),items.Get(),args));
        const auto subs=Entries(list.Get());if(subs.empty() || subs.size()>1024)throw std::runtime_error("material subtype catalog unavailable");
        const auto before=out.size();
        for(void* entry:subs) {
            Root sub(entry);int id=Get<int>(sub.Get(),Offset("ItemList.SubItem","subTypeID"));void* subArgs[]{&type,&id};
            if(Value<bool>(Method(items.Get(),"IsItemTypeObsolete",2),items.Get(),subArgs) ||
               Get<bool>(sub.Get(),Offset("ItemList.SubItem","isLegacySubType")) || Get<bool>(sub.Get(),Offset("ItemList.SubItem","obsoleteItem")))continue;
            append(type,id,Text(Invoke(Method(items.Get(),"GetItemDisplayName",2),items.Get(),subArgs)));
        }
        if(before==out.size())throw std::runtime_error("no usable materials in subtype catalog");
    }
    if(out.empty() || out.size()>4096)throw std::runtime_error("crafting material catalog unavailable");
    return out;
}
std::string MaterialsJson(const Context& c,const std::vector<MaterialInfo>& list) {
    std::ostringstream out;int shards=0,runes=0,glyphs=0;
    for(const auto& x:list){if(x.type==101)++shards;else if(x.type==102)++runes;else ++glyphs;}
    out<<"{\"ok\":true,\"player\":{\"id\":"<<Json(c.id)<<",\"name\":"<<Json(c.name)<<"},\"kinds\":{\"shards\":"<<shards<<",\"runes\":"<<runes<<",\"glyphs\":"<<glyphs<<"},\"materials\":[";
    bool comma=false;for(const auto& x:list){if(comma)out<<',';comma=true;out<<"{\"type\":"<<x.type<<",\"id\":"<<x.id<<",\"name\":"<<Json(x.name)<<",\"count\":"<<x.count<<'}';}
    return out.str()+"]}";
}
// Generate only catalog-validated materials through the same native generator
// used by the game. No custom gear headers or save-file patches are involved.
void* GenerateMaterial(const Context& c,int type,int subtype) {
    void* generator=Get<void*>(c.actor,Offset("Actor","generateItems"));
    bool no=false;int level=8,location=0,rarity=0,sockets=0,unique=-1,lp=0,rank=0,legendaryType=0,corrupt=0;
    alignas(8) unsigned char faction[8]{},replacement[8]{},corruption[8]{},effectiveness[8]{},special[8]{},minSpecial[8]{},overrideLP[8]{};
    void* args[]{&no,&level,&no,&location,&type,&subtype,&rarity,&sockets,&unique,&no,&lp,faction,&rank,&no,&legendaryType,&no,
        replacement,corruption,&corrupt,effectiveness,nullptr,nullptr,special,&no,minSpecial,overrideLP};
    return Invoke(Resolve("GenerateItems","initialiseRandomItemData",26),generator,args);
}
const il2cpp::Method* AddMaterialMethod(void* materials) {
    const auto& api=il2cpp::api();void* iter=nullptr;
    while(const auto* m=api.class_get_methods(api.object_get_class(materials),&iter)) {
        if(std::strcmp(api.method_get_name(m),"TryAddItem") || api.method_get_param_count(m)!=4)continue;
        char* type=api.type_get_name(api.method_get_param(m,3));const bool match=type && !std::strcmp(type,"System.Boolean");
        if(type)api.free(type);if(match)return m;
    }
    throw std::runtime_error("native material storage method unavailable");
}
}
std::string MaterialsRead() {
    return Run([]{const auto c=Current();(void)Owner(c);return MaterialsJson(c,MaterialCatalog(c));});
}
std::string MaximizeMaterials(const std::string& id,int target) {
    if(id.empty() || id.find_first_not_of("0123456789")!=std::string::npos || target<1 || target>1000000)
        return "{\"ok\":false,\"error\":\"use a loaded offline save ID and a target from 1 to 1000000\"}";
    Context prepared{};MaterialOwner owner{};std::vector<MaterialInfo> plan;progression::SnapshotData snapshot;
    std::vector<std::unique_ptr<Root>> roots;const il2cpp::Method* add=nullptr;
    const auto captured=Run([&]{
        prepared=Current();if(prepared.id!=id)throw std::runtime_error("loaded offline character changed; refresh materials");
        owner=Owner(prepared);add=AddMaterialMethod(owner.materials);plan=MaterialCatalog(prepared);
        for(void* obj:{prepared.actor,prepared.manager,prepared.craft,prepared.storage,owner.stash,owner.container,owner.materials})roots.push_back(std::make_unique<Root>(obj));
        SyncMaterials(prepared);snapshot=progression::CaptureSnapshotCurrent(id,"maximize-crafting-materials");
        return std::string("{\"ok\":true}");
    });if(captured!="{\"ok\":true}")return captured;
    std::string backup;
    try{backup=progression::WriteCapturedSnapshot(snapshot);}catch(const std::exception& e){return "{\"ok\":false,\"error\":"+Json(e.what())+"}";}
    size_t index=0;int updated=0;int64_t added=0;bool checked=false;std::string result,why;
    const bool ran=mainthread::RunSteps([&]{
        try{
            const auto c=Current();const auto now=Owner(c);
            if(c.id!=prepared.id || c.name!=prepared.name || c.actor!=prepared.actor || c.manager!=prepared.manager ||
               c.craft!=prepared.craft || now.stash!=owner.stash || now.container!=owner.container || now.materials!=owner.materials)
                throw std::runtime_error("character or material stash changed; no further additions made");
            if(!checked){
                for(const auto& x:plan)if(SafeCount(x.type==101?Count(c.storage,x.id):MaterialCount(c,x.type,x.id))!=x.count)
                    throw std::runtime_error("crafting materials changed while backing up; no additions made; refresh live");
                checked=true;
            }
            const auto end=std::min(plan.size(),index+8);
            for(;index<end;++index){
                auto& x=plan[index];const int before=SafeCount(x.type==101?Count(c.storage,x.id):MaterialCount(c,x.type,x.id));
                int amount=std::max(0,target-before);if(!amount){x.count=before;continue;}
                if(x.type==101){int shard=x.id;bool notify=false;void* args[]{&shard,&amount,&notify};Invoke(Method(c.storage,"AddShard",3),c.storage,args);}
                else{
                    Root item(GenerateMaterial(c,x.type,x.id));
                    if(Get<uint8_t>(item.Get(),itemType)!=x.type || Get<uint16_t>(item.Get(),itemSubtype)!=x.id)throw std::runtime_error("native generator returned a different material");
                    int context=0;bool notify=false;void* args[]{item.Get(),&amount,&context,&notify};
                    if(!Value<bool>(add,owner.materials,args))throw std::runtime_error("native material storage refused addition; earlier additions may remain");
                }
                const int after=SafeCount(x.type==101?Count(c.storage,x.id):MaterialCount(c,x.type,x.id));
                if(after!=before+amount)throw std::runtime_error("native material quantity verification failed; use the recovery backup");
                x.count=after;++updated;added+=amount;
            }
            if(index<plan.size())return false;
            SyncMaterials(c);progression::SaveCurrent(id);Invoke(Method(c.manager,"UpdateCraftingSlotManagerItemInfo",0),c.manager);
            result=MaterialsJson(c,plan);result.pop_back();result+=",\"target\":"+std::to_string(target)+",\"updatedKinds\":"+std::to_string(updated)+",\"addedUnits\":"+std::to_string(added)+"}";
        }catch(const std::exception& e){result="{\"ok\":false,\"error\":"+Json(e.what())+",\"updatedKinds\":"+std::to_string(updated)+",\"addedUnits\":"+std::to_string(added)+"}";}
        return true;
    },5000,&why);
    if(!ran)result="{\"ok\":false,\"error\":"+Json(why)+"}";
    if(result.empty())result="{\"ok\":false,\"error\":\"material operation failed; use the recovery backup\"}";
    result.pop_back();return result+",\"backup\":"+Json(backup)+"}";
}
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
namespace {
void* Fixture(const Context& c,int type,int subtype,int rarity,bool exalted=false,int unique=-1,int forcedLP=-1,int level=8) {
    void* generator=Get<void*>(c.actor,Offset("Actor","generateItems"));
    bool no=false; int location=0, sockets=0, lp=0, rank=0, legendaryType=0, corrupt=0;
    alignas(8) unsigned char faction[8]{}, replacement[8]{}, corruption[8]{}, effectiveness[8]{}, special[8]{}, minSpecial[8]{}, overrideLP[8]{};
    if(forcedLP>=0) {overrideLP[0]=1; std::memcpy(overrideLP+4,&forcedLP,sizeof(forcedLP));}
    void* args[]{&no,&level,&no,&location,&type,&subtype,&rarity,&sockets,&unique,&exalted,&lp,faction,&rank,&no,&legendaryType,&no,
        replacement,corruption,&corrupt,effectiveness,nullptr,nullptr,special,&no,minSpecial,overrideLP};
    return Invoke(Resolve("GenerateItems","initialiseRandomItemData",26),generator,args);
}
}
std::string Test(const std::string& id,const std::string& action) {
    if(action.rfind("visit:",0)==0) {
        // Waypoints queues its own main-thread job; never nest that job in Run.
        const auto permission=Run([=] {
            const auto c=Current();
            if(c.id!=id || c.name!="EpCraftTest") throw std::runtime_error("visit requires isolated EpCraftTest and matching id");
            return std::string("{\"ok\":true}");
        });
        if(permission!="{\"ok\":true}")return permission;
        const auto result=statedit::Waypoints(action.substr(6),id);
        const bool ok=result.rfind("travel: normal waypoint transition requested",0)==0;
        return std::string("{\"ok\":")+Boolean(ok)+(ok?",\"transition\":":",\"error\":")+Json(result)+"}";
    }
    return Run([=] {
        const auto c=Current();
        if(c.id!=id || (c.name!="EpCraftTest" && !(c.name=="EpFeatureTest" && (action=="customsetup"||action=="custominspect"||action=="customstats"||action=="custom-equip"||action=="clear"||action.rfind("feature-",0)==0)))) throw std::runtime_error("craft probe requires an isolated test clone and matching id");
        if(action=="custom-equip"){
            if(!c.main||Get<uint8_t>(c.main,itemType)!=0)throw std::runtime_error("test helmet required");Root item(c.main);
            void* paper=Get<void*>(c.manager,Offset("ItemContainersManager","equipment"));void* slot=Get<void*>(paper,Offset("ItemContainersManager.PaperDollContainer","helmet"));
            void* character=Get<void*>(Get<void*>(c.actor,actorTracker),trackerData);int* level=reinterpret_cast<int*>(static_cast<char*>(character)+Offset("CharacterData","<Level>k__BackingField","LE.Data"));
            struct Level{int* p;int old;~Level(){*p=old;}} restore{level,*level};*level=100;
            int amount=1,context=0;const auto prior=Entries(Invoke(Method(slot,"GetContent",0),slot));
            if(!prior.empty()){void* old=Get<void*>(prior[0],entryData);void* drop[]{c.actor,old,&amount};if(!Value<bool>(Method(c.manager,"DropItem",3),c.manager,drop))throw std::runtime_error("test helmet drop refused");void* remove[]{prior[0],&amount,&context};if(!Value<bool>(Method(slot,"TryRemoveItem",3),slot,remove))throw std::runtime_error("test helmet removal refused");}
            void* container=Get<void*>(c.craft,forgeMain);const auto entries=Entries(Invoke(Method(container,"GetContent",0),container));void* remove[]{entries.at(0),&amount,&context};if(!Value<bool>(Method(container,"TryRemoveItem",3),container,remove))throw std::runtime_error("test forge removal refused");
            void* add[]{item.Get(),&amount,&context};if(!Value<bool>(Method(slot,"TryAddItem",3),slot,add))throw std::runtime_error("test equipment insertion refused");
            return std::string("{\"ok\":true,\"equipped\":true}");
        }
        if(action=="customstats"){
            if(!c.main)throw std::runtime_error("select test forge item first");void* actorStats=Get<void*>(c.actor,Offset("Actor","stats"));void* old=Get<void*>(actorStats,Offset("Stats","stats"));
            Root list(il2cpp::api().object_new(il2cpp::api().object_get_class(old)));Invoke(Method(list.Get(),".ctor",0),list.Get());void* current=list.Get();bool tooltip=false;
            for(void* a:Entries(Get<void*>(c.main,affixList))){void* args[]{a,&tooltip,&current};Invoke(Method(c.main,"AddAffixValueToStatList",3),c.main,args);}
            Root result(current);std::ostringstream out;out<<"{\"ok\":true,\"stats\":[";bool comma=false;
            for(void* s:Entries(current)){if(comma)out<<',';comma=true;out<<"{\"sp\":"<<int(Get<uint8_t>(s,Offset("Stats.Stat","property")))<<",\"added\":"<<Get<float>(s,Offset("Stats.Stat","addedValue"))<<",\"increased\":"<<Get<float>(s,Offset("Stats.Stat","increasedValue"))<<'}';}return out.str()+"]}";
        }
        if(action=="custominspect") {
            if(!c.main)throw std::runtime_error("test forge selection required");std::ostringstream out;out<<"{\"ok\":true,\"type\":"<<int(Get<uint8_t>(c.main,itemType))<<",\"rarity\":"<<int(Get<uint8_t>(c.main,itemRarity))<<",\"uniqueId\":"<<Get<uint16_t>(c.main,Offset("ItemData","uniqueID"))<<",\"bytes\":[";
            void* bytes=Get<void*>(c.main,Offset("ItemData","id"));const auto count=Get<uintptr_t>(bytes,0x18);if(count>1024)throw std::runtime_error("invalid test item size");for(uintptr_t i=0;i<count;++i){if(i)out<<',';out<<int(Get<uint8_t>(bytes,0x20+i));}out<<"],\"affixes\":[";bool next=false;
            for(void* a:Entries(Get<void*>(c.main,affixList))){if(next)out<<',';next=true;out<<"{\"id\":"<<Get<uint16_t>(a,affixId)<<",\"tier\":"<<int(Get<uint8_t>(a,affixTier))+1<<",\"roll\":"<<int(Get<uint8_t>(a,Offset("ItemAffix","affixRoll")))<<",\"sealed\":"<<Get<int>(a,Offset("ItemAffix","sealedAffixType"))<<'}';}return out.str()+"],\"primordial\":"+Boolean(Get<bool>(c.main,Offset("ItemData","hasSealedPrimordialAffix")))+"}";
        }
        if(action=="roundtrip"){
            if(!c.main)throw std::runtime_error("test forge selection required");Root copy(CloneItem(c.main));
            const auto original=Entries(Get<void*>(c.main,affixList)),loaded=Entries(Get<void*>(copy.Get(),affixList));
            if(original.size()!=loaded.size()||Get<uint8_t>(copy.Get(),itemRarity)!=Get<uint8_t>(c.main,itemRarity))throw std::runtime_error("serialized rarity/affix count mismatch");
            for(size_t i=0;i<original.size();++i)if(Get<uint16_t>(original[i],affixId)!=Get<uint16_t>(loaded[i],affixId)||Get<uint8_t>(original[i],affixTier)!=Get<uint8_t>(loaded[i],affixTier)||Get<uint8_t>(original[i],affixRoll)!=Get<uint8_t>(loaded[i],affixRoll))throw std::runtime_error("serialized affix mismatch");
            return std::string("{\"ok\":true,\"roundtrip\":true,\"rarity\":")+std::to_string(Get<uint8_t>(copy.Get(),itemRarity))+"}";
        }
        if(action.rfind("feature-",0)==0) {
            void* equipment=Get<void*>(c.manager,Offset("ItemContainersManager","equipment"));const bool main=action=="feature-main-staff",set=action=="feature-set";
            const char* slotName=set?"amulet":main?"weapon":"offhand";void* slot=Get<void*>(equipment,Offset("ItemContainersManager.PaperDollContainer",slotName));
            const int type=set?20:main||action=="feature-off-staff"?15:action=="feature-off-shield"?18:action=="feature-off-catalyst"?19:-1;if(type<0)throw std::runtime_error("unknown equipment test");
            Root item(Fixture(c,type,0,2,false,-1,-1,8));std::unique_ptr<Root> custom;if(set)custom=std::make_unique<Root>(CustomItem(item.Get(),57,"keep",true));
            const auto current=Entries(Invoke(Method(slot,"GetContent",0),slot));int amount=1,context=0;
            if(!current.empty()) {Root entry(current.at(0));void* old=Get<void*>(entry.Get(),entryData);void* drop[]{c.actor,old,&amount};if(!Value<bool>(Method(c.manager,"DropItem",3),c.manager,drop))throw std::runtime_error("test equipment drop refused");void* remove[]{entry.Get(),&amount,&context};if(!Value<bool>(Method(slot,"TryRemoveItem",3),slot,remove))throw std::runtime_error("test equipment removal refused");}
            void* args[]{set?custom->Get():item.Get(),&amount,&context};const bool accepted=Value<bool>(Method(slot,"TryAddItem",3),slot,args);
            return "{\"ok\":true,\"accepted\":"+std::string(Boolean(accepted))+",\"slot\":"+Json(slotName)+",\"class\":"+Json(il2cpp::api().class_get_name(il2cpp::api().object_get_class(slot)))+",\"type\":"+std::to_string(type)+"}";
        }
        if(action=="customsetup") {
            if(c.main)throw std::runtime_error("empty the test forge first");
            Root item(Fixture(c,0,0,4,true,-1,-1,8));int amount=1,context=0;void* args[]{item.Get(),&amount,&context};void* container=Get<void*>(c.craft,forgeMain);
            if(!Value<bool>(Method(container,"TryAddItem",3),container,args))throw std::runtime_error("normal test forge insertion refused");
            SyncItems(Current());progression::SaveCurrent(id);return "{\"ok\":true,\"preview\":"+Preview()+"}";
        }
        if(action=="metadata") {
            std::ostringstream out; out << "{\"ok\":true,\"storageClass\":" << Json(il2cpp::api().class_get_name(il2cpp::api().object_get_class(c.storage))) << ",\"parameters\":[";
            const auto m=Resolve("GenerateItems","initialiseRandomItemData",26); const auto& a=il2cpp::api();
            for(uint32_t i=0;i<26;++i) {if(i) out<<','; out<<Json(a.method_get_param_name(m.info,i));}
            out << "]}"; return out.str();
        }
        if(action=="stashunique0" || action=="stashunique2") {
            (void)progression::SnapshotFixtureCurrent(id,"collection-test-stash");
            Root item(Fixture(c,-1,-1,7,false,1,action=="stashunique0"?0:2,55));
            void* holder=Get<void*>(c.manager,Offset("ItemContainersManager","stash"));
            Root stash(Invoke(Method(holder,"get_CurrentContainer",0),holder));
            const auto tabs=Entries(Get<void*>(stash.Get(),Offset("TabbedItemContainer","containers")));
            if(tabs.empty()) throw std::runtime_error("test stash has no tab");
            int amount=1,context=0; void* args[]{item.Get(),&amount,&context};
            if(!Value<bool>(Method(tabs.front(),"TryAddItem",3),tabs.front(),args))
                throw std::runtime_error("normal stash insertion refused");
            return std::string("{\"ok\":true,\"fixture\":")+Json(action)+"}";
        }
        if(action=="unique0" || action=="unique2" || action=="t7") {
            Root transform(Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Component","get_transform",0),c.actor));
            struct Position {float x,y,z;} position=Value<Position>(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Transform","get_position",0),transform.Get());
            int scene=Value<int>(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine.SceneManagement","SceneManager","GetActiveScene",0));
            void* generator=Get<void*>(c.actor,Offset("Actor","generateItems"));
            for(int attempt=0;attempt<(action=="t7"?128:1);++attempt) {
                // initialiseRandomItemData takes raw Item.rarity values, not
                // either UI RarityGroup or achievement ItemRarity enum IDs.
                Root item(action=="t7" ? Fixture(c,0,0,4,true,-1,-1,100) : Fixture(c,-1,-1,7,false,1,action=="unique0"?0:2,55));
                if(action!="t7" && (!Value<bool>(Method(item.Get(),"isUnique",0),item.Get()) ||
                   Get<uint8_t>(item.Get(),Offset("ItemData","legendaryPotential"))!=(action=="unique0"?0:2)))
                    throw std::runtime_error("native unique fixture generation mismatch");
                bool found=action!="t7";
                if(void* list=Get<void*>(item.Get(),affixList)) for(void* a:Entries(list)) if(a && Get<uint8_t>(a,affixTier)==6) found=true;
                if(!found) continue;
                void* dropArgs[]{item.Get(),&position,&scene}; Invoke(Resolve("GenerateItems","DropItemAtPoint",3),generator,dropArgs);
                return "{\"ok\":true,\"fixture\":"+Json(action)+",\"attempts\":"+std::to_string(attempt+1)+"}";
            }
            throw std::runtime_error("native generation did not produce a T7 fixture in bounded attempts");
        }
        if(action=="zerofp"){
            if(!c.main)throw std::runtime_error("test forge item required");int fp=0;void* args[]{&fp};Invoke(setFP,c.main,args);Invoke(rebuild,c.main);return "{\"ok\":true,\"preview\":"+Preview()+"}";
        }
        if(action=="setup" || action=="lowsetup" || action=="t7setup") {
            (void)progression::SnapshotFixtureCurrent(id,"crafting-test-setup");
            // Empty through the normal return-to-inventory APIs. This command is
            // compile-time research only and refuses every owner character.
            const auto mainContainer=Get<void*>(c.craft,forgeMain);
            if(c.main) throw std::runtime_error("remove the previous test item from the forge first");
            void* tracker=Get<void*>(c.actor,actorTracker); void* character=Get<void*>(tracker,trackerData);
            if(action!="lowsetup" && Get<int>(character,Offset("CharacterData","<Level>k__BackingField","LE.Data"))<(action=="t7setup"?90:55)) {
                int64_t target=0;
                for(int level=1;level<(action=="t7setup"?90:55);++level) {void* args[]{&level}; target+=Value<int64_t>(Resolve("PlayerUtility","NextLevelExpFromLevel",1),nullptr,args);}
                int64_t amount=target-Get<int64_t>(character,Offset("CharacterData","<CurrentExp>k__BackingField","LE.Data"));
                if(amount<=0) throw std::runtime_error("test level/XP mismatch");
                Root holder(Invoke(Resolve("Actor","getPlayerQuestListHolder",0),c.actor));
                void* quests=Invoke(Method(holder.Get(),"get_StatefulQuestList",0),holder.Get());
                void* experience=Get<void*>(quests,Offset("StatefulQuestList","experienceTracker"));
                bool noFavor=true; void* args[]{&amount,&noFavor}; Invoke(Method(experience,"GainExpDirect",2),experience,args);
            }
            std::unique_ptr<Root> item; int selected=-1;
            for(int attempt=0;attempt<16 && selected<0;++attempt) {
                auto candidate=std::make_unique<Root>(Fixture(c,0,0,2,false,-1,-1,action=="lowsetup"?55:8));
                const auto aff=Entries(Invoke(Method(candidate->Get(),"GetNonSealedAffixes",0),candidate->Get()));
                for(void* a:aff) {
                    if(Get<uint8_t>(a,affixTier)>=4)continue;
                    int candidateId=Get<uint16_t>(a,affixId),tiers=1;void* args[]{&candidateId,&tiers};
                    if(action=="lowsetup" && Value<int>(requiredLevel,candidate->Get(),args)<=1)continue;
                    selected=candidateId;item=std::move(candidate);break;
                }
            }
            if(!item)throw std::runtime_error("native generation did not produce a qualifying upgrade fixture in bounded attempts");
            if(action=="t7setup"){
                if(!sevenTierAffixes.contains(selected))throw std::runtime_error("enable T7 and select a seven-tier fixture first");
                for(void* a:Entries(Invoke(Method(item->Get(),"GetNonSealedAffixes",0),item->Get())))if(Get<uint16_t>(a,affixId)==selected)*reinterpret_cast<uint8_t*>(static_cast<char*>(a)+affixTier)=4;
            }
            int fp=63; void* fpArgs[]{&fp}; Invoke(setFP,item->Get(),fpArgs); Invoke(Method(item->Get(),"RebuildID",0),item->Get());
            int quantity=1, context=0; void* addArgs[]{item->Get(),&quantity,&context};
            if(!Value<bool>(Method(mainContainer,"TryAddItem",3),mainContainer,addArgs)) throw std::runtime_error("normal forge insertion refused");
            int shardId=selected, amount=20; bool notify=false; void* shardArgs[]{&shardId,&amount,&notify}; Invoke(Method(c.storage,"AddShard",3),c.storage,shardArgs);
            void* popArgs[]{&shardId}; if(!Value<bool>(Method(c.craft,"PopShardToModifierSlot",1),c.craft,popArgs)) throw std::runtime_error("normal shard selection refused");
            return std::string("{\"ok\":true,\"selectedAffix\":")+std::to_string(selected)+",\"preview\":"+Preview()+"}";
        }
        if(action=="materials") {
            return std::string("{\"ok\":true,\"runeCount\":")+std::to_string(MaterialCount(c,102,1))+
                ",\"glyphCount\":"+std::to_string(MaterialCount(c,103,0))+"}";
        }
        if(action=="modifierrune") {
            if(!c.main) throw std::runtime_error("forge fixture required");
            bool returnShard=true;void* emptyArgs[]{&returnShard};Invoke(Method(c.manager,"EmptyCraftingModifierSlot",1),c.manager,emptyArgs);
            Root rune(Fixture(c,102,1,0)); // Rune of Refinement; keeps the main item.
            if(Get<uint8_t>(rune.Get(),itemType)!=102) throw std::runtime_error("native rune fixture failed");
            int amount=1,context=0;void* args[]{rune.Get(),&amount,&context};
            void* container=Get<void*>(c.craft,forgeModifier);
            if(!Value<bool>(Method(container,"TryAddItem",3),container,args)) throw std::runtime_error("native rune selection refused");
            return std::string("{\"ok\":true,\"preview\":")+Preview()+"}";
        }
        if(action=="forge") {
            void* error=nullptr; void* success=nullptr; bool warning=false, green=false;
            void* checkArgs[]{&error,&warning,&green,&success};
            if(!Value<bool>(Method(c.craft,"CheckForgeCapability",4),c.craft,checkArgs)) throw std::runtime_error("normal forge UI refuses: "+Text(error));
            (void)progression::SnapshotFixtureCurrent(id,"crafting-test-forge");
            const std::string before=Preview(); Invoke(forge,c.craft);
            return "{\"ok\":true,\"before\":"+before+",\"after\":"+Preview()+"}";
        }
        if(action=="supporthope" || action=="supportdespair") {
            if(!c.main) throw std::runtime_error("isolated forge fixture required");
            if(!Value<bool>(Method(c.manager,"EmptyCraftingSupportSlot",0),c.manager)) throw std::runtime_error("normal support slot return refused");
            const int subtype=action=="supporthope"?0:3;
            Root glyph(Fixture(c,103,subtype,0));
            if(!Glyph(glyph.Get(),subtype)) throw std::runtime_error("normal glyph generation failed");
            void* container=Get<void*>(c.craft,forgeSupport); int quantity=1, context=0;
            void* addArgs[]{glyph.Get(),&quantity,&context};
            if(!Value<bool>(Method(container,"TryAddItem",3),container,addArgs)) throw std::runtime_error("normal support insertion refused");
            return "{\"ok\":true,\"preview\":"+Preview()+"}";
        }
        if(action=="clear") {
            // Test fixtures return to the ground through normal drop/remove APIs,
            // so repeated isolated checks do not fill the clone's inventory.
            const auto mainContainer=Get<void*>(c.craft,forgeMain);
            if(c.main) {
                const auto entries=Entries(Invoke(Method(mainContainer,"GetContent",0),mainContainer));
                if(entries.size()!=1) throw std::runtime_error("unexpected test forge contents");
                Root entry(entries[0]); int quantity=1, context=0;
                void* checkArgs[]{entry.Get(),&quantity};
                if(!Value<bool>(Method(mainContainer,"CanRemoveItemPreCheck",2),mainContainer,checkArgs)) throw std::runtime_error("normal removal refused");
                void* dropArgs[]{c.actor,c.main,&quantity};
                if(!Value<bool>(Method(c.manager,"DropItem",3),c.manager,dropArgs)) throw std::runtime_error("normal test fixture drop refused");
                void* removeArgs[]{entry.Get(),&quantity,&context};
                if(!Value<bool>(Method(mainContainer,"TryRemoveItem",3),mainContainer,removeArgs)) throw std::runtime_error("normal test fixture removal refused");
                bool no=false; void* emptyArgs[]{&no}; Invoke(Method(c.manager,"EmptyCraftingModifierSlot",1),c.manager,emptyArgs);
            }
            if(!Value<bool>(Method(c.manager,"EmptyCraftingSupportSlot",0),c.manager)) throw std::runtime_error("normal support return refused");
            return std::string("{\"ok\":true}");
        }
        throw std::runtime_error("unknown isolated crafting probe");
    });
}
#endif
}
