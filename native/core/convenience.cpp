#include "convenience.hpp"
#include "managed.hpp"
#include "hook.hpp"
#include "items.hpp"
#include "progression.hpp"
#include "commands.hpp"
#include "lifecycle.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <tuple>

namespace ep::convenience {
namespace {
using namespace managed;
using MethodInfo=const il2cpp::Method*;
struct Vec3{float x,y,z;};
struct XY{int x,y;};
struct OptionalXY{bool has;XY value;};
struct Setting{const char* key;std::atomic<double> value{0};};
Setting chests{"chests"},shrines{"shrines"},portals{"portals"},shadows{"shadows"},reflections{"reflections"},
    bloom{"bloom"},motionBlur{"motionblur"},occlusion{"occlusion"},fog{"fog"};
std::array<Setting*,9> settings{&chests,&shrines,&portals,&shadows,&reflections,&bloom,&motionBlur,&occlusion,&fog};
game::MethodRef frame,findObjects,componentTransform,transformPosition;
void(*originalFrame)(void*,MethodInfo)=nullptr;
std::atomic<bool> active{false};
uint64_t lastScan=0,portalGrace=0,faults=0,openedChests=0,usedShrines=0,usedPortals=0;
game::Handle owner;
std::map<std::string,std::unique_ptr<Root>> typeRoots;
struct Saved {std::unique_ptr<Root> value;std::unique_ptr<Root> volume;const il2cpp::Field* field;bool before;Setting* setting;};
struct Shortcut{std::unique_ptr<Root> button;std::unique_ptr<Root> object;const char* action;const char* label;std::vector<std::unique_ptr<Root>> labels;};
std::vector<Shortcut> shortcuts;
game::Handle inventoryPanel;
std::atomic<bool> shortcutBusy{false};
void(*originalClick)(void*,void*,MethodInfo)=nullptr;
void(*originalSubmit)(void*,void*,MethodInfo)=nullptr;
uint64_t lastButtons=0;
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
size_t F(const char* cls,const char* field,const char* ns=""){return Offset(cls,field,ns);}
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
    auto& a=il2cpp::api();auto* c=game::FindClass(image,ns,cls);void* iterator=nullptr;
    while(auto* m=a.class_get_methods(c,&iterator)){
        if(std::string(a.method_get_name(m))!=name||a.method_get_param_count(m)!=parameters.size())continue;
        bool match=true;unsigned i=0;for(const char* expected:parameters){char* actual=a.type_get_name(a.method_get_param(m,i++));match &= actual&&std::string(actual)==expected;if(actual)a.free(actual);}
        if(match)return m;
    }throw std::runtime_error(std::string("Missing overload: ")+cls+"."+name);
}
void ClearShortcuts(){
    for(auto& b:shortcuts)if(game::IsAlive(b.object->Get())){void* a[]{b.object->Get()};Invoke(M("Object","Destroy",1,"UnityEngine","UnityEngine.CoreModule.dll"),nullptr,a);}
    shortcuts.clear();inventoryPanel.Reset();
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
void InventoryButtons(){
    if(!shortcuts.empty()&&inventoryPanel.Get()){
        bool enabled=!shortcutBusy;void* a[]{&enabled};
        for(auto& b:shortcuts){Invoke(Method(b.button->Get(),"set_interactable",1),b.button->Get(),a);
            for(auto& label:b.labels)if(Text(Invoke(M("TMP_Text","get_text",0,"TMPro","Unity.TextMeshPro.dll"),label->Get()))!=b.label){
                Root text(il2cpp::api().string_new(b.label));void* args[]{text.Get()};Invoke(M("TMP_Text","set_text",1,"TMPro","Unity.TextMeshPro.dll"),label->Get(),args);}}
        return;
    }
    ClearShortcuts();
    auto panels=Find("LE.dll","","InventoryPanelUI");if(panels.empty())return;
    void* panel=panels.front();void* source=Get<void*>(panel,F("InventoryPanelUI","confirmRespecButton"));
    void* parent=Get<void*>(panel,F("InventoryPanelUI","leftButtonsContainer"));if(!source||!parent)return;
    auto click=M("Button","OnPointerClick",1,"UnityEngine.UI","UnityEngine.UI.dll"),submit=M("Button","OnSubmit",1,"UnityEngine.UI","UnityEngine.UI.dll");
    std::string why;
    if(!hook::IsInstalled(click.code)&&!hook::Install(click.code,reinterpret_cast<void*>(&Click),reinterpret_cast<void**>(&originalClick),&why))throw std::runtime_error(why);
    if(!hook::IsInstalled(submit.code)&&!hook::Install(submit.code,reinterpret_cast<void*>(&Submit),reinterpret_cast<void**>(&originalSubmit),&why))throw std::runtime_error(why);
    auto* buttonType=game::FindClass("UnityEngine.UI.dll","UnityEngine.UI","Button");
    auto* textType=game::FindClass("Unity.TextMeshPro.dll","TMPro","TMP_Text");
    Root bt(il2cpp::api().type_get_object(il2cpp::api().class_get_type(buttonType))),tt(il2cpp::api().type_get_object(il2cpp::api().class_get_type(textType)));
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
        Root text(il2cpp::api().string_new(pair.second));bool inactive=true;void* targs[]{tt.Get(),&inactive};Root texts(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),object.Get(),targs));
        for(uintptr_t i=0;i<Get<uintptr_t>(texts.Get(),0x18);++i){void* label=Get<void*>(texts.Get(),0x20+8*i);shortcuts.back().labels.push_back(std::make_unique<Root>(label));
            bool autosize=false;float fontSize=14;void* autoArgs[]{&autosize},*sizeArgs[]{&fontSize};
            Invoke(M("TMP_Text","set_enableAutoSizing",1,"TMPro","Unity.TextMeshPro.dll"),label,autoArgs);Invoke(M("TMP_Text","set_fontSize",1,"TMPro","Unity.TextMeshPro.dll"),label,sizeArgs);
            void* args[]{text.Get()};Invoke(M("TMP_Text","set_text",1,"TMPro","Unity.TextMeshPro.dll"),label,args);}
        Root transform(Invoke(M("GameObject","get_transform",0,"UnityEngine","UnityEngine.CoreModule.dll"),object.Get()));
        // Use the empty space after Transfer/Sort in the footer. Never cover a
        // usable inventory cell or the currency counters on the right.
        struct Vec2{float x,y;};Vec2 size{100,32},pos{244+static_cast<float>((shortcuts.size()-1)*106),0},zero{0,0};Vec3 scale{1,1,1};
        void* za[]{&zero};for(const char* name:{"set_anchorMin","set_anchorMax","set_pivot"})Invoke(M("RectTransform",name,1,"UnityEngine","UnityEngine.CoreModule.dll"),transform.Get(),za);
        void* sa[]{&size},*pa[]{&pos},*sca[]{&scale};Invoke(M("RectTransform","set_sizeDelta",1,"UnityEngine","UnityEngine.CoreModule.dll"),transform.Get(),sa);
        Invoke(M("RectTransform","set_anchoredPosition",1,"UnityEngine","UnityEngine.CoreModule.dll"),transform.Get(),pa);Invoke(M("Transform","set_localScale",1,"UnityEngine","UnityEngine.CoreModule.dll"),transform.Get(),sca);
        bool on=true;void* args[]{&on};Invoke(Method(object.Get(),"SetActive",1),object.Get(),args);
    }
    inventoryPanel.Set(panel);
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
void Interactions(void* actor){
    if(items::IsInspectingItem()||PanelOpen())return;
    const Vec3 player=Position(actor);int calls=0;
    for(auto* setting:{&chests,&shrines})if(setting->value){
        // Offline zones can contain the local visuals without an active Sync
        // component. Both paths invoke the same normal, validated handler.
        const char* visual=setting==&chests?"ChestVisualsCreator":"ShrineVisualsCreator";
        const char* sync=setting==&chests?"ChestSync":"ShrineSync";
        for(const char* cls:{visual,sync})for(void* object:Find("LE.dll","",cls)){
            if(calls>=1||!game::IsAlive(object)||!Near(object,player))continue;
            void* handler=Get<void*>(object,F(cls,"conditionHandler"));
            if(!handler||Value<bool>(Method(handler,"triggeredAndCannotTriggerMultipleTimes",0),handler))continue;
            Root playerObject(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),actor));
            void* a[]{playerObject.Get()};if(!Value<bool>(Method(handler,"CheckConditionReadiness",1),handler,a))continue;
            Invoke(Method(handler,"TryTriggerInteraction",1),handler,a);++calls;
            if(setting==&chests)++openedChests;else ++usedShrines;
        }
    }
    if(portals.value && GetTickCount64()>=portalGrace && calls==0){
        for(void* portal:Find("LE.dll","Networking.Multiplayer.Interactables.Portals","PortalSync")){
            if(!game::IsAlive(portal)||!Get<bool>(portal,F("PortalSync","initialized","Networking.Multiplayer.Interactables.Portals"))||!Near(portal,player))continue;
            Invoke(Method(portal,"SendAttemptUse",0),portal);++usedPortals;
            // Returning or arriving near another portal must not loop through it.
            // Each transition consumes the opt-in; the player rearms explicitly.
            portals.value=0;portalGrace=GetTickCount64()+10000;break;
        }
        if(portals.value)for(void* cave:Find("LE.dll","","CaveEntranceLogic")){
            void* interaction=Get<void*>(cave,F("CaveEntranceLogic","usePlayerSpawnInteraction"));
            if(!interaction||!game::IsAlive(interaction)||!Near(interaction,player)||Get<bool>(interaction,F("UsePlayerSpawnInteraction","startedAsyncInteraction")))continue;
            void* handler=Get<void*>(interaction,F("BaseInteraction","conditionHandler"));
            if(!handler||Value<bool>(Method(handler,"triggeredAndCannotTriggerMultipleTimes",0),handler))continue;
            Root playerObject(Invoke(M("Component","get_gameObject",0,"UnityEngine","UnityEngine.CoreModule.dll"),actor));void* args[]{playerObject.Get()};
            if(!Value<bool>(Method(handler,"CheckConditionReadiness",1),handler,args))continue;
            Invoke(Method(handler,"TryTriggerInteraction",1),handler,args);++usedPortals;
            portals.value=0;portalGrace=GetTickCount64()+10000;break;
        }
    }
}
void Frame(void* self,MethodInfo info){
    originalFrame(self,info);
    if(lifecycle::Stopping())return;
    const auto clock=GetTickCount64();
    if(clock>=lastButtons+1000){lastButtons=clock;std::string why;
        if(!game::Guarded([]{if(Actor())InventoryButtons();else if(!shortcuts.empty())ClearShortcuts();},&why)){
            ++faults;lastButtons=clock+14000;Log("inventory buttons: %s",why.c_str());
            game::Guarded([]{ClearShortcuts();},nullptr);
        }
    }
    if(!active.load(std::memory_order_relaxed))return;
    const auto now=GetTickCount64();if(now-lastScan<1000)return;lastScan=now;
    std::string why;
    if(!game::Guarded([&]{
        void* actor=Actor();
        if(!actor|| (owner.Get()&&owner.Get()!=actor)){
            RestoreGraphics();for(auto* setting:settings)setting->value=0;owner.Reset();active=false;return;
        }
        if(!owner.Get()){owner.Set(actor);portalGrace=now+10000;}
        Graphics();if(chests.value||shrines.value||portals.value)Interactions(actor);
    },&why)){++faults;Log("convenience: %s",why.c_str());}
}
std::string State(){std::ostringstream out;out<<"{\"ok\":true,\"settings\":{";bool comma=false;for(auto* s:settings){if(comma)out<<',';comma=true;out<<Json(s->key)<<":{\"value\":"<<s->value<<",\"available\":"<<Boolean(frame&&hook::IsInstalled(frame.code))<<'}';}
    return out.str()+"},\"inventoryButtons\":"+std::to_string(shortcuts.size())+",\"graphicsOverrides\":"+std::to_string(saved.size())+",\"chestsOpened\":"+std::to_string(openedChests)+",\"shrinesUsed\":"+std::to_string(usedShrines)+",\"portalsUsed\":"+std::to_string(usedPortals)+",\"faults\":"+std::to_string(faults)+"}";}
}
bool Init(){frame=game::FindMethod("LE.dll","","CameraManager","Update",0);
    findObjects=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Object","FindObjectsOfType",2);
    componentTransform=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Component","get_transform",0);
    transformPosition=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Transform","get_position",0);
    if(!(frame&&findObjects&&componentTransform&&transformPosition))return false;
    std::string why;
    if(!mainthread::Post([]{std::string reason;if(!hook::Install(frame.code,reinterpret_cast<void*>(&Frame),reinterpret_cast<void**>(&originalFrame),&reason))Log("convenience frame hook refused: %s",reason.c_str());},&why)){Log("convenience bootstrap refused: %s",why.c_str());return false;}
    return true;
}
std::string Read(){return Run([]{return State();});}
std::string Set(const std::string& key,double value){return Run([&]{
    auto it=std::find_if(settings.begin(),settings.end(),[&](Setting* s){return key==s->key;});
    if(it==settings.end()||!std::isfinite(value)||(value!=0&&value!=1))throw std::runtime_error("Unknown convenience setting or invalid switch.");
    Current();if(!frame)throw std::runtime_error("Camera update entry point is unavailable.");
    if((key=="fog"||key=="reflections")&&value){auto enabled=M("Behaviour","set_enabled",1,"UnityEngine","UnityEngine.CoreModule.dll");std::string reason;
        if(!hook::IsInstalled(enabled.code)&&!hook::Install(enabled.code,reinterpret_cast<void*>(&Enabled),reinterpret_cast<void**>(&originalEnabled),&reason))throw std::runtime_error(reason);}
    std::string why;if(!hook::IsInstalled(frame.code)&&!hook::Install(frame.code,reinterpret_cast<void*>(&Frame),reinterpret_cast<void**>(&originalFrame),&why))throw std::runtime_error(why);
    const double previous=(*it)->value;
    (*it)->value=value;active=true;lastScan=0;
    try {Graphics();}
    catch(...){(*it)->value=previous;try{Graphics();}catch(...){++faults;}throw;}
    return State();
});}
std::string Summons(){return Run([]{
    const auto c=Current();void* tracker=Get<void*>(c.actor,F("Actor","summonTracker"));
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
        void* a[]{&property,&tags,&special,&extra};out<<Value<float>(M("Stats","GetTotalIncreased",4),stats,a)*100.<<'}';
    }}
    return out.str()+"]}";
});}
std::string InventoryRead(){return Run([]{const auto c=Current();void* inventory=Get<void*>(c.manager,F("ItemContainersManager","inventory"));
    Root content(Invoke(Method(inventory,"GetContent",0),inventory));
    return "{\"ok\":true,\"player\":"+Player(c)+",\"inventoryItems\":"+std::to_string(Entries(content.Get()).size())+"}";
});}
std::string Inventory(const std::string& id,const std::string& action){
    if(action=="stash"||action=="vendor")return Run([&]{const auto c=Current(id);void* ui=Ui();
        if(action=="stash"){bool show=true,focus=true,clear=false,preserve=false;void* a[]{&show,&focus,&clear,nullptr,&preserve};Invoke(Method(ui,"openStash",5),ui,a);}
        else {bool show=true,appearances=false,stash=false,sound=false;int tab=0;
            void* inventoryArgs[]{&show,&appearances,&stash,&tab,&sound};Invoke(Method(ui,"openInventory",5),ui,inventoryArgs);
            void* a[]{&show};Invoke(Method(ui,"openShop",1),ui,a);}
        return "{\"ok\":true,\"player\":"+Player(c)+",\"action\":"+Json(action)+"}";});
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
    if(action=="inventory-open"){bool show=true,appearances=false,stash=false,last=false;int tab=0;void* a[]{&show,&appearances,&stash,&tab,&last};Invoke(Method(ui,"openInventory",5),ui,a);return State();}
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
    if(action=="summon-cast"){
        uint16_t abilityId=379;void* a[]{&abilityId};Root ability(Invoke(Signature("Ability","getAbility",{"System.UInt16"},"","LE.dll"),nullptr,a));
        if(!ability.Get())throw std::runtime_error("Ballista ability unavailable.");
        void* constructor=Get<void*>(c.actor,F("Actor","abilityObjectConstructor"));Vec3 p=Position(c.actor),direction{0,0,1};p.x+=2;float delay=0;int type=1;bool mutate=true;void* args[]{constructor,&p,&direction,&delay,&type,&mutate,nullptr};Invoke(Method(ability.Get(),"CastAfterDelay",7),ability.Get(),args);
        return "{\"ok\":true,\"requested\":true}";
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
void ReleaseRoots(){disabledBehaviours.store(std::make_shared<const std::vector<DisabledBehaviour>>());shortcuts.clear();inventoryPanel.Reset();saved.clear();typeRoots.clear();owner.Reset();active=false;}
}
