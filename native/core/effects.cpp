// Native, click-through HUD. Reads the game's existing UI definitions/icons;
// never adds/removes ailments, invokes tooltips, changes target or saves data.
#include "effects.hpp"
#include "effect_rules.hpp"
#include "managed.hpp"
#include "lifecycle.hpp"
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
#include "stat_editor.hpp"
#endif
#include <atomic>
#include <map>
#include <memory>
#include <sstream>
#include <cstdio>
#include <limits>

namespace ep::effects {
namespace {
using namespace managed;
struct V2{float x,y;};struct V3{float x,y,z;};struct Color{float r,g,b,a;};struct Rect{float x,y,w,h;};
constexpr Color gold{.84f,.74f,.49f,1},ink{.035f,.075f,.055f,.95f},white{.94f,.92f,.84f,1},muted{.77f,.8f,.75f,1},harm{.88f,.59f,.47f,1},buff{.57f,.79f,.63f,1};
std::atomic<bool> enabled{false};bool ready=false;double showPlayer=1,showTarget=1,details=1,scale=1,rows=5;
uint64_t nextSample=0,updates=0,faults=0,discoveries=0;std::string status="disabled",lastError;
int boundScene=0;uint64_t maximumSampleMicros=0,steadySampleMicros=0;
std::string ownerId,ownerName;
float lineRatio=1.45f; // the HUD font's line height per point of size, measured in the game
game::Handle actorOwner,managerOwner,canvasOwner,actorInfo,targetOwner,fontOwner;
struct Definition{std::unique_ptr<Root> value;std::string name,description;};
std::map<void*,Definition> definitions;
struct Widget{
    game::Handle go,rect,component;bool visibilityKnown=false,shown=true,colorKnown=false;Color color{};
    float fontSize=-1,x=std::numeric_limits<float>::quiet_NaN(),y=x,width=x,height=x;
    Widget()=default;Widget(const Widget&)=delete;Widget& operator=(const Widget&)=delete;
    ~Widget(){go.Reset();rect.Reset();component.Reset();}
    Widget(Widget&& other) noexcept{*this=std::move(other);}
    Widget& operator=(Widget&& other) noexcept{
        if(this==&other)return *this;go.Reset();rect.Reset();component.Reset();
        std::swap(go,other.go);std::swap(rect,other.rect);std::swap(component,other.component);
        visibilityKnown=other.visibilityKnown;shown=other.shown;colorKnown=other.colorKnown;color=other.color;
        fontSize=other.fontSize;x=other.x;y=other.y;width=other.width;height=other.height;return *this;
    }
};
struct Row{Widget root,icon,name,description,time,stacks,track,fill;void* sprite=nullptr;std::string nameText,descriptionText,timeText,stacksText;};
struct Panel{Widget root,inner,header,overflow;std::vector<std::unique_ptr<Row>> items;rules::Layout layout{};bool shown=false;std::string heading,overflowText;};
Panel playerPanel,targetPanel;
std::vector<rules::Effect> playerEffects,targetEffects;
std::map<std::string,std::unique_ptr<Root>> typeRoots;
const il2cpp::Method* M(const char* image,const char* ns,const char* cls,const char* name,int count){
    static std::map<std::string,const il2cpp::Method*> methods;const std::string key=std::string(image)+":"+ns+":"+cls+":"+name+":"+std::to_string(count);
    if(auto it=methods.find(key);it!=methods.end())return it->second;
    auto m=game::FindMethod(image,ns,cls,name,count);if(!m)throw std::runtime_error("Effects HUD method unavailable: "+key);
    return methods.emplace(key,m.info).first->second;
}
const il2cpp::Method* U(const char* cls,const char* name,int n){return M("UnityEngine.CoreModule.dll","UnityEngine",cls,name,n);}
const il2cpp::Method* T(const char* name,int n){return M("Unity.TextMeshPro.dll","TMPro","TMP_Text",name,n);}
const il2cpp::Field* Field(void* o,const char* name){
    if(!o)throw std::runtime_error("Effects HUD object unavailable");
    static std::map<std::pair<const il2cpp::Class*,std::string>,const il2cpp::Field*> cache;
    const auto key=std::make_pair(il2cpp::api().object_get_class(o),std::string(name));if(auto it=cache.find(key);it!=cache.end())return it->second;
    for(auto* c=il2cpp::api().object_get_class(o);c;c=il2cpp::api().class_get_parent(c))
        if(auto* f=il2cpp::api().class_get_field_from_name(c,name))return cache.emplace(key,f).first->second;
    throw std::runtime_error(std::string("Effects HUD field unavailable: ")+name);
}
template<class V> V F(void* o,const char* name){return Get<V>(o,il2cpp::api().field_get_offset(Field(o,name)));}
const il2cpp::Method* Signature(const char* cls,const char* name,std::initializer_list<const char*> params,const char* ns="UnityEngine",const char* image="UnityEngine.CoreModule.dll"){
    static std::map<std::string,const il2cpp::Method*> cache;std::string key=std::string(image)+":"+ns+":"+cls+":"+name;for(auto p:params)key+=std::string(":")+p;
    if(auto it=cache.find(key);it!=cache.end())return it->second;
    auto& a=il2cpp::api();auto* c=game::FindClass(image,ns,cls);void* it=nullptr;
    if(c)while(auto* m=a.class_get_methods(c,&it)){
        if(std::string(a.method_get_name(m))!=name||a.method_get_param_count(m)!=params.size())continue;
        bool match=true;unsigned n=0;for(auto p:params){char* actual=a.type_get_name(a.method_get_param(m,n++));match&=actual&&std::string(actual)==p;if(actual)a.free(actual);}if(match)return cache.emplace(key,m).first->second;
    }throw std::runtime_error("Effects HUD overload unavailable: "+key);
}
void* Type(const char* image,const char* ns,const char* name){
    const std::string key=std::string(image)+":"+ns+":"+name;
    if(auto it=typeRoots.find(key);it!=typeRoots.end())return it->second->Get();
    auto* c=game::FindClass(image,ns,name);if(!c)throw std::runtime_error("Effects HUD type unavailable: "+key);
    return typeRoots.emplace(key,std::make_unique<Root>(il2cpp::api().type_get_object(il2cpp::api().class_get_type(c)))).first->second->Get();
}
void* Component(void* o,const char* image,const char* ns,const char* name,bool parent=false){
    void* type=Type(image,ns,name);if(parent){bool inactive=true;void* a[]{type,&inactive};return Invoke(Signature("Component","GetComponentInParent",{"System.Type","System.Boolean"}),o,a);}
    void* a[]{type};return Invoke(Signature("Component","GetComponent",{"System.Type"}),o,a);
}
void* Transform(void* go){return Invoke(U("GameObject","get_transform",0),go);}
void Active(Widget& w,bool value){if(w.visibilityKnown&&w.shown==value)return;if(void* go=w.go.Get()){void* a[]{&value};Invoke(U("GameObject","SetActive",1),go,a);w.visibilityKnown=true;w.shown=value;}}
void Position(Widget& w,float x,float y,float width,float height){
    if(w.x==x&&w.y==y&&w.width==width&&w.height==height)return;
    V2 pos{x,y},size{width,height};void* a[]{&pos},*b[]{&size};Invoke(U("RectTransform","set_anchoredPosition",1),w.rect.Get(),a);Invoke(U("RectTransform","set_sizeDelta",1),w.rect.Get(),b);w.x=x;w.y=y;w.width=width;w.height=height;
}
void Tint(void* graphic,Color c){void* a[]{&c};Invoke(M("UnityEngine.UI.dll","UnityEngine.UI","Graphic","set_color",1),graphic,a);}
void Colorize(Widget& w,Color c){if(w.colorKnown&&w.color.r==c.r&&w.color.g==c.g&&w.color.b==c.b&&w.color.a==c.a)return;Tint(w.component.Get(),c);w.color=c;w.colorKnown=true;}
Widget New(const char* name,void* parent,const char* component=nullptr,bool text=false){
    auto& a=il2cpp::api();auto* goClass=game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","GameObject"),*typeClass=game::FindClass("mscorlib.dll","System","Type");
    if(!goClass||!typeClass)throw std::runtime_error("Effects HUD constructor types unavailable");
    Root go(a.object_new(goClass)),nameString(a.string_new(name)),types(a.array_new(typeClass,1));
    void* rectType=Type("UnityEngine.CoreModule.dll","UnityEngine","RectTransform");
    // Type[] is a reference array. Use the runtime's managed field write barrier
    // via Array.SetValue instead of storing an untracked managed pointer.
    int index=0;void* element[]{rectType,&index};Invoke(Signature("Array","SetValue",{"System.Object","System.Int32"},"System","mscorlib.dll"),types.Get(),element);
    void* ctor[]{nameString.Get(),types.Get()};Invoke(Signature("GameObject",".ctor",{"System.String","System.Type[]"}),go.Get(),ctor);
    Widget w;w.go.Set(go.Get());w.rect.Set(Transform(go.Get()));bool world=false;void* p[]{parent,&world};Invoke(Signature("Transform","SetParent",{"UnityEngine.Transform","System.Boolean"}),w.rect.Get(),p);
    Root parentObject(Invoke(U("Component","get_gameObject",0),parent));int layer=Value<int>(U("GameObject","get_layer",0),parentObject.Get());void* la[]{&layer};Invoke(U("GameObject","set_layer",1),w.go.Get(),la);
    V2 anchor{0,1};V3 unit{1,1,1};void* anch[]{&anchor},*sc[]{&unit};for(const char* field:{"set_anchorMin","set_anchorMax","set_pivot"})Invoke(U("RectTransform",field,1),w.rect.Get(),anch);Invoke(U("Transform","set_localScale",1),w.rect.Get(),sc);
    if(component){void* ct=Type(text?"Unity.TextMeshPro.dll":"UnityEngine.UI.dll",text?"TMPro":"UnityEngine.UI",component);void* arg[]{ct};w.component.Set(Invoke(Signature("GameObject","AddComponent",{"System.Type"}),w.go.Get(),arg));
        bool no=false;void* off[]{&no};Invoke(M("UnityEngine.UI.dll","UnityEngine.UI","Graphic","set_raycastTarget",1),w.component.Get(),off);
        if(text){void* font=fontOwner.Get();void* fa[]{font};Invoke(T("set_font",1),w.component.Get(),fa);bool rich=false;void* ra[]{&rich};Invoke(T("set_richText",1),w.component.Get(),ra);int ellipsis=1;void* ea[]{&ellipsis};Invoke(T("set_overflowMode",1),w.component.Get(),ea);Tint(w.component.Get(),white);}
    }return w;
}
void Label(Widget& w,const std::string& value,std::string& previous,float fontSize,Color color){
    if(value!=previous){Root text(il2cpp::api().string_new(value.c_str()));void* a[]{text.Get()};Invoke(T("set_text",1),w.component.Get(),a);previous=value;}
    if(w.fontSize!=fontSize){void* size[]{&fontSize};Invoke(T("set_fontSize",1),w.component.Get(),size);w.fontSize=fontSize;}Colorize(w,color);
}
void Destroy(Panel& panel){if(void* go=panel.root.go.Get()){void* a[]{go};Invoke(U("Object","Destroy",1),nullptr,a);}panel=Panel{};}
void Clear(){Destroy(playerPanel);Destroy(targetPanel);playerEffects.clear();targetEffects.clear();definitions.clear();managerOwner.Reset();canvasOwner.Reset();actorInfo.Reset();targetOwner.Reset();fontOwner.Reset();boundScene=0;}
void Hide(){Active(playerPanel.root,false);Active(targetPanel.root,false);playerPanel.shown=targetPanel.shown=false;}
void* Player(){
    if(!game::IsOfflinePlay())return nullptr;auto s=SessionState();if(s.state!=4||s.transitioning)return nullptr;
    void* actor=Invoke(M("LE.dll","","PlayerFinder","getPlayerActor",0));if(!actor||!game::IsAlive(actor))return nullptr;
    void* tracker=F<void*>(actor,"characterDataTracker"),*data=tracker?F<void*>(tracker,"charData"):nullptr;
    if(!data||!Value<bool>(Method(data,"get_IsOffline",0),data))return nullptr;
    void* health=F<void*>(actor,"health");if(!health||F<float>(health,"currentHealth")<=0)return nullptr;
    return actor;
}
Definition& Describe(void* value){
    if(auto it=definitions.find(value);it!=definitions.end())return it->second;
    if(definitions.size()>=1024)definitions.clear(); // bounded metadata cache, no fixed icon catalog
    Definition d;d.value=std::make_unique<Root>(value);d.name=rules::Plain(Text(Invoke(Method(value,"getName",0),value)),120);d.description=rules::Plain(Text(Invoke(Method(value,"getDescription",0),value)),220);
    if(d.name.empty())d.name="Unnamed effect";return definitions.emplace(value,std::move(d)).first->second;
}
void ReadPlayer(void* manager){
    playerEffects.clear();for(const char* list:{"debuffs","buffs"}){
        auto entries=Entries(F<void*>(manager,list));if(entries.size()>rules::kMaxEffects)throw std::runtime_error("Effects HUD active entry count outside supported bounds");
        for(void* entry:entries){if(playerEffects.size()>=rules::kMaxEffects)break;if(!entry||!game::IsAlive(entry))continue;const int stacks=F<int>(entry,"stacks");if(stacks<=0)continue;
            void* def=F<void*>(entry,"uiBuff");if(!def)continue;auto& text=Describe(def);rules::Effect e;e.id=F<int>(entry,"id");e.ailment=F<bool>(entry,"isAilment");e.positive=F<bool>(entry,"isBuff");e.stacks=stacks;e.name=text.name;e.description=text.description;
            e.remaining=F<float>(entry,"maxRemainingDuration");e.duration=F<float>(entry,"maxDurationOfMaxRemainingDuration");e.timed=rules::Timed(!F<bool>(entry,"noFixedDuration"),F<bool>(entry,"ignoreRemainingDuration"),e.remaining,e.duration);
            if(void* image=F<void*>(entry,"content"))e.sprite=Invoke(M("UnityEngine.UI.dll","UnityEngine.UI","Image","get_sprite",0),image);
            playerEffects.push_back(std::move(e));if(playerEffects.size()>=rules::kMaxEffects)break;
        }
    }rules::Order(playerEffects);
}
void* DisplayActor(void* display){
    for(auto* c=il2cpp::api().object_get_class(display);c;c=il2cpp::api().class_get_parent(c))if(std::string(il2cpp::api().class_get_name(c))=="ActorDisplayInformation"){
        void* actor=F<void*>(display,"actor");if(actor&&game::IsAlive(actor))return actor;
        // Visuals and gameplay actors are separate GameObjects in the current
        // client. The legacy display.actor field may legitimately remain null.
        void* sync=F<void*>(display,"actorSync");
        if(!sync)if(void* visuals=F<void*>(display,"actorVisuals"))sync=F<void*>(visuals,"actorSync");
        if(sync){actor=Invoke(Method(sync,"get_GameplayActor",0),sync);if(actor&&game::IsAlive(actor))return actor;}
        break;
    }return Component(display,"LE.dll","","Actor",true);
}
void ReadTarget(){
    targetEffects.clear();void* info=actorInfo.Get();if(!info){targetOwner.Reset();return;}
    void* target=F<void*>(info,"actorOnMouse");if(!F<bool>(info,"displayingInfo")||!target||!game::IsAlive(target)){targetOwner.Reset();return;}
    if(targetOwner.Get()!=target){targetOwner.Set(target);targetPanel.heading.clear();}
    void* bar=F<void*>(info,"currentBar");if(!bar||!game::IsAlive(bar))return;
    void* dict=F<void*>(bar,"activeAilmentIcons");if(!dict)return;
    Root values(Invoke(Method(dict,"get_Values",0),dict));
    const int count=Value<int>(Method(values.Get(),"get_Count",0),values.Get());
    if(count<0||static_cast<size_t>(count)>rules::kMaxEffects)throw std::runtime_error("Effects HUD target icon count outside supported bounds");
    Root icons(il2cpp::api().array_new(game::FindClass("LE.dll","","UIActorInfoAilmentIcon"),static_cast<uintptr_t>(count)));int first=0;void* copy[]{icons.Get(),&first};Invoke(Method(values.Get(),"CopyTo",2),values.Get(),copy);
    for(int i=0;i<count;++i){
        void* icon=Get<void*>(icons.Get(),0x20+sizeof(void*)*i);if(!icon||!game::IsAlive(icon))continue;
        void* ailment=F<void*>(icon,"activeAilment");if(!ailment)continue;auto& d=Describe(ailment);rules::Effect e;e.id=F<int>(ailment,"id");e.positive=F<bool>(ailment,"positive");e.name=d.name;e.description=d.description;
        if(void* image=F<void*>(icon,"ailmentIcon"))e.sprite=Invoke(M("UnityEngine.UI.dll","UnityEngine.UI","Image","get_sprite",0),image);
        if(void* label=F<void*>(icon,"stackCount")){const auto text=rules::Plain(Text(Invoke(T("get_text",0),label)),20);e.stackLabel=text;try{e.stacks=std::stoi(text);}catch(...){e.stacks=1;}}else e.stacks=1;
        targetEffects.push_back(std::move(e));
    }
    // Target duration is measured only if an actual live receiver exists. Do
    // not reuse the definition's base duration as a remaining-duration timer.
    if(targetEffects.empty())return;
    void* actor=DisplayActor(target);
    void* receiver=actor?Component(actor,"LE.dll","","AilmentReceiver"):nullptr;
    if(receiver){
        void* counts=F<void*>(receiver,"ailmentStacks");if(counts)for(auto& e:targetEffects){int stacks=0;void* args[]{&e.id,&stacks};if(Value<bool>(Method(counts,"TryGetValue",2),counts,args)){e.stacks=stacks;e.stackLabel=stacks>1?std::to_string(stacks):"";}}
        auto live=Entries(F<void*>(receiver,"ailments"));if(live.size()<=512)for(void* a:live){if(!a)continue;void* def=F<void*>(a,"ailment");if(!def)continue;const int id=F<int>(def,"id");const float remaining=F<float>(a,"remainingDuration"),duration=F<float>(a,"totalDuration");
        if(!rules::Timed(true,false,remaining,duration))continue;for(auto& e:targetEffects)if(e.id==id&&(!e.timed||remaining>e.remaining)){e.remaining=remaining;e.duration=duration;e.timed=true;}
    }}rules::Order(targetEffects);
}
void Bind(void* manager){
    managerOwner.Set(manager);void* holder=F<void*>(manager,"buffHolder");
    void* canvas=Component(holder,"UnityEngine.UIModule.dll","UnityEngine","Canvas",true);if(!canvas)throw std::runtime_error("Effects HUD canvas unavailable");
    void* root=Invoke(M("UnityEngine.UIModule.dll","UnityEngine","Canvas","get_rootCanvas",0),canvas);canvasOwner.Set(Invoke(U("Component","get_transform",0),root));
    void* prefab=F<void*>(manager,"buffEntryPrefab");void* type=Type("LE.dll","","BuffUIEntry");void* a[]{type};Root entry(Invoke(Signature("GameObject","GetComponent",{"System.Type"}),prefab,a));
    void* label=F<void*>(entry.Get(),"timeLabel");fontOwner.Set(Invoke(T("get_font",0),label));if(!fontOwner.Get())throw std::runtime_error("Effects HUD game font unavailable");
    // One bounded scene discovery per new UI owner. Never repeated per timer tick.
    bool inactive=true;void* args[]{Type("LE.dll","","UIActorInfo"),&inactive};Root found(Invoke(Signature("Object","FindObjectsOfType",{"System.Type","System.Boolean"}),nullptr,args));++discoveries;
    const auto n=Get<uintptr_t>(found.Get(),0x18);if(n>32)throw std::runtime_error("Effects HUD actor info count outside supported bounds");
    if(n)actorInfo.Set(Get<void*>(found.Get(),0x20));
}
void BuildPanel(Panel& p,const char* name){
    p.root=New(name,canvasOwner.Get(),"Image");Tint(p.root.component.Get(),gold);Active(p.root,false);
    p.inner=New("Background",p.root.rect.Get(),"Image");Tint(p.inner.component.Get(),ink);
    p.header=New("Title",p.root.rect.Get(),"TextMeshProUGUI",true);
    p.overflow=New("More effects",p.root.rect.Get(),"TextMeshProUGUI",true);
    // No EventSystem components; every Graphic is explicitly non-raycastable.
    // One line of the font these labels really use (a chosen interface font can
    // differ from the game's), from the font's own face data, plus a small margin.
    if(void* font=Invoke(T("get_font",0),p.header.component.Get())){
        Root face(Invoke(Method(font,"get_faceInfo",0),font));
        const float point=F<float>(face.Get(),"m_PointSize"),unit=F<float>(face.Get(),"m_Scale"),line=F<float>(face.Get(),"m_LineHeight");
        lineRatio=rules::LineRatio(point>0?line/point*(unit>0?unit:1.f)*1.05f:0.f);
    }
}
std::unique_ptr<Row> BuildRow(Panel& p){
    auto row=std::make_unique<Row>();row->root=New("Effect",p.root.rect.Get());
    row->icon=New("Game icon",row->root.rect.Get(),"Image");row->name=New("Name",row->root.rect.Get(),"TextMeshProUGUI",true);row->description=New("Description",row->root.rect.Get(),"TextMeshProUGUI",true);
    row->time=New("Remaining",row->root.rect.Get(),"TextMeshProUGUI",true);row->stacks=New("Stacks",row->root.rect.Get(),"TextMeshProUGUI",true);
    bool wrap=true;void* wa[]{&wrap};Invoke(T("set_enableWordWrapping",1),row->description.component.Get(),wa);
    row->track=New("Timer track",row->root.rect.Get(),"Image");Tint(row->track.component.Get(),Color{.15f,.2f,.17f,1});row->fill=New("Timer",row->track.rect.Get(),"Image");return row;
}
void Render(Panel& p,const std::vector<rules::Effect>& effects,bool target,Rect canvas){
    const auto layout=rules::Place(canvas.w,canvas.h,scale,static_cast<size_t>(rows),target,details!=0,lineRatio);
    const bool show=layout.visible&&!effects.empty()&&(target?showTarget:showPlayer)!=0;
    if(!show){Active(p.root,false);p.shown=false;return;}
    if(!p.root.go.Get())BuildPanel(p,target?"EpochPact Target Effects":"EpochPact Player Effects");
    const size_t desired=std::min(effects.size(),layout.rows);
    // Create at most one row per panel per sample, then reuse it indefinitely.
    if(p.items.size()<desired)p.items.push_back(BuildRow(p));
    const size_t visible=std::min(desired,p.items.size());const float s=layout.scale,height=layout.chrome+static_cast<float>(visible)*layout.row;
    const float timeWidth=std::max(50*s,3.5f*layout.timeSize),timeX=layout.width-20*s-timeWidth;
    Position(p.root,layout.x,layout.y,layout.width,height);Position(p.inner,1,-1,layout.width-2,height-2);Position(p.header,10*s,-4*s,layout.width-20*s,layout.titleHeight);
    std::string title="ON YOU";if(target&&targetOwner.Get())title=rules::Plain(Text(Invoke(Method(targetOwner.Get(),"GetLocalizedName",0),targetOwner.Get())),100);
    Label(p.header,title,p.heading,layout.titleSize,gold);
    for(size_t i=0;i<p.items.size();++i){auto& r=*p.items[i];Active(r.root,i<visible);if(i>=visible)continue;const auto& e=effects[i];const Color tone=e.positive?buff:harm;
        Position(r.root,8*s,-(layout.top+static_cast<float>(i)*layout.row),layout.width-16*s,layout.row);Position(r.icon,0,-1*s,layout.icon,layout.icon);
        if(r.sprite!=e.sprite){void* a[]{e.sprite};Invoke(M("UnityEngine.UI.dll","UnityEngine.UI","Image","set_sprite",1),r.icon.component.Get(),a);r.sprite=e.sprite;}
        Position(r.name,40*s,0,timeX-44*s,layout.nameHeight);Label(r.name,e.name,r.nameText,layout.nameSize,white);
        Position(r.description,40*s,-layout.nameHeight,layout.width-60*s,std::max(1.f,layout.descriptionHeight));Active(r.description,details!=0);Label(r.description,e.description.empty()?"Description unavailable":e.description,r.descriptionText,layout.descriptionSize,muted);
        char time[40];if(e.timed)std::snprintf(time,sizeof time,"%.1fs",e.remaining);else std::snprintf(time,sizeof time,"—");
        Position(r.time,timeX,0,timeWidth,layout.timeHeight);Label(r.time,time,r.timeText,layout.timeSize,e.timed&&e.remaining<=2?gold:white);
        Position(r.stacks,17*s,-(layout.icon-layout.stackHeight),std::max(23*s,2.2f*layout.stackSize),layout.stackHeight);Label(r.stacks,e.stackLabel.empty()?(e.stacks>1?std::to_string(e.stacks):""):e.stackLabel,r.stacksText,layout.stackSize,gold);
        const float width=layout.width-62*s;Position(r.track,40*s,-(layout.row-7*s),width,2*s);Active(r.track,e.timed);Position(r.fill,0,0,width*rules::Fraction(e.remaining,e.duration),2*s);Colorize(r.fill,tone);
    }
    Position(p.overflow,10*s,-(height-layout.footerHeight-3*s),layout.width-20*s,layout.footerHeight);Label(p.overflow,effects.size()>visible?"+"+std::to_string(effects.size()-visible)+" more active effects":"",p.overflowText,layout.footerSize,muted);
    Active(p.root,true);p.shown=true;p.layout=layout;
}
void Sample(){
    void* actor=Player();if(!actor){Hide();playerEffects.clear();targetEffects.clear();targetOwner.Reset();status="waiting-for-offline-character";return;}
    void* data=F<void*>(F<void*>(actor,"characterDataTracker"),"charData");const auto id=Text(Invoke(Method(data,"get_Id",0),data)),name=Text(F<void*>(data,"<CharacterName>k__BackingField"));
    if(!ownerId.empty()&&(ownerId!=id||ownerName!=name)){Clear();actorOwner.Reset();ownerId.clear();ownerName.clear();enabled=false;status="character-changed";return;}
    if(ownerId.empty()){ownerId=id;ownerName=name;}if(actorOwner.Get()!=actor)actorOwner.Set(actor);
    void* manager=game::StaticObject(game::FindStaticField("LE.dll","","BuffsUIManager","instance"));if(!manager||!game::IsAlive(manager)){Hide();status="waiting-for-game-ui";return;}
    const int scene=Value<int>(M("UnityEngine.CoreModule.dll","UnityEngine.SceneManagement","SceneManager","GetActiveScene",0));
    if(managerOwner.Get()!=manager||!canvasOwner.Get()||boundScene!=scene){Clear();Bind(manager);boundScene=scene;}
    void* ui=game::StaticObject(game::FindStaticField("LE.dll","","UIBase","instance"));if(!ui||!F<bool>(ui,"uiEnabled")){Hide();status="game-ui-hidden";return;}
    if(Value<bool>(Method(ui,"get_menuOpen",0),ui)||Value<bool>(Method(ui,"IsWorldMapPanelOpen",0),ui)){Hide();status="menu-open";return;}
    ReadPlayer(manager);ReadTarget();const Rect rect=Value<Rect>(U("RectTransform","get_rect",0),canvasOwner.Get());Render(playerPanel,playerEffects,false,rect);Render(targetPanel,targetEffects,true,rect);++updates;status="active";
}
std::string State(){std::ostringstream out;out<<"{\"ok\":true,\"available\":"<<Boolean(ready)<<",\"settings\":{";
    const std::pair<const char*,double> values[]{{"enabled",enabled?1.:0.},{"player",showPlayer},{"target",showTarget},{"descriptions",details},{"scale",scale},{"rows",rows}};bool comma=false;
    for(auto [key,value]:values){if(comma)out<<',';comma=true;out<<Json(key)<<":{\"value\":"<<value<<",\"available\":"<<Boolean(ready)<<'}';}
    out<<"},\"status\":"<<Json(status)<<",\"playerEffects\":"<<playerEffects.size()<<",\"targetEffects\":"<<targetEffects.size()<<",\"updates\":"<<updates<<",\"discoveries\":"<<discoveries<<",\"lineRatio\":"<<lineRatio<<",\"maximumSampleMicros\":"<<maximumSampleMicros<<",\"steadySampleMicros\":"<<steadySampleMicros<<",\"faults\":"<<faults<<",\"error\":"<<Json(lastError)<<",\"nativeIcons\":true,\"readOnly\":true}";return out.str();
}
}
bool Init(){
    ready=game::FindStaticField("LE.dll","","BuffsUIManager","instance")&&game::FindClass("LE.dll","","BuffUIEntry")&&game::FindClass("LE.dll","","UIActorInfo");
#ifdef EPOCHPACT_TESTING
    // Test-only convenience for IPC checks behind another window; no keyboard
    // or mouse input is sent and no persisted game preference is changed.
    mainthread::Post([]{bool on=true;void* a[]{&on};Invoke(U("Application","set_runInBackground",1),nullptr,a);});
#endif
    return ready;
}
void Frame(){if(!enabled.load(std::memory_order_relaxed)||lifecycle::Stopping())return;const auto now=GetTickCount64();if(now<nextSample)return;nextSample=now+250;std::string why,managedWhy;bool completed=true;
    LARGE_INTEGER before{},after{},frequency{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&before);
    const size_t beforeRows=playerPanel.items.size()+targetPanel.items.size();const auto beforeDiscovery=discoveries;
    const bool safe=game::Guarded([&]{try{Sample();}catch(const std::exception& e){managedWhy=e.what();completed=false;}},&why);
    QueryPerformanceCounter(&after);const auto micros=static_cast<uint64_t>((after.QuadPart-before.QuadPart)*1000000/frequency.QuadPart);maximumSampleMicros=std::max(maximumSampleMicros,micros);
    if(beforeRows==playerPanel.items.size()+targetPanel.items.size()&&beforeDiscovery==discoveries)steadySampleMicros=std::max(steadySampleMicros,micros);
    if(!safe||!completed){if(!managedWhy.empty())why=managedWhy;++faults;lastError=why;status="data-unavailable";enabled=false;game::Guarded([]{Clear();},nullptr);actorOwner.Reset();ownerId.clear();ownerName.clear();Log("effects HUD stopped: %s",why.c_str());}
}
std::string Read(){return Run([]{return State();});}
std::string Set(const std::string& key,double value){return Run([&]{
    if(!std::isfinite(value))throw std::runtime_error("Invalid effects HUD setting");
    const bool toggle=key=="enabled"||key=="player"||key=="target"||key=="descriptions";
    if((toggle&&value!=0&&value!=1)||(key=="scale"&&(value<.7||value>1.5))||(key=="rows"&&(value<1||value>12||std::floor(value)!=value))||(!toggle&&key!="scale"&&key!="rows"))throw std::runtime_error("Invalid effects HUD setting");
    if(key=="enabled"&&value==0){enabled=false;Clear();actorOwner.Reset();ownerId.clear();ownerName.clear();status="disabled";return State();}
    if(!ready||!Player())throw std::runtime_error("Load an offline character and finish loading the area first");
    if(key=="enabled"){
        enabled=true;nextSample=0;lastError.clear();status="starting";
        try{Sample();}catch(const std::exception& e){enabled=false;lastError=e.what();status="data-unavailable";Log("effects HUD start refused: %s",lastError.c_str());Clear();actorOwner.Reset();ownerId.clear();ownerName.clear();throw std::runtime_error("The effect HUD could not start in this game build. Technical details are in the local core log.");}
    }
    else if(key=="player")showPlayer=value;else if(key=="target")showTarget=value;else if(key=="descriptions")details=value;else if(key=="scale")scale=value;else if(key=="rows")rows=value;
    return State();
});}
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Test(const std::string& expectedId,const std::string& action){
    if(action.rfind("visit-",0)==0){
        const auto allowed=Run([&]{void* actor=Player();if(!actor)throw std::runtime_error("Offline test character unavailable");void* data=F<void*>(F<void*>(actor,"characterDataTracker"),"charData");if(Text(Invoke(Method(data,"get_Id",0),data))!=expectedId||Text(F<void*>(data,"<CharacterName>k__BackingField"))!="EpEffectsTest")throw std::runtime_error("Isolated test identity required");return std::string("{\"ok\":true}");});
        if(allowed!="{\"ok\":true}")return allowed;const auto result=statedit::Waypoints(action.substr(6),expectedId);return "{\"ok\":"+std::string(Boolean(result.rfind("travel: normal waypoint transition requested",0)==0))+",\"result\":"+Json(result)+"}";
    }
    return Run([&]{
    void* actor=Player();if(!actor)throw std::runtime_error("Offline test character unavailable");
    void* data=F<void*>(F<void*>(actor,"characterDataTracker"),"charData");
    const auto id=Text(Invoke(Method(data,"get_Id",0),data)),name=Text(F<void*>(data,"<CharacterName>k__BackingField"));
    if(id!=expectedId||name!="EpEffectsTest")throw std::runtime_error("Effect checks require the isolated EpEffectsTest character");
    void* manager=game::StaticObject(game::FindStaticField("LE.dll","","BuffsUIManager","instance"));if(!manager)throw std::runtime_error("Buff UI unavailable");
    if(action=="map-close"){void* ui=game::StaticObject(game::FindStaticField("LE.dll","","UIBase","instance"));Invoke(Method(ui,"closeMap",0),ui);return std::string("{\"ok\":true}");}
    if(action=="inventory-open"||action=="inventory-close"){
        // A game menu over the HUD: the panels must hide while it is open.
        void* ui=game::StaticObject(game::FindStaticField("LE.dll","","UIBase","instance"));if(!ui)throw std::runtime_error("Game UI unavailable");
        if(action=="inventory-open"){bool show=true,appearances=false,stash=false,last=false;int tab=0;void* a[]{&show,&appearances,&stash,&tab,&last};Invoke(Method(ui,"openInventory",5),ui,a);}
        else Invoke(Method(ui,"closeInventory",0),ui);
        return std::string("{\"ok\":true,\"menuOpen\":")+Boolean(Value<bool>(Method(ui,"get_menuOpen",0),ui))+"}";
    }
    if(action=="target-self"){
        bool inactive=false;void* args[]{Type("LE.dll","","ActorDisplayInformation"),&inactive};Root found(Invoke(Signature("Object","FindObjectsOfType",{"System.Type","System.Boolean"}),nullptr,args));
        const auto count=Get<uintptr_t>(found.Get(),0x18);if(count>2048)throw std::runtime_error("Test display count outside bounds");void* candidate=nullptr;void* sourceActor=nullptr;
        for(uintptr_t i=0;i<count;++i){void* display=Get<void*>(found.Get(),0x20+8*i);if(!display||!game::IsAlive(display))continue;void* a=DisplayActor(display);if(a&&game::IsAlive(a)&&F<void*>(a,"ailmentReceiver")){candidate=display;sourceActor=a;break;}}
        if(!candidate)throw std::runtime_error("No native actor display in this area; use an isolated combat area");
        void* receiver=F<void*>(sourceActor,"ailmentReceiver");for(int aid:{3,6}){void* lookup[]{&aid};Root ailment(Invoke(Signature("Ailment","getAilment",{"System.Int32"},"","LE.dll"),nullptr,lookup));int stacks=3;float increased=9;bool conversions=false;void* a[]{ailment.Get(),nullptr,&stacks,&increased,actor,&conversions};Invoke(Method(receiver,"SelfInflictAilment",6),receiver,a);}
        void* alignment=Invoke(Method(sourceActor,"GetAlignment",0),sourceActor),*vfx=F<void*>(candidate,"ailmentVFXCreator");bool replace=true;void* mouse[]{candidate,alignment,&replace,vfx};Invoke(M("LE.dll","","UIActorInfo","NotifyMouseOver",4),nullptr,mouse);
        void* bar=F<void*>(actorInfo.Get(),"currentBar");if(!bar)throw std::runtime_error("Target bar unavailable");
        void* dict=F<void*>(bar,"activeAilmentIcons");for(int aid:{1,3,6}){void* key[]{&aid};if(Value<bool>(Method(dict,"ContainsKey",1),dict,key))continue;int stacks=3;void* icon[]{&aid,&stacks};Invoke(Method(bar,"setupNewAilmentIcon",2),bar,icon);}
        return std::string("{\"ok\":true,\"source\":\"native target UI fixture using a scene actor's actual receiver\"}");
    }
    if(action=="actual-ailments"){
        void* receiver=F<void*>(actor,"ailmentReceiver");
        for(int aid:{3,6,33,34}){void* lookup[]{&aid};Root ailment(Invoke(Signature("Ailment","getAilment",{"System.Int32"},"","LE.dll"),nullptr,lookup));
            int stacks=3;float increased=9;bool conversions=false;void* args[]{ailment.Get(),nullptr,&stacks,&increased,actor,&conversions};Invoke(Method(receiver,"SelfInflictAilment",6),receiver,args);
        }return std::string("{\"ok\":true,\"source\":\"actual AilmentReceiver effects on isolated character\"}");
    }
    if(action=="many-ui"){
        for(int aid:{1,2,3,6,7,16,33,34,60}){float duration=60;unsigned char stacks=4;bool ignore=false;void* args[]{&aid,&duration,&stacks,&ignore};Invoke(Method(manager,"AddAilmentStacks",4),manager,args);}
        int buffId=9;float duration=60;unsigned char stacks=6;bool ignore=false;void* args[]{&buffId,&duration,&stacks,&ignore};Invoke(Method(manager,"AddNonAilmentStacks",4),manager,args);
        return std::string("{\"ok\":true,\"source\":\"synthetic native UI entries, real game definitions and sprites\"}");
    }
    if(action=="target-clear"){Invoke(M("LE.dll","","UIActorInfo","RequestClearActorOnMouse",0));return std::string("{\"ok\":true}");}
    if(action!="read")throw std::runtime_error("Unknown isolated effect check");
    std::ostringstream out;auto base=State();out<<base.substr(0,base.size()-1)<<",\"effects\":[";bool comma=false;
    for(const auto& e:playerEffects){if(comma)out<<',';comma=true;out<<"{\"id\":"<<e.id<<",\"ailment\":"<<Boolean(e.ailment)<<",\"name\":"<<Json(e.name)<<",\"description\":"<<Json(e.description)<<",\"timed\":"<<Boolean(e.timed)<<",\"remaining\":"<<(e.timed?e.remaining:0)<<",\"stacks\":"<<e.stacks<<",\"icon\":"<<Boolean(e.sprite!=nullptr)<<'}';}
    out<<"],\"target\":[";comma=false;for(const auto& e:targetEffects){if(comma)out<<',';comma=true;out<<"{\"id\":"<<e.id<<",\"name\":"<<Json(e.name)<<",\"timed\":"<<Boolean(e.timed)<<",\"remaining\":"<<(e.timed?e.remaining:0)<<",\"stacks\":"<<e.stacks<<",\"icon\":"<<Boolean(e.sprite!=nullptr)<<'}';}
    out<<"],\"panels\":[";comma=false;for(auto* p:{&playerPanel,&targetPanel}){if(comma)out<<',';comma=true;out<<"{\"visible\":"<<Boolean(p->shown)<<",\"rows\":"<<p->items.size()<<",\"raycasts\":false,\"width\":"<<(p->shown?p->root.width:0)<<",\"height\":"<<(p->shown?p->root.height:0)<<",\"footer\":"<<Json(p->overflowText)<<'}';for(auto& row:p->items)for(auto* widget:{&row->icon,&row->name,&row->description,&row->time,&row->stacks,&row->track,&row->fill})if(void* graphic=widget->component.Get())if(Value<bool>(M("UnityEngine.UI.dll","UnityEngine.UI","Graphic","get_raycastTarget",0),graphic))throw std::runtime_error("Effect row captures clicks");}
    out<<"]}";return out.str();
});}
#endif
// Runtime shutdown has already stopped frames. Do not call Unity Destroy here.
void ReleaseRoots(){enabled=false;playerPanel=Panel{};targetPanel=Panel{};definitions.clear();typeRoots.clear();actorOwner.Reset();managerOwner.Reset();canvasOwner.Reset();actorInfo.Reset();targetOwner.Reset();fontOwner.Reset();ownerId.clear();ownerName.clear();}
}
