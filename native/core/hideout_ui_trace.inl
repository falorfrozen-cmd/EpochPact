// Private investigation only. Observe actual native menu openings without
// changing their behavior; no desktop/UIA automation and no character writes.
struct UiTraceEvent {std::string name;std::vector<std::string> stack;};
std::vector<UiTraceEvent> uiTraceEvents;
bool uiTraceOn=false;
void TraceMenu(const char* name){
    if(!uiTraceOn||uiTraceEvents.size()>=128)return;
    UiTraceEvent e;e.name=name;void* addresses[16]{};
    const auto n=CaptureStackBackTrace(1,16,addresses,nullptr);
    for(USHORT k=0;k<n;++k){HMODULE module=nullptr;wchar_t file[MAX_PATH]{};
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(addresses[k]),&module))GetModuleFileNameW(module,file,MAX_PATH);
        std::ostringstream item;item<<std::filesystem::path(file).filename().string()<<"+0x"<<std::hex<<(reinterpret_cast<uintptr_t>(addresses[k])-reinterpret_cast<uintptr_t>(module));e.stack.push_back(item.str());
    }
    Log("private native menu trace: %s",name);uiTraceEvents.push_back(std::move(e));
}
using UiInventoryFn=void(*)(void*,bool,bool,bool,int,bool,const il2cpp::Method*);
using UiStashFn=void(*)(void*,bool,bool,bool,void*,bool,const il2cpp::Method*);
using UiOneFn=void(*)(void*,bool,const il2cpp::Method*);
using UiSkillsFn=void(*)(void*,bool,void*,const il2cpp::Method*);
using UiInteractionFn=void(*)(void*,void*,const il2cpp::Method*);
UiInventoryFn uiInventoryOriginal=nullptr;UiStashFn uiStashOriginal=nullptr;
UiOneFn uiStatsOriginal=nullptr,uiShopOriginal=nullptr,uiCosmeticsOriginal=nullptr;
UiSkillsFn uiSkillsOriginal=nullptr;UiInteractionFn uiMonolithOriginal=nullptr;
void UiInventoryTrace(void* s,bool a,bool b,bool c,int d,bool e,const il2cpp::Method* m){TraceMenu("inventory");uiInventoryOriginal(s,a,b,c,d,e,m);}
void UiStashTrace(void* s,bool a,bool b,bool c,void* d,bool e,const il2cpp::Method* m){TraceMenu("stash");uiStashOriginal(s,a,b,c,d,e,m);}
void UiStatsTrace(void* s,bool a,const il2cpp::Method* m){TraceMenu("character stats");uiStatsOriginal(s,a,m);}
void UiShopTrace(void* s,bool a,const il2cpp::Method* m){TraceMenu("vendor");uiShopOriginal(s,a,m);}
void UiCosmeticsTrace(void* s,bool a,const il2cpp::Method* m){TraceMenu("cosmetics");uiCosmeticsOriginal(s,a,m);}
void UiSkillsTrace(void* s,bool a,void* b,const il2cpp::Method* m){TraceMenu("skills");uiSkillsOriginal(s,a,b,m);}
void UiMonolithTrace(void* s,void* a,const il2cpp::Method* m){TraceMenu("monolith interaction");uiMonolithOriginal(s,a,m);}
std::string UiTrace(const std::string& mode){
    if(mode=="on"){
        const auto install=[](const char* cls,const char* method,int arity,void* detour,void** original){
            const auto f=game::FindMethod("LE.dll","",cls,method,arity);std::string why;
            if(!f)throw std::runtime_error(std::string("No trace target: ")+method);
            if(!*original&&!hook::Install(f.code,detour,original,&why))throw std::runtime_error(why);
        };
        install("UIBase","openInventory",5,reinterpret_cast<void*>(&UiInventoryTrace),reinterpret_cast<void**>(&uiInventoryOriginal));
        install("UIBase","openStash",5,reinterpret_cast<void*>(&UiStashTrace),reinterpret_cast<void**>(&uiStashOriginal));
        install("UIBase","OpenCharacterStats",1,reinterpret_cast<void*>(&UiStatsTrace),reinterpret_cast<void**>(&uiStatsOriginal));
        install("UIBase","openShop",1,reinterpret_cast<void*>(&UiShopTrace),reinterpret_cast<void**>(&uiShopOriginal));
        install("UIBase","openCosmetics",1,reinterpret_cast<void*>(&UiCosmeticsTrace),reinterpret_cast<void**>(&uiCosmeticsOriginal));
        install("UIBase","openSkills",2,reinterpret_cast<void*>(&UiSkillsTrace),reinterpret_cast<void**>(&uiSkillsOriginal));
        install("OpenMonolithPanelInteraction","Interaction",1,reinterpret_cast<void*>(&UiMonolithTrace),reinterpret_cast<void**>(&uiMonolithOriginal));
        uiTraceEvents.clear();uiTraceOn=true;
    }else if(mode=="off")uiTraceOn=false;
    else if(mode=="clear")uiTraceEvents.clear();
    else if(mode!="read")throw std::runtime_error("Use uitrace on|off|clear|read.");
    std::ostringstream out;out<<"{\"ok\":true,\"enabled\":"<<Boolean(uiTraceOn)<<",\"events\":[";
    for(size_t k=0;k<uiTraceEvents.size();++k){const auto& e=uiTraceEvents[k];out<<(k?",":"")<<"{\"name\":"<<Json(e.name)<<",\"stack\":[";for(size_t j=0;j<e.stack.size();++j)out<<(j?",":"")<<Json(e.stack[j]);out<<"]}";}
    return out.str()+"]}";
}
