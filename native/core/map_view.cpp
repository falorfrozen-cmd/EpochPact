// Reveal the minimap/overlay by substituting its fog shader input only.
// The real FoW texture, explored volumes, quests and scene objects are untouched.
#include "map_view.hpp"
#include "managed.hpp"
#include "hook.hpp"
#include <atomic>

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
}
void Update(void* self,const il2cpp::Method* method) {
    // One inexpensive gate check while enabled. An online transition restores
    // the current game's own texture immediately, without touching that texture.
    if(substituted && !game::IsOfflinePlay()) CheckedBind(false);
    originalUpdate(self,method);
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
            for(auto m:{mapLoaded,mapUpdate}) if(hook::IsInstalled(m.code)&&!hook::Remove(m.code,&why)) throw std::runtime_error(why);
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
