// Execute production Identity()/Current() with absent quest metadata.
// Controlled IL2CPP facade; this is not a live Last Epoch test.
#include "../core/progression.cpp"
#include <cstring>
namespace fixture {
ep::il2cpp::Api api;
bool offline=true, alive=true, dataOffline=true, transition=false;
int actorMethod, offlineMethod, sceneMethod, idMethod, klass, stateMethod, transitionField, state=4;
int calls=0, questCalls=0;
struct String {void* c{};void* m{};int n=0;wchar_t text[16]{};} name,id,scene;
struct Data {void* c{};void* m{};void* name{};} data;
struct Tracker {void* c{};void* m{};void* data{};} tracker;
struct Actor {void* c{};void* m{};void* tracker{};} actor;
struct Scene {void* c{};void* m{};void* name{};} details;
void* Invoke(const ep::il2cpp::Method* method,void*,void**,void** error) {
    *error=nullptr; ++calls;
    if(method==&actorMethod)return &actor;
    if(method==&offlineMethod)return &dataOffline;
    if(method==&sceneMethod)return &details;
    if(method==&idMethod)return &id;
    if(method==&stateMethod)return &state;
    ++questCalls;throw std::runtime_error("unexpected quest method");
}
void StringValue(String& value,const wchar_t* text) {value.n=int(wcslen(text));wcscpy_s(value.text,text);}
}
namespace ep::il2cpp {const Api& api(){return fixture::api;}}
namespace ep {
const std::wstring& PluginDir(){throw std::runtime_error("fixture forbids filesystem mutation");}
void Log(const char*,...){}
}
namespace ep::game {
bool IsOfflinePlay(bool*){return fixture::offline;}
std::string GateText(){return "not offline";}
bool IsAlive(void*){return fixture::alive;}
MethodRef FindMethod(const char*,const char*,const char*,const char* name,int){
    const void* method=strcmp(name,"getPlayerActor")==0?&fixture::actorMethod:
        strcmp(name,"get_IsOffline")==0?&fixture::offlineMethod:
        strcmp(name,"GetCurrentSceneDetails")==0?&fixture::sceneMethod:
        strcmp(name,"get_CurrentClientAppStateType")==0?&fixture::stateMethod:nullptr;
    return {method,const_cast<void*>(method)};
}
const il2cpp::Field* FindStaticField(const char*,const char*,const char*,const char*){return &fixture::transitionField;}
const il2cpp::Class* FindClass(const char*,const char*,const char*){return nullptr;}
void* StaticObject(const il2cpp::Field*){throw std::runtime_error("fixture forbids quest UI reads");}
size_t FieldOffset(const char*,const char*,const char*,const char*) {return 0x10;}
}
namespace ep::mainthread {
bool Run(std::function<void()> job,unsigned timeout,std::string*) {if(timeout!=5000)throw std::runtime_error("timeout");job();return true;}
bool RunSteps(std::function<bool()>,unsigned,std::string*){throw std::runtime_error("fixture forbids save mutations");}
}
namespace ep::hook {
bool Install(void*,void*,void**,std::string*){throw std::runtime_error("fixture forbids hooks");}
bool Remove(void*,std::string*){throw std::runtime_error("fixture forbids hooks");}
}
int main() {
    using namespace fixture; using namespace ep::progression;
    api.runtime_invoke=fixture::Invoke;
    api.gchandle_new=[](void* p,bool)->uintptr_t{return reinterpret_cast<uintptr_t>(p);};
    api.gchandle_get_target=[](uintptr_t p)->void*{return reinterpret_cast<void*>(p);};
    api.gchandle_free=[](uintptr_t){};
    api.object_unbox=[](void* p)->void*{return p;};
    api.field_static_get_value=[](const ep::il2cpp::Field*,void* p){*static_cast<bool*>(p)=transition;};
    api.object_get_class=[](void*)->const ep::il2cpp::Class*{return &klass;};
    api.class_get_method_from_name=[](const ep::il2cpp::Class*,const char* method,int)->const ep::il2cpp::Method*{return strcmp(method,"get_Id")==0?&idMethod:nullptr;};
    StringValue(name,L"Fixture");StringValue(id,L"42");StringValue(scene,L"M_Rest");
    data.name=&name;tracker.data=&data;actor.tracker=&tracker;details.name=&scene;
    m_actor={&actorMethod,&actorMethod};m_offline={&offlineMethod,&offlineMethod};m_currentScene={&sceneMethod,&sceneMethod};
    ready=false; // No quest catalog, state list or waypoint methods initialized.
    int tests=0,failures=0;
    auto check=[&](bool ok,const char* text){++tests;if(!ok)++failures;std::printf("%s %s\n",ok?"ok":"FAIL",text);};
    check(!Init(),"quest initialization failure remains a progression failure");
    auto result=Identity();
    check(result.find("\"ok\":true")!=std::string::npos && result.find("\"id\":\"42\"")!=std::string::npos,"identity succeeds with unavailable progression metadata");
    check(questCalls==0,"identity never accesses quest list");
    try{Current();check(false,"save readiness");}catch(const std::exception&){check(true,"save actions still require progression readiness");}
    for(auto* flag:{&offline,&alive,&dataOffline}) {
        *flag=false;check(Identity().find("\"ok\":false")!=std::string::npos,"identity retains offline and live actor gates");*flag=true;
    }
    transition=true;check(Identity().find("\"ok\":false")!=std::string::npos,"identity refuses area transition");transition=false;
    StringValue(scene,L"");check(Identity().find("\"ok\":false")!=std::string::npos,"identity waits for an actual loaded area");StringValue(scene,L"M_Rest");
    check(questCalls==0,"all rejected reads leave quests untouched");
    std::printf("%d checks, %d failures\n",tests,failures);return failures?1:0;
}
