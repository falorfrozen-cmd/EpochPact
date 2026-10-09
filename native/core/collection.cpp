#include "collection.hpp"
#include "managed.hpp"
#include <map>
#include <set>
#include <sstream>
#include <iomanip>

// Demand-only reads. No detours, inventory transactions, save calls or scene scans.
namespace ep::collection {
namespace {
using namespace managed;
size_t F(const char* cls, const char* field, const char* ns = "") {
    static std::map<std::string,size_t> fields;
    const std::string key = std::string(ns)+":"+cls+":"+field;
    auto it=fields.find(key);
    if(it!=fields.end()) return it->second;
    return fields.emplace(key,Offset(cls,field,ns)).first->second;
}
game::MethodRef M(const char* cls,const char* name,int arity,const char* ns="") {
    const auto m=game::FindMethod("LE.dll",ns,cls,name,arity);
    if(!m) throw std::runtime_error(std::string("collection method unavailable: ")+cls+"."+name);
    return m;
}
struct Context { void* actor; void* manager; std::string id,name; };
Context Current() {
    if(!game::IsOfflinePlay()) throw std::runtime_error(game::GateText());
    RequireSession(4);
    void* actor=Invoke(M("PlayerFinder","getPlayerActor",0));
    if(!actor || !game::IsAlive(actor)) throw std::runtime_error("load an offline character first");
    void* tracker=Get<void*>(actor,F("Actor","characterDataTracker"));
    void* data=Get<void*>(tracker,F("CharacterDataTracker","charData"));
    if(!Value<bool>(Method(data,"get_IsOffline",0),data)) throw std::runtime_error("offline character required");
    void* manager=Invoke(M("ItemContainersManager","get_Instance",0));
    if(!manager || Get<void*>(manager,F("ItemContainersManager","actor"))!=actor ||
       !Value<bool>(Method(manager,"get_IsFullyInitializedAndLoaded",0),manager))
        throw std::runtime_error("wait for character inventory to finish loading");
    return {actor,manager,Text(Invoke(Method(data,"get_Id",0),data)),
        Text(Get<void*>(data,F("CharacterData","<CharacterName>k__BackingField","LE.Data")))};
}
std::string Player(const Context& c) {return "{\"id\":"+Json(c.id)+",\"name\":"+Json(c.name)+"}";}
std::vector<uint8_t> Bytes(void* a) {
    if(!a) return {};
    const auto n=Get<uintptr_t>(a,0x18);
    if(n>4096) throw std::runtime_error("invalid item byte array bounds");
    const auto* p=static_cast<const uint8_t*>(a)+0x20;
    return {p,p+n};
}
struct Hash {
    uint64_t h=14695981039346656037ull;
    template<class T> void Add(T v) {const auto* p=reinterpret_cast<const uint8_t*>(&v);for(size_t i=0;i<sizeof v;++i){h^=p[i];h*=1099511628211ull;}}
    void Text(const std::string& s){Add(s.size());for(unsigned char c:s)Add(c);}
    std::string Hex()const {std::ostringstream o;o<<std::hex<<std::setw(16)<<std::setfill('0')<<h;return o.str();}
};
struct Located {void* entry;void* item;std::string source;int tab;};
std::string Fingerprint(void* item) {
    Hash h;
    for(const char* f:{"individualID","itemType","subType","rarity","uniqueID","legendaryPotential","weaversWill","weaversTouch","forgingPotential","corrupted","ruined"}) {
        if(std::string(f)=="individualID")h.Add(Get<uint32_t>(item,F("ItemData",f)));
        else if(std::string(f)=="subType"||std::string(f)=="uniqueID")h.Add(Get<uint16_t>(item,F("ItemData",f)));
        else h.Add(Get<uint8_t>(item,F("ItemData",f)));
    }
    for(const char* f:{"implicitRolls","uniqueRolls"}) {const auto b=Bytes(Get<void*>(item,F("ItemData",f)));h.Add(b.size());for(auto v:b)h.Add(v);}
    void* affixes=Get<void*>(item,F("ItemData","affixes"));
    if(affixes)for(void* a:Entries(affixes))if(a){h.Add(Get<uint16_t>(a,F("ItemAffix","affixId")));h.Add(Get<uint8_t>(a,F("ItemAffix","affixTier")));h.Add(Get<uint8_t>(a,F("ItemAffix","affixRoll")));h.Add(Get<int>(a,F("ItemAffix","sealedAffixType")));}
    return h.Hex();
}
std::string ItemJson(const Located& l,void* itemList) {
    Root pin(l.item);
    const int type=Get<uint8_t>(l.item,F("ItemData","itemType")),sub=Get<uint16_t>(l.item,F("ItemData","subType"));
    const auto uid=Get<uint32_t>(l.item,F("ItemData","individualID"));
    const auto uniqueId=Get<uint16_t>(l.item,F("ItemData","uniqueID"));
    const bool unique=Value<bool>(Method(l.item,"isUniqueSetOrLegendary",0),l.item);
    const bool legendary=Value<bool>(Method(l.item,"isLegendary",0),l.item);
    const bool set=Value<bool>(Method(l.item,"isSet",0),l.item);
    int ty=type,st=sub;void* baseArgs[]{&ty};void* nameArgs[]{&ty,&st};
    const std::string base=Text(Invoke(Method(itemList,"GetBaseTypeDisplayName",1),itemList,baseArgs));
    std::string name=Text(Invoke(Method(itemList,"GetItemDisplayName",2),itemList,nameArgs));
    if(unique){auto id=uniqueId;bool display=true;void* a[]{&id,&display};name=Text(Invoke(M("UniqueList","getUniqueName",2),nullptr,a));}
    struct XY {int x,y;};const auto pos=Get<XY>(l.entry,F("ItemContainerEntry","<Position>k__BackingField"));
    const std::string fp=Fingerprint(l.item);
    // Individual IDs survive moves. Zero-ID items use content identity; equivalent copies share protection.
    const std::string key=uid?std::to_string(uid)+":"+std::to_string(type)+":"+std::to_string(sub)+":"+(unique?std::to_string(uniqueId):"-"):"content:"+fp;
    std::ostringstream out;
    out<<"{\"key\":"<<Json(key)<<",\"fingerprint\":"<<Json(fp)<<",\"name\":"<<Json(name)<<",\"baseType\":"<<Json(base)
       <<",\"type\":"<<type<<",\"subType\":"<<sub<<",\"uniqueId\":"<<(unique?std::to_string(uniqueId):"null")
       <<",\"kind\":"<<Json(legendary?"Legendary":set?"Set":unique?"Unique":"Equipment")<<",\"rarity\":"<<static_cast<int>(Get<uint8_t>(l.item,F("ItemData","rarity")))
       <<",\"lp\":"<<static_cast<int>(Get<uint8_t>(l.item,F("ItemData","legendaryPotential")))<<",\"weaversWill\":"<<static_cast<int>(Get<uint8_t>(l.item,F("ItemData","weaversWill")))
       <<",\"fp\":"<<static_cast<int>(Get<uint8_t>(l.item,F("ItemData","forgingPotential")))<<",\"corrupted\":"<<Boolean(Get<bool>(l.item,F("ItemData","corrupted")))
       <<",\"source\":"<<Json(l.source)<<",\"tab\":"<<l.tab<<",\"x\":"<<pos.x<<",\"y\":"<<pos.y<<",\"quantity\":"<<Get<int>(l.entry,F("ItemContainerEntry","quantity"))<<",\"affixes\":[";
    void* affixes=Get<void*>(l.item,F("ItemData","affixes"));bool first=true;
    if(affixes)for(void* a:Entries(affixes))if(a){if(!first)out<<',';first=false;
        out<<"{\"id\":"<<Get<uint16_t>(a,F("ItemAffix","affixId"))<<",\"name\":"<<Json(Text(Get<void*>(a,F("ItemAffix","affixName"))))
           <<",\"tier\":"<<static_cast<int>(Get<uint8_t>(a,F("ItemAffix","affixTier")))+1<<",\"roll\":"<<static_cast<int>(Get<uint8_t>(a,F("ItemAffix","affixRoll")))
           <<",\"sealed\":"<<Boolean(Get<int>(a,F("ItemAffix","sealedAffixType"))!=0)<<'}';}
    out<<"],\"uniqueRolls\":[";const auto rolls=Bytes(Get<void*>(l.item,F("ItemData","uniqueRolls")));
    for(size_t i=0;i<rolls.size();++i){if(i)out<<',';out<<static_cast<int>(rolls[i]);}
    out<<"],\"implicitRolls\":[";const auto implicits=Bytes(Get<void*>(l.item,F("ItemData","implicitRolls")));
    for(size_t i=0;i<implicits.size();++i){if(i)out<<',';out<<static_cast<int>(implicits[i]);}
    out<<"],\"uniqueMods\":[";
    if(unique) {
        auto id=uniqueId; void* args[]{&id}; Root entry(Invoke(M("UniqueList","getUnique",1),nullptr,args));
        bool fm=true;
        for(void* mod:Entries(Get<void*>(entry.Get(),F("UniqueList.Entry","mods"))))if(mod&&!Get<bool>(mod,F("UniqueItemMod","hideInTooltip"))) {
            Root info(Invoke(Method(mod,"GetPropertyInfo",0),mod));
            auto special=Get<uint8_t>(mod,F("UniqueItemMod","specialTag")); void* modNameArgs[]{&special};
            const auto rollID=Get<uint8_t>(mod,F("UniqueItemMod","rollID"));
            uint8_t roll=rollID<rolls.size()?rolls[rollID]:0; void* valueArgs[]{&roll};
            const bool canRoll=Get<bool>(mod,F("UniqueItemMod","canRoll"));
            if(canRoll&&rollID>=rolls.size())throw std::runtime_error("unique roll index outside item data");
            const int mode=Get<int>(mod,F("UniqueItemMod","type"));
            const bool percent=mode!=0||Get<bool>(info.Get(),F("BasePropertyInfo","displayAddedAsPercentage"))||Get<bool>(info.Get(),F("BasePropertyInfo","displayAsPercentageOf"));
            const float scale=percent?100.f:Get<bool>(info.Get(),F("BasePropertyInfo","displayAddedAsTenthOfValue"))?.1f:1.f;
            struct Range {float low,high;}; const auto range=Value<Range>(Method(mod,"GetRange",0),mod);
            if(!fm)out<<',';fm=false;
            out<<"{\"name\":"<<Json(Text(Invoke(Method(info.Get(),"getPropertyName",1),info.Get(),modNameArgs)))
               <<",\"value\":"<<Value<float>(Method(mod,"getValue",1),mod,valueArgs)*scale
               <<",\"min\":"<<range.low*scale<<",\"max\":"<<range.high*scale<<",\"percent\":"<<Boolean(percent)
               <<",\"mode\":"<<mode<<",\"canRoll\":"<<Boolean(canRoll)<<",\"roll\":"<<static_cast<int>(roll)
               <<",\"lessIsBetter\":"<<Boolean(Get<bool>(info.Get(),F("BasePropertyInfo","lessIsBetter")))<<'}';
        }
    }
    return out.str()+"]}";
}
}
std::string Catalog() {return Run([]{
    const auto c=Current();Root list(Invoke(M("UniqueList","get",0)));Root types(Invoke(M("ItemList","get",0)));
    std::ostringstream out;out<<"{\"ok\":true,\"player\":"<<Player(c)<<",\"items\":[";bool first=true;std::set<int> ids;
    for(void* e:Entries(Get<void*>(list.Get(),F("UniqueList","uniques"))))if(e && !Get<bool>(e,F("UniqueList.Entry","hideFromPlayers"))){
        const int id=Get<uint16_t>(e,F("UniqueList.Entry","uniqueID"));if(!ids.insert(id).second)throw std::runtime_error("duplicate unique catalog id");
        if(!first)out<<',';first=false;int type=Get<uint8_t>(e,F("UniqueList.Entry","baseType"));void* a[]{&type};
        out<<"{\"id\":"<<id<<",\"name\":"<<Json(Text(Invoke(Method(e,"getDisplayName",0),e)))<<",\"type\":"<<type
           <<",\"baseType\":"<<Json(Text(Invoke(Method(types.Get(),"GetBaseTypeDisplayName",1),types.Get(),a)))
           <<",\"set\":"<<Boolean(Get<bool>(e,F("UniqueList.Entry","isSetItem")))<<",\"randomDrop\":"<<Boolean(Get<bool>(e,F("UniqueList.Entry","canDropRandomly")))
           <<",\"level\":"<<static_cast<int>(Value<uint8_t>(Method(e,"getDefaultLevelRequirement",0),e))<<'}';
    }
    if(ids.empty())throw std::runtime_error("unique catalog not loaded yet");return out.str()+"]}";
});}
std::string Items(int offset) {return Run([=]{
    const ULONGLONG start=GetTickCount64();if(offset<0||offset>65536)throw std::runtime_error("invalid collection page offset");
    const auto c=Current();std::vector<Located> located;std::set<void*> seen;
    auto add=[&](void* container,const std::string& source,int tab){
        if(!container)throw std::runtime_error("required inventory container unavailable");
        Root content(Invoke(Method(container,"GetContent",0),container));
        for(void* entry:Entries(content.Get()))if(entry){void* item=Get<void*>(entry,F("ItemContainerEntry","data"));
            if(item&&Value<bool>(Method(item,"isEquipment",0),item)&&seen.insert(entry).second)located.push_back({entry,item,source,tab});}
    };
    void* holder=Get<void*>(c.manager,F("ItemContainersManager","stash"));
    Root stash(Invoke(Method(holder,"get_CurrentContainer",0),holder));
    auto tabs=Entries(Get<void*>(stash.Get(),F("TabbedItemContainer","containers")));
    for(size_t i=0;i<tabs.size();++i)add(tabs[i],"Stash",static_cast<int>(i));
    add(Get<void*>(c.manager,F("ItemContainersManager","inventory")),"Inventory",-1);
    void* eq=Get<void*>(c.manager,F("ItemContainersManager","equipment"));
    for(void* container:Entries(Get<void*>(eq,F("ItemContainersManager.PaperDollContainer","containers"))))add(container,"Equipped",-1);
    add(Get<void*>(c.manager,F("ItemContainersManager","idols")),"Equipped idols",-1);
    add(Get<void*>(c.manager,F("ItemContainersManager","idolsInventory")),"Idol inventory",-1);
    add(Get<void*>(c.manager,F("ItemContainersManager","cursor")),"Cursor",-1);
    void* forge=Get<void*>(c.manager,F("ItemContainersManager","crafting"));
    if(forge)for(void* container:Entries(Get<void*>(forge,F("ItemContainersManager.CraftingContainers","containers"))))add(container,"Forge",-1);
    void* eternity=Get<void*>(c.manager,F("ItemContainersManager","eternityCache"));
    if(eternity)for(void* container:Entries(Get<void*>(eternity,F("ItemContainersManager.EternityCacheContainers","containers"))))add(container,"Eternity Cache",-1);
    if(located.size()>65536 || offset>static_cast<int>(located.size()))throw std::runtime_error("collection changed; refresh again");
    void* data=Invoke(Method(stash.Get(),"get_LinkedStash",0),stash.Get());
    const std::string scope=Text(Invoke(Method(data,"get_Id",0),data));
    if(scope.empty())throw std::runtime_error("loaded stash identity unavailable");
    Hash revision;revision.Text(c.id);revision.Text(scope);revision.Add(located.size());
    for(const auto& l:located){revision.Text(l.source);revision.Add(l.tab);revision.Text(Fingerprint(l.item));
        const auto p=Get<uint64_t>(l.entry,F("ItemContainerEntry","<Position>k__BackingField"));revision.Add(p);revision.Add(Get<int>(l.entry,F("ItemContainerEntry","quantity")));}
    Root types(Invoke(M("ItemList","get",0)));const int end=std::min(offset+100,static_cast<int>(located.size()));
    std::ostringstream out;out<<"{\"ok\":true,\"player\":"<<Player(c)<<",\"scope\":"<<Json(scope)<<",\"revision\":"<<Json(revision.Hex())
       <<",\"total\":"<<located.size()<<",\"tabs\":"<<tabs.size()<<",\"offset\":"<<offset<<",\"next\":"<<(end<static_cast<int>(located.size())?std::to_string(end):"null")<<",\"items\":[";
    for(int i=offset;i<end;++i){if(i!=offset)out<<',';out<<ItemJson(located[i],types.Get());}
    out<<"],\"readMs\":"<<GetTickCount64()-start<<'}';return out.str();
});}
}
