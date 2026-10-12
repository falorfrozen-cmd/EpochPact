#include "convenience.hpp"
#include "managed.hpp"
#include "hook.hpp"
#include "items.hpp"
#include "progression.hpp"
#include "commands.hpp"
#include "lifecycle.hpp"
#include "convenience_rules.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <map>
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
bool currencyResized=false;
Vec2 footerOriginalPosition{};
bool footerSplit=false;
struct Candidate{std::unique_ptr<Root> object;std::unique_ptr<Root> handler;int kind;uint64_t retryAfter=0;};
std::vector<Candidate> candidates;
std::vector<std::unique_ptr<Root>> pendingHandlers;
size_t candidateCursor=0,bootstrapType=0;
bool discover=true;
int interactionScene=0;
uint64_t nextInteraction=0,interactionChecks=0,sceneSearches=0,maximumInteractionMicros=0;
uint64_t lastWork=0,retryWork=0,maximumDiscoveryMicros=0;
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
bool measureFrames=false;
uint64_t previousFrame=0;
std::vector<uint64_t> frameGaps;
Vec3 testOriginalMenuScale{};
bool testMenuScaleSaved=false;
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
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
uint64_t testDamageCalls=0,testDamageOwned=0;
float testDamagePrimary=0,testDamageSecondary=0;
#endif
DamageResult ObserveDamage(void* self,void* stats,void* source,float factor,void* attacker,bool leech,MethodInfo method){
    const auto result=originalDamage(self,stats,source,factor,attacker,leech,method);
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
    ++testDamageCalls;if(monitoredAllies.contains(attacker))++testDamageOwned;
    testDamagePrimary=result.damage;testDamageSecondary=result.secondary;
#endif
    // Preserve the authoritative result and all game effects. The supported
    // native tuple's second field is also sent to the game's damage events.
    // Collection has no allocations and expires when the Summons page stops polling.
    const auto now=GetTickCount64();
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
bool Near(void* object,const Vec3& p){auto v=Position(object);double x=v.x-p.x,y=v.y-p.y,z=v.z-p.z;return x*x+y*y+z*z<=9.;}
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
    if(footerSplit&&footerTransform.Get()&&game::IsAlive(footerTransform.Get())){
        void* args[]{&footerOriginalPosition};Invoke(M("RectTransform","set_anchoredPosition",1,"UnityEngine","UnityEngine.CoreModule.dll"),footerTransform.Get(),args);
    }
    if(currencyResized&&currencyTransform.Get()&&game::IsAlive(currencyTransform.Get())){
        void* size[]{&currencyOriginalSize},*position[]{&currencyOriginalPosition};
        Invoke(M("RectTransform","set_sizeDelta",1,"UnityEngine","UnityEngine.CoreModule.dll"),currencyTransform.Get(),size);
        Invoke(M("RectTransform","set_anchoredPosition",1,"UnityEngine","UnityEngine.CoreModule.dll"),currencyTransform.Get(),position);
    }
    currencyResized=false;footerSplit=false;
    for(auto& b:shortcuts)if(game::IsAlive(b.object->Get())){void* a[]{b.object->Get()};Invoke(M("Object","Destroy",1,"UnityEngine","UnityEngine.CoreModule.dll"),nullptr,a);}
    shortcuts.clear();inventoryPanel.Reset();menuTransform.Reset();footerTransform.Reset();currencyTransform.Reset();gridTransform.Reset();
}
bool ShortcutClick(void* button){
    auto it=std::find_if(shortcuts.begin(),shortcuts.end(),[&](const Shortcut& s){return s.button->Get()==button;});
    if(it==shortcuts.end())return false;
    if(shortcutBusy)return true;
    std::string why;game::Guarded([&]{const auto c=Current();const char* action=it->action;
        shortcutBusy=true;
        if(!commands::SubmitUi("inventoryaction "+c.id+" "+action,[](const std::string& result){Log("inventory shortcut: %s",result.c_str());shortcutBusy=false;}))shortcutBusy=false;
    },&why);if(!why.empty())Log("inventory shortcut refused: %s",why.c_str());
    return true;
}
void Click(void* self,void* event,MethodInfo info){bool ours=false;game::Guarded([&]{ours=ShortcutClick(self);},nullptr);if(!ours)originalClick(self,event,info);}
void Submit(void* self,void* event,MethodInfo info){bool ours=false;game::Guarded([&]{ours=ShortcutClick(self);},nullptr);if(!ours)originalSubmit(self,event,info);}
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
    position.x-=(desired-size.x)*(1-pivot.x);
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
void ReflowFooter(void* menu,rules::Rect screen){
    void* footer=footerTransform.Get(),*currency=currencyTransform.Get(),*grid=gridTransform.Get();if(!grid)return;
    const auto f=RectIn(footer,menu),c=CurrencyBounds(menu),left=RectIn(Get<void*>(inventoryPanel.Get(),F("InventoryPanelUI","leftButtonsContainer")),menu);
    if(c.x-(left.x+left.w)-12>=236){
        if(footerSplit){
            void* args[]{&footerOriginalPosition};Invoke(M("RectTransform","set_anchoredPosition",1,"UnityEngine","UnityEngine.CoreModule.dll"),footer,args);
            auto pos=Value<Vec2>(M("RectTransform","get_anchoredPosition",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency);pos.y=currencyOriginalPosition.y;
            void* ca[]{&pos};Invoke(M("RectTransform","set_anchoredPosition",1,"UnityEngine","UnityEngine.CoreModule.dll"),currency,ca);footerSplit=false;
        }return;
    }
    const auto rows=rules::SplitFooter(RectIn(menu,menu),screen,RectIn(grid,menu),f,left,c);
    if(!rows.visible)return;
    if(!footerSplit){footerOriginalPosition=Value<Vec2>(M("RectTransform","get_anchoredPosition",0,"UnityEngine","UnityEngine.CoreModule.dll"),footer);footerSplit=true;currencyResized=true;}
    CenterY(footer,menu,rows.controlsCenter);CenterY(currency,menu,rows.currencyCenter);
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
            FitCurrencies();ReflowFooter(menu,screen);const auto occupied=CurrencyBounds(menu);
            if(rules::Valid(occupied))placement=rules::PlaceToolbar(bounds,screen,RectIn(footer,menu),RectIn(Get<void*>(inventoryPanel.Get(),F("InventoryPanelUI","leftButtonsContainer")),menu),occupied);
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
        PlaceShortcuts();return;
    }
    ClearShortcuts();
    ++inventorySearches;
    auto panels=Find("LE.dll","","InventoryPanelUI");if(panels.empty()){inventoryStatus="inventory-closed";return;}
    void* panel=panels.front();void* source=Get<void*>(panel,F("InventoryPanelUI","confirmRespecButton"));
    void* parent=Get<void*>(panel,F("InventoryPanelUI","menuRectTransform"));if(!source||!parent){inventoryStatus="inventory-template-unavailable";return;}
    void* left=Get<void*>(panel,F("InventoryPanelUI","leftButtonsContainer"));if(!left){inventoryStatus="inventory-footer-unavailable";return;}
    Root footer(Invoke(M("Transform","get_parent",0,"UnityEngine","UnityEngine.CoreModule.dll"),left));
    Root currencyName(il2cpp::api().string_new("Currencies"));void* findArgs[]{currencyName.Get()};
    Root currency(Invoke(M("Transform","Find",1,"UnityEngine","UnityEngine.CoreModule.dll"),footer.Get(),findArgs));
    if(!currency.Get()){inventoryStatus="inventory-currency-container-unavailable";return;} // No guessed screen coordinates.
    Root items(Invoke(M("Transform","get_parent",0,"UnityEngine","UnityEngine.CoreModule.dll"),footer.Get()));
    Root gridName(il2cpp::api().string_new("Inventory Grid "));void* ga[]{gridName.Get()};
    Root grid(Invoke(M("Transform","Find",1,"UnityEngine","UnityEngine.CoreModule.dll"),items.Get(),ga));
    auto click=M("Button","OnPointerClick",1,"UnityEngine.UI","UnityEngine.UI.dll"),submit=M("Button","OnSubmit",1,"UnityEngine.UI","UnityEngine.UI.dll");
    std::string why;
    if(!hook::IsInstalled(click.code)&&!hook::Install(click.code,reinterpret_cast<void*>(&Click),reinterpret_cast<void**>(&originalClick),&why))throw std::runtime_error(why);
    if(!hook::IsInstalled(submit.code)&&!hook::Install(submit.code,reinterpret_cast<void*>(&Submit),reinterpret_cast<void**>(&originalSubmit),&why))throw std::runtime_error(why);
    auto* buttonType=game::FindClass("UnityEngine.UI.dll","UnityEngine.UI","Button");
    Root bt(il2cpp::api().type_get_object(il2cpp::api().class_get_type(buttonType)));
    Root templateObject(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),source));
    const auto* instantiate=Signature("Object","Instantiate",{"UnityEngine.Object","UnityEngine.Transform","System.Boolean"});
    for(auto pair:std::array<std::pair<const char*,const char*>,3>{{{"stash","Stash"},{"stashall","Stash all"},{"vendor","Vendor"}}}){
        bool world=false;void* a[]{templateObject.Get(),parent,&world};Root object(Invoke(instantiate,nullptr,a));
        bool off=false;void* offArgs[]{&off};Invoke(Method(object.Get(),"SetActive",1),object.Get(),offArgs);
        void* bargs[]{bt.Get()};Root button(Invoke(Signature("GameObject","GetComponent",{"System.Type"}),object.Get(),bargs));
        shortcuts.push_back({std::make_unique<Root>(button.Get()),std::make_unique<Root>(object.Get()),pair.first,pair.second,{}});
        Root layoutType(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("UnityEngine.UI.dll","UnityEngine.UI","LayoutElement"))));
        void* la[]{layoutType.Get()};void* element=Invoke(Signature("GameObject","GetComponent",{"System.Type"}),object.Get(),la);
        if(!element)element=Invoke(Signature("GameObject","AddComponent",{"System.Type"}),object.Get(),la);
        Root layout(element);
        bool ignore=true;void* ia[]{&ignore};Invoke(M("LayoutElement","set_ignoreLayout",1,"UnityEngine.UI","UnityEngine.UI.dll"),layout.Get(),ia);
        // A cloned game button can use legacy UI.Text rather than TMP, and its
        // localization component must not restore the original blessing label.
        // Only components below our cloned object are touched.
        bool inactive=true;
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
        Root text(il2cpp::api().string_new(pair.second));
        for(bool tmp:{true,false}){
            Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass(tmp?"Unity.TextMeshPro.dll":"UnityEngine.UI.dll",tmp?"TMPro":"UnityEngine.UI",tmp?"TMP_Text":"Text"))));
            void* targs[]{type.Get(),&inactive};Root texts(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),object.Get(),targs));
            for(uintptr_t i=0;i<Get<uintptr_t>(texts.Get(),0x18);++i){void* label=Get<void*>(texts.Get(),0x20+8*i);shortcuts.back().labels.push_back(std::make_unique<Root>(label));
                bool autosize=false;void* autoArgs[]{&autosize};Invoke(Method(label,tmp?"set_enableAutoSizing":"set_resizeTextForBestFit",1),label,autoArgs);
                if(tmp){float fontSize=14;void* a[]{&fontSize};Invoke(Method(label,"set_fontSize",1),label,a);}else{int fontSize=14;void* a[]{&fontSize};Invoke(Method(label,"set_fontSize",1),label,a);}
                void* args[]{text.Get()};Invoke(Method(label,"set_text",1),label,args);}
        }
        if(shortcuts.back().labels.empty())throw std::runtime_error("Inventory shortcut has no supported text label.");
        Root transform(Invoke(M("GameObject","get_transform",0,"UnityEngine","UnityEngine.CoreModule.dll"),object.Get()));
        // Parent to the inventory so closing/tabs inherit its visibility. The
        // measured footer gap determines the final horizontal row.
        Vec2 size{80,26.4f},pos{0,0},zero{0,0};Vec3 scale{1,1,1};
        void* za[]{&zero};for(const char* name:{"set_anchorMin","set_anchorMax","set_pivot"})Invoke(M("RectTransform",name,1,"UnityEngine","UnityEngine.CoreModule.dll"),transform.Get(),za);
        void* sa[]{&size},*pa[]{&pos},*sca[]{&scale};Invoke(M("RectTransform","set_sizeDelta",1,"UnityEngine","UnityEngine.CoreModule.dll"),transform.Get(),sa);
        Invoke(M("RectTransform","set_anchoredPosition",1,"UnityEngine","UnityEngine.CoreModule.dll"),transform.Get(),pa);Invoke(M("Transform","set_localScale",1,"UnityEngine","UnityEngine.CoreModule.dll"),transform.Get(),sca);
        bool on=true;void* args[]{&on};Invoke(Method(object.Get(),"SetActive",1),object.Get(),args);
    }
    inventoryPanel.Set(panel);menuTransform.Set(parent);footerTransform.Set(footer.Get());currencyTransform.Set(currency.Get());gridTransform.Set(grid.Get());
    currencyOriginalSize=Value<Vec2>(M("RectTransform","get_sizeDelta",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency.Get());
    currencyOriginalPosition=Value<Vec2>(M("RectTransform","get_anchoredPosition",0,"UnityEngine","UnityEngine.CoreModule.dll"),currency.Get());
    shortcutEnabled=false;inventoryStatus="created";PlaceShortcuts();
}
void RestoreGraphics(){
    disabledBehaviours.store(std::make_shared<const std::vector<DisabledBehaviour>>());
    for(auto& entry:saved){bool v=entry.before;if(entry.field)il2cpp::api().field_set_value(entry.value->Get(),entry.field,&v);
        else if(game::IsAlive(entry.value->Get())){void* a[]{&v};Invoke(M("Behaviour","set_enabled",1,"UnityEngine","UnityEngine.CoreModule.dll"),entry.value->Get(),a);}}
    saved.clear();
    if(shadowCaptured){void* a[]{&originalShadows};Invoke(M("QualitySettings","set_shadows",1,"UnityEngine","UnityEngine.CoreModule.dll"),nullptr,a);shadowCaptured=false;}
}
void Graphics(){
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
    if(shadowCaptured){int value=shadows.value?0:originalShadows;void* a[]{&value};Invoke(M("QualitySettings","set_shadows",1,"UnityEngine","UnityEngine.CoreModule.dll"),nullptr,a);if(!shadows.value)shadowCaptured=false;}
    if(!(reflections.value||bloom.value||motionBlur.value||occlusion.value||fog.value))return;
    auto remember=[&](void* value,void* volume,const il2cpp::Field* field,Setting* setting){
        auto exists=std::find_if(saved.begin(),saved.end(),[&](const Saved& s){return s.value->Get()==value&&s.field==field;});
        if(exists==saved.end()){
            if(saved.size()>=256)throw std::runtime_error("Too many graphics overrides; disable effects and retry.");
            bool before=false;il2cpp::api().field_get_value(value,field,&before);
            saved.push_back({std::make_unique<Root>(value),std::make_unique<Root>(volume),field,before,setting});
        }
        bool disabled=false;il2cpp::api().field_set_value(value,field,&disabled);
    };
    for(void* volume:Find("Unity.Postprocessing.Runtime.dll","UnityEngine.Rendering.PostProcessing","PostProcessVolume")){
        Root profile(Invoke(Method(volume,"get_profile",0),volume));
        auto* field=Field(profile.Get(),"settings");void* list=nullptr;il2cpp::api().field_get_value(profile.Get(),field,&list);
        for(void* effect:Entries(list))if(effect){
            const std::string name=il2cpp::api().class_get_name(il2cpp::api().object_get_class(effect));
            Setting* setting=name=="Bloom"?&bloom:name=="MotionBlur"?&motionBlur:name=="AmbientOcclusion"?&occlusion:name=="ScreenSpaceReflections"?&reflections:nullptr;
            if(!setting||!setting->value)continue;
            void* enabled=nullptr;il2cpp::api().field_get_value(effect,Field(effect,"enabled"),&enabled);
            if(enabled)remember(enabled,volume,Field(enabled,"value"),setting);
        }
    }
    for(auto spec:std::array<std::tuple<Setting*,const char*,const char*,const char*>,2>{{{&fog,"LE.dll","","HxVolumetricCamera"},{&reflections,"UnityEngine.CoreModule.dll","UnityEngine","ReflectionProbe"}}})if(std::get<0>(spec)->value)for(void* camera:Find(std::get<1>(spec),std::get<2>(spec),std::get<3>(spec))){
        auto exists=std::find_if(saved.begin(),saved.end(),[&](const Saved& s){return !s.field&&s.value->Get()==camera;});
        if(exists==saved.end()){bool before=Value<bool>(M("Behaviour","get_enabled",0,"UnityEngine","UnityEngine.CoreModule.dll"),camera);
            saved.push_back({std::make_unique<Root>(camera),std::make_unique<Root>(camera),nullptr,before,std::get<0>(spec)});}
        PublishDisabled();
        bool enabled=false;void* a[]{&enabled};Invoke(M("Behaviour","set_enabled",1,"UnityEngine","UnityEngine.CoreModule.dll"),camera,a);
    }
}
void ClearInteractions(){candidates.clear();pendingHandlers.clear();candidateCursor=0;bootstrapType=0;discover=true;interactionScene=0;}
void AddCandidate(void* object,void* handler,int kind){
    if(!object||!game::IsAlive(object)||candidates.size()>=4096)return;
    for(const auto& entry:candidates)if((handler&&entry.handler&&entry.handler->Get()==handler)||entry.object->Get()==object)return;
    candidates.push_back({std::make_unique<Root>(object),handler?std::make_unique<Root>(handler):nullptr,kind,0});
}
void HandlerAwake(void* self,MethodInfo info){
    originalHandlerAwake(self,info);
    if(active&&!lifecycle::Stopping())game::Guarded([&]{if(pendingHandlers.size()<4096)pendingHandlers.push_back(std::make_unique<Root>(self));},nullptr);
}
void PortalInitialize(void* self,MethodInfo info){originalPortalInitialize(self,info);
    if(active&&!lifecycle::Stopping())game::Guarded([&]{AddCandidate(self,nullptr,2);},nullptr);}
void CaveAwake(void* self,MethodInfo info){originalCaveAwake(self,info);
    if(active&&!lifecycle::Stopping())game::Guarded([&]{AddCandidate(self,nullptr,3);},nullptr);}
void ShrineEra(void* self,int era,MethodInfo info){originalShrineEra(self,era,info);
    if(active&&!lifecycle::Stopping())game::Guarded([&]{AddCandidate(self,nullptr,1);},nullptr);}
void ShrineSetup(void* self,void* object,bool networking,int kind,const Vec3* position,MethodInfo info){
    originalShrineSetup(self,object,networking,kind,position,info);
    if(active&&!lifecycle::Stopping())game::Guarded([&]{
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
void Interactions(void* actor){
    if(items::IsInspectingItem()||PanelOpen())return;
    const auto start=Micros();
    Root go(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),actor));
    // A Scene is one handle-sized integer in this supported Unity build.
    const int scene=Value<int>(M("GameObject","get_scene",0,"UnityEngine","UnityEngine.CoreModule.dll"),go.Get());
    if(scene!=interactionScene){ClearInteractions();interactionScene=scene;portalGrace=GetTickCount64()+10000;}
    if(discover){
        // Bootstrap existing objects once, one type per frame. Awake/initialize
        // notifications register later spawns; idle frames never search a scene.
        static const char* types[]{"ChestVisualsCreator","ChestSync","ShrineVisualsCreator","ShrineSync","PortalSync","CaveEntranceLogic"};
        const char* cls=types[bootstrapType];int kind=static_cast<int>(bootstrapType/2);
        const char* ns=bootstrapType==4?"Networking.Multiplayer.Interactables.Portals":"";
        for(void* object:Find("LE.dll",ns,cls)){
            void* handler=bootstrapType<4?Get<void*>(object,F(cls,"conditionHandler")):nullptr;
            AddCandidate(object,handler,bootstrapType==5?3:kind);
        }
        ++sceneSearches;if(++bootstrapType==6)discover=false;
        maximumDiscoveryMicros=std::max(maximumDiscoveryMicros,Micros()-start);return;
    }
    if(!pendingHandlers.empty()){auto handler=std::move(pendingHandlers.back());pendingHandlers.pop_back();ClassifyHandler(handler->Get());
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
            Invoke(Method(object,"SendAttemptUse",0),object);}
        else{
            if(!handler||!game::IsAlive(handler)||Value<bool>(Method(handler,"triggeredAndCannotTriggerMultipleTimes",0),handler))continue;
            void* a[]{go.Get()};if(!Value<bool>(Method(handler,"CheckConditionReadiness",1),handler,a)){entry.retryAfter=now+1000;continue;}
            Invoke(Method(handler,"TryTriggerInteraction",1),handler,a);
        }
        entry.retryAfter=now+2000;nextInteraction=now+250;
        if(entry.kind==0)++openedChests;else if(entry.kind==1)++usedShrines;
        else{++usedPortals;portals.value=0;portalGrace=now+10000;}
        break; // Never run a second native interaction in this frame.
    }
    maximumInteractionMicros=std::max(maximumInteractionMicros,Micros()-start);
}
void Frame(){
    ++frameUpdates;
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
    if(measureFrames){const auto sample=Micros();if(previousFrame&&frameGaps.size()<10000)frameGaps.push_back(sample-previousFrame);previousFrame=sample;}
#endif
    if(lifecycle::Stopping())return;
    const auto clock=GetTickCount64();
    if(clock>=lastButtons+250){lastButtons=clock;std::string why;const auto started=Micros();
        if(!game::Guarded([]{if(Actor())InventoryButtons();else if(!shortcuts.empty())ClearShortcuts();},&why)){
            ++faults;lastButtons=clock+14000;Log("inventory buttons: %s",why.c_str());
            game::Guarded([]{ClearShortcuts();},nullptr);
        }
        const auto spent=Micros()-started;inventoryMicros+=spent;maximumInventoryMicros=std::max(maximumInventoryMicros,spent);
    }
    if(!active.load(std::memory_order_relaxed))return;
    const auto now=GetTickCount64();
    if(now<retryWork||now<lastWork+50)return;lastWork=now;
    std::string why;
    if(!game::Guarded([&]{
        void* actor=Actor();
        if(!actor)return; // Loading/alt-tab is a pause, not a setting reset.
        if(owner.Get()&&owner.Get()!=actor){RestoreGraphics();ClearInteractions();for(auto* setting:settings)setting->value=0;owner.Reset();active=false;return;}
        if(!owner.Get()){owner.Set(actor);portalGrace=now+10000;}
        if(now>=lastScan+1000){lastScan=now;Graphics();}
        if(chests.value||shrines.value||portals.value)Interactions(actor);
    },&why)){++faults;retryWork=now+5000;Log("convenience: %s",why.c_str());}
}
std::string State(){std::ostringstream out;out<<"{\"ok\":true,\"settings\":{";bool comma=false;for(auto* s:settings){if(comma)out<<',';comma=true;out<<Json(s->key)<<":{\"value\":"<<s->value<<",\"available\":"<<Boolean(mainthread::HookInstalled())<<'}';}
    unsigned visible=0;for(auto& b:shortcuts)if(Value<bool>(Method(b.object->Get(),"get_activeInHierarchy",0),b.object->Get()))++visible;
    return out.str()+"},\"inventoryButtons\":"+std::to_string(shortcuts.size())+",\"inventoryButtonsVisible\":"+std::to_string(visible)+",\"frameUpdates\":"+std::to_string(frameUpdates)+",\"inventoryChecks\":"+std::to_string(inventoryChecks)+",\"inventorySearches\":"+std::to_string(inventorySearches)+",\"inventoryMicros\":"+std::to_string(inventoryMicros)+",\"maximumInventoryMicros\":"+std::to_string(maximumInventoryMicros)+",\"inventoryStatus\":"+Json(inventoryStatus)+",\"graphicsOverrides\":"+std::to_string(saved.size())+",\"chestsOpened\":"+std::to_string(openedChests)+",\"shrinesUsed\":"+std::to_string(usedShrines)+",\"portalsUsed\":"+std::to_string(usedPortals)+",\"interactionCandidates\":"+std::to_string(candidates.size())+",\"interactionChecks\":"+std::to_string(interactionChecks)+",\"sceneSearches\":"+std::to_string(sceneSearches)+",\"maximumDiscoveryMicros\":"+std::to_string(maximumDiscoveryMicros)+",\"maximumInteractionMicros\":"+std::to_string(maximumInteractionMicros)+",\"faults\":"+std::to_string(faults)+"}";}
}
bool Init(){
    findObjects=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Object","FindObjectsOfType",2);
    componentTransform=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Component","get_transform",0);
    transformPosition=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Transform","get_position",0);
    if(!(findObjects&&componentTransform&&transformPosition))return false;
    std::string why;
    if(!mainthread::RegisterFrameCallback(&Frame,&why)){Log("convenience bootstrap refused: %s",why.c_str());return false;}
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
    try {if(key!="chests"&&key!="shrines"&&key!="portals")Graphics();}
    catch(...){(*it)->value=previous;try{Graphics();}catch(...){++faults;}throw;}
    return State();
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
std::string Test(const std::string& id,const std::string& action){return Run([=]() -> std::string {
    const auto c=Current(id);if(c.name!="EpFeedbackTest")throw std::runtime_error("Convenience checks require the isolated EpFeedbackTest character.");
    void* ui=Ui();
    if(action=="currency-max"||action=="currency-read"){
        void* gold=Get<void*>(c.actor,F("Actor","goldTracker"));
        void* bones=Get<void*>(c.actor,F("Actor","ancientBonesTracker"));
        if(!gold||!bones)throw std::runtime_error("Currency trackers unavailable.");
        if(action=="currency-max"){
            // Stress the real Int32 currency trackers through their native
            // absolute-load APIs, only in the disposable named test character.
            int value=std::numeric_limits<int>::max();void* args[]{&value};
            Invoke(Method(gold,"loadGold",1),gold,args);
            Invoke(Method(bones,"loadAncientBones",1),bones,args);
            if(Get<int>(gold,F("GoldTracker","value"))!=value||Get<int>(bones,F("AncientBonesTracker","value"))!=value)
                throw std::runtime_error("Native currency maximum readback failed.");
        }
        return "{\"ok\":true,\"gold\":"+std::to_string(Get<int>(gold,F("GoldTracker","value")))+",\"ancientBones\":"+std::to_string(Get<int>(bones,F("AncientBonesTracker","value")))+"}";
    }
    if(action=="frames-start"){frameGaps.clear();previousFrame=0;measureFrames=true;maximumInteractionMicros=maximumDiscoveryMicros=0;inventoryChecks=inventorySearches=inventoryMicros=maximumInventoryMicros=0;return State();}
    if(action=="frames-stop"){measureFrames=false;std::ostringstream out;out<<"{\"ok\":true,\"gapsMicros\":[";bool comma=false;for(auto value:frameGaps){if(comma)out<<',';comma=true;out<<value;}return out.str()+"]}";}
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
    if(action=="inventory-open"){bool show=true,appearances=false,stash=false,last=false;int tab=0;void* a[]{&show,&appearances,&stash,&tab,&last};Invoke(Method(ui,"openInventory",5),ui,a);return State();}
    if(action=="skills-open"){Invoke(Method(ui,"SkillsKeyDown",0),ui);return "{\"ok\":true}";}
    if(action=="inventory-open-beside"){bool overlapping=false,cosmetics=false,dontOpen=false,sound=false;int tab=0;void* a[]{&overlapping,&cosmetics,&dontOpen,&tab,&sound};Invoke(Method(ui,"openInventory",5),ui,a);return State();}
    if(action=="passives-open"){bool respec=false;void* a[]{&respec};Invoke(Method(ui,"OpenPassivePanel",1),ui,a);return "{\"ok\":true,\"passivesOpen\":"+std::string(Boolean(Value<bool>(Method(ui,"IsPassivesPanelOpen",0),ui)))+"}";}
    if(action=="passives-close"){Invoke(Method(ui,"closePassiveTree",0),ui);return State();}
    if(action=="skills-close"){bool tooltip=true;void* a[]{&tooltip};Invoke(Method(ui,"closeSkills",1),ui,a);return State();}
    if(action=="blessings-open"){bool sound=false,respec=false;void* a[]{&sound,&respec};Invoke(Method(inventoryPanel.Get(),"OpenBlessingPanel",2),inventoryPanel.Get(),a);return State();}
    if(action=="panels-close"){bool sound=false;void* a[]{&sound};Invoke(Method(ui,"closeShop",0),ui);Invoke(Method(ui,"closeStash",1),ui,a);Invoke(Method(ui,"closeInventory",0),ui);return State();}
    if(action=="panels")return "{\"ok\":true,\"inventory\":"+std::string(Boolean(Value<bool>(Method(ui,"IsInventoryOpen",0),ui)))+",\"stash\":"+Boolean(Value<bool>(Method(ui,"IsStashOpen",0),ui))+",\"shop\":"+Boolean(Value<bool>(Method(ui,"IsShopOpen",0),ui))+",\"buttons\":"+std::to_string(shortcuts.size())+"}";
    if(action=="screenshot"){
        auto capture=game::FindMethod("UnityEngine.ScreenCaptureModule.dll","UnityEngine","ScreenCapture","CaptureScreenshot",2);
        if(!capture)throw std::runtime_error("Native screenshot method unavailable.");
        auto text=PluginDir()+L"feedback-inventory.png";
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
    if(action=="summon-cast"){
        uint16_t abilityId=379;void* a[]{&abilityId};Root ability(Invoke(Signature("Ability","getAbility",{"System.UInt16"},"","LE.dll"),nullptr,a));
        if(!ability.Get())throw std::runtime_error("Ballista ability unavailable.");
        void* constructor=Get<void*>(c.actor,F("Actor","abilityObjectConstructor"));Vec3 p=Position(c.actor),direction{0,0,1};p.x+=2;float delay=0;int type=1;bool mutate=true;void* args[]{constructor,&p,&direction,&delay,&type,&mutate,nullptr};Invoke(Method(ability.Get(),"CastAfterDelay",7),ability.Get(),args);
        return "{\"ok\":true,\"requested\":true}";
    }
    if(action=="combat-clock")return "{\"ok\":true,\"timeScale\":"+std::to_string(Value<float>(M("Time","get_timeScale",0,"UnityEngine","UnityEngine.CoreModule.dll")))+",\"damageCalls\":"+std::to_string(testDamageCalls)+",\"ownedCalls\":"+std::to_string(testDamageOwned)+",\"lastPrimary\":"+std::to_string(testDamagePrimary)+",\"lastSecondary\":"+std::to_string(testDamageSecondary)+"}";
    if(action=="ally-attack"){
        void* source=nullptr;void* tracker=Get<void*>(c.actor,F("Actor","summonTracker"));
        for(void* summoned:Entries(Get<void*>(tracker,F("SummonTracker","summons")))){
            if(!summoned||!game::IsAlive(summoned))continue;
            void* references=Get<void*>(summoned,F("Summoned","references"));
            if(references&&Get<void*>(references,F("CreationReferences","creator"))==c.actor){source=Get<void*>(summoned,F("Summoned","actor"));break;}
        }
        if(!source||!game::IsAlive(source))throw std::runtime_error("Cast a test ally first.");Root pin(source);
        Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("LE.dll","","AbilityList"))));
        void* ca[]{type.Get()};Root list(Invoke(Signature("Component","GetComponent",{"System.Type"}),source,ca));
        if(!list.Get())throw std::runtime_error("This ally has no ability list.");
        const auto abilities=Entries(Get<void*>(list.Get(),F("AbilityList","abilities")));if(abilities.empty())throw std::runtime_error("This ally has no loaded attack.");
        Root ability(abilities.front());void* alignment=Get<void*>(Get<void*>(source,F("Actor","alignmentManager")),F("AlignmentManager","alignment"));
        auto start=Position(source);void* nearest=nullptr;double distance=1.e30;
        for(void* target:Find("LE.dll","","Actor")){
            if(!game::IsAlive(target))continue;void* manager=Get<void*>(target,F("Actor","alignmentManager"));if(!manager)continue;
            void* other=Get<void*>(manager,F("AlignmentManager","alignment"));void* args[]{other};if(!other||!Value<bool>(Method(alignment,"IsFoe",1),alignment,args))continue;
            void* health=Get<void*>(target,F("Actor","health"));if(!health||Get<float>(health,F("BaseHealth","currentHealth"))<=0)continue;
            auto p=Position(target);double x=p.x-start.x,z=p.z-start.z,d=x*x+z*z;if(d<distance){distance=d;nearest=target;}
        }
        if(!nearest)throw std::runtime_error("No test enemy available.");
        Vec3 p=Position(nearest),direction{p.x-start.x,p.y-start.y,p.z-start.z};float length=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);if(length>0){direction.x/=length;direction.y/=length;direction.z/=length;}
        void* constructor=Get<void*>(source,F("Actor","abilityObjectConstructor"));float delay=0;int castType=1;bool mutate=true;
        void* args[]{constructor,&p,&direction,&delay,&castType,&mutate,nullptr};Invoke(Method(ability.Get(),"CastAfterDelay",7),ability.Get(),args);
        return "{\"ok\":true,\"requested\":true,\"ability\":"+Json(Text(Get<void*>(ability.Get(),F("Ability","abilityName"))))+"}";
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
    if(action=="world"||action=="near-chest"||action=="near-shrine"||action=="near-portal"){
        Root playerObject(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),c.actor));void* a[]{playerObject.Get()};Vec3 player=Position(c.actor);
        std::ostringstream out;out<<"{\"ok\":true,\"objects\":[";bool comma=false;
        for(const char* cls:{"ChestVisualsCreator","ShrineVisualsCreator","ChestSync","ShrineSync"})for(void* object:Find("LE.dll","",cls)){
            void* handler=Get<void*>(object,F(cls,"conditionHandler"));if(!handler)continue;
            bool used=Value<bool>(Method(handler,"triggeredAndCannotTriggerMultipleTimes",0),handler),ready=!used&&Value<bool>(Method(handler,"CheckConditionReadiness",1),handler,a);
            if(comma)out<<',';comma=true;out<<"{\"kind\":"<<Json(cls)<<",\"ready\":"<<Boolean(ready)<<",\"near\":"<<Boolean(Near(object,player))<<'}';
            if(ready&&((action=="near-chest"&&std::string(cls).rfind("Chest",0)==0)||(action=="near-shrine"&&std::string(cls).rfind("Shrine",0)==0))){Vec3 p=Position(object);void* agent=Get<void*>(c.actor,F("Actor","navMeshAgent"));void* args[]{&p};if(!Value<bool>(Method(agent,"Warp",1),agent,args))throw std::runtime_error("Native NavMesh warp refused.");return "{\"ok\":true,\"warped\":true}";}
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
void ReleaseRoots(){monitorUntil=0;monitoredAllies.clear();damageOwner.Reset();disabledBehaviours.store(std::make_shared<const std::vector<DisabledBehaviour>>());shortcuts.clear();inventoryPanel.Reset();menuTransform.Reset();footerTransform.Reset();currencyTransform.Reset();gridTransform.Reset();ClearInteractions();saved.clear();typeRoots.clear();owner.Reset();active=false;}
}
