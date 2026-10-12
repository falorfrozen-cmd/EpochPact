// Reveal the minimap/overlay by substituting its fog shader input only.
// The real FoW texture, explored volumes, quests and scene objects are untouched.
#include "map_view.hpp"
#include "managed.hpp"
#include "hook.hpp"
#include "mainthread.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace ep::mapview {
namespace {
using namespace managed;
bool ready=false;
std::atomic<bool> enabled{false};
std::atomic<uint64_t> bindings{0}, faults{0};
const il2cpp::Field *activeMap=nullptr,*fogShader=nullptr;
size_t fogOffset=0;
game::MethodRef setTexture,getTexture,blackTexture,mapLoaded,mapUpdate;
using LoadedFn=void (*)(void*,void*,void*,const il2cpp::Method*);
using UpdateFn=void (*)(void*,const il2cpp::Method*);
LoadedFn originalLoaded=nullptr; UpdateFn originalUpdate=nullptr;
bool substituted=false;
bool MapCustomized();
void ApplyMap();
int ShaderId() {int id=0;il2cpp::api().field_static_get_value(fogShader,&id);return id;}
void Bind(bool reveal,void* loadedMap=nullptr) {
    void* map=loadedMap?loadedMap:game::StaticObject(activeMap);
    if(!map) {substituted=false;return;}
    void* nativeFog=Get<void*>(map,fogOffset);
    if(!nativeFog || !game::IsAlive(nativeFog)) {substituted=false;return;}
    void* texture=reveal?Invoke(blackTexture):nativeFog;
    int id=ShaderId(); void* args[]{&id,texture}; Invoke(setTexture,nullptr,args);
    substituted=reveal; ++bindings;
}
void CheckedBind(bool reveal,void* loadedMap=nullptr) {
    std::string why;
    if(!game::Guarded([=]{Bind(reveal,loadedMap);},&why)) {substituted=false;++faults;Log("map view: %s",why.c_str());}
}
void Loaded(void* self,void* map,void* scene,const il2cpp::Method* method) {
    originalLoaded(self,map,scene,method);
    // Only map-load events rebind; no scene enumeration or fog painting loop.
    // The event is raised before MinimapManager assigns activeMinimap. Bind the
    // exact incoming map, not a stale/null global from the preceding scene.
    if(enabled.load() && game::IsOfflinePlay()) CheckedBind(true,map);
    else substituted=false;
    if(MapCustomized()&&game::IsOfflinePlay())
        mainthread::Post([]{if(!MapCustomized()||!game::IsOfflinePlay())return;std::string why;if(!game::Guarded([]{ApplyMap();},&why)){++faults;Log("map settings: %s",why.c_str());}});
}
void Update(void* self,const il2cpp::Method* method) {
    // One inexpensive gate check while enabled. An online transition restores
    // the current game's own texture immediately, without touching that texture.
    if(substituted && !game::IsOfflinePlay()) CheckedBind(false);
    originalUpdate(self,method);
}
// ---- minimap and overlay display settings (session only; never saved to the
// game's own settings). Values are applied to the game's map configurations
// and re-applied with DMMap.ReloadConfig.
struct Vec3{float x,y,z;};
struct Vec2{float x,y;};
struct MapOriginal{bool captured=false;float minimapZoom=0,minimapOpacity=0,fullZoom=0,fullOpacity=0,minZoom=0,maxZoom=0;Vec3 minimapScale{1,1,1};};
MapOriginal original;
// Minimap opacity is a CanvasGroup alpha on the minimap panel (frame, map and
// icons together). The quest tracker below the minimap moves with its bottom
// edge so a larger minimap does not cover it.
struct Group{std::unique_ptr<Root> group;bool added=false;float alpha=1;};
Group canvasGroup;
struct Tracker{std::unique_ptr<Root> transform;Vec3 local{0,0,0};};
Tracker tracker;
double minimapSize=1,minimapZoom=1,minimapOpacity=-1,overlayOpacity=-1,overlayZoom=1;
uint64_t mapApplies=0;
size_t MF(const char* cls,const char* field,const char* ns=""){return Offset(cls,field,ns);}
void* MapGlobal(){return game::StaticObject(game::FindStaticField("LE.dll","","DMMapGlobalConfigs","instance"));}
void* MapDmm(){return game::StaticObject(game::FindStaticField("LE.dll","DMM","DMMap","Instance"));}
void* GlobalConfig(void* global,const char* name){
    for(void* cfg:Entries(Get<void*>(global,MF("DMMapGlobalConfigs","configs"))))
        if(cfg&&Text(Get<void*>(cfg,MF("DMMapGlobalConfigs.config","name")))==name)return cfg;
    return nullptr;
}
void* DmmRect(void* dmm,const char* name){
    for(void* cfg:Entries(Get<void*>(dmm,MF("DMMap","configs","DMM"))))
        if(cfg&&Text(Get<void*>(cfg,MF("DMMapConfig","name","DMM")))==name)return Get<void*>(cfg,MF("DMMapConfig","uiPosition","DMM"));
    return nullptr;
}
// The corner minimap's frame, mask and map share the "Minimap" panel above the
// config rect (Minimap-RectTransform -> SquareMinimap -> Minimap). Scaling the
// panel keeps the map inside its frame; its pivot is the top-right corner.
void* MinimapPanel(void* rect){
    void* t=rect;
    for(int depth=0;depth<4&&t;++depth){
        Root go(Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Component","get_gameObject",0),t));
        if(Text(Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Object","get_name",0),go.Get()))=="Minimap")return t;
        t=Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Transform","get_parent",0),t);
    }
    return nullptr;
}
template<class T> void SetField(void* object,size_t offset,T value){*reinterpret_cast<T*>(static_cast<char*>(object)+offset)=value;}
bool MapCustomized(){return minimapSize!=1||minimapZoom!=1||minimapOpacity>=0||overlayOpacity>=0||overlayZoom!=1;}
const il2cpp::Method* TypedMethod(const char* image,const char* ns,const char* cls,const char* name,const char* parameter){
    static std::map<std::string,const il2cpp::Method*> found;
    const std::string key=std::string(cls)+"."+name+"("+parameter+")";
    if(auto it=found.find(key);it!=found.end())return it->second;
    auto& a=il2cpp::api();auto* c=game::FindClass(image,ns,cls);void* iterator=nullptr;
    while(auto* m=c?a.class_get_methods(c,&iterator):nullptr){
        if(std::string(a.method_get_name(m))!=name||a.method_get_param_count(m)!=1)continue;
        char* type=a.type_get_name(a.method_get_param(m,0));const bool match=type&&std::string(type)==parameter;if(type)a.free(type);
        if(match){found.emplace(key,m);return m;}}
    throw std::runtime_error("Missing overload: "+key);
}
game::MethodRef CoreMethod(const char* cls,const char* name,int args){return game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine",cls,name,args);}
// The map-load hook re-applies custom settings in each new area. It is shared
// with map reveal and stays while either feature needs it.
void SyncLoadedHook(){
    const bool needed=enabled.load()||MapCustomized();
    std::string why;
    if(needed&&!hook::IsInstalled(mapLoaded.code)&&!hook::Install(mapLoaded.code,reinterpret_cast<void*>(&Loaded),reinterpret_cast<void**>(&originalLoaded),&why))
        throw std::runtime_error("Map settings hook refused: "+why);
    if(!needed&&hook::IsInstalled(mapLoaded.code)&&!hook::Remove(mapLoaded.code,&why))Log("map settings: hook removal deferred: %s",why.c_str());
}
void ApplyOpacity(void* panel){
    void* current=canvasGroup.group?canvasGroup.group->Get():nullptr;
    if(current&&!game::IsAlive(current)){canvasGroup={};current=nullptr;}
    if(!current){
        if(minimapOpacity<0)return;  // nothing to restore and nothing to change
        Root go(Invoke(CoreMethod("Component","get_gameObject",0),panel));
        Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(game::FindClass("UnityEngine.UIModule.dll","UnityEngine","CanvasGroup"))));
        void* ta[]{type.Get()};
        void* group=Invoke(TypedMethod("UnityEngine.CoreModule.dll","UnityEngine","GameObject","GetComponent","System.Type"),go.Get(),ta);
        bool added=false;
        if(!group){group=Invoke(TypedMethod("UnityEngine.CoreModule.dll","UnityEngine","GameObject","AddComponent","System.Type"),go.Get(),ta);added=true;}
        if(!group)throw std::runtime_error("The minimap panel cannot change opacity in this game build.");
        canvasGroup={std::make_unique<Root>(group),added,Value<float>(game::FindMethod("UnityEngine.UIModule.dll","UnityEngine","CanvasGroup","get_alpha",0),group)};
        current=group;
    }
    float alpha=minimapOpacity>=0?static_cast<float>(minimapOpacity):canvasGroup.alpha;void* aa[]{&alpha};
    Invoke(game::FindMethod("UnityEngine.UIModule.dll","UnityEngine","CanvasGroup","set_alpha",1),current,aa);
    if(minimapOpacity<0)canvasGroup={};  // restored; an added group stays at its original alpha 1
}
void ApplyTracker(void* panel){
    auto* trackerClass=game::FindClass("LE.dll","","QuestTrackerUI");
    if(!trackerClass)return;
    Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(trackerClass)));bool inactive=false;void* fa[]{type.Get(),&inactive};
    Root found(Invoke(CoreMethod("Object","FindObjectsOfType",2),nullptr,fa));
    void* ui=Get<uintptr_t>(found.Get(),0x18)?Get<void*>(found.Get(),0x20):nullptr;
    void* previous=tracker.transform?tracker.transform->Get():nullptr;
    Root transform(ui?Invoke(CoreMethod("Component","get_transform",0),ui):nullptr);
    if(previous&&previous!=transform.Get()){
        // A different tracker (new scene) starts from its own position.
        if(game::IsAlive(previous)){Vec3 local=tracker.local;void* la[]{&local};Invoke(CoreMethod("Transform","set_localPosition",1),previous,la);}
        tracker={};
    }
    if(!transform.Get())return;
    if(!tracker.transform){
        if(minimapSize==1)return;
        tracker={std::make_unique<Root>(transform.Get()),Value<Vec3>(CoreMethod("Transform","get_localPosition",0),transform.Get())};
    }
    // The panel's pivot is its top-right corner: scaling by s moves its bottom
    // edge by (s - 1) x its unscaled height in world units.
    Root panelParent(Invoke(CoreMethod("Transform","get_parent",0),panel));
    Root trackerParent(Invoke(CoreMethod("Transform","get_parent",0),transform.Get()));
    const float height=Value<Vec2>(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","RectTransform","get_sizeDelta",0),panel).y*original.minimapScale.y;
    const float panelScale=panelParent.Get()?Value<Vec3>(CoreMethod("Transform","get_lossyScale",0),panelParent.Get()).y:1;
    const float trackerScale=trackerParent.Get()?Value<Vec3>(CoreMethod("Transform","get_lossyScale",0),trackerParent.Get()).y:1;
    if(!std::isfinite(height)||height<=0||!(panelScale>0)||!(trackerScale>0))return;
    Vec3 local=tracker.local;local.y-=static_cast<float>((minimapSize-1)*height*panelScale/trackerScale);
    void* la[]{&local};Invoke(CoreMethod("Transform","set_localPosition",1),transform.Get(),la);
    if(minimapSize==1)tracker={};
}
void ApplyMap(){
    void* global=MapGlobal();void* dmm=MapDmm();
    if(!global||!dmm)throw std::runtime_error("The map has not loaded yet. Enter an area and try again.");
    void* mini=GlobalConfig(global,"Minimap");void* full=GlobalConfig(global,"Fullscreen");void* configRect=DmmRect(dmm,"Minimap");
    void* rect=configRect&&game::IsAlive(configRect)?MinimapPanel(configRect):nullptr;
    if(!mini||!full||!rect||!game::IsAlive(rect))throw std::runtime_error("This game build's map configuration is not recognized.");
    const auto zoomOff=MF("DMMapGlobalConfigs.config","zoom"),opacityOff=MF("DMMapGlobalConfigs.config","opacity");
    if(!original.captured){
        original={true,Get<float>(mini,zoomOff),Get<float>(mini,opacityOff),Get<float>(full,zoomOff),Get<float>(full,opacityOff),
            Get<float>(global,MF("DMMapGlobalConfigs","minOverlayZoom")),Get<float>(global,MF("DMMapGlobalConfigs","maxOverlayZoom")),
            Value<Vec3>(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Transform","get_localScale",0),rect)};
    }
    const float fullZoom=static_cast<float>(original.fullZoom*overlayZoom);
    SetField<float>(mini,zoomOff,static_cast<float>(original.minimapZoom*minimapZoom));
    SetField<float>(full,zoomOff,fullZoom);
    const float gameOverlayOpacity=Get<float>(global,MF("DMMapGlobalConfigs","OpacityValue"));
    const float fullOpacity=static_cast<float>(overlayOpacity>=0?overlayOpacity:(gameOverlayOpacity>0&&gameOverlayOpacity<=1?gameOverlayOpacity:original.fullOpacity));
    SetField<float>(full,opacityOff,fullOpacity);
    SetField<float>(global,MF("DMMapGlobalConfigs","minOverlayZoom"),std::min(original.minZoom,fullZoom));
    SetField<float>(global,MF("DMMapGlobalConfigs","maxOverlayZoom"),std::max(original.maxZoom,fullZoom));
    Vec3 scale{static_cast<float>(original.minimapScale.x*minimapSize),static_cast<float>(original.minimapScale.y*minimapSize),original.minimapScale.z};
    void* sa[]{&scale};Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Transform","set_localScale",1),rect,sa);
    ApplyOpacity(rect);
    ApplyTracker(rect);
    Invoke(Method(dmm,"ReloadConfig",0),dmm);
    // The overlay's shader opacity is applied by the minimap component itself.
    auto* minimapClass=game::FindClass("LE.dll","LE.UI.Minimap","Minimap");
    if(minimapClass){
        Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(minimapClass)));bool inactive=true;void* fa[]{type.Get(),&inactive};
        Root found(Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Object","FindObjectsOfType",2),nullptr,fa));
        const auto count=Get<uintptr_t>(found.Get(),0x18);
        for(uintptr_t i=0;i<count&&i<16;++i){void* minimap=Get<void*>(found.Get(),0x20+8*i);if(!minimap||!game::IsAlive(minimap))continue;
            float value=fullOpacity;void* oa[]{&value};Invoke(Method(minimap,"UpdateOverlayMapOpacity",1),minimap,oa);}
    }
    ++mapApplies;
}
std::string MapState(){
    char buffer[512];
    std::snprintf(buffer,sizeof buffer,"{\"ok\":true,\"minimapSize\":%.3g,\"minimapZoom\":%.3g,\"minimapOpacity\":%.3g,\"overlayOpacity\":%.3g,\"overlayZoom\":%.3g,\"captured\":%s,\"applies\":%llu,\"original\":{\"minimapZoom\":%.4g,\"minimapOpacity\":%.4g,\"overlayZoom\":%.4g,\"overlayOpacity\":%.4g}}",
        minimapSize,minimapZoom,minimapOpacity,overlayOpacity,overlayZoom,original.captured?"true":"false",static_cast<unsigned long long>(mapApplies),
        original.minimapZoom,original.minimapOpacity,original.fullZoom,original.fullOpacity);
    return buffer;
}
std::string State() {
    bool active=false, hasMap=false;
    if(ready) {
        void* map=game::StaticObject(activeMap); hasMap=map!=nullptr;
        if(map) {
            int id=ShaderId();void* args[]{&id};
            active=Invoke(getTexture,nullptr,args)==Invoke(blackTexture);
        }
    }
    return std::string("{\"ok\":true,\"ready\":")+Boolean(ready)+",\"enabled\":"+Boolean(enabled.load())+
        ",\"hasMap\":"+Boolean(hasMap)+",\"revealed\":"+Boolean(enabled.load()&&active&&game::IsOfflinePlay())+
        ",\"renderOnly\":true,\"hooks\":{\"loaded\":"+Boolean(hook::IsInstalled(mapLoaded.code))+
        ",\"gate\":"+Boolean(hook::IsInstalled(mapUpdate.code))+"},\"bindings\":"+std::to_string(bindings.load())+
        ",\"faults\":"+std::to_string(faults.load())+"}";
}
}
bool Init() {
    std::string why;ready=game::Guarded([]{
        activeMap=game::FindStaticField("LE.dll","LE.UI.Minimap","MinimapManager","activeMinimap");
        fogShader=game::FindStaticField("LE.dll","LE.UI.Minimap","Minimap","shaderFowSdf");
        fogOffset=Offset("MinimapInformation","FoWSdf");
        setTexture=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Shader","SetGlobalTextureImpl",2);
        getTexture=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Shader","GetGlobalTextureImpl",1);
        blackTexture=game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Texture2D","get_blackTexture",0);
        mapLoaded=game::FindMethod("LE.dll","LE.UI.Minimap","Minimap","OnMapLoaded",2);
        mapUpdate=game::FindMethod("LE.dll","LE.UI.Minimap","Minimap","Update",0);
        if(!activeMap||!fogShader||!setTexture||!getTexture||!blackTexture||!mapLoaded||!mapUpdate)
            throw std::runtime_error("map rendering metadata unavailable");
    },&why);
    Log("map view: %s%s",ready?"ready":"unavailable: ",ready?"":why.c_str());return ready;
}
std::string Read(){return Run([]{return State();});}
std::string SettingsRead(){return Run([]{return MapState();});}
std::string SetSetting(const std::string& key,double value){
    struct Range{const char* key;double* target;double low,high;};
    const Range ranges[]{{"minimapsize",&minimapSize,0.5,2.5},{"minimapzoom",&minimapZoom,0.25,4},{"minimapopacity",&minimapOpacity,0.05,1},
        {"overlayopacity",&overlayOpacity,0.05,1},{"overlayzoom",&overlayZoom,0.25,4}};
    if(key=="reset")return Run([]{
        minimapSize=1;minimapZoom=1;minimapOpacity=-1;overlayOpacity=-1;overlayZoom=1;
        if(original.captured&&MapGlobal()&&MapDmm())ApplyMap();
        if(ready)SyncLoadedHook();
        return MapState();});
    const Range* range=nullptr;for(const auto& r:ranges)if(key==r.key)range=&r;
    const bool opacity=key=="minimapopacity"||key=="overlayopacity";
    const bool neutral=opacity&&value<=0;  // 0 = the game's own opacity
    if(neutral)value=-1;
    if(!range||!std::isfinite(value)||(!neutral&&(value<range->low||value>range->high)))
        return "{\"ok\":false,\"error\":\"use mapset minimapsize 0.5-2.5 | minimapzoom 0.25-4 | minimapopacity 0.05-1 | overlayopacity 0.05-1 | overlayzoom 0.25-4 | reset\"}";
    return Run([=]{
        if(!game::IsOfflinePlay())throw std::runtime_error("Map settings work in offline play only.");
        (void)neutral;
        const double previous=*range->target;*range->target=value;
        try{ApplyMap();}catch(...){*range->target=previous;throw;}
        if(ready)SyncLoadedHook();
        return MapState();});
}
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Capture() {
    return Run([]{
        if(!game::IsOfflinePlay()) throw std::runtime_error("offline capture only");
        RequireSession(4);
        // The game renders its own frame; this does not control desktop input.
        const std::wstring path=PluginDir()+L"logs\\map-check.png";
        const int length=WideCharToMultiByte(CP_UTF8,0,path.data(),static_cast<int>(path.size()),nullptr,0,nullptr,nullptr);
        std::string utf8(length,'\0');WideCharToMultiByte(CP_UTF8,0,path.data(),static_cast<int>(path.size()),utf8.data(),length,nullptr,nullptr);
        Root filename(il2cpp::api().string_new(utf8.c_str()));int size=1;void* args[]{filename.Get(),&size};
        Invoke(game::FindMethod("UnityEngine.ScreenCaptureModule.dll","UnityEngine","ScreenCapture","CaptureScreenshot",2),nullptr,args);
        return std::string("{\"ok\":true,\"path\":")+Json(utf8)+"}";
    });
}
#endif
std::string Set(double value) {
    return Run([=]{
        if(!ready || (value!=0 && value!=1)) throw std::runtime_error("map reveal expects 0 or 1 and compatible metadata");
        if(value==0) {
            if(substituted) Bind(false);
            enabled=false;
            std::string why;
            if(hook::IsInstalled(mapUpdate.code)&&!hook::Remove(mapUpdate.code,&why)) throw std::runtime_error(why);
            SyncLoadedHook();  // kept while custom map settings need it
        } else {
            if(!game::IsOfflinePlay()) throw std::runtime_error("map reveal is offline only");
            RequireSession(4);
            if(!game::StaticObject(activeMap)) throw std::runtime_error("wait until the zone map is loaded");
            const bool wasEnabled=enabled.load(); std::vector<void*> installed; std::string why;
            try {
                if(!hook::IsInstalled(mapLoaded.code)) {
                    if(!hook::Install(mapLoaded.code,reinterpret_cast<void*>(&Loaded),reinterpret_cast<void**>(&originalLoaded),&why)) throw std::runtime_error(why);
                    installed.push_back(mapLoaded.code);
                }
                if(!hook::IsInstalled(mapUpdate.code)) {
                    if(!hook::Install(mapUpdate.code,reinterpret_cast<void*>(&Update),reinterpret_cast<void**>(&originalUpdate),&why)) throw std::runtime_error(why);
                    installed.push_back(mapUpdate.code);
                }
                Bind(true); enabled=true;
            } catch(...) {
                if(!wasEnabled) {if(substituted) CheckedBind(false);enabled=false;}
                for(void* p:installed) hook::Remove(p,nullptr); throw;
            }
        }
        return State();
    });
}
}
