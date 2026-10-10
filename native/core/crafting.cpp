#include "crafting.hpp"
#include "managed.hpp"
#include "hook.hpp"
#include "loot_crafting_rules.hpp"
#include "progression.hpp"
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
#include "stat_editor.hpp"
#endif
#include <atomic>
#include <cstring>
#include <memory>
#include <sstream>

namespace ep::crafting {
namespace {
using namespace managed;
using namespace lootcraft::rules;
std::atomic<double> fpFactor{1}, hope{-1}, despair{-1};
std::atomic<bool> preserveShards{false};
std::atomic<bool> preserveRunes{false}, preserveGlyphs{false}, bypassLevel{false};
std::atomic<uint64_t> refundedRunes{0}, refundedGlyphs{0}, levelChecks{0};
std::atomic<uint64_t> crafts{0}, fpChanges{0}, hopeChanges{0}, sealChanges{0}, refunded{0}, faults{0};
std::atomic<int> lastNormalLoss{0}, lastEffectiveLoss{0};
bool ready=false;
size_t managerActor, managerCraft, managerLoaded, managerStorage, forgeMain, forgeSupport, forgeModifier;
size_t actorTracker, trackerData, forgingPotential, affixList, affixId, affixTier, itemSubtype, itemRarity;
size_t itemType, entryData, entryQuantity;
const il2cpp::Field* managerInstance; const il2cpp::Field* craftingRng;
game::MethodRef getActor, offline, forge, cost, setFP, seal, roll, minCost, maxCost, shardCheck, glyphCheck, getItem;
game::MethodRef capability, requiredLevel;
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
bool CapabilityDetour(void* self,void** error,bool* warning,bool* green,void** success,const il2cpp::Method* method) {
    void* item=nullptr;
    if(bypassLevel.load()) Checked([&] {const auto c=Current(); if(c.craft==self) item=c.main;});
    CapabilityBinding bind(item);
    return originalCapability(self,error,warning,green,success,method);
}
int LevelDetour(void* item,int affix,int tier,const il2cpp::Method* method) {
    const int normal=originalLevel(item,affix,tier,method);
    if(capabilityItem==item && item && normal>0 && game::IsOfflinePlay()) {++levelChecks;return 0;}
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
    if(fpFactor.load()!=1 || hope.load()>=0 || despair.load()>=0 || preserveShards.load() || preserveRunes.load() || preserveGlyphs.load())
        result.push_back({forge,reinterpret_cast<void*>(&ForgeDetour),reinterpret_cast<void**>(&originalForge)});
    if(fpFactor.load()!=1) {
        result.push_back({cost,reinterpret_cast<void*>(&CostDetour),reinterpret_cast<void**>(&originalCost)});
        result.push_back({setFP,reinterpret_cast<void*>(&SetFPDetour),reinterpret_cast<void**>(&originalSetFP)});
    }
    if(despair.load()>=0) result.push_back({seal,reinterpret_cast<void*>(&SealDetour),reinterpret_cast<void**>(&originalSeal)});
    if(hope.load()>=0) result.push_back({roll,reinterpret_cast<void*>(&RollDetour),reinterpret_cast<void**>(&originalRoll)});
    if(bypassLevel.load()) {
        result.push_back({capability,reinterpret_cast<void*>(&CapabilityDetour),reinterpret_cast<void**>(&originalCapability)});
        result.push_back({requiredLevel,reinterpret_cast<void*>(&LevelDetour),reinterpret_cast<void**>(&originalLevel)});
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
        for(const auto& m:{forge,cost,setFP,seal,roll,capability,requiredLevel}) if(std::none_of(required.begin(),required.end(),[&](const Hook& h){return h.m.code==m.code;}) &&
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
    out << ",\"canForge\":" << Boolean(can) << ",\"message\":" << Json(Text(error)) << ",\"successMessage\":" << Json(Text(success));
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
        glyphCheck=Resolve("ItemData","IsGlyph",0); getItem=Resolve("OneSlotItemContainer","getItem",0);
        managerInstance=game::FindStaticField("LE.dll","","ItemContainersManager","_instance");
        craftingRng=game::FindStaticField("LE.dll","","CraftingManager","rng");
        if(!managerInstance||!craftingRng) throw std::runtime_error("crafting singleton/RNG unavailable");
        managerActor=Offset("ItemContainersManager","actor"); managerCraft=Offset("ItemContainersManager","craftingManager");
        managerLoaded=Offset("ItemContainersManager","fullyInitialisedAndLoaded"); managerStorage=Offset("ItemContainersManager","shardStorage");
        forgeMain=Offset("CraftingManager","main"); forgeSupport=Offset("CraftingManager","support"); forgeModifier=Offset("CraftingManager","modifier");
        actorTracker=Offset("Actor","characterDataTracker"); trackerData=Offset("CharacterDataTracker","charData");
        forgingPotential=Offset("ItemData","forgingPotential"); affixList=Offset("ItemData","affixes");
        affixId=Offset("ItemAffix","affixId"); affixTier=Offset("ItemAffix","affixTier");
        itemSubtype=Offset("ItemData","subType"); itemRarity=Offset("ItemData","rarity");
        itemType=Offset("ItemData","itemType"); entryData=Offset("ItemContainerEntry","data"); entryQuantity=Offset("ItemContainerEntry","quantity");
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
        const bool oldRunes=preserveRunes.load(),oldGlyphs=preserveGlyphs.load(),oldLevel=bypassLevel.load();
        if(key=="reset") { fpFactor=1; hope=-1; despair=-1; preserveShards=false; preserveRunes=false; preserveGlyphs=false; bypassLevel=false; }
        else {
            if(key=="fp" && !Factor(value)) throw std::runtime_error("FP cost factor 0..1");
            if((key=="hope"||key=="despair") && !Chance(value)) throw std::runtime_error("glyph chance 0..100/reset");
            const bool toggle=key=="shards"||key=="runes"||key=="glyphs"||key=="level";
            if(toggle && value!=0 && value!=1) throw std::runtime_error("crafting toggles use 0 or 1");
            if(key!="fp"&&key!="hope"&&key!="despair"&&!toggle) throw std::runtime_error("unknown crafting setting");
            if((key=="fp"&&value!=1)||((key=="hope"||key=="despair")&&value>=0)||(toggle&&value==1)) (void)Current();
            if(key=="fp") fpFactor=value; else if(key=="hope") hope=value; else if(key=="despair") despair=value;
            else if(key=="shards") preserveShards=value!=0; else if(key=="runes") preserveRunes=value!=0;
            else if(key=="glyphs") preserveGlyphs=value!=0; else bypassLevel=value!=0;
        }
        try { Reconcile(); } catch(...) { fpFactor=oldFP; hope=oldHope; despair=oldDespair; preserveShards=oldShards;
            preserveRunes=oldRunes; preserveGlyphs=oldGlyphs; bypassLevel=oldLevel;
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
        if(c.id!=id || c.name!="EpCraftTest") throw std::runtime_error("craft probe requires isolated EpCraftTest and matching id");
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
        if(action=="setup" || action=="lowsetup") {
            (void)progression::SnapshotFixtureCurrent(id,"crafting-test-setup");
            // Empty through the normal return-to-inventory APIs. This command is
            // compile-time research only and refuses every owner character.
            const auto mainContainer=Get<void*>(c.craft,forgeMain);
            if(c.main) throw std::runtime_error("remove the previous test item from the forge first");
            void* tracker=Get<void*>(c.actor,actorTracker); void* character=Get<void*>(tracker,trackerData);
            if(action!="lowsetup" && Get<int>(character,Offset("CharacterData","<Level>k__BackingField","LE.Data"))<55) {
                int64_t target=0;
                for(int level=1;level<55;++level) {void* args[]{&level}; target+=Value<int64_t>(Resolve("PlayerUtility","NextLevelExpFromLevel",1),nullptr,args);}
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
