#include "workshop.hpp"
#include "managed.hpp"
#include "file_safety.hpp"
#include <map>
#include <set>
#include <sstream>
#include <cmath>
#include <iomanip>

// One native multi-affix contains an immutable recipe of modifier lines. The
// normal item serializer stores its ID; no game assets/save format are patched.
// Definitions are persisted separately and restored before offline item decode.
namespace ep::workshop {
namespace {
using namespace managed;
struct Mod {int sp,tags,special,extra,mode;float value;};
struct RecipeData {int id;std::string key;std::vector<Mod> mods;};
std::map<int,RecipeData> recipes;
std::set<int> installed;
game::Handle owner;
bool loaded=false;
std::wstring Path(){return PluginDir()+L"workshop-recipes.txt";}
void Field(void* self,const char* field,void* value){const auto* f=il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(self),field);
    if(!f)throw std::runtime_error(std::string("recipe field unavailable: ")+field);il2cpp::api().field_set_value(self,f,value);}
void* Clone(void* object){return Invoke(game::FindMethod("mscorlib.dll","System","Object","MemberwiseClone",0),object);}
void* EmptyLike(void* list){void* result=il2cpp::api().object_new(il2cpp::api().object_get_class(list));Root r(result);Invoke(Method(result,".ctor",0),result);return result;}
void Add(void* list,void* item){void* a[]{item};Invoke(Method(list,"Add",1),list,a);}
std::vector<void*> Array(void* object){const auto n=Get<uintptr_t>(object,0x18);if(n>8192)throw std::runtime_error("invalid recipe array");std::vector<void*> out;for(uintptr_t i=0;i<n;++i)out.push_back(Get<void*>(object,0x20+i*sizeof(void*)));return out;}
void Save(const std::map<int,RecipeData>& next){
    const auto path=std::filesystem::path(Path()),tmp=path.parent_path()/(L"workshop-recipes."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64())+L".pending");
    if(files::Linked(path.parent_path()) || (std::filesystem::exists(path)&&files::Linked(path)))throw std::runtime_error("recipe storage path is unavailable");
    HANDLE handle=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("recipe storage cannot be written; item unchanged");
    std::ostringstream out;out<<"EPOCHPACT_RECIPES_1\n"<<std::setprecision(9);
    for(auto& [id,r]:next){out<<id<<' '<<r.key<<' '<<r.mods.size()<<'\n';for(auto& m:r.mods)out<<m.sp<<' '<<m.tags<<' '<<m.special<<' '<<m.extra<<' '<<m.mode<<' '<<m.value<<'\n';}
    const auto text=out.str();DWORD bytes=0;const bool ok=WriteFile(handle,text.data(),static_cast<DWORD>(text.size()),&bytes,nullptr)&&bytes==text.size()&&FlushFileBuffers(handle);CloseHandle(handle);
    if(!ok || !MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(tmp.c_str());throw std::runtime_error("recipe storage commit failed; item unchanged");}
}
void* Make(void* list,const RecipeData& r){
    const auto templates=Array(Get<void*>(list,Offset("AffixList","multiAffixes")));
    if(templates.empty())throw std::runtime_error("native multi-affix template unavailable");
    Root item(Clone(templates[0]));int id=r.id,zero=0;float standard=0,weight=0;bool no=false;uint8_t rollsOn=0;
    Field(item.Get(),"affixId",&id);Field(item.Get(),"levelRequirement",&zero);Field(item.Get(),"weighting",&weight);Field(item.Get(),"standardAffixEffectModifier",&standard);
    Field(item.Get(),"specialAffixType",&zero);Field(item.Get(),"classSpecificity",&zero);Field(item.Get(),"rollsOn",&rollsOn);Field(item.Get(),"convertOnIncompatibleItemType",&no);
    Root name(il2cpp::api().string_new(("EpochPact custom recipe "+std::to_string(id)).c_str()));for(const char* f:{"affixName","affixDisplayName","affixTitle","affixLootFilterOverrideName"})Field(item.Get(),f,name.Get());
    Root allowed(EmptyLike(Get<void*>(item.Get(),Offset("AffixList.Affix","canRollOn"))));Field(item.Get(),"canRollOn",allowed.Get());
    void* oldProperties=Get<void*>(item.Get(),Offset("AffixList.MultiAffix","affixProperties"));Root properties(EmptyLike(oldProperties));
    const auto* pc=game::FindClass("LE.dll","","AffixList.AffixProperty");
    for(auto& m:r.mods){Root prop(il2cpp::api().object_new(pc));Invoke(Method(prop.Get(),".ctor",0),prop.Get());uint8_t sp=static_cast<uint8_t>(m.sp),special=static_cast<uint8_t>(m.special);
        Field(prop.Get(),"property",&sp);Field(prop.Get(),"tags",const_cast<int*>(&m.tags));Field(prop.Get(),"specialTag",&special);Field(prop.Get(),"extraTag",const_cast<int*>(&m.extra));Field(prop.Get(),"modifierType",const_cast<int*>(&m.mode));
        bool yes=true;Field(prop.Get(),"useGeneratedNameForDisplayName",&yes);Add(properties.Get(),prop.Get());}
    Field(item.Get(),"affixProperties",properties.Get());
    void* oldTiers=Get<void*>(item.Get(),Offset("AffixList.Affix","tiers"));const auto base=Entries(oldTiers);if(base.size()<7)throw std::runtime_error("seven native affix tiers unavailable");Root tiers(EmptyLike(oldTiers));
    for(unsigned t=0;t<7;++t){Root tier(Clone(base[t]));float first=r.mods.at(0).value;Field(tier.Get(),"minRoll",&first);Field(tier.Get(),"maxRoll",&first);bool yes=true;Field(tier.Get(),"isMultiAffix",&yes);Field(tier.Get(),"checkedIfMultiAffix",&yes);
        Root extra(EmptyLike(Get<void*>(tier.Get(),Offset("AffixList.Tier","extraRolls"))));
        for(size_t i=1;i<r.mods.size();++i){Root pair(il2cpp::api().object_new(game::FindClass("LE.dll","","AffixList.RollPair")));float v=r.mods[i].value;void* args[]{&v,&v};Invoke(Method(pair.Get(),".ctor",2),pair.Get(),args);Add(extra.Get(),pair.Get());}
        Field(tier.Get(),"extraRolls",extra.Get());Add(tiers.Get(),tier.Get());}
    Field(item.Get(),"tiers",tiers.Get());return item.Get();
}
}
void Load(){
    if(loaded)return;std::map<int,RecipeData> next;const auto path=std::filesystem::path(Path());
    if(std::filesystem::exists(path)){
        if(files::Linked(path)||std::filesystem::file_size(path)>4*1024*1024)throw std::runtime_error("unsafe recipe database");
        std::ifstream in(path);std::string header;std::getline(in,header);if(header!="EPOCHPACT_RECIPES_1")throw std::runtime_error("unsupported recipe database");
        int id;while(in>>id){RecipeData r{};r.id=id;size_t n=0;if(!(in>>r.key>>n)||id<2000||id>4095||n<1||n>64||next.contains(id))throw std::runtime_error("invalid recipe database record");
            for(size_t i=0;i<n;++i){Mod m{};if(!(in>>m.sp>>m.tags>>m.special>>m.extra>>m.mode>>m.value)||m.sp<0||m.sp>255||m.special<0||m.special>255||m.mode<0||m.mode>3||!std::isfinite(m.value))throw std::runtime_error("invalid recipe modifier");r.mods.push_back(m);}next.emplace(id,std::move(r));}
        if(!in.eof())throw std::runtime_error("truncated recipe database");
    }recipes=std::move(next);loaded=true;
}
void Ensure(){
    Load();if(recipes.empty())return;
    Root list(Invoke(game::FindMethod("LE.dll","","AffixList","get",0)));if(owner.Get()!=list.Get()){owner.Set(list.Get());installed.clear();}
    for(auto& [id,r]:recipes){if(installed.contains(id))continue;
        void* args[]{const_cast<int*>(&id)};if(Invoke(Method(list.Get(),"GetAffix",1),list.Get(),args))throw std::runtime_error("custom affix ID collides with a native affix");
        Root item(Make(list.Get(),r));auto entries=Array(Get<void*>(list.Get(),Offset("AffixList","multiAffixes")));entries.push_back(item.Get());
        Root array(il2cpp::api().array_new(game::FindClass("LE.dll","","AffixList.MultiAffix"),entries.size()));
        // Use the managed array setter's GC write barrier rather than raw writes.
        const auto* ac=game::FindClass("mscorlib.dll","System","Array");const il2cpp::Method* setter=nullptr;void* it=nullptr;
        while(auto* m=il2cpp::api().class_get_methods(ac,&it)){if(std::string(il2cpp::api().method_get_name(m))!="SetValue"||il2cpp::api().method_get_param_count(m)!=2)continue;
            char* name=il2cpp::api().type_get_name(il2cpp::api().method_get_param(m,1));bool valid=name&&std::string(name)=="System.Int32";if(name)il2cpp::api().free(name);if(valid){setter=m;break;}}
        if(!setter)throw std::runtime_error("native array setter unavailable");
        for(int i=0;i<static_cast<int>(entries.size());++i){void* set[]{entries[i],&i};Invoke(setter,array.Get(),set);}
        Field(list.Get(),"multiAffixes",array.Get());void* nil=nullptr;Field(list.Get(),"allAffixes",nil);installed.insert(id);
    }
}
std::string Catalog(){return Run([]{
    RequireSession(4);std::ostringstream out;out<<"{\"ok\":true,\"modifiers\":[";bool comma=false;
    auto emit=[&](void* mod,const std::string& id,const std::string& source){const char* cls=il2cpp::api().class_get_name(il2cpp::api().object_get_class(mod));
        int mode=Get<int>(mod,Offset(cls,"type"));if(mode<0||mode>2)return;
        if(comma)out<<',';comma=true;out<<"{\"id\":"<<Json(id)<<",\"source\":"<<Json(source)<<",\"property\":"<<int(Get<uint8_t>(mod,Offset(cls,"property")))<<",\"tags\":"<<Get<int>(mod,Offset(cls,"tags"))<<",\"special\":"<<int(Get<uint8_t>(mod,Offset(cls,"specialTag")))<<",\"extra\":"<<Get<int>(mod,Offset(cls,"extraTag"))<<",\"mode\":"<<mode<<",\"value\":"<<Get<float>(mod,Offset(cls,"value"));
        const auto* max=il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(mod),"maxValue");const auto* roll=il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(mod),"canRoll");float v=Get<float>(mod,Offset(cls,"value"));if(max&&roll&&Get<bool>(mod,il2cpp::api().field_get_offset(roll)))v=Get<float>(mod,il2cpp::api().field_get_offset(max));out<<",\"maximum\":"<<v<<'}';};
    Root unique(Invoke(game::FindMethod("LE.dll","","UniqueList","get",0)));for(void* entry:Entries(Get<void*>(unique.Get(),Offset("UniqueList","uniques")))){if(!entry||Get<bool>(entry,Offset("UniqueList.Entry","hideFromPlayers")))continue;
        const int id=Get<uint16_t>(entry,Offset("UniqueList.Entry","uniqueID"));const auto name=Text(Get<void*>(entry,Offset("UniqueList.Entry","name")));auto mods=Entries(Get<void*>(entry,Offset("UniqueList.Entry","mods")));for(size_t i=0;i<mods.size();++i)if(mods[i])emit(mods[i],"u"+std::to_string(id)+":"+std::to_string(i),name);}
    Root sets(game::StaticObject(game::FindStaticField("LE.dll","","SetBonusesList","instance")));for(void* entry:Entries(Get<void*>(sets.Get(),Offset("SetBonusesList","entries")))){if(!entry)continue;const int id=Get<uint8_t>(entry,Offset("SetBonusesList.Entry","setID"));auto mods=Entries(Get<void*>(entry,Offset("SetBonusesList.Entry","mods")));for(size_t i=0;i<mods.size();++i)if(mods[i])emit(mods[i],"s"+std::to_string(id)+":"+std::to_string(i),Text(Get<void*>(entry,Offset("SetBonusesList.Entry","setName"))));}
    return out.str()+"]}";
});}
int Recipe(const std::string& input,bool perfect,float baseEffect){
    Ensure();if(input.empty()||input=="none")return -1;
    // getAffixEffectModifier returns an additive increase, not a factor.
    baseEffect+=1;
    if(!std::isfinite(baseEffect)||baseEffect<=0||baseEffect>100)throw std::runtime_error("unsupported base item affix scaling");
    std::set<std::string> tokens;std::istringstream split(input);std::string token;std::vector<Mod> mods;
    while(std::getline(split,token,',')){
        if(token.size()<4||token.size()>20||tokens.size()>=64||!tokens.insert(token).second)throw std::runtime_error("choose up to 64 different modifier lines");
        const auto colon=token.find(':');int id=-1,index=-1;char tail=0;std::istringstream numbers(token.substr(1));if(!(numbers>>id>>tail>>index)||tail!=':'||!numbers.eof()||id<0||id>65535||index<0||index>1024||(token[0]!='u'&&token[0]!='s'))throw std::runtime_error("invalid modifier selection");
        if(token[0]=='s'&&id>255)throw std::runtime_error("invalid set selection");
        (void)colon;void* args[]{&id};Root entry(Invoke(game::FindMethod("LE.dll","",token[0]=='u'?"UniqueList":"SetBonusesList",token[0]=='u'?"getUnique":"getEntry",1),nullptr,args));
        const char* ec=token[0]=='u'?"UniqueList.Entry":"SetBonusesList.Entry";
        if(token[0]=='u'&&(Get<bool>(entry.Get(),Offset(ec,"hideFromPlayers"))||Get<uint16_t>(entry.Get(),Offset(ec,"uniqueID"))!=id))throw std::runtime_error("modifier template is not public");
        if(token[0]=='s'&&Get<uint8_t>(entry.Get(),Offset(ec,"setID"))!=id)throw std::runtime_error("set modifier template is unavailable");
        const auto values=Entries(Get<void*>(entry.Get(),Offset(ec,"mods")));if(index>=static_cast<int>(values.size())||!values[index])throw std::runtime_error("modifier selection no longer exists");void* source=values[index];const char* mc=token[0]=='u'?"UniqueItemMod":"SetBonus";
        Mod m{Get<uint8_t>(source,Offset(mc,"property")),Get<int>(source,Offset(mc,"tags")),Get<uint8_t>(source,Offset(mc,"specialTag")),Get<int>(source,Offset(mc,"extraTag")),Get<int>(source,Offset(mc,"type")),Get<float>(source,Offset(mc,"value"))};
        if(m.mode<0||m.mode>2||!std::isfinite(m.value))throw std::runtime_error("modifier mode is not supported by native affixes");
        if(token[0]=='u'&&Get<bool>(source,Offset(mc,"canRoll"))){const auto maximum=Get<float>(source,Offset(mc,"maxValue"));if(!std::isfinite(maximum))throw std::runtime_error("invalid modifier range");m.value=perfect?maximum:(m.value+maximum)*.5f;}
        // Unique/Set lines are absolute values; normal affixes additionally use
        // the base equipment's affix factor. Normalize it once in this immutable
        // recipe so the final native contribution equals the selected value.
        m.value/=baseEffect;if(!std::isfinite(m.value))throw std::runtime_error("modifier value exceeds native range");mods.push_back(m);
    }
    if(mods.empty())throw std::runtime_error("choose modifier lines");std::ostringstream scale;scale<<std::setprecision(9)<<baseEffect;
    std::string key=(perfect?"max:":"mid:")+scale.str()+":";for(auto& t:tokens)key+=t+",";
    for(auto& [id,r]:recipes)if(r.key==key)return id;
    Root list(Invoke(game::FindMethod("LE.dll","","AffixList","get",0)));
    // Native v6 packs the ID into twelve bits (a nibble plus a byte).
    // Higher IDs overwrite tier/sealed bits and must never reach a save.
    int id=2000;for(;id<=4095;++id){void* query[]{&id};if(!recipes.contains(id)&&!Invoke(Method(list.Get(),"GetAffix",1),list.Get(),query))break;}if(id>4095)throw std::runtime_error("native recipe ID space is full");
    auto next=recipes;next.emplace(id,RecipeData{id,key,mods});Save(next);recipes=std::move(next);Ensure();return id;
}
bool Owns(int id){return recipes.contains(id);}
}
