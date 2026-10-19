#include "convenience.hpp"
#include "inventory_tools.hpp"
#include "stat_editor.hpp"
#include "managed.hpp"
#include "hook.hpp"
#include "items.hpp"
#include "progression.hpp"
#include "commands.hpp"
#include "lifecycle.hpp"
#include "convenience_rules.hpp"
#include "common.hpp"
#include "effects.hpp"
#include "ingame_panel.hpp"
#include "rage.hpp"
#include <array>
#include <deque>
#include <cstring>
#include <fstream>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <limits>
#include <set>
#include <sstream>
#include <tuple>

namespace ep::convenience {
namespace {
using namespace managed;
using MethodInfo=const il2cpp::Method*;
struct Vec3{float x,y,z;};
struct Vec2{float x,y;};
struct XY{int x,y;};
struct OptionalXY{bool has;XY value;};
struct Setting{const char* key;std::atomic<double> value{0};};
Setting chests{"chests"},shrines{"shrines"},portals{"portals"},shadows{"shadows"},reflections{"reflections"},
    bloom{"bloom"},motionBlur{"motionblur"},occlusion{"occlusion"},fog{"fog"};
std::array<Setting*,9> settings{&chests,&shrines,&portals,&shadows,&reflections,&bloom,&motionBlur,&occlusion,&fog};
game::MethodRef findObjects,componentTransform,transformPosition;
std::atomic<bool> active{false};
// Interaction area: how near a chest, shrine or portal must be (rules::InArea),
// and an optional outline drawn around each one with the game's LineRenderer.
std::atomic<double> areaRadius{3};std::atomic<int> areaShape{0};std::atomic<bool> areaShow{false};std::atomic<uint32_t> areaColor{0x3FD0FF};
uint64_t areaVersion=1;  // shape, radius or colour changed: outlines are rebuilt
uint64_t lastScan=0,portalGrace=0,faults=0,openedChests=0,usedShrines=0,usedPortals=0;
game::Handle owner;
std::map<std::string,std::unique_ptr<Root>> typeRoots;
struct Saved {std::unique_ptr<Root> value;std::unique_ptr<Root> volume;const il2cpp::Field* field;bool before;Setting* setting;};
struct Shortcut{std::unique_ptr<Root> button;std::unique_ptr<Root> object;const char* action;const char* label;std::vector<std::unique_ptr<Root>> labels;rules::Rect placed{};};
std::vector<Shortcut> shortcuts;
game::Handle inventoryPanel;
std::atomic<bool> shortcutBusy{false};
void(*originalClick)(void*,void*,MethodInfo)=nullptr;
void(*originalSubmit)(void*,void*,MethodInfo)=nullptr;
uint64_t lastButtons=0;
bool shortcutEnabled=false;
uint64_t frameUpdates=0,inventoryChecks=0,inventoryMicros=0,maximumInventoryMicros=0,inventorySearches=0;
const char* inventoryStatus="waiting-for-frame";
game::Handle menuTransform;
game::Handle footerTransform,currencyTransform,gridTransform;
Vec2 currencyOriginalSize{},currencyOriginalPosition{};
Vec3 currencyOriginalScale{},gridOriginalScale{};
Vec2 gridOriginalPosition{};
bool currencyResized=false,currencyScaled=false,gridAdjusted=false;
struct Candidate{std::unique_ptr<Root> object;std::unique_ptr<Root> handler;int kind;uint64_t retryAfter=0;int attempts=0;};
std::vector<Candidate> candidates;
std::vector<std::unique_ptr<Root>> pendingHandlers;
size_t candidateCursor=0,bootstrapType=0;
bool discover=true;
int interactionScene=0;
uint64_t nextInteraction=0,interactionChecks=0,sceneSearches=0,maximumInteractionMicros=0;
uint64_t lastWork=0,retryWork=0,maximumDiscoveryMicros=0,handlersQueued=0,handlersClassified=0;
// A scene-wide object search costs about 25 ms of one frame in play, so the
// graphics switches avoid them: post-processing volumes come from the game's
// own PostProcessManager list and the volumetric fog camera from
// HxVolumetricCamera.Active, both read once a second. Only reflection probes
// (native components without a registry) need a search: once a scene settles,
// and for an explicit switch. Search flags for Graphics():
constexpr int kSearchProbes=1,kSearchFogCameras=2;
int graphicsScene=0;
uint64_t probeSearchAt=0,graphicsScans=0,maximumGraphicsMicros=0;
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
bool measureFrames=false;
uint64_t previousFrame=0;
std::vector<uint64_t> frameGaps;
Vec3 testOriginalMenuScale{};
bool testMenuScaleSaved=false;
bool testCurrencySaved=false;
int testOriginalGold=0,testOriginalBones=0;
// Test-only timeline while frames are measured: slow frames, our own costly
// sections, scene changes and interaction triggers, in ms from frames-start.
struct Mark{uint64_t at;std::string what;uint64_t micros;};
std::vector<Mark> marks;
uint64_t marksOrigin=0;
std::atomic<uint64_t> enabledCalls{0};
void Note(std::string what,uint64_t micros=0){if(measureFrames&&marks.size()<4000)marks.push_back({GetTickCount64()-marksOrigin,std::move(what),micros});}
#define EP_NOTE(...) Note(__VA_ARGS__)
#else
#define EP_NOTE(...) ((void)0)
#endif
void(*originalHandlerAwake)(void*,MethodInfo)=nullptr;
void(*originalPortalInitialize)(void*,MethodInfo)=nullptr;
void(*originalCaveAwake)(void*,MethodInfo)=nullptr;
void(*originalShrineEra)(void*,int,MethodInfo)=nullptr;
void(*originalShrineSetup)(void*,void*,bool,int,const Vec3*,MethodInfo)=nullptr;
struct DamageResult {int events;float damage,secondary;};
static_assert(sizeof(DamageResult)==12);
using DamageFn=DamageResult(*)(void*,void*,void*,float,void*,bool,MethodInfo);
DamageFn originalDamage=nullptr;
struct DamageSample{void* actor=nullptr;uint64_t time=0;float damage=0;};
std::array<DamageSample,8192> damageSamples{};
size_t damageSampleIndex=0;
uint64_t lastDroppedDamage=0;
uint64_t monitorUntil=0,monitorStarted=0;
std::map<void*,std::unique_ptr<Root>> monitoredAllies;
game::Handle damageOwner;
// ---- damage session: on/off by the player; per ally type totals and one
// bucket per second for the last hour. The hook only adds to counters.
// Each hit is also split by damage type (the game's DamageType order:
// physical, fire, cold, lightning, necrotic, void, poison) in proportion to
// the hit's own damage per type, as the game passes it to ApplyDamage.
constexpr size_t kElements=rules::DamageTypes;
using Elements=rules::DamageParts;
struct DamageType{std::string name;double total=0;uint64_t hits=0;float maxHit=0;std::array<float,3600> buckets{};std::array<uint32_t,3600> seconds{};
    std::array<double,kElements> elementTotal{};std::array<uint64_t,kElements> elementHits{};Elements elementMax{};std::vector<Elements> elementBuckets=std::vector<Elements>(3600);  // on the heap: types are built on the stack
    std::string icon;   // the damage window's icon key: a:<skill ID> for allies, c:<class>:<mastery> for You
    std::string label;};  // the game's own name for the ally type ("Skeleton Archer"); empty for You or when unknown
std::vector<DamageType> damageTypes;
std::unordered_map<void*,size_t> damageActorType;   // ally (or the player) -> type
std::map<void*,std::unique_ptr<Root>> damageActorRoots;
// Allies that died, kept for a minute: the ailments they applied keep ticking in their name, and
// the root keeps the pointer from being reused for another object meanwhile.
std::unordered_map<void*,uint64_t> damageActorDeadSince;
constexpr uint64_t kDeadAllyKeepMs=60000;constexpr size_t kDeadAllyMax=512;
size_t damageSummonedField=0,damageReferencesField=0,damageCreatorField=0;  // resolved when the session starts
std::atomic<bool> damageSession{false};
uint64_t damageSessionStart=0,damageSessionEnd=0,damageCombatSeconds=0,damageLastCombatSecond=0,damageAllyRefresh=0,damageHits=0,damageUnsplit=0;
size_t damageArrayOffset=0;  // DamageStats.damage, resolved when the session starts
// ---- Damage the character takes (same hook; the result is unchanged): the last 20 s of hits for the
// death recap, the last 10 deaths with the 10 s before each, and during a damage session the damage
// taken per attacker type (the damage window's Taken view).
using TakenHit=rules::RecapHit;
struct DeathEffect{std::string name;int stacks=0;bool buff=false;};
struct DeathRecord{uint64_t tick=0,number=0;float maxHealth=0;std::vector<TakenHit> hits;std::vector<DeathEffect> effects;};
constexpr uint64_t kTakenKeepMs=20000,kDeathWindowMs=10000;constexpr size_t kTakenMost=600,kDeathsKept=10;
std::deque<TakenHit> takenRecent;
std::deque<DeathRecord> deathRecords;
uint64_t deathCount=0;
std::atomic<bool> deathRecap{true};          // on by default: the hook stays in while a character is loaded
void* takenActor=nullptr;void* takenProtection=nullptr;void* takenHealth=nullptr;std::unique_ptr<Root> takenRoot;  // main thread
struct TakenFields{size_t ward=0,current=0,maximum=0,isHit=0,ailment=0,ailmentName=0,refs=0,thisAbility=0,abilityName=0;bool ready=false;} takenFields;
std::vector<DamageType> takenTypes;std::vector<double> takenDot;std::unordered_map<std::string,size_t> takenIndex;
uint64_t takenCombatSeconds=0,takenLastSecond=0,takenTargetCheck=0;
std::map<void*,std::pair<std::string,std::unique_ptr<Root>>> attackerNames;  // bounded: cleared at 256
unsigned long mainThreadId=0;  // set by Frame (main thread): names are read only there
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
// Each hit on the character: the damage recorded against the health and ward it removed, per attacker.
std::map<std::string,std::array<double,4>> testTakenBy;  // recorded, lost, hits, damage over time hits
uint64_t testTakenZero=0;  // calls on the character that did no damage (avoided, immune)
#endif
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
uint64_t testDamageCalls=0,testDamageOwned=0;
// Target dummy (test fixtures dummy-*): its protection, what the session recorded for hits on it
// and what any attacker applied to it.
std::unique_ptr<Root> testDummy;void* testDummyProtection=nullptr;double testDummyRecorded=0,testDummyAll=0;uint64_t testDummyHits=0;
// Measuring targets (dummy-area-<health>): health set before each hit; per session type the recorded
// damage, the health the hit removed and the hits.
std::vector<std::unique_ptr<Root>> testTargetRoots;std::unordered_set<void*> testTargets;float testTargetHealth=0;
std::map<size_t,std::array<double,3>> testTargetByType;uint64_t testTargetOther=0,testTargetMatched=0,testTargetFailed=0,testCallsWhileTargets=0;std::string testTargetError;
// Health each target had after its last hit; health lost between hits outside the hook; health
// the game reset by recalculating maximum health; recorded hits by damage source class.
std::unordered_map<void*,float> testTargetAfter;double testOutsideLoss=0;uint64_t testOutsideEvents=0,testClamps=0;
std::map<std::string,std::array<double,2>> testSourceKinds;
// Hits on measuring targets the session did not credit: attacker type / source class, damage, hits.
std::map<std::string,std::array<double,2>> testOtherKinds;
std::string AllyType(void* actor);  // below
float testDamagePrimary=0,testDamageSecondary=0;
#endif
size_t F(const char* cls,const char* field,const char* ns);  // below; the test-only measuring targets use it here
bool ListLateAlly(void* attacker);  // below
void RecordTaken(void* attacker,void* source,void* stats,const Elements& parts,float damage,float healthBefore,float wardBefore);  // below
// One hit into a type: totals, its per-second bucket and its split by damage type (false: unsplit).
bool CreditHit(DamageType& type,float damage,const Elements& parts,uint32_t second){
    auto& slot=type.seconds[second%type.seconds.size()];
    const size_t bucket=second%type.buckets.size();
    if(slot!=second){slot=second;type.buckets[bucket]=0;type.elementBuckets[bucket]={};}
    type.buckets[bucket]+=damage;
    type.total+=damage;++type.hits;type.maxHit=std::max(type.maxHit,damage);
    Elements shares;
    if(!rules::SplitDamage(damage,parts,shares))return false;
    for(size_t e=0;e<kElements;++e)if(shares[e]>0){
        type.elementBuckets[bucket][e]+=shares[e];type.elementTotal[e]+=shares[e];++type.elementHits[e];type.elementMax[e]=std::max(type.elementMax[e],shares[e]);
    }
    return true;
}
DamageResult ObserveDamage(void* self,void* stats,void* source,float factor,void* attacker,bool leech,MethodInfo method){
    // An ally created since the last refresh (under a second ago) is listed by its first hit.
    if(attacker&&damageSession.load(std::memory_order_relaxed)&&!damageActorType.contains(attacker))ListLateAlly(attacker);
    // A hit on the character itself (death recap, Taken view): health and ward before it.
    const bool taken=self&&self==takenProtection&&takenFields.ready&&(deathRecap.load(std::memory_order_relaxed)||damageSession.load(std::memory_order_relaxed));
    float takenHealthBefore=0,takenWardBefore=0;
    if(taken){takenHealthBefore=Get<float>(takenHealth,takenFields.current);takenWardBefore=Get<float>(self,takenFields.ward);}
    // The hit's damage per type, read before the game applies it.
    Elements parts{};
    if(stats&&damageArrayOffset&&(taken||(damageSession.load(std::memory_order_relaxed)&&damageActorType.contains(attacker)))){
        if(void* array=*reinterpret_cast<void**>(static_cast<char*>(stats)+damageArrayOffset)){
            const auto n=std::min<uintptr_t>(*reinterpret_cast<const uintptr_t*>(static_cast<const char*>(array)+0x18),kElements);
            const auto* values=reinterpret_cast<const float*>(static_cast<const char*>(array)+0x20);
            for(uintptr_t i=0;i<n;++i)parts[i]=values[i];
        }
    }
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
    void* targetHealth=nullptr;float targetBefore=0;
    if(!testTargets.empty())++testCallsWhileTargets;
    if(self&&testTargets.contains(self)){
        ++testTargetMatched;
        try{
            void* actor=Get<void*>(self,F("ProtectionClass","actor",""));
            if(actor&&game::IsAlive(actor)&&(targetHealth=Get<void*>(actor,F("Actor","health","")))){
                const float now=Get<float>(targetHealth,F("BaseHealth","currentHealth",""));
                const int max=Get<int>(targetHealth,F("BaseHealth","maxHealth",""));
                if(auto last=testTargetAfter.find(self);last!=testTargetAfter.end()){
                    if(max!=static_cast<int>(testTargetHealth))++testClamps;
                    else if(now<last->second-0.5f){testOutsideLoss+=last->second-now;++testOutsideEvents;}
                }
                *reinterpret_cast<int*>(static_cast<char*>(targetHealth)+F("BaseHealth","maxHealth",""))=static_cast<int>(testTargetHealth);
                *reinterpret_cast<float*>(static_cast<char*>(targetHealth)+F("BaseHealth","currentHealth",""))=testTargetHealth;
                targetBefore=testTargetHealth;
            }
        }catch(const std::exception& e){targetHealth=nullptr;++testTargetFailed;testTargetError=e.what();}
    }
#endif
    const auto result=originalDamage(self,stats,source,factor,attacker,leech,method);
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
    if(targetHealth){
        const float after=Get<float>(targetHealth,F("BaseHealth","currentHealth",""));
        const double lost=targetBefore-after;
        testTargetAfter[self]=after;
        auto type=damageActorType.find(attacker);
        if(type!=damageActorType.end()&&damageSession.load(std::memory_order_relaxed)){
            auto& entry=testTargetByType[type->second];
            if(std::isfinite(result.damage)&&result.damage>0)entry[0]+=result.damage;
            entry[1]+=lost;++entry[2];
            const char* kind=source?il2cpp::api().class_get_name(il2cpp::api().object_get_class(source)):"(none)";
            auto& k=testSourceKinds[kind?kind:"?"];k[0]+=std::isfinite(result.damage)?result.damage:0;++k[1];
        }else{
            ++testTargetOther;std::string who="(no attacker)";
            if(attacker){try{who=game::IsAlive(attacker)?AllyType(attacker):std::string("(destroyed attacker)");}catch(...){who="(unreadable attacker)";}}
            const char* kind=source?il2cpp::api().class_get_name(il2cpp::api().object_get_class(source)):"(none)";
            auto& o=testOtherKinds[who+" / "+(kind?kind:"?")+(damageSession.load(std::memory_order_relaxed)?"":" / session off")];
            o[0]+=std::isfinite(result.damage)?result.damage:0;++o[1];
        }
    }
    ++testDamageCalls;if(monitoredAllies.contains(attacker))++testDamageOwned;
    testDamagePrimary=result.damage;testDamageSecondary=result.secondary;
    if(self&&self==testDummyProtection&&std::isfinite(result.damage)&&result.damage>0)testDummyAll+=result.damage;
#endif
    // Preserve the authoritative result and all game effects. The supported
    // native tuple's second field is also sent to the game's damage events.
    // Collection has no allocations and expires when the Summons page stops polling.
    const auto now=GetTickCount64();
    if(taken&&!lifecycle::Stopping())RecordTaken(attacker,source,stats,parts,result.damage,takenHealthBefore,takenWardBefore);
    if(damageSession.load(std::memory_order_relaxed)&&!lifecycle::Stopping()&&std::isfinite(result.damage)&&result.damage>0){
        if(auto it=damageActorType.find(attacker);it!=damageActorType.end()){
            auto& type=damageTypes[it->second];
            const uint32_t second=static_cast<uint32_t>((now-damageSessionStart)/1000)+1;  // 0 = empty bucket
            ++damageHits;
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
            if(self==testDummyProtection){testDummyRecorded+=result.damage;++testDummyHits;}
#endif
            if(!CreditHit(type,result.damage,parts,second))++damageUnsplit;
            if(second!=damageLastCombatSecond){damageLastCombatSecond=second;++damageCombatSeconds;}
        }
    }
    if(!lifecycle::Stopping()&&now<monitorUntil&&monitoredAllies.contains(attacker)&&std::isfinite(result.damage)&&result.damage>0){
        auto& sample=damageSamples[damageSampleIndex++%damageSamples.size()];
        if(sample.time&&now-sample.time<=5000)lastDroppedDamage=now;
        sample={attacker,now,result.damage};
    }
    return result;
}
std::vector<Saved> saved;
struct DisabledBehaviour {void* object;Setting* setting;};
std::atomic<std::shared_ptr<const std::vector<DisabledBehaviour>>> disabledBehaviours=std::make_shared<const std::vector<DisabledBehaviour>>();
void(*originalEnabled)(void*,bool,MethodInfo)=nullptr;
void Enabled(void* object,bool enabled,MethodInfo info){
    // Unity/game graphics updates may re-enable scene effects after our scan.
    // Clamp only the exact rooted components captured by this session.
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
    enabledCalls.fetch_add(1,std::memory_order_relaxed);
#endif
    if(enabled&&!lifecycle::Stopping()){
        auto targets=disabledBehaviours.load();
        for(const auto& target:*targets)if(target.object==object&&target.setting->value.load()!=0){enabled=false;break;}
    }
    originalEnabled(object,enabled,info);
}
void PublishDisabled(){
    auto targets=std::make_shared<std::vector<DisabledBehaviour>>();
    for(const auto& entry:saved)if(!entry.field)targets->push_back({entry.value->Get(),entry.setting});
    disabledBehaviours.store(targets);
}
bool shadowCaptured=false;
int originalShadows=0;
size_t F(const char* cls,const char* field,const char* ns=""){
    static std::map<std::string,size_t> fields;
    const std::string key=std::string(ns)+":"+cls+":"+field;
    if(auto it=fields.find(key);it!=fields.end())return it->second;
    return fields.emplace(key,Offset(cls,field,ns)).first->second;
}
uint64_t Micros(){LARGE_INTEGER counter,frequency;QueryPerformanceCounter(&counter);QueryPerformanceFrequency(&frequency);return static_cast<uint64_t>(counter.QuadPart*1000000/frequency.QuadPart);}
game::MethodRef M(const char* cls,const char* name,int arity,const char* ns="",const char* image="LE.dll"){
    static std::map<std::string,game::MethodRef> methods;
    const std::string key=std::string(image)+":"+ns+":"+cls+":"+name+":"+std::to_string(arity);
    if(auto it=methods.find(key);it!=methods.end())return it->second;
    auto m=game::FindMethod(image,ns,cls,name,arity);
    if(!m)throw std::runtime_error(std::string("required convenience method unavailable: ")+cls+"."+name);
    methods.emplace(key,m);return m;
}
const il2cpp::Field* Field(void* object,const char* name){
    for(auto* cls=il2cpp::api().object_get_class(object);cls;cls=il2cpp::api().class_get_parent(cls))
        if(auto* field=il2cpp::api().class_get_field_from_name(cls,name))return field;
    throw std::runtime_error(std::string("missing object field: ")+name);
}
void* Actor(){
    if(!game::IsOfflinePlay())return nullptr;
    auto session=SessionState();if(session.state!=4||session.transitioning)return nullptr;
    void* actor=Invoke(M("PlayerFinder","getPlayerActor",0));
    if(!actor||!game::IsAlive(actor))return nullptr;
    void* health=Get<void*>(actor,F("Actor","health"));
    if(!health||Get<float>(health,F("BaseHealth","currentHealth"))<=0)return nullptr;
    void* tracker=Get<void*>(actor,F("Actor","characterDataTracker"));
    void* data=Get<void*>(tracker,F("CharacterDataTracker","charData"));
    return Value<bool>(Method(data,"get_IsOffline",0),data)?actor:nullptr;
}
struct Context{void* actor;void* manager;std::string id,name;};
Context Current(const std::string& expected=""){
    void* actor=Actor();if(!actor)throw std::runtime_error("Load an offline character and finish loading the area first.");
    void* data=Get<void*>(Get<void*>(actor,F("Actor","characterDataTracker")),F("CharacterDataTracker","charData"));
    std::string id=Text(Invoke(Method(data,"get_Id",0),data));
    if(!expected.empty()&&id!=expected)throw std::runtime_error("The loaded character changed. Refresh and try again.");
    void* manager=Invoke(M("ItemContainersManager","get_Instance",0));
    if(!manager||Get<void*>(manager,F("ItemContainersManager","actor"))!=actor||!Value<bool>(Method(manager,"get_IsFullyInitializedAndLoaded",0),manager))
        throw std::runtime_error("Wait for the character inventory to finish loading.");
    return {actor,manager,id,Text(Get<void*>(data,F("CharacterData","<CharacterName>k__BackingField","LE.Data")))};
}
std::string Player(const Context& c){return "{\"id\":"+Json(c.id)+",\"name\":"+Json(c.name)+"}";}
std::vector<void*> Find(const char* image,const char* ns,const char* cls){
    const std::string key=std::string(image)+":"+ns+":"+cls;
    auto it=typeRoots.find(key);
    if(it==typeRoots.end()){
        auto* type=game::FindClass(image,ns,cls);if(!type)throw std::runtime_error("Object type unavailable: "+key);
        it=typeRoots.emplace(key,std::make_unique<Root>(il2cpp::api().type_get_object(il2cpp::api().class_get_type(type)))).first;
    }
    bool inactive=false;void* args[]{it->second->Get(),&inactive};Root array(Invoke(findObjects,nullptr,args));
    const auto n=Get<uintptr_t>(array.Get(),0x18);if(n>4096)throw std::runtime_error("Scene object count outside supported bounds.");
    std::vector<void*> out;out.reserve(n);
    for(uintptr_t i=0;i<n;++i)if(void* object=Get<void*>(array.Get(),0x20+8*i))out.push_back(object);
    return out;
}
Vec3 Position(void* component){Root t(Invoke(componentTransform,component));return Value<Vec3>(transformPosition,t.Get());}
bool Near(void* object,const Vec3& p){auto v=Position(object);return rules::InArea(v.x-p.x,v.y-p.y,v.z-p.z,areaRadius.load(),areaShape.load());}
void* Ui(){void* ui=game::StaticObject(game::FindStaticField("LE.dll","","UIBase","instance"));if(!ui||!game::IsAlive(ui))throw std::runtime_error("Game interface unavailable.");return ui;}
bool PanelOpen(){void* ui=Ui();return Value<bool>(Method(ui,"IsInventoryOpen",0),ui)||Value<bool>(Method(ui,"IsStashOpen",0),ui)||Value<bool>(Method(ui,"IsShopOpen",0),ui);}
const il2cpp::Method* Signature(const char* cls,const char* name,std::initializer_list<const char*> parameters,const char* ns="UnityEngine",const char* image="UnityEngine.CoreModule.dll"){
    static std::map<std::string,const il2cpp::Method*> signatures;
    std::string key=std::string(image)+":"+ns+":"+cls+":"+name;
    for(const char* parameter:parameters)key+=std::string(":")+parameter;
    if(auto it=signatures.find(key);it!=signatures.end())return it->second;
    auto& a=il2cpp::api();auto* c=game::FindClass(image,ns,cls);void* iterator=nullptr;
    while(auto* m=a.class_get_methods(c,&iterator)){
        if(std::string(a.method_get_name(m))!=name||a.method_get_param_count(m)!=parameters.size())continue;
        bool match=true;unsigned i=0;for(const char* expected:parameters){char* actual=a.type_get_name(a.method_get_param(m,i++));match &= actual&&std::string(actual)==expected;if(actual)a.free(actual);}
        if(match){signatures.emplace(key,m);return m;}
    }throw std::runtime_error(std::string("Missing overload: ")+cls+"."+name);
}
void ClearShortcuts(){
    if(gridAdjusted&&gridTransform.Get()&&game::IsAlive(gridTransform.Get())){
        void* pos[]{&gridOriginalPosition},*scale[]{&gridOriginalScale};
        Invoke(M("RectTransform","set_anchoredPosition",1,"UnityEngine","UnityEngine.CoreModule.dll"),gridTransform.Get(),pos);
        Invoke(M("Transform","set_localScale",1,"UnityEngine","UnityEngine.CoreModule.dll"),gridTransform.Get(),scale);
    }
    if((currencyResized||currencyScaled)&&currencyTransform.Get()&&game::IsAlive(currencyTransform.Get())){
        void* size[]{&currencyOriginalSize},*position[]{&currencyOriginalPosition},*scale[]{&currencyOriginalScale};
        Invoke(M("RectTransform","set_sizeDelta",1,"UnityEngine","UnityEngine.CoreModule.dll"),currencyTransform.Get(),size);
        Invoke(M("RectTransform","set_anchoredPosition",1,"UnityEngine","UnityEngine.CoreModule.dll"),currencyTransform.Get(),position);
        Invoke(M("Transform","set_localScale",1,"UnityEngine","UnityEngine.CoreModule.dll"),currencyTransform.Get(),scale);
    }
    currencyResized=false;currencyScaled=false;gridAdjusted=false;
    for(auto& b:shortcuts)if(game::IsAlive(b.object->Get())){void* a[]{b.object->Get()};Invoke(M("Object","Destroy",1,"UnityEngine","UnityEngine.CoreModule.dll"),nullptr,a);}
    shortcuts.clear();inventoryPanel.Reset();menuTransform.Reset();footerTransform.Reset();currencyTransform.Reset();gridTransform.Reset();
}
// Sell all asks once more: its first press shows "Confirm sell" for a few seconds.
uint64_t sellConfirmUntil=0;
void SetShortcutText(Shortcut& b,const char* text){
    Root value(il2cpp::api().string_new(text));
    for(auto& label:b.labels)if(label->Get()&&game::IsAlive(label->Get())){void* a[]{value.Get()};Invoke(Method(label->Get(),"set_text",1),label->Get(),a);}
}
void ExpireSellConfirm(){
    if(!sellConfirmUntil||GetTickCount64()<sellConfirmUntil)return;
    sellConfirmUntil=0;
    for(auto& b:shortcuts)if(std::string(b.action)=="sellall")SetShortcutText(b,b.label);
}
bool ShortcutClick(void* button){
    auto it=std::find_if(shortcuts.begin(),shortcuts.end(),[&](const Shortcut& s){return s.button->Get()==button;});
    if(it==shortcuts.end())return false;
    if(shortcutBusy)return true;
    if(std::string(it->action)=="sellall"){
        if(!sellConfirmUntil||GetTickCount64()>=sellConfirmUntil){sellConfirmUntil=GetTickCount64()+4000;game::Guarded([&]{SetShortcutText(*it,"Confirm sell");},nullptr);return true;}
        sellConfirmUntil=0;game::Guarded([&]{SetShortcutText(*it,it->label);},nullptr);
    }
    std::string why;game::Guarded([&]{const auto c=Current();const char* action=it->action;
        shortcutBusy=true;
        if(!commands::SubmitUi("inventoryaction "+c.id+" "+action,[](const std::string& result){Log("inventory shortcut: %s",result.c_str());shortcutBusy=false;}))shortcutBusy=false;
    },&why);if(!why.empty())Log("inventory shortcut refused: %s",why.c_str());
    return true;
}
void Click(void* self,void* event,MethodInfo info){bool ours=false;game::Guarded([&]{ours=ShortcutClick(self);},nullptr);if(!ours)originalClick(self,event,info);}
void Submit(void* self,void* event,MethodInfo info){bool ours=false;game::Guarded([&]{ours=ShortcutClick(self);},nullptr);if(!ours)originalSubmit(self,event,info);}
// The game's own inventory panel, remembered when it is enabled, so creating the
// shortcuts needs no FindObjectsOfType scene search (about 50 ms in play).
game::Handle enabledInventoryPanel;
void(*originalPanelEnable)(void*,MethodInfo)=nullptr;
// Stash tab memory: the game reopens the stash on its default tab. Remember
// the tab that was active when the stash closed and select it again through
// the tab bar's own click path once the reopened panel has built its tabs.
int lastStashTab=-1;
game::Handle pendingStashPanel;
unsigned stashRestoreTicks=0;
uint64_t stashRestores=0,stashRestoreFailures=0;
const char* kPanelNs="LE.UI.PanelSystem";
// The remembered tab survives zone changes in memory and game restarts in
// <game>\EpochPact\stash-tab.txt (one integer; written only when it changes).
std::wstring StashTabPath(){return PluginDir()+L"stash-tab.txt";}
void LoadStashTab(){
    std::ifstream in(StashTabPath());int tab=-1;
    if(in>>tab&&tab>=0&&tab<100000)lastStashTab=tab;
}
void RememberStashTab(int tab){
    if(tab<0||tab==lastStashTab)return;
    lastStashTab=tab;
    const std::wstring path=StashTabPath(),temp=path+L".pending";
    {std::ofstream out(temp,std::ios::trunc);if(!out)return;out<<tab<<'\n';if(!out)return;}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING))DeleteFileW(temp.c_str());
}
void* StashControls(void* panel){return panel?Get<void*>(panel,F("StashPanelV2","_stashControls",kPanelNs)):nullptr;}
int ActiveStashTab(void* controls){return Get<int>(controls,F("NewStashTabbedUIControls","<ActiveTabId>k__BackingField"));}
void(*originalStashOpen)(void*,void*,int,MethodInfo)=nullptr;
void(*originalStashClose)(void*,MethodInfo)=nullptr;
void StashOpen(void* self,void* system,int location,MethodInfo info){
    originalStashOpen(self,system,location,info);
    if(!lifecycle::Stopping()&&lastStashTab>=0)game::Guarded([&]{pendingStashPanel.Set(self);stashRestoreTicks=0;},nullptr);
}
void StashClose(void* self,MethodInfo info){
    if(!lifecycle::Stopping())game::Guarded([&]{
        pendingStashPanel.Reset();
        if(void* controls=StashControls(self);controls&&game::IsAlive(controls))RememberStashTab(ActiveStashTab(controls));
    },nullptr);
    originalStashClose(self,info);
}
// The 1.5 stash uses StashTabbedUIControls (activeTab / SwitchToStashTab);
// StashPanelV2 above covers the newer panel when a game build enables it.
game::Handle pendingLegacyStash;
void(*originalLegacyEnable)(void*,MethodInfo)=nullptr;
void(*originalLegacyDisable)(void*,MethodInfo)=nullptr;
int LegacyTab(void* controls){return Get<int>(controls,F("StashTabbedUIControls","activeTab"));}
void LegacyStashEnable(void* self,MethodInfo info){
    originalLegacyEnable(self,info);
    if(!lifecycle::Stopping()&&lastStashTab>=0)game::Guarded([&]{pendingLegacyStash.Set(self);stashRestoreTicks=0;},nullptr);
}
void LegacyStashDisable(void* self,MethodInfo info){
    if(!lifecycle::Stopping())game::Guarded([&]{pendingLegacyStash.Reset();RememberStashTab(LegacyTab(self));},nullptr);
    originalLegacyDisable(self,info);
}
void RestoreLegacyStashTab(){
    void* controls=pendingLegacyStash.Get();
    if(!controls)return;
    if(!game::IsAlive(controls)||++stashRestoreTicks>120){pendingLegacyStash.Reset();++stashRestoreFailures;return;}
    // Wait for the tab bar's own asynchronous rebuild before switching.
    if(Get<bool>(controls,F("StashTabbedUIControls","_isUpdatingTabs"))||stashRestoreTicks<3)return;
    if(LegacyTab(controls)==lastStashTab){pendingLegacyStash.Reset();if(stashRestoreTicks>3)++stashRestores;return;}
    if(stashRestoreTicks>60){pendingLegacyStash.Reset();lastStashTab=-1;++stashRestoreFailures;return;}  // tab gone
    int id=lastStashTab;bool flag=false;void* args[]{&id,&flag};
    Invoke(Method(controls,"SwitchToStashTab",2),controls,args);
}
// Runs on the frame callback while a reopened stash waits for its tab bar.
void RestoreStashTab(){
    void* panel=pendingStashPanel.Get();
    if(!panel)return;
    if(!game::IsAlive(panel)||++stashRestoreTicks>120){pendingStashPanel.Reset();++stashRestoreFailures;return;}
    void* controls=StashControls(panel);
    if(!controls||!game::IsAlive(controls))return;
    if(ActiveStashTab(controls)==lastStashTab){pendingStashPanel.Reset();if(stashRestoreTicks>1)++stashRestores;return;}
    // The tab may not exist any more (deleted) or may not be built yet.
    void* tabs=Get<void*>(controls,F("NewStashTabbedUIControls","_tabById"));
    if(!tabs)return;
    int id=lastStashTab;void* has[]{&id};
    if(!Value<bool>(Method(tabs,"ContainsKey",1),tabs,has)){if(stashRestoreTicks>20){pendingStashPanel.Reset();lastStashTab=-1;}return;}
    int button=0;void* args[]{&id,&button,nullptr};
    Invoke(Method(controls,"TriggerTabClicked",3),controls,args);
}
void PanelEnable(void* self,MethodInfo info){
    originalPanelEnable(self,info);
    if(!lifecycle::Stopping())game::Guarded([&]{enabledInventoryPanel.Set(self);},nullptr);
}
rules::Rect RectIn(void* transform,void* space){
    const auto rect=Value<rules::Rect>(M("RectTransform","get_rect",0,"UnityEngine","UnityEngine.CoreModule.dll"),transform);
    Vec3 lo{rect.x,rect.y,0},hi{rect.x+rect.w,rect.y+rect.h,0};void* la[]{&lo},*ha[]{&hi};
    lo=Value<Vec3>(M("Transform","TransformPoint",1,"UnityEngine","UnityEngine.CoreModule.dll"),transform,la);
    hi=Value<Vec3>(M("Transform","TransformPoint",1,"UnityEngine","UnityEngine.CoreModule.dll"),transform,ha);
    lo=Value<Vec3>(M("Transform","InverseTransformPoint",1,"UnityEngine","UnityEngine.CoreModule.dll"),space,la);
    hi=Value<Vec3>(M("Transform","InverseTransformPoint",1,"UnityEngine","UnityEngine.CoreModule.dll"),space,ha);
    return {lo.x,lo.y,hi.x-lo.x,hi.y-lo.y};
}
rules::Rect CurrencyBounds(void* menu){
    void* currency=currencyTransform.Get();if(!currency)return {};
    rules::Rect occupied{};bool any=false;
    int count=Value<int>(M("Transform","get_childCount",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency);
    if(count<1||count>64)return {};
    for(int i=0;i<count;++i){void* a[]{&i};Root child(Invoke(M("Transform","GetChild",1,"UnityEngine","UnityEngine.CoreModule.dll"),currency,a));
        Root object(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),child.Get()));
        if(!Value<bool>(Method(object.Get(),"get_activeInHierarchy",0),object.Get()))continue;
        auto r=RectIn(child.Get(),menu);if(!rules::Valid(r))return {};
        if(!any){occupied=r;any=true;}else{float right=std::max(occupied.x+occupied.w,r.x+r.w),top=std::max(occupied.y+occupied.h,r.y+r.h);occupied.x=std::min(occupied.x,r.x);occupied.y=std::min(occupied.y,r.y);occupied.w=right-occupied.x;occupied.h=top-occupied.y;}
    }
    return occupied;
}
void FitCurrencies(){
    void* currency=currencyTransform.Get();
    Root object(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency));
    Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("UnityEngine.UI.dll","UnityEngine.UI","HorizontalLayoutGroup"))));
    void* args[]{type.Get()};Root group(Invoke(Signature("GameObject","GetComponent",{"System.Type"}),object.Get(),args));
    if(!group.Get())return;
    // Keep the native right edge, font, formatting, events and interaction.
    // Give the native group enough width to lay out the full balance strings.
    float width=0;int visible=0;
    const int count=Value<int>(M("Transform","get_childCount",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency);
    if(count<1||count>64)return;
    for(int i=0;i<count;++i){void* a[]{&i};Root child(Invoke(M("Transform","GetChild",1,"UnityEngine","UnityEngine.CoreModule.dll"),currency,a));
        Root go(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),child.Get()));
        if(!Value<bool>(Method(go.Get(),"get_activeInHierarchy",0),go.Get()))continue;
        void* ca[]{child.Get()};const float preferred=Value<float>(M("LayoutUtility","GetPreferredWidth",1,"UnityEngine.UI","UnityEngine.UI.dll"),nullptr,ca);
        if(!std::isfinite(preferred)||preferred<0)return;
        width+=preferred;++visible;
    }
    if(!visible)return;
    Root padding(Invoke(Method(group.Get(),"get_padding",0),group.Get()));
    width+=Value<int>(Method(padding.Get(),"get_left",0),padding.Get())+Value<int>(Method(padding.Get(),"get_right",0),padding.Get());
    width+=std::max(0.f,Value<float>(Method(group.Get(),"get_spacing",0),group.Get()))*(visible-1);
    const auto size=Value<Vec2>(M("RectTransform","get_sizeDelta",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency);
    if(!currencyResized){currencyOriginalSize=size;currencyOriginalPosition=Value<Vec2>(M("RectTransform","get_anchoredPosition",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency);}
    const float desired=std::max(currencyOriginalSize.x,width);
    if(std::abs(size.x-desired)<.1f)return;
    const auto pivot=Value<Vec2>(M("RectTransform","get_pivot",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency);
    Vec2 nextSize{desired,size.y},position=Value<Vec2>(M("RectTransform","get_anchoredPosition",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency);
    const auto scale=Value<Vec3>(M("Transform","get_localScale",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency);
    position.x-=(desired-size.x)*(1-pivot.x)*scale.x;
    void* sa[]{&nextSize},*pa[]{&position};currencyResized=true;
    Invoke(M("RectTransform","set_sizeDelta",1,"UnityEngine","UnityEngine.CoreModule.dll"),currency,sa);
    Invoke(M("RectTransform","set_anchoredPosition",1,"UnityEngine","UnityEngine.CoreModule.dll"),currency,pa);
    void* ra[]{currency};Invoke(M("LayoutRebuilder","ForceRebuildLayoutImmediate",1,"UnityEngine.UI","UnityEngine.UI.dll"),nullptr,ra);
}
void CenterY(void* transform,void* menu,float center){
    const auto rect=RectIn(transform,menu);
    const float change=center-(rect.y+rect.h/2);if(std::abs(change)<.05f)return;
    Root parent(Invoke(M("Transform","get_parent",0,"UnityEngine","UnityEngine.CoreModule.dll"),transform));
    const auto local=Value<rules::Rect>(M("RectTransform","get_rect",0,"UnityEngine","UnityEngine.CoreModule.dll"),parent.Get());
    const float scale=RectIn(parent.Get(),menu).h/local.h;if(!std::isfinite(scale)||scale<=0)return;
    auto pos=Value<Vec2>(M("RectTransform","get_anchoredPosition",0,"UnityEngine","UnityEngine.CoreModule.dll"),transform);pos.y+=change/scale;
    void* args[]{&pos};Invoke(M("RectTransform","set_anchoredPosition",1,"UnityEngine","UnityEngine.CoreModule.dll"),transform,args);
}
void FitCurrencyRow(void* menu){
    void* currency=currencyTransform.Get();
    const auto scale=Value<Vec3>(M("Transform","get_localScale",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency);
    if(!std::isfinite(scale.x)||scale.x<=0||!std::isfinite(scale.y)||scale.y<=0)throw std::runtime_error("Invalid currency scale.");
    const auto before=RectIn(currency,menu),footer=RectIn(footerTransform.Get(),menu);
    const auto left=RectIn(Get<void*>(inventoryPanel.Get(),F("InventoryPanelUI","leftButtonsContainer")),menu);
    const float factor=rules::CurrencyScale(footer,left,before.w*currencyOriginalScale.x/scale.x);
    Vec3 next{currencyOriginalScale.x*factor,currencyOriginalScale.y*factor,currencyOriginalScale.z};
    if(std::abs(scale.x-next.x)<.001f&&std::abs(scale.y-next.y)<.001f)return;
    void* a[]{&next};Invoke(M("Transform","set_localScale",1,"UnityEngine","UnityEngine.CoreModule.dll"),currency,a);currencyScaled=true;
    const auto after=RectIn(currency,menu);
    Root parent(Invoke(M("Transform","get_parent",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency));
    const auto local=Value<rules::Rect>(M("RectTransform","get_rect",0,"UnityEngine","UnityEngine.CoreModule.dll"),parent.Get());
    const float parentScale=RectIn(parent.Get(),menu).w/local.w;
    auto pos=Value<Vec2>(M("RectTransform","get_anchoredPosition",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency);
    pos.x+=(before.x+before.w-after.x-after.w)/parentScale;
    void* pa[]{&pos};Invoke(M("RectTransform","set_anchoredPosition",1,"UnityEngine","UnityEngine.CoreModule.dll"),currency,pa);
    CenterY(currency,menu,footer.y+footer.h/2);
}
void PlaceShortcuts(){
    void* menu=menuTransform.Get();if(!menu||shortcuts.empty())return;
    Root ct(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("UnityEngine.UIModule.dll","UnityEngine","Canvas"))));bool includeInactive=true;void* ca[]{ct.Get(),&includeInactive};
    Root canvas(Invoke(Signature("Component","GetComponentInParent",{"System.Type","System.Boolean"}),menu,ca));
    Root root(Invoke(M("Canvas","get_rootCanvas",0,"UnityEngine","UnityEngine.UIModule.dll"),canvas.Get()));
    Root rt(Invoke(componentTransform,root.Get()));
    const auto bounds=RectIn(menu,menu),screen=RectIn(rt.Get(),menu);
    rules::Toolbar placement{};
    void* footer=footerTransform.Get(),*currency=currencyTransform.Get();
    if(footer&&currency){Root footerObject(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),footer));
        if(Value<bool>(Method(footerObject.Get(),"get_activeInHierarchy",0),footerObject.Get())){
            // Currency containers deliberately leave empty space. Measure each
            // visible native currency, including counters enabled by factions.
            FitCurrencies();FitCurrencyRow(menu);const auto occupied=CurrencyBounds(menu);
            void* grid=gridTransform.Get();
            if(grid&&rules::Valid(occupied)){
                auto base=RectIn(grid,menu);
                const auto scale=Value<Vec3>(M("Transform","get_localScale",0,"UnityEngine","UnityEngine.CoreModule.dll"),grid);
                const float originalWidth=base.w*gridOriginalScale.x/scale.x,originalHeight=base.h*gridOriginalScale.y/scale.y;
                base={base.x+(base.w-originalWidth)/2,base.y+base.h-originalHeight,originalWidth,originalHeight};
                placement=rules::PlaceToolbar(bounds,screen,base,RectIn(footer,menu),RectIn(Get<void*>(inventoryPanel.Get(),F("InventoryPanelUI","leftButtonsContainer")),menu),occupied);
                if(placement.visible){
                    const float factor=placement.grid.w/base.w;Vec3 next{gridOriginalScale.x*factor,gridOriginalScale.y*factor,gridOriginalScale.z};
                    if(std::abs(scale.x-next.x)>.001f||std::abs(scale.y-next.y)>.001f){
                        void* a[]{&next};Invoke(M("Transform","set_localScale",1,"UnityEngine","UnityEngine.CoreModule.dll"),grid,a);gridAdjusted=true;
                    }
                    CenterY(grid,menu,placement.grid.y+placement.grid.h/2);
                }
            }
        }
    }
    for(size_t i=0;i<shortcuts.size();++i){auto& b=shortcuts[i];bool show=placement.visible;void* a[]{&show};
        if(Value<bool>(Method(b.object->Get(),"get_activeSelf",0),b.object->Get())!=show)Invoke(Method(b.object->Get(),"SetActive",1),b.object->Get(),a);
        if(show){const auto r=placement.buttons[i];
            if(b.placed.x!=r.x||b.placed.y!=r.y||b.placed.w!=r.w||b.placed.h!=r.h){
                Root tr(Invoke(M("GameObject","get_transform",0,"UnityEngine","UnityEngine.CoreModule.dll"),b.object->Get()));
                Vec2 pos{r.x-bounds.x,r.y-bounds.y},size{r.w,r.h};void* p[]{&pos},*s[]{&size};
                Invoke(M("RectTransform","set_sizeDelta",1,"UnityEngine","UnityEngine.CoreModule.dll"),tr.Get(),s);
                Invoke(M("RectTransform","set_anchoredPosition",1,"UnityEngine","UnityEngine.CoreModule.dll"),tr.Get(),p);b.placed=r;
            }
        }
    }
}
// The inventory/stash/shop panel state through direct calls to the game's own
// UIBase queries: no scene search, no boxing, no managed allocation per check.
bool InventoryVisible(){
    static const il2cpp::Field* instance=game::FindStaticField("LE.dll","","UIBase","instance");
    static const void* resolvedFor=nullptr;
    static const il2cpp::Method* queries[3]{};
    void* ui=game::StaticObject(instance);if(!ui||!game::IsAlive(ui))return false;
    // Resolve on the instance's runtime class once (as PanelOpen does), then call directly.
    const void* cls=il2cpp::api().object_get_class(ui);
    if(cls!=resolvedFor){
        int i=0;for(const char* name:{"IsInventoryOpen","IsStashOpen","IsShopOpen"})queries[i++]=Method(ui,name,0);
        resolvedFor=cls;
    }
    using Query=bool(*)(void*,const il2cpp::Method*);
    for(const auto* q:queries)if(reinterpret_cast<Query>(*reinterpret_cast<void* const*>(q))(ui,q))return true;
    return false;
}
void InventoryButtons(){
    ++inventoryChecks;
    // The shortcuts are children of the inventory panel and hide with it. While it
    // is closed there is nothing to place, and a closed (inactive) panel cannot be
    // found anyway: FindObjectsOfType skips it. Searching four times a second
    // during play only cost frame time.
    if(!InventoryVisible()){if(shortcuts.empty())inventoryStatus="inventory-closed";return;}
    if(!shortcuts.empty()&&inventoryPanel.Get()){
        bool enabled=!shortcutBusy;if(shortcutEnabled!=enabled){shortcutEnabled=enabled;void* a[]{&enabled};
            for(auto& b:shortcuts)Invoke(Method(b.button->Get(),"set_interactable",1),b.button->Get(),a);}
        ExpireSellConfirm();PlaceShortcuts();return;
    }
    ClearShortcuts();sellConfirmUntil=0;
    // Creation profile: one core.log line per creation, so a slow step is visible.
    std::string profile;uint64_t mark=Micros();
    const auto phase=[&](const char* name){const auto now=Micros();profile+=std::string(" ")+name+"="+std::to_string((now-mark)/1000.0).substr(0,6)+"ms";mark=now;};
    void* panel=enabledInventoryPanel.Get();
    if(!panel){ // Enable hook unavailable or the panel was enabled before it went in.
        ++inventorySearches;
        auto panels=Find("LE.dll","","InventoryPanelUI");if(panels.empty()){inventoryStatus="inventory-closed";return;}
        panel=panels.front();phase("find");
    }else phase("panel");
    void* source=Get<void*>(panel,F("InventoryPanelUI","confirmRespecButton"));
    void* parent=Get<void*>(panel,F("InventoryPanelUI","menuRectTransform"));if(!source||!parent){inventoryStatus="inventory-template-unavailable";return;}
    void* left=Get<void*>(panel,F("InventoryPanelUI","leftButtonsContainer"));if(!left){inventoryStatus="inventory-footer-unavailable";return;}
    Root footer(Invoke(M("Transform","get_parent",0,"UnityEngine","UnityEngine.CoreModule.dll"),left));
    Root currencyName(il2cpp::api().string_new("Currencies"));void* findArgs[]{currencyName.Get()};
    Root currency(Invoke(M("Transform","Find",1,"UnityEngine","UnityEngine.CoreModule.dll"),footer.Get(),findArgs));
    if(!currency.Get()){inventoryStatus="inventory-currency-container-unavailable";return;} // No guessed screen coordinates.
    Root items(Invoke(M("Transform","get_parent",0,"UnityEngine","UnityEngine.CoreModule.dll"),footer.Get()));
    Root gridName(il2cpp::api().string_new("Inventory Grid "));void* ga[]{gridName.Get()};
    Root grid(Invoke(M("Transform","Find",1,"UnityEngine","UnityEngine.CoreModule.dll"),items.Get(),ga));
    phase("footer");
    auto click=M("Button","OnPointerClick",1,"UnityEngine.UI","UnityEngine.UI.dll"),submit=M("Button","OnSubmit",1,"UnityEngine.UI","UnityEngine.UI.dll");
    std::string why;
    if(!hook::IsInstalled(click.code)&&!hook::Install(click.code,reinterpret_cast<void*>(&Click),reinterpret_cast<void**>(&originalClick),&why))throw std::runtime_error(why);
    if(!hook::IsInstalled(submit.code)&&!hook::Install(submit.code,reinterpret_cast<void*>(&Submit),reinterpret_cast<void**>(&originalSubmit),&why))throw std::runtime_error(why);
    phase("hooks");
    auto* buttonType=game::FindClass("UnityEngine.UI.dll","UnityEngine.UI","Button");
    Root bt(il2cpp::api().type_get_object(il2cpp::api().class_get_type(buttonType)));
    Root templateObject(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),source));
    const auto* instantiate=Signature("Object","Instantiate",{"UnityEngine.Object","UnityEngine.Transform","System.Boolean"});
    phase("template");
    for(auto pair:std::array<std::pair<const char*,const char*>,4>{{{"stash","Stash"},{"stashall","Stash all"},{"vendor","Vendor"},{"sellall","Sell all"}}}){
        bool world=false;void* a[]{templateObject.Get(),parent,&world};Root object(Invoke(instantiate,nullptr,a));
        bool off=false;void* offArgs[]{&off};Invoke(Method(object.Get(),"SetActive",1),object.Get(),offArgs);
        void* bargs[]{bt.Get()};Root button(Invoke(Signature("GameObject","GetComponent",{"System.Type"}),object.Get(),bargs));
        shortcuts.push_back({std::make_unique<Root>(button.Get()),std::make_unique<Root>(object.Get()),pair.first,pair.second,{}});
        phase("clone");
        Root layoutType(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("UnityEngine.UI.dll","UnityEngine.UI","LayoutElement"))));
        void* la[]{layoutType.Get()};void* element=Invoke(Signature("GameObject","GetComponent",{"System.Type"}),object.Get(),la);
        if(!element)element=Invoke(Signature("GameObject","AddComponent",{"System.Type"}),object.Get(),la);
        Root layout(element);
        bool ignore=true;void* ia[]{&ignore};Invoke(M("LayoutElement","set_ignoreLayout",1,"UnityEngine.UI","UnityEngine.UI.dll"),layout.Get(),ia);
        // A cloned game button can use legacy UI.Text rather than TMP, and its
        // localization component must not restore the original blessing label.
        // Only components below our cloned object are touched.
        bool inactive=true;
        phase("layout");
        for(auto spec:std::array<std::tuple<const char*,const char*,const char*>,4>{{
            {"Unity.Localization.dll","UnityEngine.Localization.Components","LocalizeStringEvent"},
            {"LE.dll","PixelCrushers.DialogueSystem","LocalizeUIText"},
            {"LE.dll","PixelCrushers.DialogueSystem.TextMeshPro","LocalizeTextMeshPro"},
            {"LE.dll","PixelCrushers.DialogueSystem","LocalizeTextMesh"}}}){
            auto* cls=game::FindClass(std::get<0>(spec),std::get<1>(spec),std::get<2>(spec));if(!cls)continue;
            Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls)));void* a[]{type.Get(),&inactive};
            Root components(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),object.Get(),a));
            for(uintptr_t i=0;i<Get<uintptr_t>(components.Get(),0x18);++i){void* component=Get<void*>(components.Get(),0x20+8*i);bool enabled=false;void* ea[]{&enabled};Invoke(M("Behaviour","set_enabled",1,"UnityEngine","UnityEngine.CoreModule.dll"),component,ea);}
        }
        phase("localize");
        Root text(il2cpp::api().string_new(pair.second));
        for(bool tmp:{true,false}){
            Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass(tmp?"Unity.TextMeshPro.dll":"UnityEngine.UI.dll",tmp?"TMPro":"UnityEngine.UI",tmp?"TMP_Text":"Text"))));
            void* targs[]{type.Get(),&inactive};Root texts(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),object.Get(),targs));
            for(uintptr_t i=0;i<Get<uintptr_t>(texts.Get(),0x18);++i){void* label=Get<void*>(texts.Get(),0x20+8*i);shortcuts.back().labels.push_back(std::make_unique<Root>(label));
                bool autosize=false;void* autoArgs[]{&autosize};Invoke(Method(label,tmp?"set_enableAutoSizing":"set_resizeTextForBestFit",1),label,autoArgs);
                if(tmp){float fontSize=15;void* a[]{&fontSize};Invoke(Method(label,"set_fontSize",1),label,a);}else{int fontSize=15;void* a[]{&fontSize};Invoke(Method(label,"set_fontSize",1),label,a);}
                void* args[]{text.Get()};Invoke(Method(label,"set_text",1),label,args);}
        }
        if(shortcuts.back().labels.empty())throw std::runtime_error("Inventory shortcut has no supported text label.");
        phase("labels");
        Root transform(Invoke(M("GameObject","get_transform",0,"UnityEngine","UnityEngine.CoreModule.dll"),object.Get()));
        // Parent to the inventory so closing/tabs inherit its visibility. The
        // dedicated row below the item grid determines its position.
        Vec2 size{100,32},pos{0,0},zero{0,0};Vec3 scale{1,1,1};
        void* za[]{&zero};for(const char* name:{"set_anchorMin","set_anchorMax","set_pivot"})Invoke(M("RectTransform",name,1,"UnityEngine","UnityEngine.CoreModule.dll"),transform.Get(),za);
        void* sa[]{&size},*pa[]{&pos},*sca[]{&scale};Invoke(M("RectTransform","set_sizeDelta",1,"UnityEngine","UnityEngine.CoreModule.dll"),transform.Get(),sa);
        Invoke(M("RectTransform","set_anchoredPosition",1,"UnityEngine","UnityEngine.CoreModule.dll"),transform.Get(),pa);Invoke(M("Transform","set_localScale",1,"UnityEngine","UnityEngine.CoreModule.dll"),transform.Get(),sca);
        bool on=true;void* args[]{&on};Invoke(Method(object.Get(),"SetActive",1),object.Get(),args);
        phase("show");
    }
    inventoryPanel.Set(panel);menuTransform.Set(parent);footerTransform.Set(footer.Get());currencyTransform.Set(currency.Get());gridTransform.Set(grid.Get());
    currencyOriginalSize=Value<Vec2>(M("RectTransform","get_sizeDelta",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency.Get());
    currencyOriginalPosition=Value<Vec2>(M("RectTransform","get_anchoredPosition",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency.Get());
    currencyOriginalScale=Value<Vec3>(M("Transform","get_localScale",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency.Get());
    if(grid.Get()){
        gridOriginalScale=Value<Vec3>(M("Transform","get_localScale",0,"UnityEngine","UnityEngine.CoreModule.dll"),grid.Get());
        gridOriginalPosition=Value<Vec2>(M("RectTransform","get_anchoredPosition",0,"UnityEngine","UnityEngine.CoreModule.dll"),grid.Get());
    }
    shortcutEnabled=false;inventoryStatus="created";PlaceShortcuts();
    phase("place");Log("inventory shortcuts created:%s",profile.c_str());
}
void RestoreGraphics(){
    disabledBehaviours.store(std::make_shared<const std::vector<DisabledBehaviour>>());
    for(auto& entry:saved){bool v=entry.before;if(entry.field)il2cpp::api().field_set_value(entry.value->Get(),entry.field,&v);
        else if(game::IsAlive(entry.value->Get())){void* a[]{&v};Invoke(M("Behaviour","set_enabled",1,"UnityEngine","UnityEngine.CoreModule.dll"),entry.value->Get(),a);}}
    saved.clear();
    if(shadowCaptured){void* a[]{&originalShadows};Invoke(M("QualitySettings","set_shadows",1,"UnityEngine","UnityEngine.CoreModule.dll"),nullptr,a);shadowCaptured=false;}
}
void DisableBehaviour(void* behaviour,Setting* setting){
    if(!behaviour||!game::IsAlive(behaviour))return;
    auto exists=std::find_if(saved.begin(),saved.end(),[&](const Saved& s){return !s.field&&s.value->Get()==behaviour;});
    if(exists==saved.end()){
        if(saved.size()>=256)throw std::runtime_error("Too many graphics overrides; disable effects and retry.");
        bool before=Value<bool>(M("Behaviour","get_enabled",0,"UnityEngine","UnityEngine.CoreModule.dll"),behaviour);
        saved.push_back({std::make_unique<Root>(behaviour),std::make_unique<Root>(behaviour),nullptr,before,setting});
        PublishDisabled();
    }
    if(Value<bool>(M("Behaviour","get_enabled",0,"UnityEngine","UnityEngine.CoreModule.dll"),behaviour)){
        bool enabled=false;void* a[]{&enabled};Invoke(M("Behaviour","set_enabled",1,"UnityEngine","UnityEngine.CoreModule.dll"),behaviour,a);}
}
void Graphics(int searches=kSearchProbes|kSearchFogCameras){
    // Runtime renderer parameters only. Never write the graphics INI/settings
    // repository. Keep the original value of each instantiated scene profile.
    for(auto it=saved.begin();it!=saved.end();){
        if(it->setting->value==0||!game::IsAlive(it->volume->Get())){
            bool v=it->before;if(it->field)il2cpp::api().field_set_value(it->value->Get(),it->field,&v);
            else if(game::IsAlive(it->value->Get())){void* a[]{&v};Invoke(M("Behaviour","set_enabled",1,"UnityEngine","UnityEngine.CoreModule.dll"),it->value->Get(),a);}
            it=saved.erase(it);
        }else ++it;
    }
    PublishDisabled();
    if(shadows.value && !shadowCaptured){originalShadows=Value<int>(M("QualitySettings","get_shadows",0,"UnityEngine","UnityEngine.CoreModule.dll"));shadowCaptured=true;}
    if(shadowCaptured){
        // Write the quality setting only when it differs from the wanted value.
        int value=shadows.value?0:originalShadows;
        if(Value<int>(M("QualitySettings","get_shadows",0,"UnityEngine","UnityEngine.CoreModule.dll"))!=value){void* a[]{&value};Invoke(M("QualitySettings","set_shadows",1,"UnityEngine","UnityEngine.CoreModule.dll"),nullptr,a);}
        if(!shadows.value)shadowCaptured=false;
    }
    if(!(reflections.value||bloom.value||motionBlur.value||occlusion.value||fog.value))return;
    auto remember=[&](void* value,void* volume,const il2cpp::Field* field,Setting* setting){
        auto exists=std::find_if(saved.begin(),saved.end(),[&](const Saved& s){return s.value->Get()==value&&s.field==field;});
        if(exists==saved.end()){
            if(saved.size()>=256)throw std::runtime_error("Too many graphics overrides; disable effects and retry.");
            bool before=false;il2cpp::api().field_get_value(value,field,&before);
            saved.push_back({std::make_unique<Root>(value),std::make_unique<Root>(volume),field,before,setting});
        }
        bool current=false;il2cpp::api().field_get_value(value,field,&current);
        if(current){bool disabled=false;il2cpp::api().field_set_value(value,field,&disabled);}
    };
    if(reflections.value||bloom.value||motionBlur.value||occlusion.value){
        // Every enabled volume registers itself with the manager in OnEnable.
        constexpr const char* kImage="Unity.Postprocessing.Runtime.dll";constexpr const char* kNs="UnityEngine.Rendering.PostProcessing";
        static const auto volumesOffset=game::FieldOffset(kImage,kNs,"PostProcessManager","m_Volumes");
        void* manager=nullptr;
        if(!volumesOffset||!game::TryStaticObject(game::FindStaticField(kImage,kNs,"PostProcessManager","s_Instance"),&manager))throw std::runtime_error("Post-processing volume list unavailable.");
        if(void* list=manager?Get<void*>(manager,volumesOffset):nullptr)for(void* volume:Entries(list)){
            if(!volume||!game::IsAlive(volume))continue;
            Root profile(Invoke(Method(volume,"get_profile",0),volume));
            auto* field=Field(profile.Get(),"settings");void* settingsList=nullptr;il2cpp::api().field_get_value(profile.Get(),field,&settingsList);
            for(void* effect:Entries(settingsList))if(effect){
                const std::string name=il2cpp::api().class_get_name(il2cpp::api().object_get_class(effect));
                Setting* setting=name=="Bloom"?&bloom:name=="MotionBlur"?&motionBlur:name=="AmbientOcclusion"?&occlusion:name=="ScreenSpaceReflections"?&reflections:nullptr;
                if(!setting||!setting->value)continue;
                void* enabled=nullptr;il2cpp::api().field_get_value(effect,Field(effect,"enabled"),&enabled);
                if(enabled)remember(enabled,volume,Field(enabled,"value"),setting);
            }
        }
    }
    if(fog.value){
        // The camera rendering volumetric fog; re-enables are clamped by the
        // set_enabled detour. Other fog cameras are found by an explicit switch.
        void* camera=nullptr;
        if(game::TryStaticObject(game::FindStaticField("LE.dll","","HxVolumetricCamera","Active"),&camera))DisableBehaviour(camera,&fog);
        if(searches&kSearchFogCameras){++graphicsScans;for(void* other:Find("LE.dll","","HxVolumetricCamera"))DisableBehaviour(other,&fog);}
    }
    if(reflections.value&&(searches&kSearchProbes)){++graphicsScans;for(void* probe:Find("UnityEngine.CoreModule.dll","UnityEngine","ReflectionProbe"))DisableBehaviour(probe,&reflections);}
}
bool InteractionsOn(){return active.load(std::memory_order_relaxed)&&(chests.value||shrines.value||portals.value);}
void ClearInteractions(){candidates.clear();pendingHandlers.clear();candidateCursor=0;bootstrapType=0;discover=true;interactionScene=0;}
// A new zone's chests, shrines, portals and caves register through the Awake
// and initialization hooks while it loads, so a scene change only drops the
// previous zone's objects; the six scene searches run once, when switched on.
void PruneInteractions(){
    std::erase_if(candidates,[](const Candidate& c){return !game::IsAlive(c.object->Get());});
    std::erase_if(pendingHandlers,[](const std::unique_ptr<Root>& h){return !game::IsAlive(h->Get());});
    candidateCursor=0;
}
void AddCandidate(void* object,void* handler,int kind){
    if(!object||!game::IsAlive(object)||candidates.size()>=4096)return;
    for(const auto& entry:candidates)if((handler&&entry.handler&&entry.handler->Get()==handler)||entry.object->Get()==object)return;
    candidates.push_back({std::make_unique<Root>(object),handler?std::make_unique<Root>(handler):nullptr,kind,0});
}
void HandlerAwake(void* self,MethodInfo info){
    originalHandlerAwake(self,info);
    if(InteractionsOn()&&!lifecycle::Stopping())game::Guarded([&]{if(pendingHandlers.size()<4096){pendingHandlers.push_back(std::make_unique<Root>(self));++handlersQueued;}},nullptr);
}
void PortalInitialize(void* self,MethodInfo info){originalPortalInitialize(self,info);
    if(InteractionsOn()&&!lifecycle::Stopping())game::Guarded([&]{AddCandidate(self,nullptr,2);},nullptr);}
void CaveAwake(void* self,MethodInfo info){originalCaveAwake(self,info);
    if(InteractionsOn()&&!lifecycle::Stopping())game::Guarded([&]{AddCandidate(self,nullptr,3);},nullptr);}
void ShrineEra(void* self,int era,MethodInfo info){originalShrineEra(self,era,info);
    if(InteractionsOn()&&!lifecycle::Stopping())game::Guarded([&]{AddCandidate(self,nullptr,1);},nullptr);}
void ShrineSetup(void* self,void* object,bool networking,int kind,const Vec3* position,MethodInfo info){
    originalShrineSetup(self,object,networking,kind,position,info);
    if(InteractionsOn()&&!lifecycle::Stopping())game::Guarded([&]{
        Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("LE.dll","","ShrineVisualsCreator"))));
        void* args[]{type.Get()};void* component=Invoke(Signature("GameObject","GetComponent",{"System.Type"}),object,args);
        AddCandidate(component,nullptr,1);
    },nullptr);
}
void ClassifyHandler(void* handler){
    if(!handler||!game::IsAlive(handler))return;
    for(const char* cls:{"ChestVisualsCreator","ChestSync","ShrineVisualsCreator","ShrineSync"}){
        Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("LE.dll","",cls))));bool includeInactive=false;void* a[]{type.Get(),&includeInactive};
        void* component=Invoke(Signature("Component","GetComponentInParent",{"System.Type","System.Boolean"}),handler,a);
        // Awake may precede assignment of the component's conditionHandler.
        // Keep the owning component; resolve its own handler after initialization.
        if(component){void* own=Get<void*>(component,F(cls,"conditionHandler"));AddCandidate(component,own,cls[0]=='C'?0:1);return;}
    }
}
// A Scene is one handle-sized integer in this supported Unity build. The
// player object can outlive a zone, so zone changes use the active scene.
int ActiveScene(){return Value<int>(M("SceneManager","GetActiveScene",0,"UnityEngine.SceneManagement","UnityEngine.CoreModule.dll"));}
void Interactions(void* actor){
    if(items::IsInspectingItem()||PanelOpen())return;
    const auto start=Micros();
    Root go(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),actor));
    const int scene=ActiveScene();
    if(scene!=interactionScene){EP_NOTE("interaction-scene "+std::to_string(interactionScene)+"->"+std::to_string(scene));
        if(interactionScene)PruneInteractions();interactionScene=scene;portalGrace=GetTickCount64()+10000;}
    if(discover){
        // Bootstrap existing objects once, one type per frame. Awake/initialize
        // notifications register later spawns; idle frames never search a scene.
        static const char* types[]{"ChestVisualsCreator","ChestSync","ShrineVisualsCreator","ShrineSync","PortalSync","CaveEntranceLogic"};
        const char* cls=types[bootstrapType];int kind=static_cast<int>(bootstrapType/2);
        const char* ns=bootstrapType==4?"Networking.Multiplayer.Interactables.Portals":"";
        const auto found=Find("LE.dll",ns,cls);
        for(void* object:found){
            void* handler=bootstrapType<4?Get<void*>(object,F(cls,"conditionHandler")):nullptr;
            AddCandidate(object,handler,bootstrapType==5?3:kind);
        }
        EP_NOTE(std::string("discover ")+cls+" "+std::to_string(found.size()),Micros()-start);
        ++sceneSearches;if(++bootstrapType==6)discover=false;
        maximumDiscoveryMicros=std::max(maximumDiscoveryMicros,Micros()-start);return;
    }
    if(!pendingHandlers.empty()){
        rules::ClassifyBudget budget{0,start};
        while(!pendingHandlers.empty()&&budget.Next(Micros())){auto handler=std::move(pendingHandlers.back());pendingHandlers.pop_back();ClassifyHandler(handler->Get());++handlersClassified;}
        EP_NOTE("classify left="+std::to_string(pendingHandlers.size()),Micros()-start);
        maximumInteractionMicros=std::max(maximumInteractionMicros,Micros()-start);return;}
    const auto now=GetTickCount64();if(now<nextInteraction||candidates.empty())return;
    const Vec3 player=Position(actor);rules::Budget budget{0,start};size_t visited=0;
    while(visited<candidates.size()&&budget.Next(Micros())){
        if(candidateCursor>=candidates.size())candidateCursor=0;
        auto& entry=candidates[candidateCursor++];++visited;++interactionChecks;
        auto* setting=entry.kind==0?&chests:entry.kind==1?&shrines:&portals;
        void* object=entry.object->Get();
        if(!setting->value||now<entry.retryAfter||!game::IsAlive(object)||(entry.kind>=2&&now<portalGrace))continue;
        void* handler=entry.handler?entry.handler->Get():nullptr;
        if(!handler&&entry.kind<2){handler=Get<void*>(object,F(il2cpp::api().class_get_name(il2cpp::api().object_get_class(object)),"conditionHandler"));
            if(handler)entry.handler=std::make_unique<Root>(handler);}
        if(entry.kind==3){object=Get<void*>(object,F("CaveEntranceLogic","usePlayerSpawnInteraction"));
            if(!object||!game::IsAlive(object)||Get<bool>(object,F("UsePlayerSpawnInteraction","startedAsyncInteraction")))continue;
            handler=Get<void*>(object,F("BaseInteraction","conditionHandler"));}
        if(!Near(object,player))continue;
        if(entry.kind==2){if(!Get<bool>(object,F("PortalSync","initialized","Networking.Multiplayer.Interactables.Portals")))continue;
            EP_NOTE("trigger portal");Invoke(Method(object,"SendAttemptUse",0),object);}
        else{
            if(!handler||!game::IsAlive(handler)||Value<bool>(Method(handler,"triggeredAndCannotTriggerMultipleTimes",0),handler))continue;
            void* a[]{go.Get()};if(!Value<bool>(Method(handler,"CheckConditionReadiness",1),handler,a)){entry.retryAfter=now+1000;EP_NOTE(std::string("not-ready ")+il2cpp::api().class_get_name(il2cpp::api().object_get_class(object)),Micros()-start);continue;}
            EP_NOTE(std::string("trigger ")+il2cpp::api().class_get_name(il2cpp::api().object_get_class(object)),Micros()-start);
            Invoke(Method(handler,"TryTriggerInteraction",1),handler,a);
        }
        ++entry.attempts;const auto wait=rules::TriggerRetryMs(entry.attempts);
        entry.retryAfter=wait==UINT64_MAX?UINT64_MAX:now+wait;nextInteraction=now+250;
        if(entry.kind==0)++openedChests;else if(entry.kind==1)++usedShrines;
        else{++usedPortals;portals.value=0;portalGrace=now+10000;}
        break; // Never run a second native interaction in this frame.
    }
    maximumInteractionMicros=std::max(maximumInteractionMicros,Micros()-start);
}
// ---- Interaction area outlines: one LineRenderer per eligible chest, shrine
// or portal within 60 game units, in the active scene (gone with the zone).
struct Ring{std::unique_ptr<Root> object;uint64_t version=0;};
std::map<void*,Ring> rings;  // candidate object -> its outline GameObject
std::unique_ptr<Root> ringMaterial;std::string ringShader,areaError;
uint64_t nextRingUpdate=0,ringsCreated=0;
const il2cpp::Class* UnityClass(const char* name){return game::FindClass("UnityEngine.CoreModule.dll","UnityEngine",name);}
void* RingMaterial(){
    if(ringMaterial&&game::IsAlive(ringMaterial->Get()))return ringMaterial->Get();
    for(const char* name:{"Sprites/Default","Unlit/Color","UI/Default","Hidden/Internal-Colored"}){
        Root text(il2cpp::api().string_new(name));void* a[]{text.Get()};
        void* shader=Invoke(Signature("Shader","Find",{"System.String"}),nullptr,a);
        if(!shader||!game::IsAlive(shader))continue;
        Root material(il2cpp::api().object_new(UnityClass("Material")));void* sa[]{shader};
        Invoke(Signature("Material",".ctor",{"UnityEngine.Shader"}),material.Get(),sa);
        ringMaterial=std::make_unique<Root>(material.Get());ringShader=name;return material.Get();
    }
    throw std::runtime_error("The game has no shader for the interaction area outline.");
}
void DestroyRing(Ring& ring){
    void* object=ring.object?ring.object->Get():nullptr;
    if(object&&game::IsAlive(object)){void* a[]{object};Invoke(Signature("Object","Destroy",{"UnityEngine.Object"}),nullptr,a);}
    ring.object.reset();
}
void ClearRings(){for(auto& [object,ring]:rings)game::Guarded([&]{DestroyRing(ring);},nullptr);rings.clear();}
void* CreateRing(void* target){
    const Vec3 c=Position(target);const int shape=areaShape.load();const double radius=areaRadius.load();
    void* material=RingMaterial();
    Root name(il2cpp::api().string_new("EpochPact interaction area"));
    Root go(il2cpp::api().object_new(UnityClass("GameObject")));void* na[]{name.Get()};
    Invoke(Signature("GameObject",".ctor",{"System.String"}),go.Get(),na);
    Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(UnityClass("LineRenderer"))));void* ta[]{type.Get()};
    void* line=Invoke(Signature("GameObject","AddComponent",{"System.Type"}),go.Get(),ta);
    if(!line)throw std::runtime_error("LineRenderer unavailable.");
    auto call=[&](const char* cls,const char* method,void* value){void* a[]{value};Invoke(M(cls,method,1,"UnityEngine","UnityEngine.CoreModule.dll"),line,a);};
    bool yes=true,no=false;float width=0.08f;int shadows=0;const int points=rules::AreaPoints(shape);
    call("LineRenderer","set_useWorldSpace",&yes);call("LineRenderer","set_loop",&yes);
    call("LineRenderer","set_startWidth",&width);call("LineRenderer","set_endWidth",&width);
    int count=points;call("LineRenderer","set_positionCount",&count);
    for(int i=0;i<points;++i){double x=0,z=0;rules::AreaPoint(shape,radius,i,x,z);Vec3 p{c.x+static_cast<float>(x),c.y+0.15f,c.z+static_cast<float>(z)};
        int index=i;void* a[]{&index,&p};Invoke(M("LineRenderer","SetPosition",2,"UnityEngine","UnityEngine.CoreModule.dll"),line,a);}
    struct Color{float r,g,b,a;};const uint32_t rgb=areaColor.load();
    Color color{((rgb>>16)&255)/255.f,((rgb>>8)&255)/255.f,(rgb&255)/255.f,0.9f};
    call("LineRenderer","set_startColor",&color);call("LineRenderer","set_endColor",&color);
    call("Renderer","set_sharedMaterial",material);call("Renderer","set_shadowCastingMode",&shadows);call("Renderer","set_receiveShadows",&no);
    return go.Get();
}
void UpdateRings(void* actor){
    const auto now=GetTickCount64();if(now<nextRingUpdate)return;nextRingUpdate=now+250;
    if(!areaShow.load()){if(!rings.empty())ClearRings();return;}
    const Vec3 player=Position(actor);std::set<void*> keep;int created=0;
    for(auto& entry:candidates){
        auto* setting=entry.kind==0?&chests:entry.kind==1?&shrines:&portals;
        void* object=entry.object->Get();
        if(!setting->value||!object||!game::IsAlive(object)||entry.retryAfter==UINT64_MAX)continue;
        const Vec3 at=Position(object);const double dx=at.x-player.x,dz=at.z-player.z;if(dx*dx+dz*dz>3600)continue;
        if(entry.kind<2){void* handler=entry.handler?entry.handler->Get():nullptr;
            if(handler&&game::IsAlive(handler)&&Value<bool>(Method(handler,"triggeredAndCannotTriggerMultipleTimes",0),handler))continue;}
        keep.insert(object);
        auto found=rings.find(object);
        if(found!=rings.end()&&found->second.version==areaVersion&&found->second.object&&game::IsAlive(found->second.object->Get()))continue;
        if(created>=6)continue;  // a few new outlines per update
        if(found!=rings.end()){DestroyRing(found->second);rings.erase(found);}
        rings.emplace(object,Ring{std::make_unique<Root>(CreateRing(object)),areaVersion});++created;++ringsCreated;
    }
    for(auto it=rings.begin();it!=rings.end();)if(!keep.contains(it->first)){DestroyRing(it->second);it=rings.erase(it);}else ++it;
}
std::string AllyType(void* actor){
    Root gameObject(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),actor));
    std::string name=Text(Invoke(M("Object","get_name",0,"UnityEngine","UnityEngine.CoreModule.dll"),gameObject.Get()));
    for(size_t at;(at=name.find("(Clone)"))!=std::string::npos;)name.erase(at,7);
    while(!name.empty()&&name.back()==' ')name.pop_back();
    return name.empty()?"Ally":name;
}
// The game's own name for an ally, as its health bar shows it ("Skeleton Archer"):
// the display name; for an ally with a custom name, the base name it had before.
std::string AllyLabel(void* actor){
    try{
        static const auto* cls=game::FindClass("LE.dll","","ActorDisplayInformation");
        if(!cls)return {};
        Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls)));
        void* args[]{type.Get()};void* info=Invoke(Signature("Component","GetComponent",{"System.Type"}),actor,args);
        if(!info)info=Invoke(Signature("Component","GetComponentInChildren",{"System.Type"}),actor,args);
        if(!info)return {};
        const bool custom=Get<bool>(info,F("BaseDisplayInformation","CustomNameSet"));
        std::string name=Text(Get<void*>(info,F("BaseDisplayInformation",custom?"baseDisplayName":"displayName")));
        if(name.empty())name=Text(Get<void*>(info,F("BaseDisplayInformation",custom?"displayName":"baseDisplayName")));
        // Rich text tags and control characters are not part of the name.
        std::string clean;bool tag=false;
        for(char ch:name){if(ch=='<')tag=true;else if(ch=='>')tag=false;else if(!tag&&static_cast<unsigned char>(ch)>=0x20)clean+=ch;}
        while(!clean.empty()&&clean.back()==' ')clean.pop_back();
        while(!clean.empty()&&clean.front()==' ')clean.erase(0,1);
        return clean.size()<=96?clean:std::string();
    }catch(const std::exception&){return {};}
}
// ---- damage window icons: the game's own skill icon for each ally type (the
// skill that summoned it, or the one the game's minion cards show instead) and
// the class or mastery icon for You. Keys are found as allies are seen; the
// picture is only read when the damage window asks for it (damageicon <key>).
std::map<std::string,std::unique_ptr<Root>> damageIconSkills;  // a:<skill ID> -> the skill
std::map<std::string,std::string> damageIcons;                 // key -> PNG, base64
std::map<void*,std::string> damageIconSkillKeys;              // the skill -> its key
// A player skill is keyed by its skill ID; a picture-only skill without one (the
// game's minion cards use such skills, e.g. for skeleton warriors and archers)
// by its number in this list.
std::string SkillIconKey(void* skill){
    if(!skill||!Get<void*>(skill,F("Ability","abilitySpriteSoftRef")))return {};
    if(auto it=damageIconSkillKeys.find(skill);it!=damageIconSkillKeys.end())return it->second;
    if(damageIconSkills.size()>=256)return {};
    const std::string id=Text(Get<void*>(skill,F("Ability","playerAbilityID")));
    const bool player=!id.empty()&&id.size()<=32&&id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos;
    const std::string key=player?"a:"+id:"a:_"+std::to_string(damageIconSkills.size());
    if(!damageIconSkills.contains(key))damageIconSkills.emplace(key,std::make_unique<Root>(skill));
    damageIconSkillKeys.emplace(skill,key);
    return key;
}
// The picture the game's minion cards show for this ally, else the skill that summoned it.
std::string AllyIconKey(void* summon){
    try{
        if(Get<bool>(summon,F("Summoned","overrideMinionSprite")))
            if(auto key=SkillIconKey(Get<void*>(summon,F("Summoned","minionSpriteAbility")));!key.empty())return key;
        void* references=Get<void*>(summon,F("Summoned","references"));
        return references?SkillIconKey(Get<void*>(references,F("CreationReferences","thisAbility"))):std::string();
    }catch(const std::exception&){return {};}
}
std::string PlayerIconKey(void* player){
    try{
        void* data=Get<void*>(Get<void*>(player,F("Actor","characterDataTracker")),F("CharacterDataTracker","charData"));
        const int cls=Value<int>(Method(data,"get_CharacterClass",0),data),mastery=Value<uint8_t>(Method(data,"get_ChosenMastery",0),data);
        if(cls<0||cls>99||mastery>99)return {};
        return "c:"+std::to_string(cls)+":"+std::to_string(mastery);
    }catch(const std::exception&){return {};}
}
std::string Base64(const unsigned char* data,size_t size){
    static const char digits[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;out.reserve((size+2)/3*4);
    for(size_t i=0;i<size;i+=3){
        const uint32_t v=uint32_t(data[i])<<16|(i+1<size?uint32_t(data[i+1])<<8:0)|(i+2<size?data[i+2]:0);
        out+=digits[v>>18&63];out+=digits[v>>12&63];out+=i+1<size?digits[v>>6&63]:'=';out+=i+2<size?digits[v&63]:'=';
    }
    return out;
}
// A sprite as a PNG of at most 64×64. Sprites usually sit in a shared atlas
// texture the game keeps unreadable, so the atlas is drawn into a temporary
// render texture, the sprite's rectangle copied out, and that drawn again at
// the small size. Everything made here is released before returning.
std::string SpritePng(void* sprite){
    auto core=[](const char* cls,const char* name,int n){return M(cls,name,n,"UnityEngine","UnityEngine.CoreModule.dll");};
    void* texture=Invoke(core("Sprite","get_texture",0),sprite);if(!texture)throw std::runtime_error("The icon has no texture.");
    struct Rect{float x,y,width,height;};
    const Rect area=Value<Rect>(core("Sprite","get_textureRect",0),sprite);
    int width=Value<int>(Method(texture,"get_width",0),texture),height=Value<int>(Method(texture,"get_height",0),texture);
    if(width<1||height<1||width>8192||height>8192||!(area.width>=1&&area.height>=1)||area.x<0||area.y<0||area.x+area.width>width+0.5f||area.y+area.height>height+0.5f)
        throw std::runtime_error("The icon's size is not readable.");
    const auto* temporary=Signature("RenderTexture","GetTemporary",{"System.Int32","System.Int32","System.Int32","UnityEngine.RenderTextureFormat","UnityEngine.RenderTextureReadWrite"});
    const auto* release=Signature("RenderTexture","ReleaseTemporary",{"UnityEngine.RenderTexture"});
    const auto* blit=Signature("Graphics","Blit",{"UnityEngine.Texture","UnityEngine.RenderTexture"});
    const auto* create=Signature("Texture2D",".ctor",{"System.Int32","System.Int32","UnityEngine.TextureFormat","System.Boolean"});
    const auto* read=Signature("Texture2D","ReadPixels",{"UnityEngine.Rect","System.Int32","System.Int32"});
    const auto* apply=Signature("Texture2D","Apply",{});
    const auto* destroy=Signature("Object","Destroy",{"UnityEngine.Object"});
    auto setActive=core("RenderTexture","set_active",1);
    const auto* texture2d=game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","Texture2D");
    if(!texture2d)throw std::runtime_error("Texture2D unavailable.");
    // ARGB32, written as sRGB so the PNG keeps the colours the game shows.
    int depth=0,format=0,readWrite=2,rgba32=4;bool mipmaps=false;
    std::vector<void*> renderTextures,textures;
    void* previous=Invoke(core("RenderTexture","get_active",0));
    auto cleanup=[&]{
        try{void* args[]{previous};Invoke(setActive,nullptr,args);}catch(const std::exception&){}
        for(void* rt:renderTextures)try{void* args[]{rt};Invoke(release,nullptr,args);}catch(const std::exception&){}
        for(void* t:textures)try{void* args[]{t};Invoke(destroy,nullptr,args);}catch(const std::exception&){}
    };
    try{
        auto target=[&](int w,int h){void* args[]{&w,&h,&depth,&format,&readWrite};void* rt=Invoke(temporary,nullptr,args);if(!rt)throw std::runtime_error("No render texture.");renderTextures.push_back(rt);return rt;};
        auto copy=[&](void* source,void* rt,Rect from,int w,int h){
            void* blitArgs[]{source,rt};Invoke(blit,nullptr,blitArgs);
            void* activeArgs[]{rt};Invoke(setActive,nullptr,activeArgs);
            void* t=il2cpp::api().object_new(texture2d);if(!t)throw std::runtime_error("No texture.");
            void* createArgs[]{&w,&h,&rgba32,&mipmaps};Invoke(create,t,createArgs);textures.push_back(t);
            int zero=0;void* readArgs[]{&from,&zero,&zero};Invoke(read,t,readArgs);Invoke(apply,t);
            return t;};
        const int cropW=static_cast<int>(area.width+0.5f),cropH=static_cast<int>(area.height+0.5f);
        void* crop=copy(texture,target(width,height),Rect{area.x,area.y,float(cropW),float(cropH)},cropW,cropH);
        const int side=64,w=std::max(1,cropW>=cropH?side:side*cropW/cropH),h=std::max(1,cropH>=cropW?side:side*cropH/cropW);
        void* small=copy(crop,target(w,h),Rect{0,0,float(w),float(h)},w,h);
        void* encodeArgs[]{small};
        void* png=Invoke(M("ImageConversion","EncodeToPNG",1,"UnityEngine","UnityEngine.ImageConversionModule.dll"),nullptr,encodeArgs);
        if(!png)throw std::runtime_error("The icon could not be encoded.");
        const auto size=Get<uintptr_t>(png,0x18);if(!size||size>1024*1024)throw std::runtime_error("The icon could not be encoded.");
        std::string out=Base64(reinterpret_cast<const unsigned char*>(png)+0x20,size);
        cleanup();return out;
    }catch(...){cleanup();throw;}
}
size_t DamageTypeIndex(const std::string& name){
    for(size_t i=0;i<damageTypes.size();++i)if(damageTypes[i].name==name)return i;
    damageTypes.push_back(DamageType{name});return damageTypes.size()-1;
}
void ListDamageAlly(void* actor,void* summon){
    const size_t index=DamageTypeIndex(AllyType(actor));damageActorType[actor]=index;damageActorRoots.emplace(actor,std::make_unique<Root>(actor));
    if(damageTypes[index].icon.empty())damageTypes[index].icon=AllyIconKey(summon);
    if(damageTypes[index].label.empty())damageTypes[index].label=AllyLabel(actor);
}
// ---- Damage taken. The attacker's name as the game shows it (cached per attacker).
std::string AttackerName(void* attacker){
    if(!attacker)return "Unknown";
    if(auto it=attackerNames.find(attacker);it!=attackerNames.end())return it->second.first;
    if(!game::IsAlive(attacker))return "Unknown";
    std::string name=AllyLabel(attacker);if(name.empty())name=AllyType(attacker);
    if(name.empty()||name=="Ally")name="Unknown";
    if(attackerNames.size()>=256)attackerNames.clear();
    attackerNames.emplace(attacker,std::make_pair(name,std::make_unique<Root>(attacker)));
    return name;
}
// What hit: an ailment ticking (its name) or a skill's damage (the skill's name).
std::string HitWhat(void* source,void* stats,bool* dot){
    *dot=stats&&takenFields.isHit&&!Get<bool>(stats,takenFields.isHit);
    if(!source)return {};
    for(auto* c=il2cpp::api().object_get_class(source);c;c=il2cpp::api().class_get_parent(c)){
        const char* name=il2cpp::api().class_get_name(c);if(!name)break;
        if(std::strcmp(name,"ActiveAilment")==0){
            *dot=true;void* ailment=Get<void*>(source,takenFields.ailment);
            return ailment?Text(Get<void*>(ailment,takenFields.ailmentName)):std::string();
        }
        if(std::strcmp(name,"DamageStatsHolder")==0){
            void* refs=Get<void*>(source,takenFields.refs);void* ability=refs?Get<void*>(refs,takenFields.thisAbility):nullptr;
            return ability?Text(Get<void*>(ability,takenFields.abilityName)):std::string();
        }
    }
    return {};
}
std::string PlainText(std::string text){
    std::string out;bool tag=false;for(char ch:text){if(ch=='<')tag=true;else if(ch=='>')tag=false;else if(!tag)out+=ch;}
    while(!out.empty()&&out.back()==' ')out.pop_back();return out;
}
// The character's buffs and debuffs as the game's buff bar shows them.
std::vector<DeathEffect> PlayerEffects(){
    std::vector<DeathEffect> out;
    void* manager=game::StaticObject(game::FindStaticField("LE.dll","","BuffsUIManager","instance"));
    if(!manager||!game::IsAlive(manager))return out;
    for(const char* list:{"debuffs","buffs"})for(void* entry:Entries(Get<void*>(manager,F("BuffsUIManager",list)))){
        if(out.size()>=32)break;
        if(!entry||!game::IsAlive(entry))continue;const int stacks=Get<int>(entry,F("BuffUIEntry","stacks"));if(stacks<=0)continue;
        void* definition=Get<void*>(entry,F("BuffUIEntry","uiBuff"));if(!definition)continue;
        out.push_back({PlainText(Text(Invoke(Method(definition,"getName",0),definition))),stacks,Get<bool>(entry,F("BuffUIEntry","isBuff"))});
    }
    return out;
}
void RecordDeath(uint64_t tick){
    DeathRecord death;death.tick=tick;death.number=++deathCount;
    death.maxHealth=static_cast<float>(Get<int>(takenHealth,takenFields.maximum));
    for(const auto& h:takenRecent)if(h.tick<=tick&&tick-h.tick<=kDeathWindowMs)death.hits.push_back(h);
    if(GetCurrentThreadId()==mainThreadId)game::Guarded([&]{death.effects=PlayerEffects();},nullptr);
    deathRecords.push_back(std::move(death));
    while(deathRecords.size()>kDeathsKept)deathRecords.pop_front();
}
// One row per attacker type; past 64 types (each keeps an hour of seconds) the rest share one row.
size_t TakenTypeIndex(const std::string& attacker){
    const std::string name=takenIndex.contains(attacker)||takenTypes.size()<63?attacker:std::string("Other attackers");
    if(auto it=takenIndex.find(name);it!=takenIndex.end())return it->second;
    takenTypes.push_back(DamageType{name});takenDot.push_back(0);takenTypes.back().label=name;
    return takenIndex[name]=takenTypes.size()-1;
}
void RecordTaken(void* attacker,void* source,void* stats,const Elements& parts,float damage,float healthBefore,float wardBefore){
    TakenHit hit;hit.tick=GetTickCount64();hit.damage=std::isfinite(damage)&&damage>0?damage:0;hit.parts=parts;
    hit.health=Get<float>(takenHealth,takenFields.current);hit.ward=Get<float>(takenProtection,takenFields.ward);
    if(GetCurrentThreadId()==mainThreadId)game::Guarded([&]{hit.attacker=attacker&&attacker==takenActor?std::string("You"):AttackerName(attacker);hit.what=HitWhat(source,stats,&hit.dot);},nullptr);
    if(hit.attacker.empty())hit.attacker="Unknown";
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
    if(hit.damage>0){auto& t=testTakenBy[hit.attacker];t[0]+=hit.damage;t[1]+=(healthBefore-hit.health)+(wardBefore-hit.ward);++t[2];if(hit.dot)++t[3];}
    else ++testTakenZero;
#else
    (void)wardBefore;
#endif
    if(hit.damage<=0)return;
    if(damageSession.load(std::memory_order_relaxed)){
        const size_t index=TakenTypeIndex(hit.attacker);
        const uint32_t second=static_cast<uint32_t>((hit.tick-damageSessionStart)/1000)+1;
        CreditHit(takenTypes[index],hit.damage,parts,second);if(hit.dot)takenDot[index]+=hit.damage;
        if(second!=takenLastSecond){takenLastSecond=second;++takenCombatSeconds;}
    }
    const bool died=healthBefore>0&&hit.health<=0;
    takenRecent.push_back(std::move(hit));
    rules::TrimRecent(takenRecent,takenRecent.back().tick,kTakenKeepMs,kTakenMost);
    if(died)RecordDeath(takenRecent.back().tick);
}
// The character the damage taken belongs to (main thread, about once a second).
void RefreshTakenTarget(){
    void* actor=Actor();
    if(!actor){takenActor=nullptr;takenProtection=nullptr;takenHealth=nullptr;takenRoot.reset();return;}
    if(!takenFields.ready){
        takenFields.ward=F("ProtectionClass","ward");takenFields.current=F("BaseHealth","currentHealth");takenFields.maximum=F("BaseHealth","maxHealth");
        takenFields.isHit=F("DamageStats","isHit");takenFields.ailment=F("AilmentReceiver.ActiveAilment","ailment");takenFields.ailmentName=F("Ailment","displayName");
        takenFields.refs=F("DamageStatsHolder","refs");takenFields.thisAbility=F("CreationReferences","thisAbility");takenFields.abilityName=F("Ability","abilityName");
        if(!damageArrayOffset)damageArrayOffset=F("DamageStats","damage");
        takenFields.ready=true;
    }
    if(actor==takenActor&&takenProtection)return;
    takenRoot=std::make_unique<Root>(actor);takenActor=actor;
    takenProtection=Get<void*>(actor,F("Actor","protection"));takenHealth=Get<void*>(actor,F("Actor","health"));
    if(!takenProtection||!takenHealth){takenProtection=nullptr;takenHealth=nullptr;}
}
std::string DeathRecapJson(){
    std::ostringstream out;out.setf(std::ios::fixed);out.precision(1);
    const auto now=GetTickCount64();
    out<<"{\"ok\":true,\"enabled\":"<<Boolean(deathRecap.load())<<",\"recording\":"<<Boolean(takenProtection!=nullptr)<<",\"recentHits\":"<<takenRecent.size()<<",\"deaths\":[";
    bool firstDeath=true;
    for(auto d=deathRecords.rbegin();d!=deathRecords.rend();++d){
        if(!firstDeath)out<<',';firstDeath=false;
        const auto summary=rules::SummarizeRecap(d->hits,d->tick,kDeathWindowMs);
        out<<"{\"number\":"<<d->number<<",\"secondsAgo\":"<<(now-d->tick)/1000.0<<",\"maxHealth\":"<<d->maxHealth<<",\"total\":"<<summary.total
           <<",\"dot\":"<<summary.dot<<",\"unsplit\":"<<summary.unsplit<<",\"byType\":[";
        for(size_t e=0;e<kElements;++e){if(e)out<<',';out<<summary.byType[e];}
        out<<"],\"killer\":";
        if(summary.killer>=0){const auto& k=d->hits[static_cast<size_t>(summary.killer)];
            out<<"{\"attacker\":"<<Json(k.attacker)<<",\"what\":"<<Json(k.what)<<",\"dot\":"<<Boolean(k.dot)<<",\"damage\":"<<k.damage<<'}';}
        else out<<"null";
        out<<",\"attackers\":[";
        for(size_t i=0;i<summary.attackers.size();++i){const auto& a=summary.attackers[i];if(i)out<<',';
            out<<"{\"name\":"<<Json(a.name)<<",\"damage\":"<<a.damage<<",\"dot\":"<<a.dot<<",\"hits\":"<<a.hits<<'}';}
        out<<"],\"hits\":[";
        bool firstHit=true;
        for(const auto& h:d->hits){if(!firstHit)out<<',';firstHit=false;
            out<<"{\"t\":"<<-static_cast<double>(d->tick-h.tick)/1000.0<<",\"attacker\":"<<Json(h.attacker)<<",\"what\":"<<Json(h.what)<<",\"dot\":"<<Boolean(h.dot)
               <<",\"damage\":"<<h.damage<<",\"health\":"<<h.health<<",\"ward\":"<<h.ward<<",\"parts\":[";
            for(size_t e=0;e<kElements;++e){if(e)out<<',';out<<h.parts[e];}
            out<<"]}";}
        out<<"],\"effects\":[";
        for(size_t i=0;i<d->effects.size();++i){if(i)out<<',';out<<"{\"name\":"<<Json(d->effects[i].name)<<",\"stacks\":"<<d->effects[i].stacks<<",\"buff\":"<<Boolean(d->effects[i].buff)<<'}';}
        out<<"]}";
    }
    out<<"]}";
    return out.str();
}
// The player and every ally the player's summons created (recursively).
void RefreshDamageAllies(){
    void* player=Actor();if(!player)return;
    const auto now=GetTickCount64();
    for(auto it=damageActorRoots.begin();it!=damageActorRoots.end();){
        if(game::IsAlive(it->first)){damageActorDeadSince.erase(it->first);++it;continue;}
        const auto since=damageActorDeadSince.try_emplace(it->first,now).first->second;
        if(now-since>=kDeadAllyKeepMs||damageActorDeadSince.size()>kDeadAllyMax){damageActorType.erase(it->first);damageActorDeadSince.erase(it->first);it=damageActorRoots.erase(it);}
        else ++it;
    }
    if(!damageActorType.contains(player)){
        const size_t index=DamageTypeIndex("You");damageActorType[player]=index;damageActorRoots.emplace(player,std::make_unique<Root>(player));
        if(damageTypes[index].icon.empty())damageTypes[index].icon=PlayerIconKey(player);
    }
    std::vector<void*> creators{player};std::set<void*> seen{player};
    for(size_t ci=0;ci<creators.size()&&seen.size()<=256;++ci){
        void* tracker=Get<void*>(creators[ci],F("Actor","summonTracker"));if(!tracker)continue;
        for(void* summon:Entries(Get<void*>(tracker,F("SummonTracker","summons")))){
            if(!summon||!game::IsAlive(summon))continue;
            void* actor=Get<void*>(summon,F("Summoned","actor"));if(!actor||!game::IsAlive(actor)||!seen.insert(actor).second)continue;
            creators.push_back(actor);
            if(!damageActorType.contains(actor))ListDamageAlly(actor,summon);
        }
    }
}
// An attacker the session has not listed: an ally when its creator chain (Actor.summoned ->
// references -> creator) reaches the character or a listed ally. Field reads only for anything
// else (enemies), so the hook stays cheap.
bool ListLateAlly(void* attacker){
    if(!damageSummonedField||!damageReferencesField||!damageCreatorField)return false;
    void* summon=Get<void*>(attacker,damageSummonedField);if(!summon)return false;
    void* creator=attacker;bool ours=false;
    for(int depth=0;depth<8&&!ours;++depth){
        void* summoned=Get<void*>(creator,damageSummonedField);if(!summoned)return false;
        void* references=Get<void*>(summoned,damageReferencesField);if(!references)return false;
        creator=Get<void*>(references,damageCreatorField);if(!creator)return false;
        ours=damageActorType.contains(creator);
    }
    if(!ours)return false;
    bool listed=false;
    game::Guarded([&]{if(game::IsAlive(attacker)&&game::IsAlive(summon)){ListDamageAlly(attacker,summon);listed=true;}},nullptr);
    return listed;
}
std::string DamageState(){
    const auto now=damageSession.load()?GetTickCount64():damageSessionEnd;
    const double elapsed=damageSessionStart?std::max(0.0,(now-damageSessionStart)/1000.0):0;
    const uint32_t current=static_cast<uint32_t>(elapsed)+1;
    std::map<size_t,int> live;for(const auto& [actor,index]:damageActorType)if(!damageActorDeadSince.contains(actor)&&game::IsAlive(actor))++live[index];
    auto window=[&](const DamageType& t,uint32_t span){double sum=0;for(size_t i=0;i<t.seconds.size();++i)if(t.seconds[i]&&t.seconds[i]+span>current&&t.seconds[i]<=current)sum+=t.buckets[i];
        const double seconds=std::min<double>(span,std::max(1.0,elapsed));return sum/seconds;};
    // The same windows per damage type.
    using Sums=std::array<double,kElements>;
    auto elementWindow=[&](const DamageType& t,uint32_t span){Sums sum{};for(size_t i=0;i<t.seconds.size();++i)if(t.seconds[i]&&t.seconds[i]+span>current&&t.seconds[i]<=current)for(size_t e=0;e<kElements;++e)sum[e]+=t.elementBuckets[i][e];
        const double seconds=std::min<double>(span,std::max(1.0,elapsed));for(auto& v:sum)v/=seconds;return sum;};
    auto list=[&](std::ostream& o,const char* key,const auto& values){o<<'"'<<key<<"\":[";for(size_t e=0;e<kElements;++e){if(e)o<<',';o<<values[e];}o<<']';};
    struct ElementStats{Sums total{},hits{},maxHit{};std::array<Sums,4> windows{};};
    auto elements=[&](std::ostream& o,const ElementStats& s){o<<",\"elements\":{";list(o,"total",s.total);o<<',';list(o,"hits",s.hits);o<<',';list(o,"maxHit",s.maxHit);
        const char* keys[]{"dps10","dps60","dps600","dps3600"};for(size_t k=0;k<4;++k){o<<',';list(o,keys[k],s.windows[k]);}o<<'}';};
    ElementStats teamElements;
    std::ostringstream out;out.setf(std::ios::fixed);out.precision(1);
    out<<"{\"ok\":true,\"active\":"<<Boolean(damageSession.load())<<",\"elapsedSeconds\":"<<elapsed<<",\"combatSeconds\":"<<damageCombatSeconds<<",\"hits\":"<<damageHits<<",\"unsplitHits\":"<<damageUnsplit
       <<",\"elementNames\":[\"Physical\",\"Fire\",\"Cold\",\"Lightning\",\"Necrotic\",\"Void\",\"Poison\"],\"hooked\":"<<Boolean(hook::IsInstalled(M("ProtectionClass","ApplyDamage",5).code))<<",\"types\":[";
    double allyTotal=0;uint64_t allyHits=0;float allyMax=0;bool comma=false;
    std::array<double,4> teamWindows{};const std::array<uint32_t,4> spans{10,60,600,3600};
    for(size_t i=0;i<damageTypes.size();++i){const auto& t=damageTypes[i];if(comma)out<<',';comma=true;
        std::array<double,4> w{};for(size_t k=0;k<4;++k){w[k]=window(t,spans[k]);if(t.name!="You")teamWindows[k]+=w[k];}
        if(t.name!="You"){allyTotal+=t.total;allyHits+=t.hits;allyMax=std::max(allyMax,t.maxHit);}
        out<<"{\"name\":"<<Json(t.name)<<",\"label\":"<<Json(t.label)<<",\"icon\":"<<Json(t.icon)<<",\"live\":"<<live[i]<<",\"total\":"<<t.total<<",\"hits\":"<<t.hits<<",\"maxHit\":"<<t.maxHit
           <<",\"dps10\":"<<w[0]<<",\"dps60\":"<<w[1]<<",\"dps600\":"<<w[2]<<",\"dps3600\":"<<w[3]
           <<",\"average\":"<<(elapsed>0?t.total/elapsed:0)<<",\"combatAverage\":"<<(damageCombatSeconds?t.total/damageCombatSeconds:0);
        ElementStats s;
        for(size_t e=0;e<kElements;++e){s.total[e]=t.elementTotal[e];s.hits[e]=static_cast<double>(t.elementHits[e]);s.maxHit[e]=t.elementMax[e];}
        for(size_t k=0;k<4;++k)s.windows[k]=elementWindow(t,spans[k]);
        if(t.name!="You")for(size_t e=0;e<kElements;++e){teamElements.total[e]+=s.total[e];teamElements.hits[e]+=s.hits[e];teamElements.maxHit[e]=std::max(teamElements.maxHit[e],s.maxHit[e]);for(size_t k=0;k<4;++k)teamElements.windows[k][e]+=s.windows[k][e];}
        elements(out,s);out<<'}';}
    out<<"],\"allies\":{\"total\":"<<allyTotal<<",\"hits\":"<<allyHits<<",\"maxHit\":"<<allyMax<<",\"dps10\":"<<teamWindows[0]<<",\"dps60\":"<<teamWindows[1]
       <<",\"dps600\":"<<teamWindows[2]<<",\"dps3600\":"<<teamWindows[3]<<",\"average\":"<<(elapsed>0?allyTotal/elapsed:0)<<",\"combatAverage\":"<<(damageCombatSeconds?allyTotal/damageCombatSeconds:0);
    elements(out,teamElements);out<<'}';
    // Damage taken during the session, per attacker type (the Taken view).
    out<<",\"taken\":{\"types\":[";
    double takenTotal=0;uint64_t takenAllHits=0;float takenMax=0;double takenDotAll=0;std::array<double,4> takenWindows{};ElementStats takenElements;
    for(size_t i=0;i<takenTypes.size();++i){const auto& t=takenTypes[i];if(i)out<<',';
        std::array<double,4> w{};for(size_t k=0;k<4;++k){w[k]=window(t,spans[k]);takenWindows[k]+=w[k];}
        const double dot=i<takenDot.size()?takenDot[i]:0;takenTotal+=t.total;takenAllHits+=t.hits;takenMax=std::max(takenMax,t.maxHit);takenDotAll+=dot;
        out<<"{\"name\":"<<Json(t.name)<<",\"label\":"<<Json(t.name)<<",\"icon\":\"\",\"live\":0,\"total\":"<<t.total<<",\"hits\":"<<t.hits<<",\"maxHit\":"<<t.maxHit<<",\"dot\":"<<dot
           <<",\"dps10\":"<<w[0]<<",\"dps60\":"<<w[1]<<",\"dps600\":"<<w[2]<<",\"dps3600\":"<<w[3]
           <<",\"average\":"<<(elapsed>0?t.total/elapsed:0)<<",\"combatAverage\":"<<(takenCombatSeconds?t.total/takenCombatSeconds:0);
        ElementStats st;
        for(size_t e=0;e<kElements;++e){st.total[e]=t.elementTotal[e];st.hits[e]=static_cast<double>(t.elementHits[e]);st.maxHit[e]=t.elementMax[e];}
        for(size_t k=0;k<4;++k)st.windows[k]=elementWindow(t,spans[k]);
        for(size_t e=0;e<kElements;++e){takenElements.total[e]+=st.total[e];takenElements.hits[e]+=st.hits[e];takenElements.maxHit[e]=std::max(takenElements.maxHit[e],st.maxHit[e]);for(size_t k=0;k<4;++k)takenElements.windows[k][e]+=st.windows[k][e];}
        elements(out,st);out<<'}';}
    out<<"],\"total\":"<<takenTotal<<",\"hits\":"<<takenAllHits<<",\"maxHit\":"<<takenMax<<",\"dot\":"<<takenDotAll<<",\"combatSeconds\":"<<takenCombatSeconds
       <<",\"dps10\":"<<takenWindows[0]<<",\"dps60\":"<<takenWindows[1]<<",\"dps600\":"<<takenWindows[2]<<",\"dps3600\":"<<takenWindows[3]
       <<",\"average\":"<<(elapsed>0?takenTotal/elapsed:0)<<",\"combatAverage\":"<<(takenCombatSeconds?takenTotal/takenCombatSeconds:0);
    elements(out,takenElements);out<<'}';
    out<<",\"deaths\":"<<deathCount<<",\"deathRecap\":"<<Boolean(deathRecap.load())<<'}';
    return out.str();
}

void Frame(){
    ++frameUpdates;
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
    if(measureFrames){const auto sample=Micros();if(previousFrame&&frameGaps.size()<10000){frameGaps.push_back(sample-previousFrame);if(sample-previousFrame>=30000)Note("frame",sample-previousFrame);}previousFrame=sample;}
#endif
    if(lifecycle::Stopping())return;
    effects::Frame();
    rage::Frame();invtools::Frame();ingame::Frame();
    if(pendingStashPanel.Get()&&!game::Guarded([]{RestoreStashTab();},nullptr)){pendingStashPanel.Reset();++stashRestoreFailures;}
    if(pendingLegacyStash.Get()&&!game::Guarded([]{RestoreLegacyStashTab();},nullptr)){pendingLegacyStash.Reset();++stashRestoreFailures;}
    const auto clock=GetTickCount64();
    if(clock>=lastButtons+250){lastButtons=clock;std::string why;const auto started=Micros();
        if(!game::Guarded([]{if(Actor())InventoryButtons();else if(!shortcuts.empty())ClearShortcuts();},&why)){
            ++faults;lastButtons=clock+14000;Log("inventory buttons: %s",why.c_str());
            game::Guarded([]{ClearShortcuts();},nullptr);
        }
        const auto spent=Micros()-started;inventoryMicros+=spent;maximumInventoryMicros=std::max(maximumInventoryMicros,spent);
        if(spent>=1000)EP_NOTE("inventory-buttons",spent);
    }
    mainThreadId=GetCurrentThreadId();
    if(damageSession.load(std::memory_order_relaxed)&&clock>=damageAllyRefresh+1000){
        damageAllyRefresh=clock;
        if(!game::Guarded([]{RefreshDamageAllies();},nullptr))++faults;
    }
    if((deathRecap.load(std::memory_order_relaxed)||damageSession.load(std::memory_order_relaxed))&&clock>=takenTargetCheck+1000){
        // Damage taken: whose it is, and the hook kept in while the death recap is on.
        takenTargetCheck=clock;
        if(!game::Guarded([]{
            RefreshTakenTarget();
            if(deathRecap.load()&&takenProtection){auto damage=M("ProtectionClass","ApplyDamage",5);std::string why;
                if(!hook::IsInstalled(damage.code))hook::Install(damage.code,reinterpret_cast<void*>(&ObserveDamage),reinterpret_cast<void**>(&originalDamage),&why);}
        },nullptr))++faults;
    }
    if(!damageSession.load(std::memory_order_relaxed)&&!deathRecap.load(std::memory_order_relaxed)&&clock>monitorUntil+10000&&clock>=damageAllyRefresh+5000){
        // Nothing reads damage: the hook is removed so it costs nothing.
        damageAllyRefresh=clock;
        game::Guarded([]{auto damage=M("ProtectionClass","ApplyDamage",5);std::string why;if(hook::IsInstalled(damage.code))hook::Remove(damage.code,&why);},nullptr);
    }
    if(!active.load(std::memory_order_relaxed))return;
    const auto now=GetTickCount64();
    if(now<retryWork||now<lastWork+50)return;lastWork=now;
    std::string why;
    if(!game::Guarded([&]{
        void* actor=Actor();
        if(!actor)return; // Loading/alt-tab is a pause, not a setting reset.
        // Another character (or the same one loaded again): the switches stay as they are for it; only
        // the one-time portal switch goes off, and the previous scene's objects are let go.
        if(owner.Get()&&owner.Get()!=actor){RestoreGraphics();ClearInteractions();portals.value=0;owner.Reset();return;}
        if(!owner.Get()){owner.Set(actor);portalGrace=now+10000;}
        if(now>=lastScan+1000){lastScan=now;
            // Once a second: restore switched-off entries and apply the switches
            // to the game's registered volumes and the fog camera, without a
            // scene search. A new scene's reflection probes are searched once,
            // two seconds after the scene changes.
            if(reflections.value){
                const int scene=ActiveScene();
                if(scene!=graphicsScene){EP_NOTE("graphics-scene "+std::to_string(graphicsScene)+"->"+std::to_string(scene));graphicsScene=scene;probeSearchAt=now+2000;}
            }
            const auto tick=Micros();Graphics(0);
            maximumGraphicsMicros=std::max(maximumGraphicsMicros,Micros()-tick);EP_NOTE("graphics-tick",Micros()-tick);
        }else if(probeSearchAt&&now>=probeSearchAt){
            probeSearchAt=0;
            const auto started=Micros();Graphics(kSearchProbes);
            maximumGraphicsMicros=std::max(maximumGraphicsMicros,Micros()-started);
            EP_NOTE("graphics-scan probes",Micros()-started);
        }
        if(chests.value||shrines.value||portals.value){
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
            const auto started=Micros();Interactions(actor);if(Micros()-started>=1000)Note("interactions",Micros()-started);
#else
            Interactions(actor);
#endif
            std::string ringWhy;
            if(!game::Guarded([&]{UpdateRings(actor);},&ringWhy)){areaShow=false;areaError=ringWhy;game::Guarded([]{ClearRings();},nullptr);Log("interaction areas off: %s",ringWhy.c_str());}
        }else if(!rings.empty())game::Guarded([]{ClearRings();},nullptr);
    },&why)){++faults;retryWork=now+5000;Log("convenience: %s",why.c_str());}
}
std::string State(){std::ostringstream out;out<<"{\"ok\":true,\"settings\":{";bool comma=false;for(auto* s:settings){if(comma)out<<',';comma=true;out<<Json(s->key)<<":{\"value\":"<<s->value<<",\"available\":"<<Boolean(mainthread::HookInstalled())<<'}';}
    // The area settings as qol_area* controls read them.
    out<<",\"arearadius\":{\"value\":"<<areaRadius.load()<<",\"available\":true},\"areashape\":{\"value\":"<<areaShape.load()
       <<",\"available\":true},\"areashow\":{\"value\":"<<(areaShow.load()?1:0)<<",\"available\":true}";
    {char hex[8];std::snprintf(hex,sizeof hex,"%06X",areaColor.load()&0xFFFFFF);
     out<<"},\"area\":{\"radius\":"<<areaRadius.load()<<",\"shape\":"<<areaShape.load()<<",\"show\":"<<Boolean(areaShow.load())<<",\"color\":"<<Json(hex)
        <<",\"outlines\":"<<rings.size()<<",\"outlinesCreated\":"<<ringsCreated<<",\"shader\":"<<Json(ringShader)<<",\"error\":"<<Json(areaError);}
    unsigned visible=0;for(auto& b:shortcuts)if(Value<bool>(Method(b.object->Get(),"get_activeInHierarchy",0),b.object->Get()))++visible;
    return out.str()+"},\"inventoryButtons\":"+std::to_string(shortcuts.size())+",\"inventoryButtonsVisible\":"+std::to_string(visible)+",\"frameUpdates\":"+std::to_string(frameUpdates)+",\"inventoryChecks\":"+std::to_string(inventoryChecks)+",\"inventorySearches\":"+std::to_string(inventorySearches)+",\"inventoryMicros\":"+std::to_string(inventoryMicros)+",\"maximumInventoryMicros\":"+std::to_string(maximumInventoryMicros)+",\"inventoryStatus\":"+Json(inventoryStatus)+",\"graphicsOverrides\":"+std::to_string(saved.size())+",\"chestsOpened\":"+std::to_string(openedChests)+",\"shrinesUsed\":"+std::to_string(usedShrines)+",\"portalsUsed\":"+std::to_string(usedPortals)+",\"interactionCandidates\":"+std::to_string(candidates.size())+",\"handlersQueued\":"+std::to_string(handlersQueued)+",\"handlersClassified\":"+std::to_string(handlersClassified)+",\"handlersPending\":"+std::to_string(pendingHandlers.size())+",\"interactionChecks\":"+std::to_string(interactionChecks)+",\"sceneSearches\":"+std::to_string(sceneSearches)+",\"maximumDiscoveryMicros\":"+std::to_string(maximumDiscoveryMicros)+",\"maximumInteractionMicros\":"+std::to_string(maximumInteractionMicros)+",\"lastStashTab\":"+std::to_string(lastStashTab)+",\"stashTabRestores\":"+std::to_string(stashRestores)+",\"stashTabRestoreFailures\":"+std::to_string(stashRestoreFailures)+",\"graphicsScans\":"+std::to_string(graphicsScans)+",\"activeScene\":"+std::to_string(ActiveScene())+",\"graphicsScene\":"+std::to_string(graphicsScene)+",\"interactionScene\":"+std::to_string(interactionScene)+",\"maximumGraphicsMicros\":"+std::to_string(maximumGraphicsMicros)+",\"faults\":"+std::to_string(faults)+"}";}
}
bool Init(){
    findObjects=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Object","FindObjectsOfType",2);
    componentTransform=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Component","get_transform",0);
    transformPosition=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Transform","get_position",0);
    if(!(findObjects&&componentTransform&&transformPosition))return false;
    std::string why;
    if(!mainthread::RegisterFrameCallback(&Frame,&why)){Log("convenience bootstrap refused: %s",why.c_str());return false;}
    LoadStashTab();
    // Installing a hook briefly holds every game thread (about 250 ms each here).
    // Do it now, while the game is still starting, instead of on the first
    // inventory opening in play. A failure keeps the previous lazy installation.
    std::string hooks;
    if(!game::Guarded([&]{try{
        const std::tuple<game::MethodRef,void*,void**,const char*> specs[]{
            {M("Button","OnPointerClick",1,"UnityEngine.UI","UnityEngine.UI.dll"),reinterpret_cast<void*>(&Click),reinterpret_cast<void**>(&originalClick),"Button.OnPointerClick"},
            {M("Button","OnSubmit",1,"UnityEngine.UI","UnityEngine.UI.dll"),reinterpret_cast<void*>(&Submit),reinterpret_cast<void**>(&originalSubmit),"Button.OnSubmit"},
            {M("InventoryPanelUI","OnEnable",0),reinterpret_cast<void*>(&PanelEnable),reinterpret_cast<void**>(&originalPanelEnable),"InventoryPanelUI.OnEnable"},
            {M("StashPanelV2","OnOpen",2,kPanelNs),reinterpret_cast<void*>(&StashOpen),reinterpret_cast<void**>(&originalStashOpen),"StashPanelV2.OnOpen"},
            {M("StashPanelV2","OnClose",0,kPanelNs),reinterpret_cast<void*>(&StashClose),reinterpret_cast<void**>(&originalStashClose),"StashPanelV2.OnClose"},
            {M("StashTabbedUIControls","OnEnable",0),reinterpret_cast<void*>(&LegacyStashEnable),reinterpret_cast<void**>(&originalLegacyEnable),"StashTabbedUIControls.OnEnable"},
            {M("StashTabbedUIControls","OnDisable",0),reinterpret_cast<void*>(&LegacyStashDisable),reinterpret_cast<void**>(&originalLegacyDisable),"StashTabbedUIControls.OnDisable"}};
        for(const auto& [ref,detour,original,name]:specs){std::string reason;
            if(!hook::IsInstalled(ref.code)&&!hook::Install(ref.code,detour,original,&reason))hooks+=std::string(" ")+name+": "+reason;}
    }catch(const std::exception& e){hooks+=std::string(" ")+e.what();}},&hooks))hooks=" "+hooks;
    Log("inventory shortcut hooks %s%s",hooks.empty()?"installed at startup":"deferred:",hooks.c_str());
    return true;
}
std::string Read(){return Run([]{return State();});}
std::string Set(const std::string& key,double value){return Run([&]{
    auto it=std::find_if(settings.begin(),settings.end(),[&](Setting* s){return key==s->key;});
    if(it==settings.end()||!std::isfinite(value)||(value!=0&&value!=1))throw std::runtime_error("Unknown convenience setting or invalid switch.");
    Current();if(!mainthread::HookInstalled())throw std::runtime_error("UI frame dispatcher is unavailable.");
    if((key=="chests"||key=="shrines"||key=="portals")&&value){
        auto shrine=M("ShrineVisualsCreator","setEraPreset",1);std::string shrineReason;
        if(!hook::IsInstalled(shrine.code)&&!hook::Install(shrine.code,reinterpret_cast<void*>(&ShrineEra),reinterpret_cast<void**>(&originalShrineEra),&shrineReason))throw std::runtime_error(shrineReason);
        auto setup=M("ShrinesManager","SetUpShrineVisualsAndNetworking",4);
        if(!hook::IsInstalled(setup.code)&&!hook::Install(setup.code,reinterpret_cast<void*>(&ShrineSetup),reinterpret_cast<void**>(&originalShrineSetup),&shrineReason))throw std::runtime_error(shrineReason);
        for(auto spec:std::array<std::tuple<const char*,const char*,const char*,void*,void**>,3>{{
            {"ConditionHandler","Awake","",reinterpret_cast<void*>(&HandlerAwake),reinterpret_cast<void**>(&originalHandlerAwake)},
            {"PortalSync","OnInitialize","Networking.Multiplayer.Interactables.Portals",reinterpret_cast<void*>(&PortalInitialize),reinterpret_cast<void**>(&originalPortalInitialize)},
            {"CaveEntranceLogic","Awake","",reinterpret_cast<void*>(&CaveAwake),reinterpret_cast<void**>(&originalCaveAwake)}}}){
            auto ref=M(std::get<0>(spec),std::get<1>(spec),0,std::get<2>(spec));std::string reason;
            if(!hook::IsInstalled(ref.code)&&!hook::Install(ref.code,std::get<3>(spec),std::get<4>(spec),&reason))throw std::runtime_error(reason);
        }
        if(!(chests.value||shrines.value||portals.value))ClearInteractions();
    }
    if((key=="fog"||key=="reflections")&&value){auto enabled=M("Behaviour","set_enabled",1,"UnityEngine","UnityEngine.CoreModule.dll");std::string reason;
        if(!hook::IsInstalled(enabled.code)&&!hook::Install(enabled.code,reinterpret_cast<void*>(&Enabled),reinterpret_cast<void**>(&originalEnabled),&reason))throw std::runtime_error(reason);}
    const double previous=(*it)->value;
    (*it)->value=value;active=true;lastScan=0;retryWork=0;
    if(key=="reflections"&&value)graphicsScene=ActiveScene();  // searched below, not again two seconds later
    try {if(key!="chests"&&key!="shrines"&&key!="portals")Graphics(key=="reflections"?kSearchProbes:key=="fog"?kSearchFogCameras:0);}
    catch(...){(*it)->value=previous;try{Graphics(0);}catch(...){++faults;}throw;}
    return State();
});}
std::string SetArea(const std::string& key,const std::string& value){return Run([=]{
    if(key=="radius"){double r=0;if(!statedit::Number(value,&r)||!(r>=rules::MinimumAreaRadius&&r<=rules::MaximumAreaRadius))throw std::runtime_error("use qolarea radius 1-30");areaRadius=r;}
    else if(key=="shape"){if(value!="0"&&value!="1")throw std::runtime_error("use qolarea shape 0 (circle) or 1 (square)");areaShape=value=="1"?1:0;}
    else if(key=="show"){if(value!="0"&&value!="1")throw std::runtime_error("use qolarea show 0 or 1");areaShow=value=="1";areaError.clear();}
    else if(key=="color"){if(value.size()!=6||value.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)throw std::runtime_error("use qolarea color RRGGBB");
        areaColor=static_cast<uint32_t>(std::stoul(value,nullptr,16));}
    else throw std::runtime_error("use qolarea radius|shape|show|color value");
    ++areaVersion;nextRingUpdate=0;if(!areaShow)ClearRings();
    return State();
});}
std::string DamageSession(const std::string& action){return Run([=]{
    if(action=="start"){
        if(!Actor())throw std::runtime_error("Load an offline character first.");
        auto damage=M("ProtectionClass","ApplyDamage",5);std::string why;
        if(!hook::IsInstalled(damage.code)&&!hook::Install(damage.code,reinterpret_cast<void*>(&ObserveDamage),reinterpret_cast<void**>(&originalDamage),&why))throw std::runtime_error(why);
        damageArrayOffset=F("DamageStats","damage");
        damageSummonedField=F("Actor","summoned");damageReferencesField=F("Summoned","references");damageCreatorField=F("CreationReferences","creator");
        damageTypes.clear();damageActorType.clear();damageActorRoots.clear();damageActorDeadSince.clear();damageCombatSeconds=damageLastCombatSecond=damageHits=damageUnsplit=0;
        takenTypes.clear();takenDot.clear();takenIndex.clear();takenCombatSeconds=takenLastSecond=0;RefreshTakenTarget();
        damageSessionStart=GetTickCount64();damageSessionEnd=0;RefreshDamageAllies();damageAllyRefresh=GetTickCount64();damageSession=true;
    }else if(action=="stop"){
        if(damageSession.exchange(false))damageSessionEnd=GetTickCount64();
        damageActorType.clear();damageActorRoots.clear();damageActorDeadSince.clear();
    }else if(action=="reset"){
        const bool was=damageSession.load();damageTypes.clear();damageActorType.clear();damageActorRoots.clear();damageActorDeadSince.clear();damageCombatSeconds=damageLastCombatSecond=damageHits=damageUnsplit=0;
        takenTypes.clear();takenDot.clear();takenIndex.clear();takenCombatSeconds=takenLastSecond=0;
        damageSessionStart=GetTickCount64();damageSessionEnd=damageSessionStart;if(was)RefreshDamageAllies();
    }else if(!action.empty())throw std::runtime_error("use damagesession [start|stop|reset]");
    return DamageState();
});}
std::string DamageIcon(const std::string& key){return Run([=]{
    const auto reply=[&](const std::string* png){return "{\"ok\":true,\"key\":"+Json(key)+",\"png\":"+(png?"\""+*png+"\"":std::string("null"))+"}";};
    if(auto it=damageIcons.find(key);it!=damageIcons.end())return reply(&it->second);
    void* sprite=nullptr;std::unique_ptr<Root> load;
    if(key.rfind("a:",0)==0){
        // A skill seen in this game session: its icon, loaded the way the game's skill bar loads it.
        auto it=damageIconSkills.find(key);void* skill=it==damageIconSkills.end()?nullptr:it->second->Get();
        void* soft=skill?Get<void*>(skill,F("Ability","abilitySpriteSoftRef")):nullptr;
        if(!soft)return reply(nullptr);
        int line=0;void* args[]{soft,il2cpp::api().string_new("EpochPact damage window"),&line};
        void* ref=Invoke(M("SoftRefExtensions","CreateLoadRefTypeless",3,"LE.AssetBundles","LE.Addressables.dll"),nullptr,args);
        if(!ref)return reply(nullptr);
        load=std::make_unique<Root>(ref);
        Invoke(Method(ref,"BlockForLoad",0),ref);
        sprite=Invoke(Method(ref,"get_TypelessAsset",0),ref);
    }else{
        int cls=-1,mastery=-1;const auto colon=key.find(':',2);
        const auto number=[](const std::string& s,int* v){if(s.empty()||s.size()>2||s.find_first_not_of("0123456789")!=std::string::npos)return false;*v=std::stoi(s);return true;};
        if(key.rfind("c:",0)!=0||colon==std::string::npos||!number(key.substr(2,colon-2),&cls)||!number(key.substr(colon+1),&mastery))
            throw std::runtime_error("use damageicon a:<skill ID>|c:<class>:<mastery>");
        void* args[]{&cls,&mastery};sprite=Invoke(M("CharacterClassList","getClassOrMasteryIcon",2),nullptr,args);
    }
    if(!sprite||!game::IsAlive(sprite)||std::string(il2cpp::api().class_get_name(il2cpp::api().object_get_class(sprite)))!="Sprite")return reply(nullptr);
    Root keep(sprite);
    const std::string png=SpritePng(sprite);
    if(damageIcons.size()<64)damageIcons[key]=png;
    return reply(&png);
});}
std::string DeathRecap(const std::string& action){return Run([=]{
    if(action=="0"||action=="1"){deathRecap=action=="1";if(!deathRecap)takenRecent.clear();}
    else if(action=="clear"){deathRecords.clear();}
    else if(!action.empty())throw std::runtime_error("use deathrecap [0|1|clear]");
    return DeathRecapJson();
});}
std::string Summons(){return Run([]{
    const auto c=Current();void* tracker=Get<void*>(c.actor,F("Actor","summonTracker"));
    const auto now=GetTickCount64();
    if(damageOwner.Get()!=c.actor||now>monitorUntil){monitoredAllies.clear();damageSamples={};damageSampleIndex=0;lastDroppedDamage=0;monitorStarted=now;damageOwner.Set(c.actor);}
    auto damage=M("ProtectionClass","ApplyDamage",5);std::string why;
    if(!hook::IsInstalled(damage.code)&&!hook::Install(damage.code,reinterpret_cast<void*>(&ObserveDamage),reinterpret_cast<void**>(&originalDamage),&why))throw std::runtime_error(why);
    monitorUntil=now+3000;
    struct DamageTotal{double total=0;float last=0;uint64_t lastAt=0;size_t hits=0;};
    std::map<void*,DamageTotal> damageTotals;
    for(const auto& sample:damageSamples)if(sample.actor&&sample.time<=now&&now-sample.time<=5000){
        auto& total=damageTotals[sample.actor];total.total+=sample.damage;++total.hits;
        if(sample.time>=total.lastAt){total.lastAt=sample.time;total.last=sample.damage;}
    }
    std::set<void*> liveAllies;
    std::ostringstream out;out<<"{\"ok\":true,\"player\":"<<Player(c)<<",\"sampledAt\":"<<GetTickCount64()<<",\"summons\":[";bool comma=false;
    std::vector<void*> trackers{c.actor};std::set<void*> seen{c.actor};
    for(size_t ti=0;ti<trackers.size();++ti){void* creator=trackers[ti];tracker=Get<void*>(creator,F("Actor","summonTracker"));
    if(tracker)for(void* summon:Entries(Get<void*>(tracker,F("SummonTracker","summons")))){
        if(!summon||!game::IsAlive(summon))continue;
        void* references=Get<void*>(summon,F("Summoned","references"));
        if(!references||Get<void*>(references,F("CreationReferences","creator"))!=creator)continue;
        void* actor=Get<void*>(summon,F("Summoned","actor"));if(!actor||!game::IsAlive(actor))continue;
        if(!seen.insert(actor).second)continue;
        if(seen.size()>256)throw std::runtime_error("Summon count outside supported bounds.");
        trackers.push_back(actor);
        liveAllies.insert(actor);if(!monitoredAllies.contains(actor))monitoredAllies.emplace(actor,std::make_unique<Root>(actor));
        void* health=Get<void*>(actor,F("Actor","health"));void* stats=Get<void*>(actor,F("Actor","stats"));
        if(!health||!stats||Get<float>(health,F("BaseHealth","currentHealth"))<=0)continue;
        void* precalc=Get<void*>(stats,F("BaseStats","precalculatedStats"));if(!precalc)continue;
        if(comma)out<<',';comma=true;
        Root gameObject(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),actor));
        out<<"{\"name\":"<<Json(Text(Invoke(M("Object","get_name",0,"UnityEngine","UnityEngine.CoreModule.dll"),gameObject.Get())))
            <<",\"instance\":"<<Value<int>(M("Object","GetInstanceID",0,"UnityEngine","UnityEngine.CoreModule.dll"),actor)
            <<",\"health\":"<<Get<float>(health,F("BaseHealth","currentHealth"))<<",\"maxHealth\":"<<Get<int>(health,F("BaseHealth","maxHealth"))
            <<",\"armour\":"<<Get<float>(precalc,F("PrecalculatedStatsHolder","armour"))<<",\"resistances\":{";
        bool next=false;for(const char* name:{"Physical","Fire","Cold","Lightning","Void","Necrotic","Poison"}){
            if(next)out<<',';next=true;out<<Json(name)<<':'<<Get<float>(precalc,F("PrecalculatedStatsHolder",(std::string("uncapped")+name+"Resistance").c_str()))*100.;}
        out<<"},\"damageIncreased\":";
        uint8_t property=1,special=0;int tags=0,extra=0; // SP.Damage is validated from current enum below.
        auto* enumClass=game::FindClass("LE.dll","","SP");auto* field=il2cpp::api().class_get_field_from_name(enumClass,"Damage");
        if(!field)throw std::runtime_error("Damage property unavailable.");il2cpp::api().field_static_get_value(field,&property);
        void* a[]{&property,&tags,&special,&extra};out<<Value<float>(M("Stats","GetTotalIncreased",4),stats,a)*100.;
        const auto total=damageTotals[actor];
        const double seconds=std::min(5.0,(now-monitorStarted)/1000.0);
        out<<",\"damage5s\":"<<total.total<<",\"damageEvents5s\":"<<total.hits<<",\"lastDamage\":"<<total.last<<",\"measuredDps\":"<<(seconds>0?total.total/seconds:0)<<'}';
    }}
    for(auto it=monitoredAllies.begin();it!=monitoredAllies.end();)if(!liveAllies.contains(it->first))it=monitoredAllies.erase(it);else ++it;
    return out.str()+"],\"damageWindowSeconds\":"+std::to_string(std::min(5.0,(now-monitorStarted)/1000.0))+",\"damageSampleClipped\":"+Boolean(lastDroppedDamage&&now-lastDroppedDamage<=5000)+",\"damageMethod\":\"native applied damage events\"}";
});}
std::string InventoryRead(){return Run([]{const auto c=Current();void* inventory=Get<void*>(c.manager,F("ItemContainersManager","inventory"));
    Root content(Invoke(Method(inventory,"GetContent",0),inventory));
    return "{\"ok\":true,\"player\":"+Player(c)+",\"inventoryItems\":"+std::to_string(Entries(content.Get()).size())+"}";
});}
// Sell all: every inventory item the vendor buys, through the game's own quick move into
// the shop (as Ctrl+click sells), after a recovery backup. The vendor is opened first when
// it is not open. Items the sellkeep setting keeps stay; sold items are in the vendor's
// buyback list as after a normal sale.
std::string SellAll(const std::string& id){
    progression::SnapshotData snapshot;std::string why;
    if(!mainthread::Run([&]{snapshot=progression::CaptureSnapshotCurrent(id,"sell-all");},5000,&why))return "{\"ok\":false,\"error\":"+Json(why)+"}";
    const std::string backup=progression::WriteCapturedSnapshot(snapshot);
    std::string result;std::vector<XY> positions;size_t index=0;int sold=0,kept=0,refused=0,goldBefore=0,waits=0;bool requested=false,planned=false;
    if(!mainthread::RunSteps([&]{try{
        const auto c=Current(id);void* ui=Ui();
        if(!Value<bool>(Method(ui,"IsShopOpen",0),ui)){
            if(requested){if(++waits>60)throw std::runtime_error("The vendor did not open.");return false;}
            requested=true;bool show=true,appearances=false,stash=false,sound=false;int tab=0;
            void* inventoryArgs[]{&show,&appearances,&stash,&tab,&sound};Invoke(Method(ui,"openInventory",5),ui,inventoryArgs);
            void* a[]{&show};Invoke(Method(ui,"openShop",1),ui,a);return false;
        }
        void* inventory=Get<void*>(c.manager,F("ItemContainersManager","inventory"));
        void* gold=Get<void*>(c.actor,F("Actor","goldTracker"));
        if(!planned){planned=true;goldBefore=gold?Get<int>(gold,F("GoldTracker","value")):0;
            Root content(Invoke(Method(inventory,"GetContent",0),inventory));
            for(void* entry:Entries(content.Get())){if(!entry)continue;
                if(invtools::KeepWhenSelling(Get<void*>(entry,F("ItemContainerEntry","data")))){++kept;continue;}
                positions.push_back(Get<XY>(entry,F("ItemContainerEntry","<Position>k__BackingField")));}}
        for(int n=0;n<4&&index<positions.size();++n,++index){int source=1,destination=22;bool autoEquip=false,autoStack=false;XY p=positions[index];
            void* a[]{&source,&destination,&p,&autoEquip,&autoStack};
            if(Value<bool>(M("ItemContainersManager","TryQuickMove",5),c.manager,a))++sold;else ++refused;}
        if(index<positions.size())return false;
        progression::SaveCurrent(id);
        const int goldAfter=gold?Get<int>(gold,F("GoldTracker","value")):0;
        result="{\"ok\":true,\"player\":"+Player(c)+",\"sold\":"+std::to_string(sold)+",\"kept\":"+std::to_string(kept)+",\"refused\":"+std::to_string(refused)
            +",\"gold\":"+std::to_string(goldAfter-goldBefore)+",\"openedVendor\":"+Boolean(requested)+"}";
    }catch(const std::exception& e){result="{\"ok\":false,\"error\":"+Json(e.what())+",\"sold\":"+std::to_string(sold)+"}";}return true;},8000,&why))result="{\"ok\":false,\"error\":"+Json(why)+"}";
    if(result.empty())result="{\"ok\":false,\"error\":\"Inventory operation did not complete.\"}";
    result.pop_back();return result+",\"backup\":"+Json(backup)+"}";
}
std::string Inventory(const std::string& id,const std::string& action){
    if(action=="stash"||action=="vendor"){
      bool requested=false,opened=false;std::string result,reason;
      if(!mainthread::RunSteps([&]{try{const auto c=Current(id);void* ui=Ui();
       if(!requested){requested=true;
        opened=!Value<bool>(Method(ui,action=="stash"?"IsStashOpen":"IsShopOpen",0),ui);
        if(!opened){bool sound=false;void* a[]{&sound};if(action=="stash")Invoke(Method(ui,"closeStash",1),ui,a);else Invoke(Method(ui,"closeShop",0),ui);}
        else if(action=="stash"){bool show=true,focus=true,clear=false,preserve=false;void* a[]{&show,&focus,&clear,nullptr,&preserve};Invoke(Method(ui,"openStash",5),ui,a);}
        else {bool show=true,appearances=false,stash=false,sound=false;int tab=0;
            void* inventoryArgs[]{&show,&appearances,&stash,&tab,&sound};Invoke(Method(ui,"openInventory",5),ui,inventoryArgs);
            void* a[]{&show};Invoke(Method(ui,"openShop",1),ui,a);}
        return false;
       }
       if(Value<bool>(Method(ui,action=="stash"?"IsStashOpen":"IsShopOpen",0),ui)!=opened)return false;
       game::Guarded([]{PlaceShortcuts();},nullptr);result="{\"ok\":true,\"player\":"+Player(c)+",\"action\":"+Json(action)+",\"opened\":"+Boolean(opened)+"}";
      }catch(const std::exception& e){result="{\"ok\":false,\"error\":"+Json(e.what())+"}";}return true;},5000,&reason))
          return "{\"ok\":false,\"error\":"+Json(reason.empty()?"The window did not finish opening. Refresh and try again.":reason)+"}";
      return result;
    }
    if(action=="sellall")return SellAll(id);
    if(action!="stashall")return "{\"ok\":false,\"error\":\"Unknown inventory action.\"}";
    progression::SnapshotData snapshot;std::string why;
    if(!mainthread::Run([&]{snapshot=progression::CaptureSnapshotCurrent(id,"stash-all");},5000,&why))return "{\"ok\":false,\"error\":"+Json(why)+"}";
    const std::string backup=progression::WriteCapturedSnapshot(snapshot);std::string result;std::vector<XY> positions;size_t index=0;int moved=0,remaining=0;
    if(!mainthread::RunSteps([&]{try{
        const auto c=Current(id);void* inventory=Get<void*>(c.manager,F("ItemContainersManager","inventory"));
        if(index==0&&positions.empty()){Root content(Invoke(Method(inventory,"GetContent",0),inventory));for(void* entry:Entries(content.Get()))if(entry)positions.push_back(Get<XY>(entry,F("ItemContainerEntry","<Position>k__BackingField")));}
        for(int n=0;n<4&&index<positions.size();++n,++index){int source=1,destination=21;bool autoEquip=false,autoStack=true;XY p=positions[index];
            void* a[]{&source,&destination,&p,&autoEquip,&autoStack};
            if(Value<bool>(M("ItemContainersManager","TryQuickMove",5),c.manager,a))++moved;else ++remaining;}
        if(index<positions.size())return false;
        progression::SaveCurrent(id);result="{\"ok\":true,\"player\":"+Player(c)+",\"moved\":"+std::to_string(moved)+",\"remaining\":"+std::to_string(remaining)+"}";
    }catch(const std::exception& e){result="{\"ok\":false,\"error\":"+Json(e.what())+",\"moved\":"+std::to_string(moved)+"}";}return true;},5000,&why))result="{\"ok\":false,\"error\":"+Json(why)+"}";
    if(result.empty())result="{\"ok\":false,\"error\":\"Inventory operation did not complete.\"}";
    result.pop_back();return result+",\"backup\":"+Json(backup)+"}";
}
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Test(const std::string& id,const std::string& action){
    if(action.rfind("visit-",0)==0){
        // A combat area for the damage session check; the waypoint request
        // queues its own main-thread job, so it is not nested in Run.
        const auto permission=Run([=]{const auto c=Current(id);if(c.name!="EpFeedbackTest")throw std::runtime_error("Convenience checks require the isolated EpFeedbackTest character.");return std::string("{\"ok\":true}");});
        if(permission!="{\"ok\":true}")return permission;
        const auto result=statedit::Waypoints(action.substr(6),id);
        const bool ok=result.rfind("travel: normal waypoint transition requested",0)==0;
        return std::string("{\"ok\":")+Boolean(ok)+",\"result\":"+Json(result)+"}";
    }
    if(action=="player-state")return Run([]{
        // Read only, needs no loaded character: why the character check refuses (offline play,
        // session state, the player actor, its health and the offline flag of its data).
        const auto session=SessionState();void* actor=Invoke(M("PlayerFinder","getPlayerActor",0));
        const bool alive=actor&&game::IsAlive(actor);void* health=alive?Get<void*>(actor,F("Actor","health")):nullptr;
        void* tracker=alive?Get<void*>(actor,F("Actor","characterDataTracker")):nullptr;void* data=tracker?Get<void*>(tracker,F("CharacterDataTracker","charData")):nullptr;
        std::ostringstream out;out<<"{\"ok\":true,\"offlinePlay\":"<<Boolean(game::IsOfflinePlay())<<",\"state\":"<<session.state<<",\"transitioning\":"<<Boolean(session.transitioning)
           <<",\"actor\":"<<Boolean(actor!=nullptr)<<",\"alive\":"<<Boolean(alive)
           <<",\"health\":"<<(health?Get<float>(health,F("BaseHealth","currentHealth")):-1.f)<<",\"maxHealth\":"<<(health?Get<int>(health,F("BaseHealth","maxHealth")):-1)
           <<",\"charData\":"<<Boolean(data!=nullptr)<<",\"offlineData\":"<<Boolean(data&&Value<bool>(Method(data,"get_IsOffline",0),data))<<'}';
        return out.str();
    });
    if(action=="player-respawn")return Run([]{
        // Disposable test character only: the dead test character respawns as the game's own respawn does.
        void* actor=Invoke(M("PlayerFinder","getPlayerActor",0));if(!actor||!game::IsAlive(actor))throw std::runtime_error("No player actor.");
        void* tracker=Get<void*>(actor,F("Actor","characterDataTracker"));void* data=tracker?Get<void*>(tracker,F("CharacterDataTracker","charData")):nullptr;
        if(!data||!Value<bool>(Method(data,"get_IsOffline",0),data)||Text(Get<void*>(data,F("CharacterData","<CharacterName>k__BackingField","LE.Data")))!="EpFeedbackTest")
            throw std::runtime_error("Convenience checks require the isolated EpFeedbackTest character.");
        void* health=Get<void*>(actor,F("Actor","health"));if(health&&Get<float>(health,F("BaseHealth","currentHealth"))>0)throw std::runtime_error("The test character is alive.");
        void* dying=Get<void*>(actor,F("Actor","dying"));if(!dying)throw std::runtime_error("Dying state unavailable.");
        Invoke(Method(dying,"ForceRespawn",0),dying);
        return std::string("{\"ok\":true,\"requested\":true}");
    });
    return Run([=]() -> std::string {
    const auto c=Current(id);if(c.name!="EpFeedbackTest")throw std::runtime_error("Convenience checks require the isolated EpFeedbackTest character.");
    void* ui=Ui();
    if(action=="currency-max"||action=="currency-normal"||action=="currency-read"){
        void* gold=Get<void*>(c.actor,F("Actor","goldTracker"));
        void* bones=Get<void*>(c.actor,F("Actor","ancientBonesTracker"));
        if(!gold||!bones)throw std::runtime_error("Currency trackers unavailable.");
        if(action=="currency-max"){
            if(!testCurrencySaved){testOriginalGold=Get<int>(gold,F("GoldTracker","value"));testOriginalBones=Get<int>(bones,F("AncientBonesTracker","value"));testCurrencySaved=true;}
            // Stress the real Int32 currency trackers through their native
            // absolute-load APIs, only in the disposable named test character.
            int value=std::numeric_limits<int>::max();void* args[]{&value};
            Invoke(Method(gold,"loadGold",1),gold,args);
            Invoke(Method(bones,"loadAncientBones",1),bones,args);
            if(Get<int>(gold,F("GoldTracker","value"))!=value||Get<int>(bones,F("AncientBonesTracker","value"))!=value)
                throw std::runtime_error("Native currency maximum readback failed.");
        }
        if(action=="currency-normal"){
            if(!testCurrencySaved)throw std::runtime_error("No test balance snapshot.");
            void* ga[]{&testOriginalGold},*ba[]{&testOriginalBones};
            Invoke(Method(gold,"loadGold",1),gold,ga);Invoke(Method(bones,"loadAncientBones",1),bones,ba);
        }
        return "{\"ok\":true,\"gold\":"+std::to_string(Get<int>(gold,F("GoldTracker","value")))+",\"ancientBones\":"+std::to_string(Get<int>(bones,F("AncientBonesTracker","value")))+"}";
    }
    if(action=="frames-start"){frameGaps.clear();marks.clear();marksOrigin=GetTickCount64();enabledCalls=0;previousFrame=0;measureFrames=true;maximumInteractionMicros=maximumDiscoveryMicros=0;inventoryChecks=inventorySearches=inventoryMicros=maximumInventoryMicros=0;return State();}
    if(action=="frames-stop"){measureFrames=false;std::ostringstream out;out<<"{\"ok\":true,\"setEnabledCalls\":"<<enabledCalls.load()<<",\"gapsMicros\":[";bool comma=false;for(auto value:frameGaps){if(comma)out<<',';comma=true;out<<value;}
        out<<"],\"marks\":[";comma=false;for(const auto& m:marks){if(comma)out<<',';comma=true;out<<'['<<m.at<<','<<Json(m.what)<<','<<m.micros<<']';}return out.str()+"]}";}
    if(action.rfind("scaler-",0)==0){InventoryButtons();void* menu=menuTransform.Get();if(!menu)throw std::runtime_error("Open the inventory first.");
        const int percent=std::stoi(action.substr(7));if(percent<75||percent>150)throw std::runtime_error("Unsupported native scaler fixture.");
        Root menuObject(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),menu));
        Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("LE.dll","","UISubScaler"))));
        bool inactive=true;void* args[]{type.Get(),&inactive};Root scalers(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),menuObject.Get(),args));
        const auto count=Get<uintptr_t>(scalers.Get(),0x18);float value=percent/100.f;
        for(uintptr_t i=0;i<count;++i){void* scaler=Get<void*>(scalers.Get(),0x20+8*i);
            Root object(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),scaler));
            if(!Get<bool>(scaler,F("UISubScaler","_initialized"))||!Value<bool>(Method(object.Get(),"get_activeInHierarchy",0),object.Get()))continue;
            if(percent==100)Invoke(Method(scaler,"UpdateScale",0),scaler);
            else{void* a[]{&value};for(auto [flag,method]:std::array<std::pair<const char*,const char*>,6>{{
                    {"_rootScaling","UpdateRootScale"},{"_objectScaling","UpdateObjectScale"},{"_textScaling","UpdateTextScale"},{"_enableLayoutElementScaling","ScaleLayoutElements"},{"_enableLayoutGroupSpacing","ChangeLayoutGroupSpacing"},{"_canvasScaling","UpdateCanvasScale"}}})
                    if(Get<bool>(scaler,F("UISubScaler",flag)))Invoke(Method(scaler,method,1),scaler,a);
                if(Get<bool>(scaler,F("UISubScaler","_enforceScreenFit")))Invoke(Method(scaler,"FitRectToScreen",0),scaler);}}
        PlaceShortcuts();return "{\"ok\":true,\"nativeScalers\":"+std::to_string(count)+",\"percent\":"+std::to_string(percent)+"}";
    }
    if(action.rfind("menu-scale-",0)==0){InventoryButtons();void* menu=menuTransform.Get();if(!menu)throw std::runtime_error("Open the inventory first.");
        if(!testMenuScaleSaved){testOriginalMenuScale=Value<Vec3>(M("Transform","get_localScale",0,"UnityEngine","UnityEngine.CoreModule.dll"),menu);testMenuScaleSaved=true;}
        const auto percent=std::stoi(action.substr(11));if(percent<50||percent>200)throw std::runtime_error("Unsupported scale fixture.");
        Vec3 scale{testOriginalMenuScale.x*percent/100.f,testOriginalMenuScale.y*percent/100.f,testOriginalMenuScale.z};void* a[]{&scale};
        Invoke(M("Transform","set_localScale",1,"UnityEngine","UnityEngine.CoreModule.dll"),menu,a);PlaceShortcuts();return State();}
    if(action=="layout-tree"){InventoryButtons();void* menu=menuTransform.Get();if(!menu)throw std::runtime_error("Open the inventory first.");
        Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","RectTransform"))));
        Root menuObject(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),menu));
        bool inactive=true;void* args[]{type.Get(),&inactive};Root children(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),menuObject.Get(),args));
        std::ostringstream out;out<<"{\"ok\":true,\"rects\":[";bool comma=false;
        for(uintptr_t i=0;i<Get<uintptr_t>(children.Get(),0x18);++i){void* tr=Get<void*>(children.Get(),0x20+8*i);Root obj(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),tr));
            auto r=RectIn(tr,menu);Root parent(Invoke(M("Transform","get_parent",0,"UnityEngine","UnityEngine.CoreModule.dll"),tr));
            if(comma)out<<',';comma=true;out<<"{\"name\":"<<Json(Text(Invoke(Method(obj.Get(),"get_name",0),obj.Get())))<<",\"parent\":"<<Json(parent.Get()?Text(Invoke(Method(parent.Get(),"get_name",0),parent.Get())):"")
                <<",\"active\":"<<Boolean(Value<bool>(Method(obj.Get(),"get_activeInHierarchy",0),obj.Get()))<<",\"rect\":["<<r.x<<','<<r.y<<','<<r.w<<','<<r.h<<"]}";}
        return out.str()+"]}";}
    if(action=="layout"){InventoryButtons();void* menu=menuTransform.Get();if(!menu)throw std::runtime_error("Open the inventory first.");
        const auto menuRect=RectIn(menu,menu);std::ostringstream out;out<<"{\"ok\":true,\"buttons\":[";bool comma=false;
        void* left=Get<void*>(inventoryPanel.Get(),F("InventoryPanelUI","leftButtonsContainer"));const auto leftRect=RectIn(left,menu);
        const auto footerRect=RectIn(footerTransform.Get(),menu),currencies=CurrencyBounds(menu);
        for(auto& b:shortcuts){Root tr(Invoke(M("GameObject","get_transform",0,"UnityEngine","UnityEngine.CoreModule.dll"),b.object->Get()));const auto r=RectIn(tr.Get(),menu);
            if(comma)out<<',';comma=true;out<<"{\"action\":"<<Json(b.action)<<",\"visible\":"<<Boolean(Value<bool>(Method(b.object->Get(),"get_activeInHierarchy",0),b.object->Get()))<<",\"insideInventory\":"<<Boolean(rules::Inside(r,menuRect))<<",\"insideFooter\":"<<Boolean(rules::Inside(r,footerRect))<<",\"vanillaControlsOverlap\":"<<Boolean(rules::Overlaps(r,leftRect))<<",\"currencyOverlap\":"<<Boolean(rules::Overlaps(r,currencies))<<",\"labels\":[";
            bool next=false;for(auto& label:b.labels){if(next)out<<',';next=true;out<<Json(Text(Invoke(Method(label->Get(),"get_text",0),label->Get())));}out<<"],\"rect\":["<<r.x<<','<<r.y<<','<<r.w<<','<<r.h<<"]}";}
        return out.str()+"]}";}
    if(action.rfind("shortcut-press-",0)==0){
        // Test: our inventory button pressed as a click does (Sell all asks first); the labels after it.
        InventoryButtons();const std::string wanted=action.substr(15);
        auto it=std::find_if(shortcuts.begin(),shortcuts.end(),[&](const Shortcut& b){return wanted==b.action;});
        if(it==shortcuts.end())throw std::runtime_error("Open the inventory first.");
        ShortcutClick(it->button->Get());
        std::ostringstream out;out<<"{\"ok\":true,\"busy\":"<<Boolean(shortcutBusy.load())<<",\"labels\":[";bool comma=false;
        for(auto& label:it->labels){if(comma)out<<',';comma=true;out<<Json(Text(Invoke(Method(label->Get(),"get_text",0),label->Get())));}
        return out.str()+"]}";}
    if(action=="inventory-positions"||action=="stash-positions"||action=="inventory-scatter"||action=="stash-scatter"){
        // Test: where the items of the inventory (or the stash's shown tab) are, in order; scatter moves
        // the first item to a free far place in the same container (the test clone only), so a sort shows.
        void* container=nullptr;
        if(action.rfind("inventory-",0)==0)container=Get<void*>(c.manager,F("ItemContainersManager","inventory"));
        else{void* holder=Get<void*>(c.manager,F("ItemContainersManager","stash"));void* current=holder?Invoke(Method(holder,"get_CurrentContainer",0),holder):nullptr;
            if(!current)throw std::runtime_error("The stash has not loaded yet.");
            const int index=Get<int>(current,F("TabbedItemContainer","currentlyActiveTab"));const auto containers=Entries(Invoke(Method(current,"get_Containers",0),current));
            if(index<0||static_cast<size_t>(index)>=containers.size())throw std::runtime_error("No stash tab is shown.");container=containers[index];}
        Root content(Invoke(Method(container,"GetContent",0),container));
        if(action.find("-scatter")!=std::string::npos){
            const auto entries=Entries(content.Get());if(entries.empty()||!entries.front())throw std::runtime_error("The container is empty.");
            for(XY at:{XY{9,0},XY{7,1},XY{5,2},XY{3,3},XY{6,4}}){OptionalXY place{true,at};int context=0;void* a[]{entries.front(),container,&place,&context};
                if(Value<bool>(Method(container,"MoveItemTo",4),container,a))return "{\"ok\":true,\"moved\":["+std::to_string(at.x)+","+std::to_string(at.y)+"]}";}
            return std::string("{\"ok\":false,\"error\":\"no free place\"}");}
        std::ostringstream out;out<<"{\"ok\":true,\"positions\":[";bool comma=false;
        for(void* entry:Entries(content.Get())){if(!entry)continue;const auto p=Get<XY>(entry,F("ItemContainerEntry","<Position>k__BackingField"));if(comma)out<<',';comma=true;out<<'['<<p.x<<','<<p.y<<']';}
        return out.str()+"]}";}
    if(action=="filter-colors"){
        // Read only: the game's loot filter colour list (count, last colours) and what a colour number gives.
        void* master=Invoke(M("GlobalAssets","get_MasterColorList",0,"LE.AssetManagement"));if(!master)throw std::runtime_error("No colour list.");
        void* presets=Get<void*>(master,F("EpochColor","lootFilterColorPresets"));const auto n=Get<uintptr_t>(presets,0x18);
        void* beams=Get<void*>(master,F("EpochColor","lootFilterBeamColorPresets"));
        std::ostringstream out;out<<"{\"ok\":true,\"count\":"<<n<<",\"beams\":"<<(beams?Get<uintptr_t>(beams,0x18):0)<<",\"hex\":[";
        for(uintptr_t i=0;i<n;++i){void* primary=Get<void*>(presets,0x20+16*i);  // EcolorPair values: primary, secondary
            if(i)out<<',';out<<Json(primary?Text(Get<void*>(primary,F("EpochColor.Ecolor","hexCode"))):"");}
        out<<"],\"byNumber\":[";bool comma=false;
        for(int number:{0,static_cast<int>(n)-1,static_cast<int>(n)+5,999}){uint8_t rarity=0;void* a[]{&number,&rarity};void* ecolor=Invoke(M("EpochColorManager","GetLootfilterRuleColor",2),nullptr,a);
            if(comma)out<<',';comma=true;out<<Json(ecolor?Text(Get<void*>(ecolor,F("EpochColor.Ecolor","hexCode"))):"");}
        return out.str()+"]}";
    }
    if(action.rfind("rule-name-",0)==0){
        // Test clone only: a rule's own name in memory (the filter file is compared and restored by the test).
        const auto dash=action.find('-',10);if(dash==std::string::npos)throw std::runtime_error("use rule-name-<index>-<text>");
        const int index=std::stoi(action.substr(10,dash-10));std::string text=action.substr(dash+1);
        for(const auto& [code,ch]:std::vector<std::pair<std::string,std::string>>{{"%20"," "},{"%3C","<"},{"%3E",">"},{"%23","#"},{"%3D","="},{"%2F","/"},{"%22","\""}})
            for(size_t at;(at=text.find(code))!=std::string::npos;)text.replace(at,3,ch);
        void* manager=game::StaticObject(game::FindStaticField("LE.dll","ItemFiltering","ItemFilterManager","Instance"));
        void* filter=manager?Invoke(M("ItemFilterManager","get_Filter",0,"ItemFiltering"),manager):nullptr;if(!filter)throw std::runtime_error("No loot filter is active.");
        const auto rules=Entries(Get<void*>(filter,F("ItemFilter","rules","ItemFiltering")));if(index<0||static_cast<size_t>(index)>=rules.size())throw std::runtime_error("No such rule.");
        void* rule=rules[index];const auto* field=il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(rule),"nameOverride");
        const std::string before=Text(Get<void*>(rule,F("Rule","nameOverride","ItemFiltering")));
        Root value(il2cpp::api().string_new(text.c_str()));void* raw=value.Get();il2cpp::api().field_set_value(rule,field,raw);
        return "{\"ok\":true,\"before\":"+Json(before)+",\"name\":"+Json(text)+",\"description\":"+Json(Text(Invoke(Method(rule,"GetRuleDescription",0),rule)))+"}";
    }
    if(action.rfind("rule-editor-open-",0)==0){
        const int index=std::stoi(action.substr(17));
        void* manager=game::StaticObject(game::FindStaticField("LE.dll","ItemFiltering","ItemFilterManager","Instance"));
        void* filter=manager?Invoke(M("ItemFilterManager","get_Filter",0,"ItemFiltering"),manager):nullptr;if(!filter)throw std::runtime_error("No loot filter is active.");
        const auto rules=Entries(Get<void*>(filter,F("ItemFilter","rules","ItemFiltering")));if(index<0||static_cast<size_t>(index)>=rules.size())throw std::runtime_error("No such rule.");
        const auto panels=Find("LE.dll","","LootFilterUI");if(panels.empty())throw std::runtime_error("Open the loot filter window first.");
        int mode=0;void* a[]{rules[index],&mode};Invoke(Method(panels.front(),"OpenRuleEditor",2),panels.front(),a);return "{\"ok\":true}";
    }
    if(action=="rule-editor-recolor"){for(void* editor:Find("LE.dll","","RuleUI")){bool on=true;void* a[]{&on};Invoke(Method(editor,"RecolorChanged",1),editor,a);}return "{\"ok\":true}";}
    if(action=="rule-editor-colors"){for(void* editor:Find("LE.dll","","RuleUI")){void* input=Get<void*>(editor,F("RuleUI","labelsColorInput"));if(input)Invoke(Method(input,"OnButtonClicked",0),input);}return "{\"ok\":true}";}
    if(action=="rule-editor-close"){for(void* editor:Find("LE.dll","","RuleUI"))Invoke(Method(editor,"CloseNoApplyButton",0),editor);return "{\"ok\":true}";}
    if(action=="lootfilter-open"){bool sound=false;void* a[]{&sound};Invoke(Method(ui,"openLootFilterPanel",1),ui,a);return "{\"ok\":true}";}
    if(action=="lootfilter-close"){Invoke(Method(ui,"closeLootFilterPanel",0),ui);return "{\"ok\":true}";}
    if(action=="text-inputs"){
        // Read only: the loot filter rule name and stash tab name input fields (inactive ones too), the
        // rule names the filter window shows, and what the game's name sanitising makes of sample tags.
        std::ostringstream out;out<<"{\"ok\":true";
        const auto all=[&](const char* cls){std::vector<void*> found;auto* type=game::FindClass("LE.dll","",cls);if(!type)return found;
            Root t(il2cpp::api().type_get_object(il2cpp::api().class_get_type(type)));bool inactive=true;void* a[]{t.Get(),&inactive};Root array(Invoke(findObjects,nullptr,a));
            const auto n=Get<uintptr_t>(array.Get(),0x18);for(uintptr_t i=0;i<n&&i<200;++i)if(void* o=Get<void*>(array.Get(),0x20+8*i))found.push_back(o);return found;};
        const auto field=[&](void* input){if(!input)return std::string("null");std::ostringstream f;
            void* text=Invoke(M("TMP_InputField","get_textComponent",0,"TMPro","Unity.TextMeshPro.dll"),input);
            f<<"{\"validation\":"<<Value<int>(M("TMP_InputField","get_characterValidation",0,"TMPro","Unity.TextMeshPro.dll"),input)
             <<",\"limit\":"<<Value<int>(M("TMP_InputField","get_characterLimit",0,"TMPro","Unity.TextMeshPro.dll"),input)
             <<",\"content\":"<<Value<int>(M("TMP_InputField","get_contentType",0,"TMPro","Unity.TextMeshPro.dll"),input)
             <<",\"rich\":"<<Boolean(Value<bool>(M("TMP_InputField","get_richText",0,"TMPro","Unity.TextMeshPro.dll"),input))
             <<",\"validator\":"<<Boolean(Invoke(M("TMP_InputField","get_inputValidator",0,"TMPro","Unity.TextMeshPro.dll"),input)!=nullptr)
             <<",\"style\":"<<(text?Value<int>(Method(text,"get_fontStyle",0),text):-1)<<'}';return f.str();};
        out<<",\"ruleInputs\":[";bool comma=false;
        for(void* rule:all("RuleUI")){if(comma)out<<',';comma=true;void* shown=Get<void*>(rule,F("RuleUI","generatedRuleName"));
            void* colors=Get<void*>(rule,F("RuleUI","labelsColorInput"));void* grid=colors?Get<void*>(colors,F("ColorListInput","_colorListInputGrid")):nullptr;
            void* options=grid?Get<void*>(grid,F("ColorListInputGrid","_options")):nullptr;
            out<<"{\"colorOptions\":"<<(options?static_cast<long long>(Entries(options).size()):-1)<<",\"input\":"<<field(Get<void*>(rule,F("RuleUI","nameOverrideField")))<<",\"shown\":"<<Json(shown?Text(Invoke(Method(shown,"get_text",0),shown)).substr(0,160):"")
               <<",\"shownStyle\":"<<(shown?Value<int>(Method(shown,"get_fontStyle",0),shown):-1)<<'}';}
        out<<"],\"tabInputs\":[";comma=false;
        for(void* tab:all("ConfigureTabUI")){if(comma)out<<',';comma=true;out<<field(Get<void*>(tab,F("ConfigureTabUI","nameInputTMP")));}
        out<<"],\"tabLabels\":[";comma=false;
        {Root tmp(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("Unity.TextMeshPro.dll","TMPro","TMP_Text"))));int shown=0;
        for(void* tab:all("StashTabUI")){if(shown++>=4)break;Root object(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),tab));
            bool inactive=true;void* a[]{tmp.Get(),&inactive};Root texts(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),object.Get(),a));
            for(uintptr_t i=0;i<Get<uintptr_t>(texts.Get(),0x18)&&i<3;++i){void* label=Get<void*>(texts.Get(),0x20+8*i);if(comma)out<<',';comma=true;
                out<<"{\"text\":"<<Json(Text(Invoke(Method(label,"get_text",0),label)).substr(0,60))<<",\"style\":"<<Value<int>(Method(label,"get_fontStyle",0),label)<<'}';}}}
        Root sample(il2cpp::api().string_new("Big <color=#FF0000>red</color> <b>b</b> <size=50>x</size> a<b"));void* sa[]{sample.Get()};
        out<<"],\"sanitized\":"<<Json(Text(Invoke(M("TMPExtensions","SanitizeUserInputForDisplay",1),nullptr,sa)));
        return out.str()+"}";
    }
    if(action=="inventory-open"){bool show=true,appearances=false,stash=false,last=false;int tab=0;void* a[]{&show,&appearances,&stash,&tab,&last};Invoke(Method(ui,"openInventory",5),ui,a);return State();}
    if(action=="skills-open"){Invoke(Method(ui,"SkillsKeyDown",0),ui);return "{\"ok\":true}";}
    if(action=="inventory-open-beside"){bool overlapping=false,cosmetics=false,dontOpen=false,sound=false;int tab=0;void* a[]{&overlapping,&cosmetics,&dontOpen,&tab,&sound};Invoke(Method(ui,"openInventory",5),ui,a);return State();}
    if(action=="passives-open"){bool respec=false;void* a[]{&respec};Invoke(Method(ui,"OpenPassivePanel",1),ui,a);return "{\"ok\":true,\"passivesOpen\":"+std::string(Boolean(Value<bool>(Method(ui,"IsPassivesPanelOpen",0),ui)))+"}";}
    if(action=="passives-close"){Invoke(Method(ui,"closePassiveTree",0),ui);return State();}
    if(action=="skills-close"){bool tooltip=true;void* a[]{&tooltip};Invoke(Method(ui,"closeSkills",1),ui,a);return State();}
    if(action=="blessings-open"){bool sound=false,respec=false;void* a[]{&sound,&respec};Invoke(Method(inventoryPanel.Get(),"OpenBlessingPanel",2),inventoryPanel.Get(),a);return State();}
    if(action=="tooltip-tree"){
        // Read-only: the item tooltip's object tree (names, sibling order, active, components).
        void* tooltip=game::StaticObject(game::FindStaticField("LE.dll","","UITooltipItem","instance"));
        if(!tooltip)throw std::runtime_error("No item tooltip yet.");
        std::ostringstream out;out<<"{\"ok\":true,\"tree\":[";bool comma=false;
        std::function<void(void*,int)> walk=[&](void* t,int depth){
            if(depth>7)return;
            Root go(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),t));
            const bool active=Value<bool>(Method(go.Get(),"get_activeSelf",0),go.Get());
            std::string layout;
            for(const char* cls:{"SimpleLayoutGroup","VerticalLayoutGroup","LayoutElement","ContentSizeFitter","TextMeshProUGUI"}){
                auto* k=game::FindClass("LE.dll","",cls);if(!k)k=game::FindClass("UnityEngine.UI.dll","UnityEngine.UI",cls);if(!k)k=game::FindClass("Unity.TextMeshPro.dll","TMPro",cls);if(!k)continue;
                Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(k)));void* a[]{type.Get()};
                if(Invoke(Signature("GameObject","GetComponent",{"System.Type"}),go.Get(),a)){if(!layout.empty())layout+='+';layout+=cls;}
            }
            if(comma)out<<',';comma=true;
            out<<"{\"d\":"<<depth<<",\"name\":"<<Json(Text(Invoke(M("Object","get_name",0,"UnityEngine","UnityEngine.CoreModule.dll"),go.Get())))<<",\"active\":"<<Boolean(active)<<",\"c\":"<<Json(layout)<<'}';
            const int n=Value<int>(M("Transform","get_childCount",0,"UnityEngine","UnityEngine.CoreModule.dll"),t);
            for(int i=0;i<n&&i<60;++i){void* a[]{&i};Root child(Invoke(M("Transform","GetChild",1,"UnityEngine","UnityEngine.CoreModule.dll"),t,a));walk(child.Get(),depth+1);}
        };
        Root t(Invoke(M("Component","get_transform",0,"UnityEngine","UnityEngine.CoreModule.dll"),tooltip));walk(t.Get(),0);
        return out.str()+"]}";
    }
    if(action=="seal-regular"||action=="seal-primordial"){
        // Disposable character only: the game's own sealing on the first inventory item with affixes.
        void* inventory=Get<void*>(c.manager,F("ItemContainersManager","inventory"));
        Root content(Invoke(Method(inventory,"GetContent",0),inventory));
        for(void* entry:Entries(content.Get())){
            void* data=entry?Get<void*>(entry,F("ItemContainerEntry","data")):nullptr;
            void* affixes=data?Get<void*>(data,F("ItemData","affixes")):nullptr;
            if(!affixes||Entries(affixes).size()<2)continue;
            if(action=="seal-regular"){int tier=5;void* a[]{&tier};Invoke(Method(data,"AddRandomRegularSealedAffix",1),data,a);}
            else{void* first=Entries(affixes).front();bool flag=true;void* a[]{first,&flag};Invoke(Method(data,"MakeAffixSealedPrimordialAffix",2),data,a);}
            std::ostringstream out;out<<"{\"ok\":true,\"affixes\":[";bool comma=false;
            for(void* affix:Entries(Get<void*>(data,F("ItemData","affixes")))){if(!affix)continue;if(comma)out<<',';comma=true;
                out<<"{\"tier\":"<<Value<int>(M("ItemAffix","get_DisplayTier",0),affix)<<",\"sealed\":"<<Get<int>(affix,F("ItemAffix","sealedAffixType"))<<'}';}
            return out.str()+"]}";
        }
        throw std::runtime_error("No inventory item with at least two affixes.");
    }
    if(action.rfind("special-",0)==0||action=="tooltip-unique"||action=="tooltip-lines"||action=="label-colors"){
        struct C4{float r,g,b,a;};
        auto hex=[](C4 c){char b[8];auto v=[](float x){return static_cast<int>(std::lround(std::clamp(x,0.f,1.f)*255.f));};std::snprintf(b,sizeof b,"%02X%02X%02X",v(c.r),v(c.g),v(c.b));return std::string(b);};
        auto objectOf=[&](void* component){return Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),component);};
        auto activeSelf=[&](void* component){Root g(objectOf(component));return Value<bool>(Method(g.Get(),"get_activeSelf",0),g.Get());};
        auto nameOf=[&](void* component){Root g(objectOf(component));return Text(Invoke(M("Object","get_name",0,"UnityEngine","UnityEngine.CoreModule.dll"),g.Get()));};
        Root tmpType(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("Unity.TextMeshPro.dll","TMPro","TMP_Text"))));
        if(action=="label-colors"){
            // Read-only: how the game colours item labels on the ground.
            std::ostringstream out;
            out<<"{\"ok\":true,\"emphasisOpen\":"<<Json(Text(Invoke(M("GroundItemLabel","get_EmphasisOpenTag",0))))<<",\"emphasisClose\":"<<Json(Text(Invoke(M("GroundItemLabel","get_EmphasisCloseTag",0))))<<",\"labels\":[";
            bool comma=false;int n=0;
            for(void* label:Find("LE.dll","","GroundItemLabel")){
                if(!label||!game::IsAlive(label)||n>=12)continue;
                Root g(objectOf(label));if(!Value<bool>(Method(g.Get(),"get_activeInHierarchy",0),g.Get()))continue;
                void* e=Get<void*>(label,F("GroundItemLabel","eColor"));
                void* ta[]{tmpType.Get()};void* text=Invoke(Signature("GameObject","GetComponentInChildren",{"System.Type"}),g.Get(),ta);
                if(comma)out<<',';comma=true;++n;
                out<<"{\"rule\":"<<Get<int>(label,F("GroundItemLabel","<CurrentRuleNumber>k__BackingField"))<<",\"recolor\":"<<Boolean(Get<bool>(label,F("GroundItemLabel","filterDidRecolorLabel")))
                   <<",\"emphasized\":"<<Boolean(Get<bool>(label,F("GroundItemLabel","emphasized")));
                if(e)out<<",\"color\":"<<Json(hex(Get<C4>(e,F("EpochColor.Ecolor","color"))))<<",\"light\":"<<Json(hex(Get<C4>(e,F("EpochColor.Ecolor","lightColor"))))
                        <<",\"tooltipName\":"<<Json(hex(Get<C4>(e,F("EpochColor.Ecolor","tooltipNameColor"))))<<",\"hexCode\":"<<Json(Text(Get<void*>(e,F("EpochColor.Ecolor","hexCode"))));
                if(text)out<<",\"textColor\":"<<Json(hex(Value<C4>(M("Graphic","get_color",0,"UnityEngine.UI","UnityEngine.UI.dll"),text)))<<",\"text\":"<<Json(Text(Invoke(Method(text,"get_text",0),text)).substr(0,120));
                out<<'}';
            }
            return out.str()+"]}";
        }
        if(action.rfind("special-",0)==0||action=="tooltip-unique"){
            // Disposable character only. special-<kind>: give the next unchanged
            // inventory item's first affix a special kind (the affix fields as
            // the game stores them). tooltip-unique: the first unique item.
            // Then open its tooltip, as hovering it would.
            const std::string kind=action=="tooltip-unique"?"":action.substr(8);
            // affix-<id>: the first affix gets that affix id (for loot filter affix rules).
            const bool setId=kind.rfind("affix-",0)==0;
            if(!kind.empty()&&!setId&&kind!="exalted"&&kind!="experimental"&&kind!="personal"&&kind!="corrupted")throw std::runtime_error("Unsupported special affix fixture.");
            auto managers=Find("LE.dll","","TooltipItemManager");if(managers.empty())throw std::runtime_error("Tooltip manager unavailable.");
            void* chosen=nullptr;void* data=nullptr;
            for(void* icon:Find("LE.dll","","InventoryItemUI")){
                Root go(objectOf(icon));if(!Value<bool>(Method(go.Get(),"get_activeInHierarchy",0),go.Get()))continue;
                void* entry=Get<void*>(icon,F("InventoryItemUI","<EntryRef>k__BackingField"));void* d=entry?Get<void*>(entry,F("ItemContainerEntry","data")):nullptr;
                if(!d)continue;
                if(kind.empty()){if(Value<bool>(Method(d,"isUnique",0),d)){chosen=icon;data=d;break;}continue;}
                void* list=Get<void*>(d,F("ItemData","affixes"));if(!list||Entries(list).size()<2||Get<bool>(d,F("ItemData","corrupted")))continue;
                bool plain=true;for(void* x:Entries(list))plain&=x&&!Get<int>(x,F("ItemAffix","specialAffixType"))&&!Get<int>(x,F("ItemAffix","sealedAffixType"))&&Get<uint8_t>(x,F("ItemAffix","affixTier"))<5;
                if(plain){chosen=icon;data=d;break;}
            }
            if(!chosen)throw std::runtime_error(kind.empty()?"No visible unique inventory item.":"No unchanged visible inventory item with at least two affixes.");
            if(!kind.empty()){
                void* affix=Entries(Get<void*>(data,F("ItemData","affixes"))).front();
                auto bytes=static_cast<char*>(affix);
                if(setId)*reinterpret_cast<uint16_t*>(bytes+F("ItemAffix","affixId"))=static_cast<uint16_t>(std::stoi(kind.substr(6)));
                else if(kind=="exalted")*reinterpret_cast<uint8_t*>(bytes+F("ItemAffix","affixTier"))=6;
                else if(kind=="experimental")*reinterpret_cast<int*>(bytes+F("ItemAffix","specialAffixType"))=1;
                else if(kind=="personal")*reinterpret_cast<int*>(bytes+F("ItemAffix","specialAffixType"))=2;
                else{*reinterpret_cast<int*>(bytes+F("ItemAffix","sealedAffixType"))=3;Invoke(Method(data,"SetAsCorrupted",0),data);}
            }
            Root unpacked(Invoke(Method(data,"getAsUnpacked",0),data));
            Root t(Invoke(componentTransform,chosen));Root go(objectOf(chosen));
            Vec3 at=Value<Vec3>(transformPosition,t.Get());
            Vec2 size=Value<Vec2>(M("RectTransform","get_sizeDelta",0,"UnityEngine","UnityEngine.CoreModule.dll"),t.Get());
            int slot=0;Vec2 offset{0,0};void* a[]{unpacked.Get(),&slot,&offset,&at,go.Get(),&size};
            Invoke(Method(managers.front(),"OpenItemTooltip",6),managers.front(),a);
        }
        // The open item tooltip: its affix lines as drawn (tags included), our
        // loot filter rule lines and where they sit next to the game's lines.
        void* tooltip=game::StaticObject(game::FindStaticField("LE.dll","","UITooltipItem","instance"));
        if(!tooltip)throw std::runtime_error("No item tooltip yet.");
        Root tooltipTransform(Invoke(componentTransform,tooltip));
        std::ostringstream out;out<<"{\"ok\":true,\"lines\":[";bool comma=false;
        auto line=[&](const std::string& field,void* affixLine){
            if(!affixLine||!game::IsAlive(affixLine)||!activeSelf(affixLine))return;
            void* text=Get<void*>(affixLine,F("UITooltipItemAffix","_text"));if(!text)return;
            if(comma)out<<',';comma=true;out<<Json(field+": "+Text(Invoke(Method(text,"get_text",0),text)).substr(0,300));};
        for(const char* field:{"sealedAffix","sealedPrimordialAffix","sealedCorruptedAffix"})line(field,Get<void*>(tooltip,F("UITooltipItem",field)));
        for(const char* list:{"prefixesText","suffixesText","uniqueAffixesText"})for(void* l:Entries(Get<void*>(tooltip,F("UITooltipItem",list))))line(list,l);
        out<<"],\"places\":[";comma=false;
        auto place=[&](const std::string& label,void* component){
            if(!component||!game::IsAlive(component))return;
            Root tr(Invoke(componentTransform,component));const auto r=RectIn(tr.Get(),tooltipTransform.Get());
            Root parent(Invoke(M("Transform","get_parent",0,"UnityEngine","UnityEngine.CoreModule.dll"),tr.Get()));Root go(objectOf(component));
            if(comma)out<<',';comma=true;
            out<<"{\"what\":"<<Json(label)<<",\"name\":"<<Json(nameOf(component))<<",\"parent\":"<<Json(Text(Invoke(M("Object","get_name",0,"UnityEngine","UnityEngine.CoreModule.dll"),parent.Get())))
               <<",\"active\":"<<Boolean(Value<bool>(Method(go.Get(),"get_activeInHierarchy",0),go.Get()))<<",\"rect\":["<<r.x<<','<<r.y<<','<<r.w<<','<<r.h<<']';
            if(label.rfind("rule",0)==0){
                out<<",\"text\":"<<Json(Text(Invoke(Method(component,"get_text",0),component)));}
            if(label=="type"||label=="lore"||label=="requirementLevel"||label.rfind("rule",0)==0||label=="prefixesText"||label=="suffixesText"){
                struct V4{float x,y,z,w;};const auto m=Value<V4>(Method(component,"get_margin",0),component);
                out<<",\"margin\":["<<m.x<<','<<m.y<<','<<m.z<<','<<m.w<<"],\"alignment\":"<<Value<int>(Method(component,"get_alignment",0),component)<<",\"fontSize\":"<<Value<float>(Method(component,"get_fontSize",0),component);
                void* font=Invoke(Method(component,"get_font",0),component);
                out<<",\"font\":"<<Json(font?Text(Invoke(M("Object","get_name",0,"UnityEngine","UnityEngine.CoreModule.dll"),font)):std::string())<<",\"fontStyle\":"<<Value<int>(Method(component,"get_fontStyle",0),component);}
            out<<'}';};
        place("type",Get<void*>(tooltip,F("UITooltipItem","typeText")));
        place("implicits",Get<void*>(tooltip,F("UITooltipItem","implicitText")));
        place("lore",Get<void*>(tooltip,F("UITooltipItem","loreText")));
        place("requirementLevel",Get<void*>(tooltip,F("UITooltipItem","requirementLevelText")));
        for(const char* list:{"prefixesText","suffixesText"})for(void* l:Entries(Get<void*>(tooltip,F("UITooltipItem",list))))
            if(l&&game::IsAlive(l)&&activeSelf(l)){place(std::string(list),Get<void*>(l,F("UITooltipItemAffix","_text")));break;}
        {Root go(objectOf(tooltip));bool inactive=true;void* ia[]{tmpType.Get(),&inactive};
            Root all(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),go.Get(),ia));
            for(uintptr_t i=0;i<Get<uintptr_t>(all.Get(),0x18);++i){void* text=Get<void*>(all.Get(),0x20+8*i);
                if(text&&nameOf(text)=="EpochPact loot filter rule")place("rule",text);}
            void* footer=Get<void*>(tooltip,F("UITooltipItem","requirementsFooter"));
            if(footer){Root ft(Invoke(M("GameObject","get_transform",0,"UnityEngine","UnityEngine.CoreModule.dll"),footer));const auto r=RectIn(ft.Get(),tooltipTransform.Get());
                if(comma)out<<',';comma=true;out<<"{\"what\":\"footer\",\"rect\":["<<r.x<<','<<r.y<<','<<r.w<<','<<r.h<<"]}";}}
        return out.str()+"]}";
    }
    if(action.rfind("affix-special-",0)==0){
        // Disposable test character only: affix-special-<k>-<type> sets the special type (0 standard,
        // 1 experimental, 2 personal) of the first regular affix of the k-th inventory item with affixes, in
        // memory, so the tooltip marks can be checked; returns the type it had.
        const auto dash=action.find('-',14);if(dash==std::string::npos)throw std::runtime_error("use affix-special-<k>-<type>");
        const size_t wanted=static_cast<size_t>(std::stoul(action.substr(14,dash-14)));const int type=std::stoi(action.substr(dash+1));
        if(type<0||type>2)throw std::runtime_error("Unsupported special affix type.");
        size_t index=0;
        for(void* icon:Find("LE.dll","","InventoryItemUI")){
            Root go(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),icon));
            if(!Value<bool>(Method(go.Get(),"get_activeInHierarchy",0),go.Get()))continue;
            void* entry=Get<void*>(icon,F("InventoryItemUI","<EntryRef>k__BackingField"));void* data=entry?Get<void*>(entry,F("ItemContainerEntry","data")):nullptr;
            void* affixes=data?Get<void*>(data,F("ItemData","affixes")):nullptr;if(!affixes||Entries(affixes).empty())continue;
            if(index++!=wanted)continue;
            for(void* affix:Entries(affixes)){
                if(!affix||Get<int>(affix,F("ItemAffix","sealedAffixType")))continue;
                int& special=*reinterpret_cast<int*>(static_cast<char*>(affix)+F("ItemAffix","specialAffixType"));const int before=special;special=type;
                return "{\"ok\":true,\"before\":"+std::to_string(before)+",\"type\":"+std::to_string(type)+"}";
            }
            throw std::runtime_error("No regular affix.");
        }
        throw std::runtime_error("No such inventory item with affixes.");
    }
    if(action.rfind("tooltip-compare-open-",0)==0||action=="tooltip-compare-close"||action=="tooltip-read-all"){
        // Disposable test character only. open-<k>: the k-th inventory item with affixes, with the tooltip
        // manager's comparison flag on in memory (as auto compare); read-all: every open item tooltip's
        // affix lines; close: tooltip closed and the flag back to what it was.
        static int savedCompare=-1;
        auto managers=Find("LE.dll","","TooltipItemManager");if(managers.empty())throw std::runtime_error("Tooltip manager unavailable.");
        void* manager=managers.front();bool& compare=*reinterpret_cast<bool*>(static_cast<char*>(manager)+F("TooltipItemManager","autoCompareEnabled"));
        if(action=="tooltip-compare-close"){Invoke(Method(manager,"CloseTooltip",0),manager);if(savedCompare>=0)compare=savedCompare!=0;savedCompare=-1;return std::string("{\"ok\":true,\"compare\":")+Boolean(compare)+"}";}
        if(action=="tooltip-read-all"){
            std::ostringstream out;out<<"{\"ok\":true,\"compare\":"<<Boolean(compare)<<",\"tooltips\":[";bool first=true;
            for(void* tooltip:Find("LE.dll","","UITooltipItem")){
                Root go(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),tooltip));
                if(!Value<bool>(Method(go.Get(),"get_activeInHierarchy",0),go.Get()))continue;
                if(!first)out<<',';first=false;
                out<<"{\"lines\":[";bool comma=false;
                // The compare section's lines only while that section is shown.
                void* compareContent=Get<void*>(tooltip,F("UITooltipItem","compareContent"));
                const bool compareShown=compareContent&&game::IsAlive(compareContent)&&Value<bool>(Method(compareContent,"get_activeInHierarchy",0),compareContent);
                for(const char* list:{"prefixesText","suffixesText","comparePrefixes","compareSuffixes"})if(list[0]!='c'||compareShown)for(void* l:Entries(Get<void*>(tooltip,F("UITooltipItem",list)))){
                    if(!l||!game::IsAlive(l))continue;Root lg(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),l));
                    if(!Value<bool>(Method(lg.Get(),"get_activeSelf",0),lg.Get()))continue;
                    void* t=Get<void*>(l,F("UITooltipItemAffix","_text"));const std::string text=t?Text(Invoke(Method(t,"get_text",0),t)):std::string();
                    if(comma)out<<',';comma=true;out<<"{\"compare\":"<<Boolean(list[0]=='c')<<",\"coloured\":"<<Boolean(text.find("<color=#")!=std::string::npos)<<",\"text\":"<<Json(text.substr(0,90))<<'}';
                }
                out<<"]}";
            }
            return out.str()+"]}";
        }
        if(savedCompare<0)savedCompare=compare?1:0;compare=true;
        const size_t wanted=static_cast<size_t>(std::stoul(action.substr(21)));size_t index=0;
        for(void* icon:Find("LE.dll","","InventoryItemUI")){
            Root go(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),icon));
            if(!Value<bool>(Method(go.Get(),"get_activeInHierarchy",0),go.Get()))continue;
            void* entry=Get<void*>(icon,F("InventoryItemUI","<EntryRef>k__BackingField"));void* data=entry?Get<void*>(entry,F("ItemContainerEntry","data")):nullptr;
            void* affixes=data?Get<void*>(data,F("ItemData","affixes")):nullptr;if(!affixes||Entries(affixes).empty())continue;
            if(index++!=wanted)continue;
            Root unpacked(Invoke(Method(data,"getAsUnpacked",0),data));Root t(Invoke(componentTransform,icon));
            Vec3 at=Value<Vec3>(transformPosition,t.Get());Vec2 size=Value<Vec2>(M("RectTransform","get_sizeDelta",0,"UnityEngine","UnityEngine.CoreModule.dll"),t.Get());
            int slot=0;Vec2 offset{0,0};void* a[]{unpacked.Get(),&slot,&offset,&at,go.Get(),&size};
            Invoke(Method(manager,"OpenItemTooltip",6),manager,a);
            return "{\"ok\":true,\"opened\":"+std::to_string(wanted)+",\"baseType\":"+std::to_string(Get<uint8_t>(data,F("ItemData","itemType")))+"}";
        }
        throw std::runtime_error("No such inventory item with affixes.");
    }
    if(action=="tooltip-first"||action=="tooltip-close"){
        // Opens the game's own item tooltip for the first visible inventory item
        // with affixes, exactly as hovering it would, so tooltip display hooks
        // can be checked without input automation.
        auto managers=Find("LE.dll","","TooltipItemManager");
        if(managers.empty())throw std::runtime_error("Tooltip manager unavailable.");
        void* manager=managers.front();
        if(action=="tooltip-close"){Invoke(Method(manager,"CloseTooltip",0),manager);return "{\"ok\":true}";}
        // An item with a sealed affix first (it has its own tooltip line), then any item with affixes.
        auto icons=Find("LE.dll","","InventoryItemUI");
        auto sealedOf=[&](void* icon){void* e=Get<void*>(icon,F("InventoryItemUI","<EntryRef>k__BackingField"));void* d=e?Get<void*>(e,F("ItemContainerEntry","data")):nullptr;
            void* list=d?Get<void*>(d,F("ItemData","affixes")):nullptr;if(!list)return false;for(void* x:Entries(list))if(x&&Get<int>(x,F("ItemAffix","sealedAffixType")))return true;return false;};
        std::stable_partition(icons.begin(),icons.end(),sealedOf);
        for(void* icon:icons){
            Root go(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),icon));
            if(!Value<bool>(Method(go.Get(),"get_activeInHierarchy",0),go.Get()))continue;
            void* entry=Get<void*>(icon,F("InventoryItemUI","<EntryRef>k__BackingField"));
            void* data=entry?Get<void*>(entry,F("ItemContainerEntry","data")):nullptr;
            void* affixes=data?Get<void*>(data,F("ItemData","affixes")):nullptr;
            if(!affixes||Entries(affixes).empty())continue;
            Root unpacked(Invoke(Method(data,"getAsUnpacked",0),data));
            Root t(Invoke(componentTransform,icon));
            Vec3 at=Value<Vec3>(transformPosition,t.Get());
            Vec2 size=Value<Vec2>(M("RectTransform","get_sizeDelta",0,"UnityEngine","UnityEngine.CoreModule.dll"),t.Get());
            int slot=0;Vec2 offset{0,0};  // TooltipItemManager.SlotType.INVENTORY
            void* a[]{unpacked.Get(),&slot,&offset,&at,go.Get(),&size};
            Invoke(Method(manager,"OpenItemTooltip",6),manager,a);
            std::ostringstream out;out<<"{\"ok\":true,\"affixes\":[";bool comma=false;
            for(void* affix:Entries(affixes)){if(!affix)continue;if(comma)out<<',';comma=true;
                out<<"{\"type\":"<<Get<int>(affix,F("ItemAffix","affixType"))<<",\"tier\":"<<Value<int>(M("ItemAffix","get_DisplayTier",0),affix)<<",\"sealed\":"<<Boolean(Value<bool>(M("ItemAffix","get_IsSealed",0),affix))<<'}';}
            out<<"],\"lines\":[";comma=false;
            if(void* tooltip=game::StaticObject(game::FindStaticField("LE.dll","","UITooltipItem","instance"))){
                for(const char* field:{"sealedAffix","sealedPrimordialAffix","sealedCorruptedAffix"}){
                    void* line=Get<void*>(tooltip,F("UITooltipItem",field));if(!line||!game::IsAlive(line))continue;
                    Root lg(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),line));
                    if(!Value<bool>(Method(lg.Get(),"get_activeSelf",0),lg.Get()))continue;
                    void* text=Get<void*>(line,F("UITooltipItemAffix","_text"));if(!text)continue;
                    if(comma)out<<',';comma=true;out<<Json(std::string(field)+": "+Text(Invoke(Method(text,"get_text",0),text)).substr(0,140));}
                for(const char* list:{"prefixesText","suffixesText"})
                    for(void* line:Entries(Get<void*>(tooltip,F("UITooltipItem",list)))){
                        if(!line||!game::IsAlive(line))continue;Root lg(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),line));
                        if(!Value<bool>(Method(lg.Get(),"get_activeSelf",0),lg.Get()))continue;
                        void* text=Get<void*>(line,F("UITooltipItemAffix","_text"));if(!text)continue;
                        if(comma)out<<',';comma=true;out<<Json(std::string(list)+": "+Text(Invoke(Method(text,"get_text",0),text)).substr(0,120));}
            }
            return out.str()+"]}";
        }
        throw std::runtime_error("No visible inventory item with affixes.");
    }
    if(action=="panels-close"){bool sound=false;void* a[]{&sound};Invoke(Method(ui,"closeShop",0),ui);Invoke(Method(ui,"closeStash",1),ui,a);Invoke(Method(ui,"closeInventory",0),ui);Invoke(Method(ui,"closeMap",0),ui);Invoke(Method(ui,"CloseAllPanels",0),ui);return State();}
    if(action.rfind("drop-gold-",0)==0){
        // Disposable test character only: one native gold pile at a distance.
        const int distance=std::stoi(action.substr(10));if(distance<1||distance>80)throw std::runtime_error("Unsupported gold distance.");
        void* manager=game::StaticObject(game::FindStaticField("LE.dll","","GroundItemManager","instance"));
        if(!manager)throw std::runtime_error("Ground item manager unavailable.");
        Vec3 at=Position(c.actor);at.x+=static_cast<float>(distance);int amount=25;bool local=true;
        void* args[]{c.actor,&amount,&at,&local};Invoke(M("GroundItemManager","dropGoldForPlayer",4),manager,args);
        return "{\"ok\":true,\"dropped\":"+std::to_string(amount)+",\"distance\":"+std::to_string(distance)+"}";
    }
    if(action.rfind("filter-diag-",0)==0){
        // Read-only probe of the game's filter loader for one file name.
        void* manager=game::StaticObject(game::FindStaticField("LE.dll","ItemFiltering","ItemFilterManager","Instance"));
        if(!manager)throw std::runtime_error("Loot filter manager unavailable.");
        std::string name=action.substr(12);
        for(size_t at;(at=name.find("%20"))!=std::string::npos;)name.replace(at,3," ");
        void* pathObject=game::StaticObject(game::FindStaticField("LE.dll","ItemFiltering","ItemFilterManager","filtersPath"));
        const std::string folder=pathObject?Text(pathObject):"";
        auto describe=[&](void* filter){if(!filter)return std::string("null");
            return "{\"name\":"+Json(Text(Get<void*>(filter,F("ItemFilter","name","ItemFiltering"))))+",\"rules\":"+std::to_string(Entries(Get<void*>(filter,F("ItemFilter","rules","ItemFiltering"))).size())+"}";};
        std::ostringstream out;
        out<<"{\"ok\":true,\"folder\":"<<Json(folder)<<",\"active\":"<<describe(Get<void*>(manager,F("ItemFilterManager","<Filter>k__BackingField","ItemFiltering")))
           <<",\"manualDisable\":"<<Boolean(Get<bool>(manager,F("ItemFilterManager","<ManualFilterDisable>k__BackingField","ItemFiltering")))
           <<",\"lastCharacter\":"<<Json(Text(Get<void*>(manager,F("ItemFilterManager","lastLoadedCharacterName","ItemFiltering"))));
        Root nameString(il2cpp::api().string_new(name.c_str()));
        {void* a[]{nameString.Get()};out<<",\"exists\":"<<Boolean(Value<bool>(M("ItemFilterManager","FilterFileExists",1,"ItemFiltering"),manager,a));}
        const std::string full=folder+"/"+name+".xml";
        int variant=0;
        for(const auto& [first,second]:std::vector<std::pair<std::string,std::string>>{{folder,name},{full,name},{folder,name+".xml"},{full,""}}){
            Root a1(il2cpp::api().string_new(first.c_str())),a2(il2cpp::api().string_new(second.c_str()));bool flag=false;void* a[]{a1.Get(),a2.Get(),&flag};
            std::string result;
            if(!game::Guarded([&]{result=describe(Invoke(M("ItemFilterManager","LoadFilterFromPath",3,"ItemFiltering"),nullptr,a));},&result))result=Json("error: "+result);
            out<<",\"fromPath"<<variant++<<"\":"<<result;
        }
        {void* a[]{nameString.Get()};std::string why;bool loaded=false;
            if(game::Guarded([&]{loaded=Value<bool>(M("ItemFilterManager","LoadFilter",1,"ItemFiltering"),manager,a);},&why))out<<",\"loadFilter\":"<<Boolean(loaded);else out<<",\"loadFilter\":"<<Json("error: "+why);}
        out<<",\"activeAfter\":"<<describe(Get<void*>(manager,F("ItemFilterManager","<Filter>k__BackingField","ItemFiltering")));
        return out.str()+"}";
    }
    if(action=="stash-info"){
        // Read-only research: stash folders/tabs, tab cost/limit, method
        // parameter names, the active filter's rules and the tab colour palette.
        auto& api=il2cpp::api();
        std::ostringstream out;out<<"{\"ok\":true";
        auto params=[&](const char* ns,const char* cls,const char* name,int count){
            auto m=game::FindMethod("LE.dll",ns,cls,name,count);std::string r="[";
            for(int i=0;i<count;++i){if(i)r+=',';char* type=api.type_get_name(api.method_get_param(m.info,i));r+=Json(std::string(api.method_get_param_name(m.info,i))+":"+(type?type:""));if(type)api.free(type);}
            return r+"]";};
        out<<",\"params\":{\"PurchaseTabOnServer\":"<<params("","ServerStashCommands","PurchaseTabOnServer",7)
           <<",\"ServerSetTabInfo\":"<<params("","ServerStashCommands","ServerSetTabInfo",9)
           <<",\"ServerAddNewCategory\":"<<params("","ServerStashCommands","ServerAddNewCategory",3)
           <<",\"ServerSetCategoryInfo\":"<<params("","ServerStashCommands","ServerSetCategoryInfo",7)
           <<",\"ServerUpdateTabParent\":"<<params("","ServerStashCommands","ServerUpdateTabParent",4)
           <<",\"AddNewStashTab\":"<<params("","ItemContainersManager","AddNewStashTab",8)
           <<",\"RequestPurchaseNewTab\":"<<params("","StashManager","RequestPurchaseNewTab",5)<<'}';
        void* local=game::StaticObject(game::FindStaticField("LE.dll","","LocalPlayer","instance"));
        int tabCount=0;
        if(local){
            void* gold=Get<void*>(local,F("LocalPlayer","goldTracker"));
            if(gold)out<<",\"gold\":"<<Get<int>(gold,F("GoldTracker","value"));
            void* info=Get<void*>(local,F("LocalPlayer","stashInfo"));
            if(info){
                out<<",\"folders\":[";bool comma=false;
                for(void* f:Entries(Get<void*>(info,F("ClientStashInfo","categories")))){if(!f)continue;if(comma)out<<',';comma=true;
                    void* pr=Get<void*>(f,F("StashCategory","_folderPriority","LE.Data"));
                    out<<"{\"id\":"<<Get<int>(f,F("StashCategory","_categoryId","LE.Data"))<<",\"name\":"<<Json(Text(Get<void*>(f,F("StashCategory","_displayName","LE.Data"))))
                       <<",\"color\":"<<Get<int>(f,F("StashCategory","_colorId","LE.Data"))<<",\"icon\":"<<Get<int>(f,F("StashCategory","_iconId","LE.Data"))
                       <<",\"order\":"<<Get<int>(f,F("StashCategory","_displayOrder","LE.Data"))<<",\"priority\":"<<Boolean(pr&&Value<bool>(Method(pr,"HasValue",0),pr))<<'}';}
                out<<"],\"tabs\":[";comma=false;
                for(void* t:Entries(Get<void*>(info,F("ClientStashInfo","tabs")))){if(!t)continue;if(comma)out<<',';comma=true;++tabCount;
                    void* pr=Get<void*>(t,F("StashTab","_tabPriority","LE.Data"));
                    out<<"{\"id\":"<<Get<int>(t,F("StashTab","_tabId","LE.Data"))<<",\"folder\":"<<Get<int>(t,F("StashTab","_categoryId","LE.Data"))
                       <<",\"name\":"<<Json(Text(Get<void*>(t,F("StashTab","_displayName","LE.Data"))))<<",\"color\":"<<Get<int>(t,F("StashTab","_colorId","LE.Data"))
                       <<",\"icon\":"<<Get<int>(t,F("StashTab","_iconId","LE.Data"))<<",\"order\":"<<Get<int>(t,F("StashTab","DisplayOrder","LE.Data"))
                       <<",\"priority\":"<<Boolean(pr&&Value<bool>(Method(pr,"HasValue",0),pr))<<",\"ignoreFolder\":"<<Boolean(Get<bool>(t,F("StashTab","_ignoreFolderPriority","LE.Data")))<<'}';}
                out<<']';
            }
        }
        {int n=tabCount;void* a[]{&n};out<<",\"nextTabCost\":"<<Value<int>(M("StashHelpers","NextTabCost",1),nullptr,a);
         int limit=-1;for(int k=0;k<=2000;++k){void* b[]{&k};if(Value<bool>(M("StashHelpers","currentTabsAtOrAboveMaximum",1),nullptr,b)){limit=k;break;}}
         out<<",\"tabLimit\":"<<limit<<",\"costs\":[";for(int k:{0,10,25,50,75,100,150}){void* b[]{&k};if(k)out<<',';out<<Value<int>(M("StashHelpers","NextTabCost",1),nullptr,b);}out<<']';}
        // The legacy stash UI holds the tab colour palette.
        {auto controls=Find("LE.dll","","StashTabbedUIControls");
         if(!controls.empty()){out<<",\"tabColors\":[";bool comma=false;
            void* list=Get<void*>(controls.front(),F("StashTabbedUIControls","tabColors"));
            void* items=list?Get<void*>(list,0x10):nullptr;const int size=list?Get<int>(list,0x18):0;
            for(int i=0;items&&i<size&&i<64;++i){struct C{float r,g,b,a;};C c=Get<C>(items,0x20+16*static_cast<size_t>(i));if(comma)out<<',';comma=true;
                char hex[8];std::snprintf(hex,sizeof hex,"%02X%02X%02X",int(c.r*255+.5f),int(c.g*255+.5f),int(c.b*255+.5f));out<<Json(hex);}
            void* icons=Get<void*>(controls.front(),F("StashTabbedUIControls","tabIcons"));out<<"],\"tabIcons\":"<<(icons?Get<int>(icons,0x18):0);}}
        // Active loot filter rules with their condition types.
        void* manager=game::StaticObject(game::FindStaticField("LE.dll","ItemFiltering","ItemFilterManager","Instance"));
        void* filter=manager?Get<void*>(manager,F("ItemFilterManager","<Filter>k__BackingField","ItemFiltering")):nullptr;
        out<<",\"filter\":"<<(filter?Json(Text(Get<void*>(filter,F("ItemFilter","name","ItemFiltering")))):"null")<<",\"rules\":[";
        if(filter){bool comma=false;
            for(void* rule:Entries(Get<void*>(filter,F("ItemFilter","rules","ItemFiltering")))){if(!rule)continue;if(comma)out<<',';comma=true;
                out<<"{\"order\":"<<Value<int>(Method(rule,"get_Order",0),rule)<<",\"type\":"<<Get<int>(rule,F("Rule","type","ItemFiltering"))
                   <<",\"enabled\":"<<Boolean(Get<bool>(rule,F("Rule","isEnabled","ItemFiltering")))<<",\"color\":"<<Get<int>(rule,F("Rule","color","ItemFiltering"))
                   <<",\"recolor\":"<<Boolean(Get<bool>(rule,F("Rule","recolor","ItemFiltering")))<<",\"name\":"<<Json(Text(Get<void*>(rule,F("Rule","nameOverride","ItemFiltering"))))
                   <<",\"conditions\":[";bool c2=false;
                for(void* cond:Entries(Get<void*>(rule,F("Rule","conditions","ItemFiltering")))){if(!cond)continue;if(c2)out<<',';c2=true;
                    out<<Json(api.class_get_name(api.object_get_class(cond)));}
                out<<"]}";}
        }
        return out.str()+"]}";
    }
    if(action=="filter-list"){
        // The game's own list of filter files (it watches the folder for changes).
        void* manager=game::StaticObject(game::FindStaticField("LE.dll","ItemFiltering","ItemFilterManager","Instance"));
        if(!manager)throw std::runtime_error("Loot filter manager unavailable.");
        Root list(Invoke(M("ItemFilterManager","GetFilterFileList",0,"ItemFiltering"),manager));
        std::ostringstream out;out<<"{\"ok\":true,\"files\":[";bool comma=false;
        // List<ValueTuple<string,int,int>>: _items at 0x10, _size at 0x18; 16-byte elements from 0x20.
        void* items=Get<void*>(list.Get(),0x10);const int size=Get<int>(list.Get(),0x18);
        for(int i=0;items&&i<size&&i<512;++i){if(comma)out<<',';comma=true;out<<Json(Text(Get<void*>(items,0x20+16*static_cast<size_t>(i))));}
        return out.str()+"]}";
    }
    if(action.rfind("load-filter-",0)==0||action=="filter-rules"){
        void* manager=game::StaticObject(game::FindStaticField("LE.dll","ItemFiltering","ItemFilterManager","Instance"));
        if(!manager)throw std::runtime_error("Loot filter manager unavailable.");
        if(action!="filter-rules"){Root name(il2cpp::api().string_new(action.substr(12).c_str()));void* a[]{name.Get()};
            if(!Value<bool>(M("ItemFilterManager","LoadFilter",1,"ItemFiltering"),manager,a))throw std::runtime_error("The game could not load that filter.");}
        void* filter=Invoke(M("ItemFilterManager","get_Filter",0,"ItemFiltering"),manager);
        if(!filter)return "{\"ok\":true,\"filter\":null}";
        std::ostringstream out;out<<"{\"ok\":true,\"filter\":"<<Json(Text(Get<void*>(filter,F("ItemFilter","name","ItemFiltering"))))<<",\"rules\":[";bool comma=false;int index=0;
        for(void* rule:Entries(Get<void*>(filter,F("ItemFilter","rules","ItemFiltering")))){if(!rule)continue;if(comma)out<<',';comma=true;
            out<<"{\"index\":"<<index++<<",\"order\":"<<Value<int>(Method(rule,"get_Order",0),rule)<<",\"type\":"<<Get<int>(rule,F("Rule","type","ItemFiltering"))
               <<",\"enabled\":"<<Boolean(Get<bool>(rule,F("Rule","isEnabled","ItemFiltering")))<<",\"name\":"<<Json(Text(Get<void*>(rule,F("Rule","nameOverride","ItemFiltering"))))
               <<",\"description\":"<<Json(Text(Invoke(Method(rule,"GetRuleDescription",0),rule)).substr(0,160))<<'}';}
        return out.str()+"]}";
    }
    if(action.rfind("overlay-zoom-game-",0)==0){
        // Probe: the game's own overlay zoom setter (the settings slider path).
        void* global=game::StaticObject(game::FindStaticField("LE.dll","","DMMapGlobalConfigs","instance"));if(!global)throw std::runtime_error("Map unavailable.");
        float value=std::stof(action.substr(18));void* a[]{&value};Invoke(Method(global,"UpdateOverlayMapZoom",1),global,a);
        return "{\"ok\":true,\"value\":"+std::to_string(value)+"}";
    }
    if(action=="method-rva"){
        // Where selected methods are in this GameAssembly build (for offline disassembly).
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"GameAssembly.dll"));
        std::ostringstream out;out<<"{\"ok\":true";
        for(const auto& [cls,name,args]:std::vector<std::tuple<const char*,const char*,int>>{{"DMMapGlobalConfigs","UpdateOverlayMapZoom",1},{"DMMapGlobalConfigs","UpdateOverlayMapOpacity",1},{"DMMapGlobalConfigs","Start",0},{"DMMapGlobalConfigs","SetOverlayZoom",1}}){
            auto m=game::FindMethod("LE.dll","",cls,name,args);
            char buffer[32];std::snprintf(buffer,sizeof buffer,"0x%llX",static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(m.code)-base));
            out<<",\""<<cls<<"."<<name<<"\":"<<Json(buffer);}
        for(const char* name:{"UpdateOverlayMapZoom","UpdateOverlayMapOpacity"}){
            auto m=game::FindMethod("LE.dll","LE.UI.Minimap","Minimap",name,1);
            char buffer[32];std::snprintf(buffer,sizeof buffer,"0x%llX",static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(m.code)-base));
            out<<",\"Minimap."<<name<<"\":"<<Json(buffer);}
        return out.str()+"}";
    }
    if(action=="overlay-open"){Invoke(Method(ui,"OpenOverlayMap",0),ui);return "{\"ok\":true}";}
    if(action=="overlay-close"){bool close=true;void* a[]{&close};Invoke(Method(ui,"CloseOverlayMapOrRecenter",1),ui,a);return "{\"ok\":true}";}
    if(action.rfind("map-mode-",0)==0){
        void* dmm=game::StaticObject(game::FindStaticField("LE.dll","DMM","DMMap","Instance"));if(!dmm)throw std::runtime_error("Map unavailable.");
        int index=std::stoi(action.substr(9));if(index<0||index>1)throw std::runtime_error("Unsupported map mode.");
        void* a[]{&index};Invoke(Method(dmm,"LoadConfig",1),dmm,a);return "{\"ok\":true,\"mode\":"+std::to_string(index)+"}";
    }
    if(action=="map-info"){
        // Read-only: the minimap/overlay configuration objects and their values.
        std::ostringstream out;out<<"{\"ok\":true";
        void* global=game::StaticObject(game::FindStaticField("LE.dll","","DMMapGlobalConfigs","instance"));
        if(global){
            out<<",\"global\":{\"opacity\":"<<Get<float>(global,F("DMMapGlobalConfigs","OpacityValue"))<<",\"zoom\":"<<Get<float>(global,F("DMMapGlobalConfigs","ZoomValue"))
               <<",\"minZoom\":"<<Get<float>(global,F("DMMapGlobalConfigs","minOverlayZoom"))<<",\"maxZoom\":"<<Get<float>(global,F("DMMapGlobalConfigs","maxOverlayZoom"))
               <<",\"scrollSpeed\":"<<Get<float>(global,F("DMMapGlobalConfigs","minimapScrollSpeed"))<<",\"iconScale\":"<<Get<float>(global,F("DMMapGlobalConfigs","globalIconScale"))<<",\"configs\":[";
            bool comma=false;
            for(void* cfg:Entries(Get<void*>(global,F("DMMapGlobalConfigs","configs")))){if(!cfg)continue;if(comma)out<<',';comma=true;
                out<<"{\"name\":"<<Json(Text(Get<void*>(cfg,F("DMMapGlobalConfigs.config","name"))))<<",\"opacity\":"<<Get<float>(cfg,F("DMMapGlobalConfigs.config","opacity"))
                   <<",\"zoom\":"<<Get<float>(cfg,F("DMMapGlobalConfigs.config","zoom"))<<'}';}
            out<<"]}";
        }
        void* dmm=game::StaticObject(game::FindStaticField("LE.dll","DMM","DMMap","Instance"));
        if(dmm){
            out<<",\"dmm\":{\"loaded\":"<<Get<int>(dmm,F("DMMap","loadedConfig","DMM"))<<",\"configs\":[";bool comma=false;
            for(void* cfg:Entries(Get<void*>(dmm,F("DMMap","configs","DMM")))){if(!cfg)continue;if(comma)out<<',';comma=true;
                void* rect=Get<void*>(cfg,F("DMMapConfig","uiPosition","DMM"));
                out<<"{\"name\":"<<Json(Text(Get<void*>(cfg,F("DMMapConfig","name","DMM"))));
                if(rect&&game::IsAlive(rect)){const auto size=Value<Vec2>(M("RectTransform","get_sizeDelta",0,"UnityEngine","UnityEngine.CoreModule.dll"),rect);
                    const auto scale=Value<Vec3>(M("Transform","get_localScale",0,"UnityEngine","UnityEngine.CoreModule.dll"),rect);
                    out<<",\"size\":["<<size.x<<','<<size.y<<"],\"scale\":["<<scale.x<<','<<scale.y<<']';
                    out<<",\"parents\":[";void* t=rect;bool pc=false;
                    for(int depth=0;depth<7&&t;++depth){
                        Root go(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),t));
                        const auto sz=Value<Vec2>(M("RectTransform","get_sizeDelta",0,"UnityEngine","UnityEngine.CoreModule.dll"),t);
                        const auto pivot=Value<Vec2>(M("RectTransform","get_pivot",0,"UnityEngine","UnityEngine.CoreModule.dll"),t);
                        const auto amin=Value<Vec2>(M("RectTransform","get_anchorMin",0,"UnityEngine","UnityEngine.CoreModule.dll"),t);
                        const auto amax=Value<Vec2>(M("RectTransform","get_anchorMax",0,"UnityEngine","UnityEngine.CoreModule.dll"),t);
                        if(pc)out<<',';pc=true;
                        out<<"{\"name\":"<<Json(Text(Invoke(M("Object","get_name",0,"UnityEngine","UnityEngine.CoreModule.dll"),go.Get())))<<",\"size\":["<<sz.x<<','<<sz.y<<"],\"pivot\":["<<pivot.x<<','<<pivot.y<<"],\"anchors\":["<<amin.x<<','<<amin.y<<','<<amax.x<<','<<amax.y<<"]}";
                        void* parent=Invoke(M("Transform","get_parent",0,"UnityEngine","UnityEngine.CoreModule.dll"),t);
                        t=parent&&il2cpp::api().object_get_class(parent)==il2cpp::api().object_get_class(rect)?parent:nullptr;
                    }
                    out<<']';}
                out<<'}';}
            out<<"]}";
        }
        out<<",\"minimaps\":[";bool comma=false;
        for(void* m:Find("LE.dll","LE.UI.Minimap","Minimap")){if(comma)out<<',';comma=true;
            void* canvas=Get<void*>(m,F("Minimap","Canvas","LE.UI.Minimap"));
            out<<"{\"opacity\":"<<Get<float>(m,F("Minimap","opacity","LE.UI.Minimap"))<<",\"unitsPerPixel\":"<<Get<float>(m,F("Minimap","UnitsPerPixel","LE.UI.Minimap"))<<",\"maxDistance\":"<<Get<int>(m,F("Minimap","maxDistance","LE.UI.Minimap"));
            if(canvas&&game::IsAlive(canvas)){const auto size=Value<Vec2>(M("RectTransform","get_sizeDelta",0,"UnityEngine","UnityEngine.CoreModule.dll"),canvas);out<<",\"canvas\":["<<size.x<<','<<size.y<<']';}
            out<<'}';}
        return out.str()+"]}";
    }
    if(action.rfind("filter-probe-",0)==0){
        // For one generated item (not dropped): the filter's overall verdict and
        // every rule's own Match result, to relate rule numbers to rule objects.
        int rarity=std::stoi(action.substr(13));if(rarity<0||rarity>7)throw std::runtime_error("Unsupported rarity.");
        void* generator=Get<void*>(c.actor,F("Actor","generateItems"));
        bool no=false;int level=20,location=0,type=rarity==7?-1:0,subtype=rarity==7?-1:0,sockets=0,unique=rarity==7?1:-1,lp=0,rank=0,legendaryType=0,corrupt=0;
        alignas(8) unsigned char faction[8]{},replacement[8]{},corruption[8]{},effectiveness[8]{},special[8]{},minSpecial[8]{},overrideLP[8]{};
        void* gen[]{&no,&level,&no,&location,&type,&subtype,&rarity,&sockets,&unique,&no,&lp,faction,&rank,&no,&legendaryType,&no,
            replacement,corruption,&corrupt,effectiveness,nullptr,nullptr,special,&no,minSpecial,overrideLP};
        Root item(Invoke(M("GenerateItems","initialiseRandomItemData",26),generator,gen));
        Root unpacked(Invoke(Method(item.Get(),"getAsUnpacked",0),item.Get()));
        void* manager=game::StaticObject(game::FindStaticField("LE.dll","ItemFiltering","ItemFilterManager","Instance"));
        void* filter=manager?Invoke(M("ItemFilterManager","get_Filter",0,"ItemFiltering"),manager):nullptr;
        if(!filter)return "{\"ok\":true,\"filter\":null}";
        alignas(8) unsigned char color[8]{},emphasize[8]{};int number=-1,sound=0,icon=0,beamSize=0;bool beam=false;float beamColor[4]{};
        void* ma[]{unpacked.Get(),color,emphasize,&number,&sound,&icon,&beam,&beamSize,beamColor};
        const int outcome=Value<int>(Method(filter,"Match",9),filter,ma);
        std::ostringstream out;out<<"{\"ok\":true,\"rarityByte\":"<<static_cast<int>(Get<uint8_t>(item.Get(),F("ItemData","rarity")))<<",\"outcome\":"<<outcome<<",\"number\":"<<number<<",\"rules\":[";
        int index=0;bool comma=false;int characterLevel=20;
        for(void* rule:Entries(Get<void*>(filter,F("ItemFilter","rules","ItemFiltering")))){if(!rule)continue;if(comma)out<<',';comma=true;
            void* ra[]{unpacked.Get(),&characterLevel};
            out<<"{\"index\":"<<index++<<",\"order\":"<<Value<int>(Method(rule,"get_Order",0),rule)<<",\"type\":"<<Get<int>(rule,F("Rule","type","ItemFiltering"))
               <<",\"match\":"<<Boolean(Value<bool>(Method(rule,"Match",2),rule,ra))<<'}';}
        return out.str()+"]}";
    }
    if(action.rfind("drop-item-",0)==0){
        // Disposable test character only: one generated item on the ground,
        // via the game's own generator and drop call. Format drop-item-<distance>-<rarity>[-<affix id>|-w<weaver's will>|-ww];
        // the affix id replaces the first affix's id (for loot filter affix rules); w<n> gives a Unique
        // Weaver's Will n instead of Legendary Potential; ww asks the generator for a random Unique of
        // the Weaver's Will legendary type, so the game rolls its Weaver's Will itself.
        const auto dash=action.find('-',10);if(dash==std::string::npos)throw std::runtime_error("use drop-item-<distance>-<rarity>");
        const auto affixDash=action.find('-',dash+1);
        const int distance=std::stoi(action.substr(10,dash-10));int rarity=std::stoi(action.substr(dash+1,affixDash==std::string::npos?std::string::npos:affixDash-dash-1));
        const bool rolledWeaver=affixDash!=std::string::npos&&action.substr(affixDash+1)=="ww";
        const bool anyUnique=affixDash!=std::string::npos&&action.substr(affixDash+1)=="any";
        const bool weaver=!rolledWeaver&&!anyUnique&&affixDash!=std::string::npos&&action.compare(affixDash+1,1,"w")==0;
        const int affixId=affixDash==std::string::npos||weaver||rolledWeaver||anyUnique?-1:std::stoi(action.substr(affixDash+1));
        const int weaversWill=weaver?std::stoi(action.substr(affixDash+2)):-1;
        if(weaver&&(rarity!=7||weaversWill<1||weaversWill>255))throw std::runtime_error("use drop-item-<distance>-7-w<1..255>");
        if((rolledWeaver||anyUnique)&&rarity!=7)throw std::runtime_error("use drop-item-<distance>-7-ww or -7-any");
        if(distance<1||distance>80||rarity<0||rarity>7)throw std::runtime_error("Unsupported item fixture.");
        void* generator=Get<void*>(c.actor,F("Actor","generateItems"));if(!generator)throw std::runtime_error("Item generator unavailable.");
        bool no=false,requireType=rolledWeaver;int level=rolledWeaver||anyUnique?50:20,location=0,type=rarity==7?-1:0,subtype=rarity==7?-1:0,sockets=0,unique=rarity==7&&!rolledWeaver&&!anyUnique?1:-1,lp=0,rank=0,legendaryType=rolledWeaver?1:0,corrupt=0;
        alignas(8) unsigned char faction[8]{},replacement[8]{},corruption[8]{},effectiveness[8]{},special[8]{},minSpecial[8]{},overrideLP[8]{};
        void* gen[]{&no,&level,&no,&location,&type,&subtype,&rarity,&sockets,&unique,&no,&lp,faction,&rank,&requireType,&legendaryType,&no,
            replacement,corruption,&corrupt,effectiveness,nullptr,nullptr,special,&no,minSpecial,overrideLP};
        Root item(Invoke(M("GenerateItems","initialiseRandomItemData",26),generator,gen));
        if(!item.Get())throw std::runtime_error("The game did not generate an item.");
        if(affixId>=0){auto list=Entries(Get<void*>(item.Get(),F("ItemData","affixes")));if(list.empty()||!list.front())throw std::runtime_error("The generated item has no affix.");
            *reinterpret_cast<uint16_t*>(static_cast<char*>(list.front())+F("ItemAffix","affixId"))=static_cast<uint16_t>(affixId);}
        if(weaver){*reinterpret_cast<uint8_t*>(static_cast<char*>(item.Get())+F("ItemData","legendaryPotential"))=0;
            *reinterpret_cast<uint8_t*>(static_cast<char*>(item.Get())+F("ItemData","weaversWill"))=static_cast<uint8_t>(weaversWill);}
        void* manager=game::StaticObject(game::FindStaticField("LE.dll","","GroundItemManager","instance"));
        if(!manager)throw std::runtime_error("Ground item manager unavailable.");
        Vec3 at=Position(c.actor);at.x+=static_cast<float>(distance);bool sound=false;
        void* args[]{c.actor,item.Get(),&at,&sound};Invoke(M("GroundItemManager","dropItemForPlayer",4),manager,args);
        return "{\"ok\":true,\"distance\":"+std::to_string(distance)+",\"rarity\":"+std::to_string(rarity)+
               ",\"lp\":"+std::to_string(Get<uint8_t>(item.Get(),F("ItemData","legendaryPotential")))+",\"weaversWill\":"+std::to_string(Get<uint8_t>(item.Get(),F("ItemData","weaversWill")))+"}";
    }
    if(action=="woven-join"||action=="woven-read"){
        // Disposable test character only: The Woven membership through the game's
        // own FactionTracker.JoinFaction, and its Memory Amber (the faction's favor).
        void* provider=Get<void*>(c.actor,F("Actor","factionInfo"));if(!provider)throw std::runtime_error("Faction tracker unavailable.");
        uint8_t woven=3;  // LE.Factions.FactionID.TheWeaver
        if(action=="woven-join"){void* a[]{&woven};Invoke(Method(provider,"JoinFaction",1),provider,a);}
        void* faction=nullptr;void* ta[]{&woven,&faction};
        if(!Value<bool>(Method(provider,"TryGetFaction",2),provider,ta)||!faction)return std::string("{\"ok\":true,\"woven\":false}");
        return "{\"ok\":true,\"woven\":true,\"member\":"+std::string(Boolean(Get<bool>(faction,F("Faction","<IsMember>k__BackingField","LE.Factions"))))+
               ",\"amber\":"+std::to_string(Get<int>(faction,F("Faction","<Favor>k__BackingField","LE.Factions")))+
               ",\"reputation\":"+std::to_string(Get<int>(faction,F("Faction","<Reputation>k__BackingField","LE.Factions")))+
               ",\"rank\":"+std::to_string(Get<int>(faction,F("Faction","<Rank>k__BackingField","LE.Factions")))+"}";
    }
    if(action.rfind("drop-amber-",0)==0){
        // Disposable test character only: one native Memory Amber pile at a distance.
        const int distance=std::stoi(action.substr(11));if(distance<1||distance>80)throw std::runtime_error("Unsupported amber distance.");
        void* manager=game::StaticObject(game::FindStaticField("LE.dll","LE.Factions","PickupableObjectsManager","Instance"));
        if(!manager)throw std::runtime_error("Pickupable object manager unavailable.");
        Vec3 at=Position(c.actor);at.x+=static_cast<float>(distance);uint8_t type=1;uint32_t value=10;
        void* args[]{&type,c.actor,&at,&value};Invoke(M("PickupableObjectsManager","CreatePickupableObjectForPlayer",4,"LE.Factions"),manager,args);
        return "{\"ok\":true,\"amber\":10,\"distance\":"+std::to_string(distance)+"}";
    }
    if(action=="ground-count"){
        // Disposable test character only (read only): drops on the ground (equipment, affix shards, other
        // materials, gold), the item labels the game shows for them, and what the active loot filter says
        // for each shard on the ground (shown or hidden).
        void* manager=game::StaticObject(game::FindStaticField("LE.dll","","GroundItemManager","instance"));
        if(!manager)throw std::runtime_error("Ground item manager unavailable.");
        void* offline=Get<void*>(manager,F("GroundItemManager","offlineItems"));if(!offline)throw std::runtime_error("Ground item list unavailable.");
        void* filterManager=game::StaticObject(game::FindStaticField("LE.dll","ItemFiltering","ItemFilterManager","Instance"));
        void* filter=filterManager?Get<void*>(filterManager,F("ItemFilterManager","<Filter>k__BackingField","ItemFiltering")):nullptr;
        std::unique_ptr<Root> filterPin;if(filter)filterPin=std::make_unique<Root>(filter);
        int equipment=0,shards=0,otherMaterials=0,shardsShown=0,shardsHidden=0;
        for(void* record:Entries(Get<void*>(offline,F("GroundItemManager.GroundItemList","items")))){
            void* data=record?Get<void*>(record,F("GroundItemManager.GroundItem","itemData")):nullptr;if(!data)continue;Root pin(data);
            const int type=Get<uint8_t>(data,F("ItemData","itemType"));
            if(type==101){
                ++shards;
                void* unpackedItem=filter?Invoke(Method(data,"getAsUnpacked",0),data):nullptr;
                if(unpackedItem){Root unpacked(unpackedItem);
                    alignas(8) unsigned char color[8]{},emphasize[8]{};int rule=-1,sound=0,icon=0,beamSize=0;bool beam=false;float beamColor[4]{};
                    void* args[]{unpacked.Get(),color,emphasize,&rule,&sound,&icon,&beam,&beamSize,beamColor};
                    (Value<int>(Method(filter,"Match",9),filter,args)==1?shardsHidden:shardsShown)++;}
            }else if(type>=100)++otherMaterials;else ++equipment;
        }
        const size_t gold=Entries(Get<void*>(offline,F("GroundItemManager.GroundItemList","gold"))).size();
        int labels=0,shardLabels=0;
        if(void* dlist=game::StaticObject(game::FindStaticField("LE.dll","","ItemTooltipOrganizer","pickableGroundLabelList"))){
            const auto* labelClass=game::FindClass("LE.dll","","GroundItemLabel");
            void* inner=*reinterpret_cast<void* const*>(static_cast<char*>(dlist)+0x10);
            for(void* label:Entries(inner)){
                if(!label||il2cpp::api().object_get_class(label)!=labelClass||!game::IsAlive(label))continue;++labels;
                void* data=Invoke(Method(label,"getItemData",0),label);if(data&&Get<uint8_t>(data,F("ItemData","itemType"))==101)++shardLabels;
            }
        }
        std::ostringstream out;out<<"{\"ok\":true,\"equipment\":"<<equipment<<",\"shards\":"<<shards<<",\"otherMaterials\":"<<otherMaterials<<",\"gold\":"<<gold
           <<",\"labels\":"<<labels<<",\"shardLabels\":"<<shardLabels<<",\"filter\":"<<Json(filter?Text(Get<void*>(filter,F("ItemFilter","name","ItemFiltering"))):std::string())
           <<",\"filterActive\":"<<Boolean(filter!=nullptr)<<",\"shardsShownByFilter\":"<<shardsShown<<",\"shardsHiddenByFilter\":"<<shardsHidden<<'}';
        return out.str();
    }
    if(action.rfind("drop-shard-",0)==0){
        // Disposable test character only: one random affix shard at a distance, via the game's own shard spawn.
        const int distance=std::stoi(action.substr(11));if(distance<1||distance>80)throw std::runtime_error("Unsupported shard distance.");
        void* generator=Get<void*>(c.actor,F("Actor","generateItems"));if(!generator)throw std::runtime_error("Item generator unavailable.");
        Vec3 at=Position(c.actor);at.x+=static_cast<float>(distance);bool no=false;
        void* args[]{&at,&no,&no};Invoke(M("GenerateItems","SpawnShard",3),generator,args);
        return "{\"ok\":true,\"distance\":"+std::to_string(distance)+"}";
    }
    if(action=="inventory-full"){
        // Calls the game's own notification (through EpochPact's detour).
        void* manager=game::StaticObject(game::FindStaticField("LE.dll","","GroundItemManager","instance"));
        if(!manager)throw std::runtime_error("Ground item manager unavailable.");
        void* args[]{c.actor};Invoke(M("GroundItemManager","showInventoryFullNofication",1),manager,args);return "{\"ok\":true}";
    }
    if(action=="stash-tabs"||action.rfind("stash-tab-",0)==0){
        void* legacy=nullptr;for(void* candidate:Find("LE.dll","","StashTabbedUIControls"))if(game::IsAlive(candidate)){legacy=candidate;break;}
        void* panel=nullptr;for(void* candidate:Find("LE.dll",kPanelNs,"StashPanelV2"))if(game::IsAlive(candidate)){panel=candidate;break;}
        void* controls=StashControls(panel);
        if(!legacy&&!controls)throw std::runtime_error("Open the stash first.");
        if(action!="stash-tabs"){int id=std::stoi(action.substr(10));
            if(legacy){bool flag=false;void* args[]{&id,&flag};Invoke(Method(legacy,"SwitchToStashTab",2),legacy,args);}
            else{int button=0;void* args[]{&id,&button,nullptr};Invoke(Method(controls,"TriggerTabClicked",3),controls,args);}}
        return "{\"ok\":true,\"ui\":\""+std::string(legacy?"legacy":"v2")+"\",\"active\":"+std::to_string(legacy?LegacyTab(legacy):ActiveStashTab(controls))+",\"remembered\":"+std::to_string(lastStashTab)+",\"restores\":"+std::to_string(stashRestores)+",\"restoreFailures\":"+std::to_string(stashRestoreFailures)+"}";
    }
    if(action=="panels")return "{\"ok\":true,\"inventory\":"+std::string(Boolean(Value<bool>(Method(ui,"IsInventoryOpen",0),ui)))+",\"stash\":"+Boolean(Value<bool>(Method(ui,"IsStashOpen",0),ui))+",\"shop\":"+Boolean(Value<bool>(Method(ui,"IsShopOpen",0),ui))+",\"buttons\":"+std::to_string(shortcuts.size())+"}";
    if(action=="screenshot"){
        auto capture=game::FindMethod("UnityEngine.ScreenCaptureModule.dll","UnityEngine","ScreenCapture","CaptureScreenshot",2);
        if(!capture)throw std::runtime_error("Native screenshot method unavailable.");
        auto text=PluginDir()+L"feedback-inventory.png";
        // A background render may never run. Do not let an old image masquerade
        // as the result of a new capture request in the live test harness.
        if(GetFileAttributesW(text.c_str())!=INVALID_FILE_ATTRIBUTES&&!DeleteFileW(text.c_str()))
            throw std::runtime_error("Could not remove the previous test screenshot.");
        int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.c_str(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
        std::string encoded(bytes,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.c_str(),static_cast<int>(text.size()),encoded.data(),bytes,nullptr,nullptr);
        Root path(il2cpp::api().string_new(encoded.c_str()));
        int scale=1;void* args[]{path.Get(),&scale};Invoke(capture,nullptr,args);return "{\"ok\":true,\"requested\":true}";
    }
    if(action=="radius-away"){
        auto start=Position(c.actor);void* agent=Get<void*>(c.actor,F("Actor","navMeshAgent"));
        for(auto offset:std::array<Vec3,4>{{{12,0,0},{-12,0,0},{0,0,12},{0,0,-12}}}){
            Vec3 p{start.x+offset.x,start.y,start.z+offset.z};void* args[]{&p};
            if(Value<bool>(Method(agent,"Warp",1),agent,args)){auto actual=Position(c.actor);double x=actual.x-start.x,z=actual.z-start.z;
                if(x*x+z*z>100)return "{\"ok\":true,\"distance\":"+std::to_string(std::sqrt(x*x+z*z))+"}";}
        }throw std::runtime_error("Native warp did not move far enough for a radius check.");
    }
    if(action.rfind("button-",0)==0){const std::string requested=action.substr(7);auto it=std::find_if(shortcuts.begin(),shortcuts.end(),[&](const Shortcut& s){return s.action==requested;});if(it==shortcuts.end())throw std::runtime_error("Inventory shortcut missing.");
        if(!ShortcutClick(it->button->Get()))throw std::runtime_error("Click was not handled.");return "{\"ok\":true,\"queued\":true}";}
    if(action=="graphics"){
        std::ostringstream out;out<<"{\"ok\":true,\"shadows\":"<<Value<int>(M("QualitySettings","get_shadows",0,"UnityEngine","UnityEngine.CoreModule.dll"))<<",\"overrides\":[";bool comma=false;
        for(auto& e:saved){bool actual=false;if(e.field)il2cpp::api().field_get_value(e.value->Get(),e.field,&actual);else actual=Value<bool>(M("Behaviour","get_enabled",0,"UnityEngine","UnityEngine.CoreModule.dll"),e.value->Get());
            if(comma)out<<',';comma=true;out<<"{\"setting\":"<<Json(e.setting->key)<<",\"before\":"<<Boolean(e.before)<<",\"actual\":"<<Boolean(actual)<<'}';}return out.str()+"]}";
    }
    if(action=="map-F80"){
        // Test-only navigation uses the game's map/era selection, not direct travel.
        bool open=Value<bool>(Method(ui,"IsWorldMapPanelOpen",0),ui);
        if(!open)Invoke(Method(ui,"MapKeyDown",0),ui);
        for(void* point:Find("LE.dll","","UIWaypointStandard")){
            if(Text(Get<void*>(point,F("UIWaypoint","sceneName")))!="F80")continue;
            Root panel(Get<void*>(point,F("UIWaypointStandard","_mapPanel")));
            void* args[]{point};Invoke(Method(panel.Get(),"HandleZoomToLocationAndEra",1),panel.Get(),args);
            return "{\"ok\":true,\"selected\":\"F80\"}";
        }
        throw std::runtime_error("Test map destination unavailable.");
    }
    if(action=="damage-icon-probe"){
        // Which skill picture each own summon would show in the damage window.
        void* tracker=Get<void*>(c.actor,F("Actor","summonTracker"));std::ostringstream out;out<<"{\"ok\":true,\"summons\":[";bool comma=false;
        auto describe=[](void* skill){if(!skill)return std::string("null");return "{\"name\":"+Json(Text(Get<void*>(skill,F("Ability","abilityName"))))+",\"id\":"+Json(Text(Get<void*>(skill,F("Ability","playerAbilityID"))))+",\"sprite\":"+Boolean(Get<void*>(skill,F("Ability","abilitySpriteSoftRef"))!=nullptr)+"}";};
        for(void* summoned:Entries(Get<void*>(tracker,F("SummonTracker","summons")))){
            if(!summoned||!game::IsAlive(summoned))continue;void* actor=Get<void*>(summoned,F("Summoned","actor"));void* references=Get<void*>(summoned,F("Summoned","references"));
            if(comma)out<<',';comma=true;
            out<<"{\"type\":"<<Json(actor&&game::IsAlive(actor)?AllyType(actor):std::string())<<",\"override\":"<<Boolean(Get<bool>(summoned,F("Summoned","overrideMinionSprite")))
               <<",\"cardSkill\":"<<describe(Get<void*>(summoned,F("Summoned","minionSpriteAbility")))<<",\"references\":"<<Boolean(references!=nullptr)
               <<",\"summoningSkill\":"<<describe(references?Get<void*>(references,F("CreationReferences","thisAbility")):nullptr)<<",\"key\":"<<Json(AllyIconKey(summoned))
               <<",\"label\":"<<Json(actor&&game::IsAlive(actor)?AllyLabel(actor):std::string());
            if(actor&&game::IsAlive(actor)){
                Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("LE.dll","","ActorDisplayInformation"))));
                void* ga[]{type.Get()};void* info=Invoke(Signature("Component","GetComponent",{"System.Type"}),actor,ga);
                if(!info)info=Invoke(Signature("Component","GetComponentInChildren",{"System.Type"}),actor,ga);
                if(info)out<<",\"displayName\":"<<Json(Text(Get<void*>(info,F("BaseDisplayInformation","displayName"))))<<",\"baseDisplayName\":"<<Json(Text(Get<void*>(info,F("BaseDisplayInformation","baseDisplayName"))))
                           <<",\"customName\":"<<Boolean(Get<bool>(info,F("BaseDisplayInformation","CustomNameSet")))<<",\"localized\":"<<Json(Text(Invoke(Method(info,"GetLocalizedName",0),info)))
                           <<",\"hasCustomName\":"<<Boolean(Get<bool>(summoned,F("Summoned","hasCustomName")));
            }
            out<<'}';
        }
        return out.str()+"]}";
    }
    if(action=="summon-counts"){
        // The character's own summons, counted by the ability that made them.
        void* tracker=Get<void*>(c.actor,F("Actor","summonTracker"));if(!tracker)throw std::runtime_error("Summon tracker unavailable.");
        std::map<std::string,int> counts;int total=0;
        for(void* summoned:Entries(Get<void*>(tracker,F("SummonTracker","summons")))){
            if(!summoned||!game::IsAlive(summoned))continue;void* refs=Get<void*>(summoned,F("Summoned","references"));
            void* ability=refs?Get<void*>(refs,F("CreationReferences","thisAbility")):nullptr;
            ++counts[ability?Text(Get<void*>(ability,F("Ability","abilityName"))):std::string("?")];++total;
        }
        std::ostringstream out;out<<"{\"ok\":true,\"total\":"<<total<<",\"companionLimit\":";
        void* stats=Get<void*>(c.actor,F("Actor","stats"));out<<(stats?Value<int>(Method(stats,"getMaximumCompanions",0),stats):-1)<<",\"byAbility\":{";
        bool comma=false;for(const auto& [name,n]:counts){if(comma)out<<',';comma=true;out<<Json(name)<<':'<<n;}
        return out.str()+"}}";
    }
    if(action=="player-heal"){
        // Disposable test character only: current health back to maximum (a raised maximum health
        // does not refill it, and the measuring targets cannot die).
        void* health=Get<void*>(c.actor,F("Actor","health"));if(!health)throw std::runtime_error("Health unavailable.");
        const float before=Get<float>(health,F("BaseHealth","currentHealth"));const int maximum=Get<int>(health,F("BaseHealth","maxHealth"));
        *reinterpret_cast<float*>(static_cast<char*>(health)+F("BaseHealth","currentHealth"))=static_cast<float>(maximum);
        return "{\"ok\":true,\"before\":"+std::to_string(before)+",\"health\":"+std::to_string(maximum)+"}";
    }
    if(action.rfind("player-health-",0)==0){
        // Disposable test character only: current health set to this much (the next hit can be lethal).
        const float amount=std::stof(action.substr(14));void* health=Get<void*>(c.actor,F("Actor","health"));if(!health)throw std::runtime_error("Health unavailable.");
        if(!(amount>=1&&amount<=static_cast<float>(Get<int>(health,F("BaseHealth","maxHealth")))))throw std::runtime_error("Unsupported health.");
        *reinterpret_cast<float*>(static_cast<char*>(health)+F("BaseHealth","currentHealth"))=amount;
        return "{\"ok\":true,\"health\":"+std::to_string(amount)+"}";
    }
    if(action.rfind("enemy-attack-",0)==0||action.rfind("enemy-ailment-",0)==0){
        // Disposable test character only. enemy-attack-<k>: the nearest enemy uses its k-th ability at the
        // character from where it stands; enemy-ailment-<ailment id>-<stacks>: the nearest enemy's stacks of
        // that ailment on the character (as its hits apply them; 7 = poison, 1 = ignite, 2 = bleed).
        const auto me=Position(c.actor);void* alignment=Get<void*>(Get<void*>(c.actor,F("Actor","alignmentManager")),F("AlignmentManager","alignment"));
        void* nearest=nullptr;double distance=1.e30;
        for(void* target:Find("LE.dll","","Actor")){
            if(!game::IsAlive(target)||target==c.actor)continue;void* manager=Get<void*>(target,F("Actor","alignmentManager"));if(!manager)continue;
            void* other=Get<void*>(manager,F("AlignmentManager","alignment"));void* args[]{other};if(!other||!Value<bool>(Method(alignment,"IsFoe",1),alignment,args))continue;
            void* health=Get<void*>(target,F("Actor","health"));if(!health||Get<float>(health,F("BaseHealth","currentHealth"))<=0)continue;
            auto p=Position(target);double x=p.x-me.x,z=p.z-me.z,d=x*x+z*z;if(d<distance){distance=d;nearest=target;}
        }
        if(!nearest)throw std::runtime_error("No test enemy available.");Root pin(nearest);
        const std::string enemy=AttackerName(nearest);
        Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("LE.dll","","AbilityList"))));
        void* ca[]{type.Get()};Root list(Invoke(Signature("Component","GetComponent",{"System.Type"}),nearest,ca));
        if(!list.Get())throw std::runtime_error("This enemy has no ability list.");
        const auto abilities=Entries(Get<void*>(list.Get(),F("AbilityList","abilities")));if(abilities.empty())throw std::runtime_error("This enemy has no loaded attack.");
        if(action.rfind("enemy-ailment-",0)==0){
            // As the enemy's own hits apply it: its stats, its ability and its alignment.
            const auto dash=action.find('-',14);if(dash==std::string::npos)throw std::runtime_error("use enemy-ailment-<ailment id>-<stacks>");
            int ailmentId=std::stoi(action.substr(14,dash-14));float stacks=std::stof(action.substr(dash+1));if(ailmentId<0||ailmentId>200||!(stacks>=1&&stacks<=50))throw std::runtime_error("Unsupported ailment.");
            void* ia[]{&ailmentId};Root ailment(Invoke(Signature("AilmentList","getAilment",{"System.Int32"},"","LE.dll"),nullptr,ia));if(!ailment.Get())throw std::runtime_error("Ailment unavailable.");
            void* receiver=Get<void*>(c.actor,F("Actor","ailmentReceiver"));if(!receiver)throw std::runtime_error("Ailment receiver unavailable.");
            void* own=abilities.front();void* stats=Get<void*>(nearest,F("Actor","stats"));
            void* enemyAlignment=Get<void*>(Get<void*>(nearest,F("Actor","alignmentManager")),F("AlignmentManager","alignment"));
            float none=0;int useType=9;bool conversions=false;
            void* args[]{ailment.Get(),nearest,own,own,own,stats,enemyAlignment,&stacks,&none,&none,&useType,&conversions,&none,&none};
            Invoke(M("AilmentReceiver","ApplyAilment",14),receiver,args);
            return "{\"ok\":true,\"requested\":true,\"enemy\":"+Json(enemy)+",\"ailment\":"+Json(Text(Get<void*>(ailment.Get(),F("Ailment","displayName"))))+",\"distance\":"+std::to_string(std::sqrt(distance))+"}";
        }
        // The enemy is moved next to the character first, so a melee attack reaches.
        bool moved=false;
        if(void* agent=Get<void*>(nearest,F("Actor","navMeshAgent"));agent&&distance>2.25){
            const auto t=Position(nearest);const double d=std::sqrt(distance);
            Vec3 beside{static_cast<float>(me.x+(t.x-me.x)/d*1.5),me.y,static_cast<float>(me.z+(t.z-me.z)/d*1.5)};void* wa[]{&beside};
            moved=Value<bool>(Method(agent,"Warp",1),agent,wa);
        }
        Root ability(abilities[static_cast<size_t>(std::stoul(action.substr(13)))%abilities.size()]);if(!ability.Get())throw std::runtime_error("Empty ability slot.");
        Vec3 from=Position(nearest),to=me;void* constructor=Get<void*>(nearest,F("Actor","abilityObjectConstructor"));float delay=0;int useType=1;bool onDeath=false;
        void* args[]{constructor,&from,&to,&delay,&useType,&onDeath,nullptr};Invoke(Method(ability.Get(),"CastAfterDelay",7),ability.Get(),args);
        const auto now=Position(nearest);
        return "{\"ok\":true,\"requested\":true,\"enemy\":"+Json(enemy)+",\"ability\":"+Json(Text(Get<void*>(ability.Get(),F("Ability","abilityName"))))
              +",\"abilities\":"+std::to_string(abilities.size())+",\"moved\":"+Boolean(moved)+",\"distance\":"+std::to_string(std::sqrt((now.x-me.x)*(now.x-me.x)+(now.z-me.z)*(now.z-me.z)))+"}";
    }
    if(action=="player-flags"){
        // Read only: the character's damage-related flags.
        void* health=Get<void*>(c.actor,F("Actor","health"));void* protection=Get<void*>(c.actor,F("Actor","protection"));
        std::ostringstream out;out.setf(std::ios::fixed);out.precision(3);
        out<<"{\"ok\":true,\"damageable\":"<<Boolean(Get<bool>(health,F("BaseHealth","damageable")))<<",\"canDie\":"<<Boolean(Get<bool>(health,F("BaseHealth","canDie")))
           <<",\"isDamageable\":"<<Boolean(Value<bool>(Method(health,"isDamageable",0),health))<<",\"cannotDie\":"<<Boolean(Value<bool>(Method(health,"CannotDie",0),health))
           <<",\"ward\":"<<Get<float>(protection,F("ProtectionClass","ward"))<<",\"fixedDamageReduction\":"<<Get<float>(protection,F("ProtectionClass","fixedDamageReduction"))
           <<",\"damageTakenStats\":"<<Boolean(Get<bool>(protection,F("ProtectionClass","hasDamageTakenStats")))<<",\"damageTakenModifier\":"<<Get<float>(protection,F("PrecalculatedStatsHolder","untaggedDamageTakenModifier"))<<",\"invulnerable\":"<<Boolean(Value<bool>(Method(protection,"get_IsInvulnerable",0),protection))<<",\"protectionMatches\":"<<Boolean(protection==takenProtection)<<'}';
        return out.str();
    }
    if(action=="damage-taken-entries"||action=="damage-taken-entries-clear"){
        // Disposable test character only: every Stats.Stat with SP 6 (DamageTaken) in the character's stats and in
        // its character mutator's list, with its more values; -clear turns a more of -1 into 0.
        const bool clear=action=="damage-taken-entries-clear";
        void* mutator=Get<void*>(c.actor,F("Actor","characterMutator"));void* stats=mutator?Get<void*>(mutator,F("CharacterMutator","myStats")):nullptr;
        if(!stats)throw std::runtime_error("Character stats unavailable.");
        std::ostringstream out;out.setf(std::ios::fixed);out.precision(3);out<<"{\"ok\":true";int cleared=0;
        const auto dumpList=[&](const char* name,void* list){
            out<<",\""<<name<<"\":[";bool comma=false;
            for(void* e:Entries(list)){
                if(!e||Get<uint8_t>(e,F("Stats.Stat","property"))!=6)continue;
                if(comma)out<<',';comma=true;
                out<<"{\"tags\":"<<Get<int>(e,F("Stats.Stat","tags"))<<",\"special\":"<<static_cast<int>(Get<uint8_t>(e,F("Stats.Stat","specialTag")))<<",\"extra\":"<<Get<int>(e,F("Stats.Stat","extraTag"))
                   <<",\"added\":"<<Get<float>(e,F("Stats.Stat","addedValue"))<<",\"increased\":"<<Get<float>(e,F("Stats.Stat","increasedValue"))<<",\"more\":[";
                void* more=Get<void*>(e,F("Stats.Stat","moreValues"));
                if(more){const int n=Get<int>(more,0x18);void* items=Get<void*>(more,0x10);
                    for(int i=0;i<n&&i<64&&items;++i){float& v=*reinterpret_cast<float*>(static_cast<char*>(items)+0x20+sizeof(float)*i);if(i)out<<',';out<<v;if(clear&&v<=-0.999f){v=0;++cleared;}}}
                out<<"]}";
            }
            out<<']';
        };
        dumpList("stats",Get<void*>(stats,F("Stats","stats")));
        dumpList("mutator",Get<void*>(mutator,F("CharacterMutator","stats")));
        dumpList("mutatorOld",Get<void*>(mutator,F("CharacterMutator","oldStats")));
        if(clear&&cleared)*reinterpret_cast<bool*>(static_cast<char*>(stats)+F("BaseStats","statsNeedToBeUpdatedNextFrame"))=true;
        out<<",\"cleared\":"<<cleared<<'}';return out.str();
    }
    if(action=="player-damageable"){
        // Disposable test character only: the character can take damage again: the game's own flag, and its
        // damage taken modifier (0 = takes no damage) back to normal.
        void* health=Get<void*>(c.actor,F("Actor","health"));const bool before=Get<bool>(health,F("BaseHealth","damageable"));
        *reinterpret_cast<bool*>(static_cast<char*>(health)+F("BaseHealth","damageable"))=true;
        void* protection=Get<void*>(c.actor,F("Actor","protection"));float& modifier=*reinterpret_cast<float*>(static_cast<char*>(protection)+F("PrecalculatedStatsHolder","untaggedDamageTakenModifier"));
        const float modifierBefore=modifier;if(!(modifier>0))modifier=1;
        return std::string("{\"ok\":true,\"before\":")+Boolean(before)+",\"modifierBefore\":"+std::to_string(modifierBefore)+",\"modifier\":"+std::to_string(modifier)
            +",\"invulnerable\":"+Boolean(Value<bool>(Method(protection,"get_IsInvulnerable",0),protection))+"}";
    }
    if(action=="taken-read"||action=="taken-reset"){
        // Per attacker: the damage recorded for hits on the character, the health and ward those hits removed,
        // the hits and how many were damage over time.
        std::ostringstream out;out.setf(std::ios::fixed);out.precision(2);out<<"{\"ok\":true,\"byAttacker\":{";bool comma=false;
        for(const auto& [name,t]:testTakenBy){if(comma)out<<',';comma=true;out<<Json(name)<<":{\"recorded\":"<<t[0]<<",\"lost\":"<<t[1]<<",\"hits\":"<<t[2]<<",\"dot\":"<<t[3]<<'}';}
        out<<"},\"zeroHits\":"<<testTakenZero<<",\"recording\":"<<Boolean(takenProtection!=nullptr)<<",\"recent\":"<<takenRecent.size()<<",\"deaths\":"<<deathCount<<'}';
        if(action=="taken-reset"){testTakenBy.clear();testTakenZero=0;}
        return out.str();
    }
    if(action.rfind("minion-hit-",0)==0){
        // Disposable test character only: the first own summon takes this much health damage directly.
        const float amount=std::stof(action.substr(11));if(!(amount>0&&amount<1e9f))throw std::runtime_error("Unsupported damage.");
        void* tracker=Get<void*>(c.actor,F("Actor","summonTracker"));void* actor=nullptr;
        for(void* summoned:Entries(Get<void*>(tracker,F("SummonTracker","summons"))))if(summoned&&game::IsAlive(summoned)){actor=Get<void*>(summoned,F("Summoned","actor"));if(actor&&game::IsAlive(actor))break;actor=nullptr;}
        if(!actor)throw std::runtime_error("Cast a test ally first.");
        Root pin(actor);void* health=Get<void*>(actor,F("Actor","health"));Root healthPin(health);
        const float before=Get<float>(health,F("BaseHealth","currentHealth"));const bool cannot=Value<bool>(Method(health,"CannotDie",0),health);
        // A minion that is not damageable right now (emerging) is made damageable for this hit only.
        bool& damageable=*reinterpret_cast<bool*>(static_cast<char*>(health)+F("BaseHealth","damageable"));const bool wasDamageable=damageable;damageable=true;
        float hit=amount;void* a[]{&hit};const float dealt=Value<float>(Method(health,"HealthDamage",1),health,a);
        if(game::IsAlive(health))damageable=wasDamageable;
        const float after=game::IsAlive(health)?Get<float>(health,F("BaseHealth","currentHealth")):-1.f;
        return "{\"ok\":true,\"name\":"+Json(Text(Invoke(M("Object","get_name",0,"UnityEngine","UnityEngine.CoreModule.dll"),actor)))+",\"before\":"+std::to_string(before)+",\"after\":"+std::to_string(after)+
            ",\"dealt\":"+std::to_string(dealt)+",\"wasDamageable\":"+Boolean(wasDamageable)+",\"cannotDie\":"+Boolean(cannot)+",\"alive\":"+Boolean(game::IsAlive(actor))+"}";
    }
    if(action=="summon-cast"||action.rfind("summon-cast-",0)==0){
        // summon-cast: the Ballista; summon-cast-<ability id>: any ability, cast by the character.
        uint16_t abilityId=action=="summon-cast"?379:static_cast<uint16_t>(std::stoi(action.substr(12)));void* a[]{&abilityId};Root ability(Invoke(Signature("Ability","getAbility",{"System.UInt16"},"","LE.dll"),nullptr,a));
        if(!ability.Get())throw std::runtime_error("Ability unavailable.");
        void* constructor=Get<void*>(c.actor,F("Actor","abilityObjectConstructor"));Vec3 p=Position(c.actor),direction{0,0,1};p.x+=2;float delay=0;int type=1;bool mutate=true;void* args[]{constructor,&p,&direction,&delay,&type,&mutate,nullptr};Invoke(Method(ability.Get(),"CastAfterDelay",7),ability.Get(),args);
        return "{\"ok\":true,\"requested\":true,\"ability\":"+Json(Text(Get<void*>(ability.Get(),F("Ability","abilityName"))))+"}";
    }
    if(action=="combat-clock")return "{\"ok\":true,\"timeScale\":"+std::to_string(Value<float>(M("Time","get_timeScale",0,"UnityEngine","UnityEngine.CoreModule.dll")))+",\"damageCalls\":"+std::to_string(testDamageCalls)+",\"ownedCalls\":"+std::to_string(testDamageOwned)+",\"lastPrimary\":"+std::to_string(testDamagePrimary)+",\"lastSecondary\":"+std::to_string(testDamageSecondary)+"}";
    if(action=="ally-attack"||action.rfind("ally-attack-",0)==0||action.rfind("player-attack-",0)==0||action.rfind("cast-at-",0)==0||action.rfind("player-skill-",0)==0){
        // player-attack-<n>: the character's own n-th ability, cast at the nearest enemy;
        // player-skill-<skill ID>: the character's own ability with that save skill ID (its bar skill);
        // ally-attack[-<k>]: the k-th own summon's attack (default the first);
        // cast-at-<ability id>: any ability, cast by the character at the nearest enemy.
        const bool castAt=action.rfind("cast-at-",0)==0,bySkill=action.rfind("player-skill-",0)==0;
        const bool player=castAt||bySkill||action.rfind("player-attack-",0)==0;
        const size_t wantedAlly=action.rfind("ally-attack-",0)==0?static_cast<size_t>(std::stoul(action.substr(12))):0;size_t allyIndex=0;
        void* source=player?c.actor:nullptr;void* tracker=Get<void*>(c.actor,F("Actor","summonTracker"));
        if(!player)for(void* summoned:Entries(Get<void*>(tracker,F("SummonTracker","summons")))){
            if(!summoned||!game::IsAlive(summoned))continue;
            void* references=Get<void*>(summoned,F("Summoned","references"));
            if(references&&Get<void*>(references,F("CreationReferences","creator"))==c.actor&&allyIndex++==wantedAlly){source=Get<void*>(summoned,F("Summoned","actor"));break;}
        }
        if(!source||!game::IsAlive(source))throw std::runtime_error("Cast a test ally first.");Root pin(source);
        std::vector<void*> abilities;void* chosen=nullptr;
        if(castAt){
            uint16_t abilityId=static_cast<uint16_t>(std::stoi(action.substr(8)));void* a[]{&abilityId};
            chosen=Invoke(Signature("Ability","getAbility",{"System.UInt16"},"","LE.dll"),nullptr,a);if(!chosen)throw std::runtime_error("Ability unavailable.");
            abilities.push_back(chosen);
        }else{
            Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("LE.dll","","AbilityList"))));
            void* ca[]{type.Get()};Root list(Invoke(Signature("Component","GetComponent",{"System.Type"}),source,ca));
            if(!list.Get())throw std::runtime_error("This ally has no ability list.");
            abilities=Entries(Get<void*>(list.Get(),F("AbilityList","abilities")));if(abilities.empty())throw std::runtime_error("This ally has no loaded attack.");
            if(bySkill){
                const std::string wanted=action.substr(13);
                for(void* a:abilities)if(a&&Text(Get<void*>(a,F("Ability","playerAbilityID")))==wanted){chosen=a;break;}
                if(!chosen)throw std::runtime_error("The character has no ability with that skill ID.");
            }else chosen=abilities[player?static_cast<size_t>(std::stoi(action.substr(14)))%abilities.size():0];
        }
        Root ability(chosen);if(!ability.Get())throw std::runtime_error("Empty ability slot.");void* alignment=Get<void*>(Get<void*>(source,F("Actor","alignmentManager")),F("AlignmentManager","alignment"));
        auto start=Position(source);void* nearest=nullptr;double distance=1.e30;
        for(void* target:Find("LE.dll","","Actor")){
            if(!game::IsAlive(target))continue;void* manager=Get<void*>(target,F("Actor","alignmentManager"));if(!manager)continue;
            void* other=Get<void*>(manager,F("AlignmentManager","alignment"));void* args[]{other};if(!other||!Value<bool>(Method(alignment,"IsFoe",1),alignment,args))continue;
            void* health=Get<void*>(target,F("Actor","health"));if(!health||Get<float>(health,F("BaseHealth","currentHealth"))<=0)continue;
            auto p=Position(target);double x=p.x-start.x,z=p.z-start.z,d=x*x+z*z;if(d<distance){distance=d;nearest=target;}
        }
        if(!nearest)throw std::runtime_error("No test enemy available.");
        if(!player&&distance>4.0){
            // A melee ally walks up to its target before it strikes: it is moved 1.5 units from it.
            if(void* agent=Get<void*>(source,F("Actor","navMeshAgent"))){
                const auto t=Position(nearest);const double d=std::sqrt(distance);
                Vec3 beside{static_cast<float>(t.x-(t.x-start.x)/d*1.5),t.y,static_cast<float>(t.z-(t.z-start.z)/d*1.5)};void* wa[]{&beside};
                if(Value<bool>(Method(agent,"Warp",1),agent,wa)){start=Position(source);const double x=t.x-start.x,z=t.z-start.z;distance=x*x+z*z;}
            }
        }
        Vec3 p=Position(nearest),direction{p.x-start.x,p.y-start.y,p.z-start.z};float length=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);if(length>0){direction.x/=length;direction.y/=length;direction.z/=length;}
        void* constructor=Get<void*>(source,F("Actor","abilityObjectConstructor"));float delay=0;int castType=1;bool mutate=true;
        void* args[]{constructor,&p,&direction,&delay,&castType,&mutate,nullptr};Invoke(Method(ability.Get(),"CastAfterDelay",7),ability.Get(),args);
        return "{\"ok\":true,\"requested\":true,\"ability\":"+Json(Text(Get<void*>(ability.Get(),F("Ability","abilityName"))))+",\"abilities\":"+std::to_string(abilities.size())
              +",\"constructor\":"+Boolean(constructor!=nullptr)+",\"distance\":"+std::to_string(std::sqrt(distance))+"}";
    }
    // Ability.CastAfterDelay takes (constructor, cast point, target point, ...); the fixtures above pass a
    // direction as the target point. The three below pass real points (turrets such as the Ballista).
    const auto ownSummon=[&](size_t wanted)->void*{
        // The k-th own summon, counted as ally-attack-<k> counts them.
        void* tracker=Get<void*>(c.actor,F("Actor","summonTracker"));if(!tracker)throw std::runtime_error("Summon tracker unavailable.");size_t index=0;
        for(void* summoned:Entries(Get<void*>(tracker,F("SummonTracker","summons")))){
            if(!summoned||!game::IsAlive(summoned))continue;void* references=Get<void*>(summoned,F("Summoned","references"));
            if(references&&Get<void*>(references,F("CreationReferences","creator"))==c.actor&&index++==wanted)return Get<void*>(summoned,F("Summoned","actor"));
        }
        return nullptr;
    };
    const auto foesOf=[&](void* actor){
        std::vector<void*> foes;void* alignment=Get<void*>(Get<void*>(actor,F("Actor","alignmentManager")),F("AlignmentManager","alignment"));
        for(void* target:Find("LE.dll","","Actor")){
            if(!game::IsAlive(target)||target==actor||target==c.actor)continue;void* manager=Get<void*>(target,F("Actor","alignmentManager"));if(!manager)continue;
            void* other=Get<void*>(manager,F("AlignmentManager","alignment"));void* args[]{other};if(!other||!Value<bool>(Method(alignment,"IsFoe",1),alignment,args))continue;
            void* health=Get<void*>(target,F("Actor","health"));if(!health||Get<float>(health,F("BaseHealth","currentHealth"))<=0)continue;
            foes.push_back(target);
        }
        return foes;
    };
    const auto flat=[](const Vec3& a,const Vec3& b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.z-b.z)*(a.z-b.z));};
    const auto xyz=[](const Vec3& p){std::ostringstream o;o.setf(std::ios::fixed);o.precision(2);o<<'['<<p.x<<','<<p.y<<','<<p.z<<']';return o.str();};
    if(action=="summon-where"){
        // Disposable test character only (read-only): each own summon in ally-attack-<k> order, where its
        // Actor stands, how far from the character and from the nearest enemy, and the ability that made it.
        void* tracker=Get<void*>(c.actor,F("Actor","summonTracker"));if(!tracker)throw std::runtime_error("Summon tracker unavailable.");
        std::vector<Vec3> foes;for(void* foe:foesOf(c.actor))foes.push_back(Position(foe));
        const auto me=Position(c.actor);size_t index=0;bool comma=false;
        std::ostringstream out;out.setf(std::ios::fixed);out.precision(2);
        out<<"{\"ok\":true,\"me\":"<<xyz(me)<<",\"foes\":"<<foes.size()<<",\"summons\":[";
        for(void* summoned:Entries(Get<void*>(tracker,F("SummonTracker","summons")))){
            if(!summoned||!game::IsAlive(summoned))continue;void* references=Get<void*>(summoned,F("Summoned","references"));
            if(!references||Get<void*>(references,F("CreationReferences","creator"))!=c.actor)continue;
            const size_t k=index++;void* actor=Get<void*>(summoned,F("Summoned","actor"));if(!actor||!game::IsAlive(actor))continue;
            void* ability=Get<void*>(references,F("CreationReferences","thisAbility"));const auto p=Position(actor);
            double nearest=-1;for(const auto& f:foes){const double d=flat(f,p);if(nearest<0||d<nearest)nearest=d;}
            if(comma)out<<',';comma=true;
            out<<"{\"index\":"<<k<<",\"name\":"<<Json(AllyType(actor))<<",\"ability\":"<<Json(ability?Text(Get<void*>(ability,F("Ability","abilityName"))):std::string())
               <<",\"at\":"<<xyz(p)<<",\"fromMe\":"<<flat(p,me)<<",\"nearestFoe\":"<<nearest<<'}';
        }
        return out.str()+"]}";
    }
    if(action.rfind("summon-at-",0)==0){
        // Disposable test character only: summon-at-<ability id> casts that ability, summon-at-<skill ID>
        // the character's own ability with that save skill ID (its bar skill), from where the character
        // stands at the point 2 units beside it.
        const std::string wanted=action.substr(10);void* found=nullptr;
        if(!wanted.empty()&&std::all_of(wanted.begin(),wanted.end(),[](char ch){return ch>='0'&&ch<='9';})){
            uint16_t abilityId=static_cast<uint16_t>(std::stoi(wanted));void* a[]{&abilityId};found=Invoke(Signature("Ability","getAbility",{"System.UInt16"},"","LE.dll"),nullptr,a);
        }else{
            Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("LE.dll","","AbilityList"))));
            void* ca[]{type.Get()};void* list=Invoke(Signature("Component","GetComponent",{"System.Type"}),c.actor,ca);
            if(list)for(void* own:Entries(Get<void*>(list,F("AbilityList","abilities"))))if(own&&Text(Get<void*>(own,F("Ability","playerAbilityID")))==wanted){found=own;break;}
        }
        if(!found)throw std::runtime_error("Ability unavailable.");Root ability(found);
        void* constructor=Get<void*>(c.actor,F("Actor","abilityObjectConstructor"));Vec3 from=Position(c.actor),to=from;to.x+=2;float delay=0;int type=1;bool onDeath=false;
        // As a normal use: the character's target point is the target (Decoy is placed at it, without it at
        // the world origin), then the character's mutator for the ability moves the target point.
        std::string aimed="null";
        {
            Root uaType(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("LE.dll","","UsingAbility"))));
            void* ua[]{uaType.Get()};void* usingAbility=Invoke(Signature("Component","GetComponent",{"System.Type"}),c.actor,ua);
            if(usingAbility){
                const Vec3 before=Value<Vec3>(Method(usingAbility,"getTargetLocation",0),usingAbility);void* ta[]{&to};Invoke(Method(usingAbility,"UpdateTargetLocation",1),usingAbility,ta);
                aimed="{\"before\":"+xyz(before)+",\"after\":"+xyz(Value<Vec3>(Method(usingAbility,"getTargetLocation",0),usingAbility))+"}";
            }
        }
        std::string drop="null";void* manager=constructor?Get<void*>(constructor,F("AbilityObjectConstructor","mutatorManager")):nullptr;
        void* ma[]{ability.Get()};void* mutator=manager?Invoke(Method(manager,"getMutator",1),manager,ma):nullptr;
        if(mutator){
            void* ta[]{&from,&to,&type};Invoke(Method(mutator,"mutateTargetLocation",3),mutator,ta);
            if(il2cpp::api().object_get_class(mutator)==game::FindClass("LE.dll","","DecoyMutator"))drop=xyz(Get<Vec3>(mutator,F("DecoyMutator","decoyDropLocation")));
        }
        void* args[]{constructor,&from,&to,&delay,&type,&onDeath,nullptr};Invoke(Method(ability.Get(),"CastAfterDelay",7),ability.Get(),args);
        return "{\"ok\":true,\"requested\":true,\"ability\":"+Json(Text(Get<void*>(ability.Get(),F("Ability","abilityName"))))+",\"from\":"+xyz(from)+",\"to\":"+xyz(to)
              +",\"aimed\":"+aimed+",\"mutator\":"+Json(mutator?il2cpp::api().class_get_name(il2cpp::api().object_get_class(mutator)):"")+",\"decoyDrop\":"+drop+"}";
    }
    if(action.rfind("ally-shoot-",0)==0||action.rfind("ally-aim-",0)==0){
        // Disposable test character only: the k-th own summon uses its first ability at the nearest enemy
        // from where it stands, without moving. ally-shoot-<k>: CastAfterDelay(constructor, its position,
        // the enemy's position); ally-aim-<k>: castAtTargetFromTransformConstructorAfterDelay(constructor,
        // its transform, the enemy's position, standard offset).
        const bool aim=action.rfind("ally-aim-",0)==0;void* source=ownSummon(static_cast<size_t>(std::stoul(action.substr(aim?9:11))));
        if(!source||!game::IsAlive(source))throw std::runtime_error("Cast a test ally first.");Root pin(source);
        Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("LE.dll","","AbilityList"))));
        void* ca[]{type.Get()};Root list(Invoke(Signature("Component","GetComponent",{"System.Type"}),source,ca));
        if(!list.Get())throw std::runtime_error("This ally has no ability list.");
        const auto abilities=Entries(Get<void*>(list.Get(),F("AbilityList","abilities")));if(abilities.empty()||!abilities.front())throw std::runtime_error("This ally has no loaded attack.");
        Root ability(abilities.front());const Vec3 from=Position(source);void* nearest=nullptr;double distance=1.e30;
        for(void* foe:foesOf(source)){const double d=flat(Position(foe),from);if(d<distance){distance=d;nearest=foe;}}
        if(!nearest)throw std::runtime_error("No test enemy available.");
        Vec3 to=Position(nearest);void* constructor=Get<void*>(source,F("Actor","abilityObjectConstructor"));float delay=0;int useType=1;
        if(aim){
            Root transform(Invoke(componentTransform,source));bool standard=true;
            void* args[]{constructor,transform.Get(),&to,&delay,&useType,&standard};Invoke(Method(ability.Get(),"castAtTargetFromTransformConstructorAfterDelay",6),ability.Get(),args);
        }else{
            Vec3 at=from;bool onDeath=false;void* args[]{constructor,&at,&to,&delay,&useType,&onDeath,nullptr};Invoke(Method(ability.Get(),"CastAfterDelay",7),ability.Get(),args);
        }
        return "{\"ok\":true,\"requested\":true,\"ally\":"+Json(AllyType(source))+",\"ability\":"+Json(Text(Get<void*>(ability.Get(),F("Ability","abilityName"))))+",\"from\":"+xyz(from)
              +",\"to\":"+xyz(to)+",\"distance\":"+std::to_string(distance)+",\"constructor\":"+Boolean(constructor!=nullptr)+"}";
    }
    if(action=="ground-items"){
        // Disposable test character only: the items on the ground with rarity 7 or more (Unique, Set,
        // Legendary) and their Legendary Potential and Weaver's Will; other items are only counted.
        void* labels=game::StaticObject(game::FindStaticField("LE.dll","","ItemTooltipOrganizer","pickableGroundLabelList"));
        const auto* labelClass=game::FindClass("LE.dll","","GroundItemLabel");
        std::ostringstream out;out<<"{\"ok\":true,\"items\":[";bool comma=false;int others=0;
        if(labels&&labelClass){
            const auto* list=il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(labels),"_list");
            if(!list)throw std::runtime_error("Ground label list layout unavailable.");
            for(void* label:Entries(Get<void*>(labels,il2cpp::api().field_get_offset(list)))){
                if(!label||il2cpp::api().object_get_class(label)!=labelClass||!game::IsAlive(label))continue;
                void* item=Invoke(Method(label,"getItemData",0),label);if(!item)continue;
                const int rarity=Get<uint8_t>(item,F("ItemData","rarity"));
                if(rarity<7){++others;continue;}
                if(comma)out<<',';comma=true;
                const auto at=Position(label);
                out<<"{\"rarity\":"<<rarity<<",\"unique\":"<<Get<uint16_t>(item,F("ItemData","uniqueID"))<<",\"x\":"<<static_cast<int>(at.x*10)<<",\"z\":"<<static_cast<int>(at.z*10)
                   <<",\"lp\":"<<static_cast<int>(Get<uint8_t>(item,F("ItemData","legendaryPotential")))
                   <<",\"ww\":"<<static_cast<int>(Get<uint8_t>(item,F("ItemData","weaversWill")))<<'}';
            }
        }
        out<<"],\"others\":"<<others<<'}';return out.str();
    }
    if(action=="dummy-make"||action.rfind("dummy-make-",0)==0||action=="dummy-mark"||action=="dummy-read"){
        // Disposable test character only. dummy-make[-<health>]: the nearest living regular enemy
        // within 30 game units (a boss only when there is no other) gets that much health (default a
        // billion; 2^28 keeps a float health exact to 8 per hit), no regeneration and no ward, so
        // every hit on it is measurable as health it loses; dummy-mark: counters from now;
        // dummy-read: its state and the counters.
        if(action!="dummy-mark"&&action!="dummy-read"){
            const double wanted=action=="dummy-make"?1.0e9:std::stod(action.substr(11));
            if(!(wanted>=1000&&wanted<=2.0e9))throw std::runtime_error("use dummy-make-<1000..2000000000>");
            void* alignment=Get<void*>(Get<void*>(c.actor,F("Actor","alignmentManager")),F("AlignmentManager","alignment"));
            const auto start=Position(c.actor);void* nearest=nullptr;void* boss=nullptr;double best=900,bestBoss=900;
            for(void* target:Find("LE.dll","","Actor")){
                if(!game::IsAlive(target)||target==c.actor)continue;void* manager=Get<void*>(target,F("Actor","alignmentManager"));if(!manager)continue;
                void* other=Get<void*>(manager,F("AlignmentManager","alignment"));void* args[]{other};if(!other||!Value<bool>(Method(alignment,"IsFoe",1),alignment,args))continue;
                void* health=Get<void*>(target,F("Actor","health"));if(!health||Get<float>(health,F("BaseHealth","currentHealth"))<=0)continue;
                const bool special=Value<float>(Method(health,"get_EffectiveHealthMultiplier",0),health)>1.01f||AllyType(target).find("Boss")!=std::string::npos;
                const auto p=Position(target);const double x=p.x-start.x,z=p.z-start.z,d=x*x+z*z;
                if(special){if(d<bestBoss){bestBoss=d;boss=target;}}else if(d<best){best=d;nearest=target;}
            }
            if(!nearest)nearest=boss;
            if(!nearest)throw std::runtime_error("No test enemy within 30 game units.");
            void* health=Get<void*>(nearest,F("Actor","health"));
            auto at=[&](const char* name){return static_cast<char*>(health)+F("BaseHealth",name);};
            *reinterpret_cast<int*>(at("maxHealth"))=static_cast<int>(wanted);*reinterpret_cast<float*>(at("currentHealth"))=static_cast<float>(wanted);
            *reinterpret_cast<float*>(at("baseHealthRegenPerSecond"))=0;*reinterpret_cast<float*>(at("addedHealthRegenPerSecond"))=0;
            *reinterpret_cast<bool*>(at("useHealthCap"))=false;*reinterpret_cast<bool*>(at("damageable"))=true;
            void* protection=Get<void*>(nearest,F("Actor","protection"));
            if(protection)*reinterpret_cast<float*>(static_cast<char*>(protection)+F("ProtectionClass","ward"))=0;
            testDummy=std::make_unique<Root>(nearest);testDummyProtection=protection;
        }
        if(action!="dummy-read"){testDummyRecorded=testDummyAll=0;testDummyHits=0;}
        void* dummy=testDummy?testDummy->Get():nullptr;
        if(!dummy||!game::IsAlive(dummy))return "{\"ok\":true,\"alive\":false,\"recorded\":"+std::to_string(testDummyRecorded)+",\"all\":"+std::to_string(testDummyAll)+",\"hits\":"+std::to_string(testDummyHits)+"}";
        void* health=Get<void*>(dummy,F("Actor","health"));void* protection=Get<void*>(dummy,F("Actor","protection"));
        const auto me=Position(c.actor),it=Position(dummy);
        std::ostringstream out;out.setf(std::ios::fixed);out.precision(3);
        out<<"{\"ok\":true,\"alive\":true,\"name\":"<<Json(AllyType(dummy))<<",\"health\":"<<static_cast<double>(Get<float>(health,F("BaseHealth","currentHealth")))
           <<",\"maxHealth\":"<<Get<int>(health,F("BaseHealth","maxHealth"))<<",\"regen\":"<<Get<float>(health,F("BaseHealth","baseHealthRegenPerSecond"))+Get<float>(health,F("BaseHealth","addedHealthRegenPerSecond"))
           <<",\"multiplier\":"<<Value<float>(Method(health,"get_EffectiveHealthMultiplier",0),health)<<",\"ward\":"<<(protection?Get<float>(protection,F("ProtectionClass","ward")):0.f)
           <<",\"distance\":"<<std::sqrt((it.x-me.x)*(it.x-me.x)+(it.z-me.z)*(it.z-me.z))<<",\"recorded\":"<<testDummyRecorded<<",\"all\":"<<testDummyAll<<",\"hits\":"<<testDummyHits<<'}';
        return out.str();
    }
    if(action.rfind("dummy-area-",0)==0||action=="dummy-area-read"){
        // Disposable test character only. dummy-area-<health>: every living enemy within 30 game units
        // becomes a measuring target (health set to <health> before each hit on it, so it never dies
        // and each hit's health loss is exact); dummy-area-read: per damage source type the damage
        // the session recorded on them, the health those hits removed and the hits; counters restart.
        if(action!="dummy-area-read"){
            const double wanted=std::stod(action.substr(11));if(!(wanted>=1000&&wanted<=2.0e9))throw std::runtime_error("use dummy-area-<1000..2000000000>");
            testTargetRoots.clear();testTargets.clear();testTargetAfter.clear();testTargetHealth=static_cast<float>(wanted);
            void* alignment=Get<void*>(Get<void*>(c.actor,F("Actor","alignmentManager")),F("AlignmentManager","alignment"));
            const auto start=Position(c.actor);
            for(void* target:Find("LE.dll","","Actor")){
                if(!game::IsAlive(target)||target==c.actor)continue;void* manager=Get<void*>(target,F("Actor","alignmentManager"));if(!manager)continue;
                void* other=Get<void*>(manager,F("AlignmentManager","alignment"));void* args[]{other};if(!other||!Value<bool>(Method(alignment,"IsFoe",1),alignment,args))continue;
                void* health=Get<void*>(target,F("Actor","health"));if(!health||Get<float>(health,F("BaseHealth","currentHealth"))<=0)continue;
                const auto p=Position(target);if((p.x-start.x)*(p.x-start.x)+(p.z-start.z)*(p.z-start.z)>900)continue;
                void* protection=Get<void*>(target,F("Actor","protection"));if(!protection)continue;
                *reinterpret_cast<int*>(static_cast<char*>(health)+F("BaseHealth","maxHealth"))=static_cast<int>(wanted);
                *reinterpret_cast<float*>(static_cast<char*>(health)+F("BaseHealth","currentHealth"))=testTargetHealth;
                *reinterpret_cast<float*>(static_cast<char*>(protection)+F("ProtectionClass","ward"))=0;
                *reinterpret_cast<float*>(static_cast<char*>(health)+F("BaseHealth","baseHealthRegenPerSecond"))=0;*reinterpret_cast<float*>(static_cast<char*>(health)+F("BaseHealth","addedHealthRegenPerSecond"))=0;
                testTargetRoots.push_back(std::make_unique<Root>(target));testTargets.insert(protection);testTargetAfter[protection]=testTargetHealth;
            }
        }
        if(action=="dummy-area-read")for(const auto& root:testTargetRoots){
            void* actor=root->Get();if(!actor||!game::IsAlive(actor))continue;
            void* protection=Get<void*>(actor,F("Actor","protection"));void* health=Get<void*>(actor,F("Actor","health"));
            auto last=testTargetAfter.find(protection);if(!health||last==testTargetAfter.end())continue;
            const float now=Get<float>(health,F("BaseHealth","currentHealth"));
            if(Get<int>(health,F("BaseHealth","maxHealth"))!=static_cast<int>(testTargetHealth))++testClamps;
            else if(now<last->second-0.5f){testOutsideLoss+=last->second-now;++testOutsideEvents;last->second=now;}
        }
        std::ostringstream out;out.setf(std::ios::fixed);out.precision(3);
        out<<"{\"ok\":true,\"outsideLoss\":"<<testOutsideLoss<<",\"outsideEvents\":"<<testOutsideEvents<<",\"clamps\":"<<testClamps<<",\"sources\":{";
        {bool first=true;for(const auto& [kind,v]:testSourceKinds){if(!first)out<<',';first=false;out<<Json(kind)<<":{\"damage\":"<<v[0]<<",\"hits\":"<<static_cast<uint64_t>(v[1])<<'}';}}
        out<<"},\"otherKinds\":{";
        {bool first=true;for(const auto& [kind,v]:testOtherKinds){if(!first)out<<',';first=false;out<<Json(kind)<<":{\"damage\":"<<v[0]<<",\"hits\":"<<static_cast<uint64_t>(v[1])<<'}';}}
        out<<"},\"targets\":"<<testTargets.size()<<",\"otherHits\":"<<testTargetOther<<",\"matched\":"<<testTargetMatched<<",\"failed\":"<<testTargetFailed
           <<",\"calls\":"<<testCallsWhileTargets<<",\"error\":"<<Json(testTargetError)<<",\"types\":[";bool comma=false;
        for(const auto& [index,v]:testTargetByType){if(comma)out<<',';comma=true;
            out<<"{\"name\":"<<Json(index<damageTypes.size()?damageTypes[index].name:std::string("?"))<<",\"recorded\":"<<v[0]<<",\"lost\":"<<v[1]<<",\"hits\":"<<static_cast<uint64_t>(v[2])<<'}';}
        if(action=="dummy-area-read"){testOutsideLoss=0;testOutsideEvents=testClamps=0;testSourceKinds.clear();testOtherKinds.clear();testTargetByType.clear();testTargetOther=testTargetMatched=testTargetFailed=testCallsWhileTargets=0;testTargetError.clear();}  // counters restart only when read
        return out.str()+"]}";
    }
    if(action.rfind("kill-others-",0)==0){
        // Disposable test character only: like kill-enemies-<n>, but the target dummy is left alone.
        const int limit=std::stoi(action.substr(12));if(limit<1||limit>50)throw std::runtime_error("use kill-others-<1..50>");
        void* dummy=testDummy?testDummy->Get():nullptr;
        void* alignment=Get<void*>(Get<void*>(c.actor,F("Actor","alignmentManager")),F("AlignmentManager","alignment"));
        const auto start=Position(c.actor);std::vector<std::pair<double,void*>> foes;
        for(void* target:Find("LE.dll","","Actor")){
            if(!game::IsAlive(target)||target==c.actor||target==dummy)continue;void* manager=Get<void*>(target,F("Actor","alignmentManager"));if(!manager)continue;
            void* other=Get<void*>(manager,F("AlignmentManager","alignment"));void* args[]{other};if(!other||!Value<bool>(Method(alignment,"IsFoe",1),alignment,args))continue;
            void* health=Get<void*>(target,F("Actor","health"));if(!health||Get<float>(health,F("BaseHealth","currentHealth"))<=0)continue;
            const auto p=Position(target);const double x=p.x-start.x,z=p.z-start.z,d=x*x+z*z;if(d<=2500)foes.emplace_back(d,target);
        }
        std::sort(foes.begin(),foes.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        int killed=0;
        for(const auto& [d,target]:foes){
            if(killed>=limit)break;if(!game::IsAlive(target))continue;Root pin(target);
            void* health=Get<void*>(target,F("Actor","health"));if(!health)continue;Root healthPin(health);
            bool& damageable=*reinterpret_cast<bool*>(static_cast<char*>(health)+F("BaseHealth","damageable"));damageable=true;
            float hit=Get<float>(health,F("BaseHealth","currentHealth"))+1000.f;void* a[]{&hit};Value<float>(Method(health,"HealthDamage",1),health,a);
            if(!game::IsAlive(health)||Get<float>(health,F("BaseHealth","currentHealth"))<=0)++killed;
        }
        return "{\"ok\":true,\"killed\":"+std::to_string(killed)+",\"near\":"+std::to_string(foes.size())+"}";
    }
    if(action.rfind("kill-enemies-",0)==0){
        // Disposable test character only: the nearest n living enemies within 30 game units take
        // lethal health damage through their own BaseHealth.HealthDamage, so the game's normal death
        // and loot drops follow.
        const int limit=std::stoi(action.substr(13));if(limit<1||limit>50)throw std::runtime_error("use kill-enemies-<1..50>");
        void* alignment=Get<void*>(Get<void*>(c.actor,F("Actor","alignmentManager")),F("AlignmentManager","alignment"));
        const auto start=Position(c.actor);std::vector<std::pair<double,void*>> foes;
        for(void* target:Find("LE.dll","","Actor")){
            if(!game::IsAlive(target)||target==c.actor)continue;void* manager=Get<void*>(target,F("Actor","alignmentManager"));if(!manager)continue;
            void* other=Get<void*>(manager,F("AlignmentManager","alignment"));void* args[]{other};if(!other||!Value<bool>(Method(alignment,"IsFoe",1),alignment,args))continue;
            void* health=Get<void*>(target,F("Actor","health"));if(!health||Get<float>(health,F("BaseHealth","currentHealth"))<=0)continue;
            const auto p=Position(target);const double x=p.x-start.x,z=p.z-start.z,d=x*x+z*z;if(d<=900)foes.emplace_back(d,target);
        }
        std::sort(foes.begin(),foes.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        int killed=0;
        for(const auto& [d,target]:foes){
            if(killed>=limit)break;if(!game::IsAlive(target))continue;Root pin(target);
            void* health=Get<void*>(target,F("Actor","health"));if(!health)continue;Root healthPin(health);
            bool& damageable=*reinterpret_cast<bool*>(static_cast<char*>(health)+F("BaseHealth","damageable"));damageable=true;
            float hit=Get<float>(health,F("BaseHealth","currentHealth"))+1000.f;void* a[]{&hit};Value<float>(Method(health,"HealthDamage",1),health,a);
            if(!game::IsAlive(health)||Get<float>(health,F("BaseHealth","currentHealth"))<=0)++killed;
        }
        return "{\"ok\":true,\"killed\":"+std::to_string(killed)+",\"near\":"+std::to_string(foes.size())+"}";
    }
    if(action.rfind("weaver-",0)==0){
        // Disposable test character only: Weaver's Will on the equipped items, through the game's own calls.
        void* containers=Get<void*>(Get<void*>(Get<void*>(c.actor,F("Actor","itemContainersManager")),F("ItemContainersManager","equipment")),F("ItemContainersManager.PaperDollContainer","containers"));
        std::vector<void*> items;
        for(void* slot:Entries(containers)){
            if(!slot||!Value<bool>(Method(slot,"HasContent",0),slot))continue;
            void* content=Invoke(Method(slot,"GetContent",0),slot);if(!content)continue;
            const auto entries=Entries(content);if(entries.empty()||!entries.front())continue;
            if(void* item=Get<void*>(entries.front(),F("ItemContainerEntry","data")))items.push_back(item);
        }
        const auto describe=[&](void* item){
            std::ostringstream o;o<<"{\"rarity\":"<<static_cast<int>(Get<uint8_t>(item,F("ItemData","rarity")))<<",\"unique\":"<<Get<uint16_t>(item,F("ItemData","uniqueID"))
                <<",\"lp\":"<<static_cast<int>(Get<uint8_t>(item,F("ItemData","legendaryPotential")))<<",\"ww\":"<<static_cast<int>(Get<uint8_t>(item,F("ItemData","weaversWill")))<<",\"affixes\":[";
            bool first=true;if(void* list=Get<void*>(item,F("ItemData","affixes")))for(void* a:Entries(list))if(a){
                if(!first)o<<',';first=false;o<<"{\"id\":"<<Get<uint16_t>(a,F("ItemAffix","affixId"))<<",\"tier\":"<<static_cast<int>(Get<uint8_t>(a,F("ItemAffix","affixTier")))<<'}';}
            o<<"]}";return o.str();
        };
        const auto weaverItems=[&]{std::vector<void*> w;for(void* i:items)if(Get<uint8_t>(i,F("ItemData","weaversWill"))>0)w.push_back(i);return w;};
        if(action.rfind("weaver-equip-",0)==0){
            // The first equipped Unique (else Legendary) gets Weaver's Will n instead of Legendary Potential.
            const int ww=std::stoi(action.substr(13));if(ww<1||ww>28)throw std::runtime_error("use weaver-equip-<1..28>");
            void* chosen=nullptr;
            for(int wanted:{7,9}){for(void* i:items)if(Get<uint8_t>(i,F("ItemData","rarity"))==wanted){chosen=i;break;}if(chosen)break;}
            if(!chosen)throw std::runtime_error("No equipped Unique or Legendary item.");
            *reinterpret_cast<uint8_t*>(static_cast<char*>(chosen)+F("ItemData","legendaryPotential"))=0;
            *reinterpret_cast<uint8_t*>(static_cast<char*>(chosen)+F("ItemData","weaversWill"))=static_cast<uint8_t>(ww);
            Invoke(Method(chosen,"RebuildID",0),chosen);Invoke(Method(chosen,"setValuesFromSerialisation",0),chosen);
            return "{\"ok\":true,\"equipped\":"+std::to_string(items.size())+",\"item\":"+describe(chosen)+"}";
        }
        if(action.rfind("weaver-wear-",0)==0){
            // Replace an equipped item whose base type has Weaver's Will Uniques with a Weaver's Will Unique of
            // that base type from the game's generator, in the same slot (Clear, then TryAddItemAndSkipContentChecks);
            // its Weaver's Will is then set to n.
            const int ww=std::stoi(action.substr(12));if(ww<1||ww>28)throw std::runtime_error("use weaver-wear-<1..28>");
            void* generator=Get<void*>(c.actor,F("Actor","generateItems"));if(!generator)throw std::runtime_error("Item generator unavailable.");
            std::vector<void*> slots;
            for(void* slot:Entries(containers))if(slot&&Value<bool>(Method(slot,"HasContent",0),slot))slots.push_back(slot);
            for(int base:{2,3,4,20,21,22,14,15,16,18,7,9}){
                for(void* slot:slots){
                    void* content=Invoke(Method(slot,"GetContent",0),slot);if(!content)continue;const auto entries=Entries(content);if(entries.empty()||!entries.front())continue;
                    void* worn=Get<void*>(entries.front(),F("ItemContainerEntry","data"));if(!worn||Get<uint8_t>(worn,F("ItemData","itemType"))!=base)continue;
                    bool no=false,requireType=true;int level=80,location=0,type=base,subtype=-1,rarity=7,sockets=0,unique=-1,lp=0,rank=0,legendaryType=1,corrupt=0;
                    alignas(8) unsigned char faction[8]{},replacement[8]{},corruption[8]{},effectiveness[8]{},special[8]{},minSpecial[8]{},overrideLP[8]{};
                    void* gen[]{&no,&level,&no,&location,&type,&subtype,&rarity,&sockets,&unique,&no,&lp,faction,&rank,&requireType,&legendaryType,&no,
                        replacement,corruption,&corrupt,effectiveness,nullptr,nullptr,special,&no,minSpecial,overrideLP};
                    Root item(Invoke(M("GenerateItems","initialiseRandomItemData",26),generator,gen));
                    if(!item.Get()||Get<uint8_t>(item.Get(),F("ItemData","weaversWill"))==0)continue;
                    *reinterpret_cast<uint8_t*>(static_cast<char*>(item.Get())+F("ItemData","weaversWill"))=static_cast<uint8_t>(ww);
                    Invoke(Method(item.Get(),"RebuildID",0),item.Get());Invoke(Method(item.Get(),"setValuesFromSerialisation",0),item.Get());
                    Invoke(Method(slot,"Clear",0),slot);
                    int amount=1,context=0;void* args[]{item.Get(),&amount,&context};
                    const bool added=Value<bool>(Method(slot,"TryAddItemAndSkipContentChecks",3),slot,args);
                    return "{\"ok\":"+std::string(Boolean(added))+",\"baseType\":"+std::to_string(base)+",\"item\":"+describe(item.Get())+"}";
                }
            }
            throw std::runtime_error("No equipped item of a base type with Weaver's Will Uniques.");
        }
        if(action=="weaver-items"){
            // Each Weaver's Will item, and whether its fields match its serialised data (rebuilt from it).
            std::ostringstream o;o<<"{\"ok\":true,\"equipped\":"<<items.size()<<",\"items\":[";bool first=true;
            for(void* i:weaverItems()){
                const std::string before=describe(i);Invoke(Method(i,"setValuesFromSerialisation",0),i);const std::string after=describe(i);
                if(!first)o<<',';first=false;o<<"{\"item\":"<<after<<",\"consistent\":"<<Boolean(before==after)<<'}';
            }
            o<<"]}";return o.str();
        }
        if(action=="weaver-eligible"){
            const auto w=weaverItems();if(w.empty())throw std::runtime_error("No Weaver's Will item equipped.");
            int prefixes=0,suffixes=0;void* a[]{&prefixes,&suffixes};Root already(Invoke(Method(w.front(),"AffixIDsAlreadyOnItem",2),w.front(),a));
            int special=0;void* b[]{already.Get(),&special};Root tuple(Invoke(Method(w.front(),"GetEligibleAffixes",2),w.front(),b));
            void** lists=static_cast<void**>(il2cpp::api().object_unbox(tuple.Get()));
            std::ostringstream o;o<<"{\"ok\":true,\"ids\":[";bool first=true;
            for(int k=0;k<2;++k)if(lists[k])for(void* e:Entries(lists[k]))if(e){if(!first)o<<',';first=false;o<<Get<int>(e,F("AffixList.Affix","affixId"));}
            o<<"]}";return o.str();
        }
        if(action.rfind("weaver-upgrade-",0)==0){
            // CharacterMutator.UpgradeRandomWeaversWillItem, the call the game makes after a winning kill roll.
            const int n=std::stoi(action.substr(15));if(n<1||n>50)throw std::runtime_error("use weaver-upgrade-<1..50>");
            void* mutator=Get<void*>(c.actor,F("Actor","characterMutator"));if(!mutator)throw std::runtime_error("Character mutator unavailable.");
            int before=0;for(void* i:weaverItems())before+=Get<uint8_t>(i,F("ItemData","weaversWill"));
            for(int k=0;k<n;++k)Invoke(Method(mutator,"UpgradeRandomWeaversWillItem",0),mutator);
            int after=0;for(void* i:items)after+=Get<uint8_t>(i,F("ItemData","weaversWill"));
            return "{\"ok\":true,\"calls\":"+std::to_string(n)+",\"wwBefore\":"+std::to_string(before)+",\"wwAfter\":"+std::to_string(after)+"}";
        }
        throw std::runtime_error("unknown weaver fixture");
    }
    if(action.rfind("cocoon-reveal-",0)==0){
        // Disposable test character only: a generated cocooned Unique (the i-th cocooned entry of the
        // unique list), opened through the game's own ItemData.RevealCocoonedUnique; returns what it became.
        const int index=std::stoi(action.substr(14));if(index<0)throw std::runtime_error("use cocoon-reveal-<index>");
        Root uniques(Invoke(M("UniqueList","get",0),nullptr));if(!uniques.Get())throw std::runtime_error("Unique list unavailable.");
        std::vector<int> cocoons;
        for(void* e:Entries(Get<void*>(uniques.Get(),F("UniqueList","uniques"))))
            if(e&&Get<bool>(e,F("UniqueList.Entry","isCocoonedItem")))cocoons.push_back(Get<uint16_t>(e,F("UniqueList.Entry","uniqueID")));
        if(cocoons.empty())throw std::runtime_error("No cocooned Unique in this game build.");
        void* generator=Get<void*>(c.actor,F("Actor","generateItems"));if(!generator)throw std::runtime_error("Item generator unavailable.");
        bool no=false;int level=50,location=0,type=-1,subtype=-1,rarity=7,sockets=0,unique=cocoons[static_cast<size_t>(index)%cocoons.size()],lp=0,rank=0,legendaryType=0,corrupt=0;
        alignas(8) unsigned char faction[8]{},replacement[8]{},corruption[8]{},effectiveness[8]{},special[8]{},minSpecial[8]{},overrideLP[8]{};
        void* gen[]{&no,&level,&no,&location,&type,&subtype,&rarity,&sockets,&unique,&no,&lp,faction,&rank,&no,&legendaryType,&no,
            replacement,corruption,&corrupt,effectiveness,nullptr,nullptr,special,&no,minSpecial,overrideLP};
        Root item(Invoke(M("GenerateItems","initialiseRandomItemData",26),generator,gen));
        if(!item.Get())throw std::runtime_error("The game did not generate the cocoon.");
        const int before=Get<uint16_t>(item.Get(),F("ItemData","uniqueID"));
        const bool cocooned=Value<bool>(Method(item.Get(),"isCocooned",0),item.Get());
        void* a[]{c.actor};const bool revealed=Value<bool>(Method(item.Get(),"RevealCocoonedUnique",1),item.Get(),a);
        return "{\"ok\":true,\"cocoons\":"+std::to_string(cocoons.size())+",\"cocoon\":"+std::to_string(before)+",\"cocooned\":"+Boolean(cocooned)+
               ",\"revealed\":"+Boolean(revealed)+",\"unique\":"+std::to_string(Get<uint16_t>(item.Get(),F("ItemData","uniqueID")))+
               ",\"lp\":"+std::to_string(Get<uint8_t>(item.Get(),F("ItemData","legendaryPotential")))+
               ",\"weaversWill\":"+std::to_string(Get<uint8_t>(item.Get(),F("ItemData","weaversWill")))+"}";
    }
    if(action=="unique-types"){
        // Disposable test character only: the unique list by legendary type and drop flags, and every
        // Weaver's Will Unique with its flags and name (read-only).
        Root uniques(Invoke(M("UniqueList","get",0),nullptr));if(!uniques.Get())throw std::runtime_error("Unique list unavailable.");
        int counts[2][2][2]{};std::ostringstream weaver;bool comma=false;
        for(void* e:Entries(Get<void*>(uniques.Get(),F("UniqueList","uniques")))){
            if(!e)continue;
            const int type=Get<int>(e,F("UniqueList.Entry","legendaryType"))==1?1:0;
            const bool random=Get<bool>(e,F("UniqueList.Entry","canDropRandomly")),cocooned=Get<bool>(e,F("UniqueList.Entry","isCocoonedItem"));
            ++counts[type][random?1:0][cocooned?1:0];
            if(type!=1)continue;
            const int id=Get<uint16_t>(e,F("UniqueList.Entry","uniqueID"));bool alt=false;
            uint16_t uid=static_cast<uint16_t>(id);void* args[]{&uid,&alt};
            if(comma)weaver<<',';comma=true;
            weaver<<"{\"id\":"<<id<<",\"name\":"<<Json(Text(Invoke(M("UniqueList","getUniqueName",2),nullptr,args)))<<",\"random\":"<<Boolean(random)
                  <<",\"set\":"<<Boolean(Get<bool>(e,F("UniqueList.Entry","isSetItem")))<<",\"baseType\":"<<static_cast<int>(Get<uint8_t>(e,F("UniqueList.Entry","baseType")))<<'}';
        }
        std::ostringstream out;out<<"{\"ok\":true,\"counts\":{";
        const char* names[2]{"legendaryPotential","weaversWill"};
        for(int t=0;t<2;++t){if(t)out<<',';out<<Json(names[t])<<":{\"random\":"<<counts[t][1][0]+counts[t][1][1]<<",\"notRandom\":"<<counts[t][0][0]+counts[t][0][1]<<",\"cocooned\":"<<counts[t][0][1]+counts[t][1][1]<<'}';}
        out<<"},\"weaver\":["<<weaver.str()<<"]}";return out.str();
    }
    if(action=="class-abilities"){
        // Disposable test character only (read-only): every class's level and mastery abilities with the
        // playerAbilityID the save uses for its skill tree and ability bar.
        Root list(Invoke(M("CharacterClassList","get",0),nullptr));if(!list.Get())throw std::runtime_error("Class list unavailable.");
        const auto ability=[&](void* a){return "{\"name\":"+Json(Text(Get<void*>(a,F("Ability","abilityName"))))+",\"id\":"+Json(Text(Get<void*>(a,F("Ability","playerAbilityID"))))+'}';};
        std::ostringstream out;out<<"{\"ok\":true,\"classes\":[";bool first=true;
        for(void* cls:Entries(Get<void*>(list.Get(),F("CharacterClassList","classes")))){
            if(!cls)continue;if(!first)out<<',';first=false;
            out<<"{\"name\":"<<Json(Text(Get<void*>(cls,F("CharacterClass","className"))))<<",\"id\":"<<Get<int>(cls,F("CharacterClass","classID"))<<",\"base\":[";
            bool c1=true;for(void* al:Entries(Get<void*>(cls,F("CharacterClass","unlockableAbilities"))))if(al&&Get<void*>(al,F("AbilityAndLevel","ability"))){if(!c1)out<<',';c1=false;out<<ability(Get<void*>(al,F("AbilityAndLevel","ability")));}
            out<<"],\"masteries\":[";
            void* masteries=Get<void*>(cls,F("CharacterClass","masteries"));const auto n=masteries?Get<uintptr_t>(masteries,0x18):0;
            for(uintptr_t i=0;i<n&&i<8;++i){void* m=Get<void*>(masteries,0x20+sizeof(void*)*i);if(i)out<<',';
                void* own=m?Get<void*>(m,F("Mastery","masteryAbility")):nullptr;  // the ability choosing the mastery grants (Falconry)
                out<<"{\"name\":"<<Json(m?Text(Get<void*>(m,F("Mastery","name"))):"")<<",\"masteryAbility\":"<<(own?ability(own):std::string("null"))<<",\"abilities\":[";bool c2=true;
                if(m)for(void* al:Entries(Get<void*>(m,F("Mastery","abilities"))))if(al&&Get<void*>(al,F("AbilityAndLevel","ability"))){if(!c2)out<<',';c2=false;out<<ability(Get<void*>(al,F("AbilityAndLevel","ability")));}
                out<<"]}";}
            out<<"]}";
        }
        out<<"]}";return out.str();
    }
    if(action=="skill-state"){
        // Disposable test character only (read-only): the specialised skill trees the game applied and the abilities it knows.
        void* tree=Get<void*>(c.actor,F("Actor","localTreeData"));if(!tree)throw std::runtime_error("Tree data unavailable.");
        std::ostringstream out;out<<"{\"ok\":true,\"specialised\":[";bool first=true;
        for(void* t:Entries(Get<void*>(tree,F("LocalTreeData","specialisedSkillTrees")))){
            if(!t)continue;if(!first)out<<',';first=false;void* a=Get<void*>(t,F("LocalTreeData.SkillTreeData","ability"));
            out<<"{\"slot\":"<<static_cast<int>(Get<uint8_t>(t,F("LocalTreeData.SkillTreeData","slot")))<<",\"id\":"<<Json(a?Text(Get<void*>(a,F("Ability","playerAbilityID"))):"")
               <<",\"level\":"<<static_cast<int>(Get<uint8_t>(t,F("LocalTreeData.SkillTreeData","level")))<<",\"mutator\":"<<Boolean(Get<bool>(t,F("LocalTreeData.SkillTreeData","mutatorSet")))<<'}';
        }
        out<<"],\"equipped\":[";
        Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("LE.dll","","AbilityList"))));
        void* ca[]{type.Get()};Root abilities(Invoke(Signature("Component","GetComponent",{"System.Type"}),c.actor,ca));first=true;
        if(abilities.Get())for(void* a:Entries(Get<void*>(abilities.Get(),F("AbilityList","abilities")))){if(!first)out<<',';first=false;out<<Json(a?Text(Get<void*>(a,F("Ability","playerAbilityID"))):"");}
        out<<"]}";return out.str();
    }
    if(action=="area-level"){
        void* manager=game::StaticObject(game::FindStaticField("LE.dll","","DifficultyManager","instance"));
        if(!manager)throw std::runtime_error("Difficulty manager unavailable.");
        return "{\"ok\":true,\"areaLevel\":"+std::to_string(Get<int>(manager,F("DifficultyManager","areaLevel")))+"}";
    }
    if(action.rfind("spawner-",0)==0){
        // Disposable test character only: warp next to the i-th enemy spawner of the scene (in scene
        // order, wrapping), so a hunt visits every spawner once instead of returning to an emptied one.
        const int index=std::stoi(action.substr(8));if(index<0)throw std::runtime_error("use spawner-<index>");
        std::vector<void*> spawners;for(void* spawner:Find("LE.dll","","Spawner"))if(game::IsAlive(spawner))spawners.push_back(spawner);
        if(spawners.empty())throw std::runtime_error("No enemy spawner in this scene.");
        auto p=Position(spawners[static_cast<size_t>(index)%spawners.size()]);p.x-=3;void* agent=Get<void*>(c.actor,F("Actor","navMeshAgent"));void* args[]{&p};
        const bool warped=Value<bool>(Method(agent,"Warp",1),agent,args);
        return "{\"ok\":true,\"spawners\":"+std::to_string(spawners.size())+",\"warped\":"+Boolean(warped)+"}";
    }
    if(action=="near-spawner"){
        // Enemies spawn when the player comes near: warp next to the nearest enemy spawner.
        auto start=Position(c.actor);void* nearest=nullptr;double distance=1.e30;
        for(void* spawner:Find("LE.dll","","Spawner")){if(!game::IsAlive(spawner))continue;auto p=Position(spawner);double x=p.x-start.x,z=p.z-start.z,d=x*x+z*z;if(d>25&&d<distance){distance=d;nearest=spawner;}}
        if(!nearest)throw std::runtime_error("No enemy spawner in this scene.");
        auto p=Position(nearest);p.x-=3;void* agent=Get<void*>(c.actor,F("Actor","navMeshAgent"));void* args[]{&p};
        if(!Value<bool>(Method(agent,"Warp",1),agent,args))throw std::runtime_error("Native NavMesh warp refused.");
        return "{\"ok\":true,\"distance\":"+std::to_string(std::sqrt(distance))+"}";
    }
    if(action.rfind("ally-gather-",0)==0){
        // Disposable test character only: the character and the nearest three enemies are moved next
        // to the k-th own summon (a turret placed elsewhere), the enemies 3 units in front of it.
        const size_t wanted=static_cast<size_t>(std::stoul(action.substr(12)));size_t index=0;void* ally=nullptr;
        void* tracker=Get<void*>(c.actor,F("Actor","summonTracker"));
        for(void* summoned:Entries(Get<void*>(tracker,F("SummonTracker","summons")))){
            if(!summoned||!game::IsAlive(summoned))continue;void* actor=Get<void*>(summoned,F("Summoned","actor"));
            if(actor&&game::IsAlive(actor)&&index++==wanted){ally=actor;break;}
        }
        if(!ally)throw std::runtime_error("Cast a test ally first.");Root pin(ally);
        const auto at=Position(ally);void* alignment=Get<void*>(Get<void*>(c.actor,F("Actor","alignmentManager")),F("AlignmentManager","alignment"));
        std::vector<std::pair<double,void*>> enemies;const auto from=Position(c.actor);
        for(void* actor:Find("LE.dll","","Actor")){
            if(actor==c.actor||!game::IsAlive(actor))continue;void* manager=Get<void*>(actor,F("Actor","alignmentManager"));if(!manager)continue;
            void* other=Get<void*>(manager,F("AlignmentManager","alignment"));void* args[]{other};if(!other||!Value<bool>(Method(alignment,"IsFoe",1),alignment,args))continue;
            void* health=Get<void*>(actor,F("Actor","health"));if(!health||Get<float>(health,F("BaseHealth","currentHealth"))<=0)continue;
            auto p=Position(actor);enemies.push_back({(p.x-from.x)*(p.x-from.x)+(p.z-from.z)*(p.z-from.z),actor});
        }
        std::sort(enemies.begin(),enemies.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        int moved=0;
        for(size_t i=0;i<enemies.size()&&moved<3;++i){
            void* agent=Get<void*>(enemies[i].second,F("Actor","navMeshAgent"));if(!agent)continue;
            Vec3 p=at;p.x+=3;p.z+=static_cast<float>(moved)-1;void* args[]{&p};
            if(Value<bool>(Method(agent,"Warp",1),agent,args))++moved;
        }
        Vec3 me=at;me.x-=3;void* agent=Get<void*>(c.actor,F("Actor","navMeshAgent"));void* args[]{&me};const bool warped=Value<bool>(Method(agent,"Warp",1),agent,args);
        return "{\"ok\":true,\"moved\":"+std::to_string(moved)+",\"warped\":"+Boolean(warped)+",\"ally\":["+std::to_string(at.x)+","+std::to_string(at.y)+","+std::to_string(at.z)+"]}";
    }
    if(action=="near-enemy-level"){
        // Disposable test character only: next to the nearest enemy on the same floor (a warp a few
        // units away can land on a wall or a lower level, where turrets have no line of sight).
        void* alignment=Get<void*>(Get<void*>(c.actor,F("Actor","alignmentManager")),F("AlignmentManager","alignment"));
        auto start=Position(c.actor);void* nearest=nullptr;double distance=1.e30;
        for(void* actor:Find("LE.dll","","Actor")){
            if(actor==c.actor||!game::IsAlive(actor))continue;void* manager=Get<void*>(actor,F("Actor","alignmentManager"));if(!manager)continue;
            void* other=Get<void*>(manager,F("AlignmentManager","alignment"));void* args[]{other};if(!other||!Value<bool>(Method(alignment,"IsFoe",1),alignment,args))continue;
            void* health=Get<void*>(actor,F("Actor","health"));if(!health||Get<float>(health,F("BaseHealth","currentHealth"))<=0)continue;
            auto p=Position(actor);double x=p.x-start.x,z=p.z-start.z,d=x*x+z*z;if(d<distance){distance=d;nearest=actor;}
        }
        if(!nearest)throw std::runtime_error("No live enemy in this test scene.");
        const auto target=Position(nearest);void* agent=Get<void*>(c.actor,F("Actor","navMeshAgent"));
        const float offsets[][2]{{-3,0},{3,0},{0,-3},{0,3},{-2,-2},{2,2},{-2,2},{2,-2},{-4,0},{4,0},{0,-4},{0,4},{-1.5f,0},{1.5f,0}};
        for(const auto& o:offsets){
            Vec3 p=target;p.x+=o[0];p.z+=o[1];void* args[]{&p};
            if(!Value<bool>(Method(agent,"Warp",1),agent,args))continue;
            const auto now=Position(c.actor);const double dx=now.x-target.x,dz=now.z-target.z;
            if(std::abs(now.y-target.y)<0.75&&dx*dx+dz*dz<36)return "{\"ok\":true,\"warped\":true,\"height\":"+std::to_string(now.y-target.y)+",\"distance\":"+std::to_string(std::sqrt(dx*dx+dz*dz))+"}";
        }
        throw std::runtime_error("No place on the enemy's floor near it.");
    }
    if(action=="enemies"||action=="near-enemy"){
        void* alignment=Get<void*>(Get<void*>(c.actor,F("Actor","alignmentManager")),F("AlignmentManager","alignment"));
        auto start=Position(c.actor);void* nearest=nullptr;double distance=1.e30;
        std::ostringstream out;out<<"{\"ok\":true,\"enemies\":[";bool comma=false;
        for(void* actor:Find("LE.dll","","Actor")){
            if(actor==c.actor||!game::IsAlive(actor))continue;
            void* manager=Get<void*>(actor,F("Actor","alignmentManager"));if(!manager)continue;
            void* other=Get<void*>(manager,F("AlignmentManager","alignment"));if(!other)continue;
            void* args[]{other};if(!Value<bool>(Method(alignment,"IsFoe",1),alignment,args))continue;
            void* health=Get<void*>(actor,F("Actor","health"));if(!health)continue;
            const auto hp=Get<float>(health,F("BaseHealth","currentHealth"));if(hp<=0)continue;
            auto p=Position(actor);double x=p.x-start.x,z=p.z-start.z,d=x*x+z*z;
            if(d<distance){distance=d;nearest=actor;}
            if(comma)out<<',';comma=true;
            out<<"{\"instance\":"<<Value<int>(M("Object","GetInstanceID",0,"UnityEngine","UnityEngine.CoreModule.dll"),actor)<<",\"health\":"<<hp<<",\"distance\":"<<std::sqrt(d)<<'}';
        }
        if(action=="near-enemy"){
            if(!nearest)throw std::runtime_error("No live enemy in this test scene.");
            auto p=Position(nearest);p.x-=5;void* agent=Get<void*>(c.actor,F("Actor","navMeshAgent"));void* args[]{&p};
            if(!Value<bool>(Method(agent,"Warp",1),agent,args))throw std::runtime_error("Native NavMesh warp refused.");
            return "{\"ok\":true,\"warped\":true}";
        }
        return out.str()+"]}";
    }
    if(action=="handlers"){
        std::ostringstream out;out<<"{\"ok\":true,\"handlers\":[";bool comma=false;
        for(void* handler:Find("LE.dll","","ConditionHandler")){
            Root go(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),handler));
            if(comma)out<<',';comma=true;auto p=Position(handler);
            out<<"{\"name\":"<<Json(Text(Invoke(M("Object","get_name",0,"UnityEngine","UnityEngine.CoreModule.dll"),go.Get())))<<",\"x\":"<<p.x<<",\"z\":"<<p.z<<'}';
        }return out.str()+"]}";
    }
    if(action=="spawn-shrine"){
        Root scene(Invoke(M("SceneList","GetCurrentSceneDetails",0)));void* name=Get<void*>(scene.Get(),F("SceneDetails","Name")),*manager=nullptr;
        void* lookup[]{name,&manager};if(!Value<bool>(M("ShrinesManager","TryGetForScene",2),nullptr,lookup)||!manager)throw std::runtime_error("This scene has no shrine manager.");
        Root pin(manager);Vec3 position=Position(c.actor);position.x+=1;void* args[]{c.actor,&position};
        Invoke(Method(manager,"PlaceRandomShrineAtPosition",2),manager,args);return "{\"ok\":true,\"requested\":true}";
    }
    if(action=="spawn-chest"){
        auto managers=Find("LE.dll","","ChestPlacementManager");if(managers.empty())throw std::runtime_error("This scene has no chest placement manager.");
        void* manager=managers.front();auto spots=Entries(Get<void*>(manager,F("ChestPlacementManager","chestDataSpots")));
        if(spots.empty())throw std::runtime_error("This scene has no native chest spots.");
        int visual=0,spotId=Get<int>(spots.front(),F("ChestSpotData","id"));
        Root go(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),manager));void* sceneArgs[]{go.Get()};
        int scene=Value<uint16_t>(M("SceneList","GetGameObjectSceneIndex",1),nullptr,sceneArgs);
        void* args[]{&visual,&spotId,&scene};Invoke(Method(manager,"PlaceNewChest",3),manager,args);return "{\"ok\":true,\"requested\":true}";
    }
    if(action=="vendor-read"){
        void* shop=Get<void*>(c.manager,F("ItemContainersManager","shop"));Root content(Invoke(Method(shop,"GetContent",0),shop));
        return "{\"ok\":true,\"stock\":"+std::to_string(Entries(content.Get()).size())+"}";
    }
    if(action=="vendor-sell-first"){
        void* inventory=Get<void*>(c.manager,F("ItemContainersManager","inventory"));Root content(Invoke(Method(inventory,"GetContent",0),inventory));auto entries=Entries(content.Get());
        if(entries.empty())throw std::runtime_error("No test item to sell.");XY point=Get<XY>(entries.front(),F("ItemContainerEntry","<Position>k__BackingField"));
        void* shop=Get<void*>(c.manager,F("ItemContainersManager","shop"));int source=1,destination=Get<int>(shop,F("ItemContainer","id"));bool equip=false,stack=true;
        void* args[]{&source,&destination,&point,&equip,&stack};bool moved=Value<bool>(M("ItemContainersManager","TryQuickMove",5),c.manager,args);
        return "{\"ok\":true,\"sold\":"+std::string(Boolean(moved))+"}";
    }
    if(action=="handler-queue"){
        // What is waiting to be classified: object names, most frequent first.
        std::map<std::string,int> names;
        for(const auto& h:pendingHandlers){void* handler=h->Get();if(!handler||!game::IsAlive(handler)){++names["(destroyed)"];continue;}
            Root go(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),handler));
            std::string name=Text(Invoke(M("Object","get_name",0,"UnityEngine","UnityEngine.CoreModule.dll"),go.Get()));
            for(size_t at;(at=name.find("(Clone)"))!=std::string::npos;)name.erase(at,7);++names[name];}
        std::vector<std::pair<std::string,int>> sorted(names.begin(),names.end());std::sort(sorted.begin(),sorted.end(),[](auto& a,auto& b){return a.second>b.second;});
        std::ostringstream out;out<<"{\"ok\":true,\"pending\":"<<pendingHandlers.size()<<",\"queued\":"<<handlersQueued<<",\"classified\":"<<handlersClassified<<",\"names\":[";
        for(size_t i=0;i<sorted.size()&&i<25;++i){if(i)out<<',';out<<'['<<Json(sorted[i].first)<<','<<sorted[i].second<<']';}
        return out.str()+"]}";
    }
    if(action=="interaction-audit"){
        // Every object the six bootstrap searches would find, against what the
        // hooks registered since the last scene change (searches cost frames;
        // test only).
        static const char* types[]{"ChestVisualsCreator","ChestSync","ShrineVisualsCreator","ShrineSync","PortalSync","CaveEntranceLogic"};
        std::ostringstream out;out<<"{\"ok\":true,\"candidates\":"<<candidates.size()<<",\"pendingHandlers\":"<<pendingHandlers.size()<<",\"discovering\":"<<Boolean(discover)<<",\"types\":{";
        for(size_t t=0;t<6;++t){
            const char* ns=t==4?"Networking.Multiplayer.Interactables.Portals":"";int found=0,registered=0;
            for(void* object:Find("LE.dll",ns,types[t])){++found;void* handler=t<4?Get<void*>(object,F(types[t],"conditionHandler")):nullptr;
                for(const auto& entry:candidates)if(entry.object->Get()==object||(handler&&entry.handler&&entry.handler->Get()==handler)){++registered;break;}}
            if(t)out<<',';out<<Json(types[t])<<":{\"found\":"<<found<<",\"registered\":"<<registered<<'}';
        }
        return out.str()+"}}";
    }
    if(action=="world"||action=="near-chest"||action=="near-shrine"||action=="near-portal"||action.rfind("near-chest-",0)==0){
        // near-chest-<distance>: stand that far from the nearest ready chest (radius checks).
        const float away=action.rfind("near-chest-",0)==0?std::stof(action.substr(11)):0.f;
        Root playerObject(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),c.actor));void* a[]{playerObject.Get()};Vec3 player=Position(c.actor);
        std::ostringstream out;out<<"{\"ok\":true,\"objects\":[";bool comma=false;
        for(const char* cls:{"ChestVisualsCreator","ShrineVisualsCreator","ChestSync","ShrineSync"})for(void* object:Find("LE.dll","",cls)){
            void* handler=Get<void*>(object,F(cls,"conditionHandler"));if(!handler)continue;
            bool used=Value<bool>(Method(handler,"triggeredAndCannotTriggerMultipleTimes",0),handler),ready=!used&&Value<bool>(Method(handler,"CheckConditionReadiness",1),handler,a);
            if(comma)out<<',';comma=true;out<<"{\"kind\":"<<Json(cls)<<",\"ready\":"<<Boolean(ready)<<",\"near\":"<<Boolean(Near(object,player))<<'}';
            if(ready&&((action.rfind("near-chest",0)==0&&std::string(cls).rfind("Chest",0)==0)||(action=="near-shrine"&&std::string(cls).rfind("Shrine",0)==0))){Vec3 p=Position(object);p.x+=away;void* agent=Get<void*>(c.actor,F("Actor","navMeshAgent"));void* args[]{&p};if(!Value<bool>(Method(agent,"Warp",1),agent,args))throw std::runtime_error("Native NavMesh warp refused.");return "{\"ok\":true,\"warped\":true}";}
        }
        for(void* portal:Find("LE.dll","Networking.Multiplayer.Interactables.Portals","PortalSync")){
            bool ready=Get<bool>(portal,F("PortalSync","initialized","Networking.Multiplayer.Interactables.Portals"));
            if(comma)out<<',';comma=true;out<<"{\"kind\":\"PortalSync\",\"ready\":"<<Boolean(ready)<<",\"near\":"<<Boolean(Near(portal,player))<<'}';
            if(ready&&action=="near-portal"){Vec3 p=Position(portal);void* agent=Get<void*>(c.actor,F("Actor","navMeshAgent"));void* args[]{&p};if(!Value<bool>(Method(agent,"Warp",1),agent,args))throw std::runtime_error("Native NavMesh warp refused.");return "{\"ok\":true,\"warped\":true}";}
        }return out.str()+"]}";
    }
    throw std::runtime_error("Unknown convenience check.");
});}
#endif
void ReleaseRoots(){takenRoot.reset();takenActor=takenProtection=takenHealth=nullptr;attackerNames.clear();rings.clear();ringMaterial.reset();monitorUntil=0;monitoredAllies.clear();damageOwner.Reset();disabledBehaviours.store(std::make_shared<const std::vector<DisabledBehaviour>>());shortcuts.clear();inventoryPanel.Reset();menuTransform.Reset();footerTransform.Reset();currencyTransform.Reset();gridTransform.Reset();ClearInteractions();saved.clear();typeRoots.clear();damageIconSkillKeys.clear();
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
    testDummy.reset();testDummyProtection=nullptr;testTargetRoots.clear();testTargets.clear();
#endif
    damageIconSkills.clear();damageIcons.clear();owner.Reset();active=false;}
}
