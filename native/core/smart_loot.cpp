#include "smart_loot.hpp"
#include "managed.hpp"
#include "loot_crafting_rules.hpp"
#include <charconv>
#include <sstream>
#include <set>

namespace ep::smartloot {
namespace {
using namespace managed;
using namespace lootcraft::rules;
Policy policy;
bool ready = false;
size_t lp, affixes, affixId, affixTier, filterOffset, actorTracker, trackerData;
game::MethodRef getActor, isOffline, getItem, isUnique, isShard, isRune, isGlyph, match, getAffixes;
const il2cpp::Field* filterInstance;
uint64_t accepted = 0, rejected = 0, faults = 0;
game::MethodRef Resolve(const char* cls, const char* name, int arity, const char* ns = "") {
    const auto m = game::FindMethod("LE.dll", ns, cls, name, arity);
    if (!m) throw std::runtime_error(std::string("missing loot method: ") + cls + "." + name);
    return m;
}
bool FilterMatch(void* filter,void* item) {
    Root pin(filter);
    alignas(8) unsigned char color[8]{}, emphasize[8]{};
    int rule = -1, sound = 0, icon = 0, beamSize = 0;
    bool beam = false; float beamColor[4]{};
    void* args[]{item, color, emphasize, &rule, &sound, &icon, &beam, &beamSize, beamColor};
    const int outcome = Value<int>(match, pin.Get(), args);
    if (outcome < 0 || outcome > 2) throw std::runtime_error("unknown filter outcome");
    return outcome != 1;
}
bool Filter(void* item) {
    void* manager=game::StaticObject(filterInstance);
    if(!manager) throw std::runtime_error("loot filter manager unavailable");
    void* filter=Get<void*>(manager,filterOffset);
    return !filter || FilterMatch(filter,item); // The game's no-filter behavior is show everything.
}
bool Decision(void* label) {
    if (!game::IsAlive(label)) return false;
    if (policy.mode == Mode::All && policy.materials) return true;
    void* item = Invoke(getItem, label);
    if (!item) return false;
    Root pin(item);
    const bool material = Value<bool>(isShard, item) || Value<bool>(isRune, item) || Value<bool>(isGlyph, item);
    bool t7 = false;
    if (policy.mode == Mode::Quality && policy.t7 && !material) {
        void* list = Get<void*>(item, affixes);
        if (list) for (void* affix : Entries(list)) {
            if (!affix || Get<uint8_t>(affix, affixTier) != 6) continue; // stored 0 -> displayed T1
            const int id = Get<uint16_t>(affix, affixId);
            if (policy.affixes.empty() || std::find(policy.affixes.begin(), policy.affixes.end(), id) != policy.affixes.end()) t7 = true;
        }
    }
    const bool checkFilter = policy.mode == Mode::Filter || (policy.mode == Mode::Quality && policy.respectFilter);
    return lootcraft::rules::Accept(policy, {material, Value<bool>(isUnique, item), t7, !checkFilter || Filter(item), Get<uint8_t>(item, lp)});
}
std::string State() {
    const char* names[]{"all", "filter", "quality", "materials"};
    std::ostringstream out;
    out << "{\"ok\":true,\"ready\":" << Boolean(ready) << ",\"mode\":" << Json(names[static_cast<int>(policy.mode)])
        << ",\"minimumLP\":" << policy.minimumLP << ",\"t7\":" << Boolean(policy.t7)
        << ",\"respectFilter\":" << Boolean(policy.respectFilter) << ",\"materials\":" << Boolean(policy.materials) << ",\"categories\":{";
    const char* categories[]{"gold", "potions", "xp", "favor", "bones"};
    for (int i=0;i<5;++i) { if(i) out << ','; out << Json(categories[i]) << ':' << Boolean(policy.categories[i]); }
    out << "},\"affixIds\":[";
    for (size_t i=0;i<policy.affixes.size();++i) { if(i) out << ','; out << policy.affixes[i]; }
    out << "],\"telemetry\":{\"acceptedRequests\":" << accepted << ",\"rejected\":" << rejected << ",\"faults\":" << faults << "}}";
    return out.str();
}
int Integer(const std::string& s) {
    int v=0; const auto [end, error]=std::from_chars(s.data(),s.data()+s.size(),v);
    if (error!=std::errc{} || end!=s.data()+s.size()) throw std::runtime_error("expected integer");
    return v;
}
std::vector<std::pair<int,std::string>> AffixCatalog() {
    Root data(Invoke(getAffixes));
    Root list(Invoke(Method(data.Get(), "get_AllAffixes", 0), data.Get()));
    std::vector<std::pair<int,std::string>> result;
    const auto id=Offset("AffixList.Affix","affixId");
    for (void* a : Entries(list.Get())) if (a) result.emplace_back(Get<int>(a,id), Text(Invoke(Method(a,"getAffixDisplayName",0),a)));
    return result;
}
}
bool Init() {
    std::string why;
    ready = game::Guarded([] {
        lp=Offset("ItemData","legendaryPotential"); affixes=Offset("ItemData","affixes");
        affixId=Offset("ItemAffix","affixId"); affixTier=Offset("ItemAffix","affixTier");
        filterOffset=Offset("ItemFilterManager","<Filter>k__BackingField","ItemFiltering");
        filterInstance=game::FindStaticField("LE.dll","ItemFiltering","ItemFilterManager","Instance");
        if (!filterInstance) throw std::runtime_error("loot filter singleton unavailable");
        actorTracker=Offset("Actor","characterDataTracker"); trackerData=Offset("CharacterDataTracker","charData");
        getActor=Resolve("PlayerFinder","getPlayerActor",0); isOffline=Resolve("CharacterData","get_IsOffline",0,"LE.Data");
        getItem=Resolve("GroundItemLabel","getItemData",0); isUnique=Resolve("ItemData","isUnique",0);
        isShard=Resolve("ItemData","IsAffixShard",0); isRune=Resolve("ItemData","IsRune",0); isGlyph=Resolve("ItemData","IsGlyph",0);
        match=Resolve("ItemFilter","Match",9,"ItemFiltering"); getAffixes=Resolve("AffixList","get",0);
    }, &why);
    Log("smart loot: %s%s", ready ? "metadata ready" : "unavailable: ", ready ? "" : why.c_str()); return ready;
}
bool CanCollect() {
    if (!ready || !game::IsOfflinePlay()) return false;
    bool okay=false;
    game::Guarded([&] { RequireSession(4); void* actor=Invoke(getActor);
        if(actor && game::IsAlive(actor)) okay=Value<bool>(isOffline,Get<void*>(Get<void*>(actor,actorTracker),trackerData));
    },nullptr);
    return okay;
}
bool Accept(void* label) {
    bool result=false;
    if (!game::Guarded([&] { result=Decision(label); },nullptr)) ++faults;
    if (result) ++accepted; else ++rejected;
    return result;
}
bool Category(int index) { return index>=0 && index<5 && policy.categories[index]; }
std::string Read() {
    return Run([] {
        std::string state=State(); state.pop_back(); state+=" ,\"affixes\":[";
        bool first=true;
        if (ready) for (const auto& [id,name]:AffixCatalog()) { if(!first) state+=','; first=false;
            state+="{\"id\":"+std::to_string(id)+",\"name\":"+Json(name)+"}"; }
        return state+"]}";
    });
}
std::string Set(const std::string& key, const std::string& value) {
    return Run([=] {
        if (!ready) throw std::runtime_error("smart loot metadata unavailable");
        Policy next=policy;
        if(key=="reset") next=Policy{};
        else if(key=="mode") {
            if(value=="all") next.mode=Mode::All; else if(value=="filter") next.mode=Mode::Filter;
            else if(value=="quality") next.mode=Mode::Quality; else if(value=="materials") next.mode=Mode::Materials;
            else throw std::runtime_error("loot mode must be all/filter/quality/materials");
        } else if(key=="lp") { next.minimumLP=Integer(value); if(next.minimumLP<0||next.minimumLP>4) throw std::runtime_error("LP threshold 0..4"); }
        else if(key=="affixes") {
            next.affixes.clear(); std::set<int> available;
            for(const auto& a:AffixCatalog()) available.insert(a.first);
            if(value!="none") { std::istringstream in(value); std::string part;
                if(value.empty()||value.back()==',') throw std::runtime_error("invalid affix IDs");
                while(std::getline(in,part,',')) { const int id=Integer(part);
                    if(!available.contains(id) || next.affixes.size()>=64) throw std::runtime_error("unknown affix ID or more than 64 IDs");
                    if(std::find(next.affixes.begin(),next.affixes.end(),id)==next.affixes.end()) next.affixes.push_back(id); }
            }
        } else {
            const int v=Integer(value); if(v!=0&&v!=1) throw std::runtime_error("expected 0 or 1");
            if(key=="t7") next.t7=v!=0; else if(key=="filter") next.respectFilter=v!=0; else if(key=="materials") next.materials=v!=0;
            else { const char* names[]{"gold","potions","xp","favor","bones"}; int index=-1;
                for(int i=0;i<5;++i) if(key==names[i]) index=i;
                if(index<0) throw std::runtime_error("unknown pickup category"); next.categories[index]=v!=0; }
        }
        policy=std::move(next); return State();
    });
}
#ifdef EPOCHPACT_RESEARCH
std::string Test(const std::string& id,const std::string& action) {
    return Run([=] {
        if(!CanCollect()) throw std::runtime_error("isolated offline character required");
        void* actor=Invoke(getActor); void* character=Get<void*>(Get<void*>(actor,actorTracker),trackerData);
        if(Text(Invoke(Method(character,"get_Id",0),character))!=id ||
           Text(Get<void*>(character,Offset("CharacterData","<CharacterName>k__BackingField","LE.Data")))!="EpCraftTest")
            throw std::runtime_error("loot probe requires matching isolated EpCraftTest");
        const auto generator=Get<void*>(actor,Offset("Actor","generateItems"));
        if(action=="filterprobe") {
            bool compatible=false; int level=8; void* args[]{&compatible,&level};
            Root item(Invoke(Resolve("GenerateItems","GenerateItemForShop",2),generator,args));
            const auto query=[&](int outcome) {
                Root filter(il2cpp::api().object_new(game::FindClass("LE.dll","ItemFiltering","ItemFilter")));
                Invoke(Resolve("ItemFilter",".ctor",0,"ItemFiltering"),filter.Get());
                Root rule(il2cpp::api().object_new(game::FindClass("LE.dll","ItemFiltering","Rule")));
                void* ctorArgs[]{&outcome}; Invoke(Resolve("Rule",".ctor",1,"ItemFiltering"),rule.Get(),ctorArgs);
                void* rules=Get<void*>(filter.Get(),Offset("ItemFilter","rules","ItemFiltering"));
                void* addArgs[]{rule.Get()}; Invoke(Method(rules,"Add",1),rules,addArgs);
                return FilterMatch(filter.Get(),item.Get());
            };
            return std::string("{\"ok\":true,\"show\":")+Boolean(query(0))+",\"hide\":"+Boolean(query(1))+"}";
        }
        if(action=="drop" || action=="materials") {
            Root transform(Invoke(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Component","get_transform",0),actor));
            struct Position {float x,y,z;} position=Value<Position>(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine","Transform","get_position",0),transform.Get());
            int level=8; bool notify=false;
            if(action=="materials") {void* args[]{&level,&position,&notify}; for(int i=0;i<8;++i) Invoke(Resolve("GenerateItems","SpawnCraftingItem",3),generator,args);}
            else {
                bool compatible=false; void* args[]{&compatible,&level}; int scene=Value<int>(game::FindMethod("UnityEngine.CoreModule.dll","UnityEngine.SceneManagement","SceneManager","GetActiveScene",0));
                for(int i=0;i<8;++i) { Root item(Invoke(Resolve("GenerateItems","GenerateItemForShop",2),generator,args));
                    void* dropArgs[]{item.Get(),&position,&scene}; Invoke(Resolve("GenerateItems","DropItemAtPoint",3),generator,dropArgs); }
            }
            return std::string("{\"ok\":true,\"spawnRequests\":8}");
        }
        if(action!="ground") throw std::runtime_error("unknown isolated loot probe");
        const auto field=game::FindStaticField("LE.dll","","ItemTooltipOrganizer","pickableGroundLabelList");
        const auto labelClass=game::FindClass("LE.dll","","GroundItemLabel");
        void* labels=game::StaticObject(field); std::ostringstream out; out << "{\"ok\":true,\"items\":["; bool first=true;
        if(labels) {
            const auto f=il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(labels),"_list");
            if(!f) throw std::runtime_error("label list layout unavailable");
            for(void* label:Entries(Get<void*>(labels,il2cpp::api().field_get_offset(f)))) {
                if(!label || il2cpp::api().object_get_class(label)!=labelClass || !game::IsAlive(label)) continue;
                void* item=Invoke(getItem,label); if(!item) continue;
                if(!first) out<<','; first=false;
                out << "{\"id\":" << Json(std::to_string(reinterpret_cast<uintptr_t>(item))) << ",\"selected\":" << Boolean(Decision(label))
                    << ",\"filterPass\":" << Boolean(Filter(item)) << ",\"lp\":" << static_cast<int>(Get<uint8_t>(item,lp))
                    << ",\"unique\":" << Boolean(Value<bool>(isUnique,item))
                    << ",\"material\":" << Boolean(Value<bool>(isShard,item)||Value<bool>(isRune,item)||Value<bool>(isGlyph,item)) << ",\"t7Ids\":[";
                bool firstAffix=true;
                if(void* list=Get<void*>(item,affixes)) for(void* a:Entries(list)) if(a && Get<uint8_t>(a,affixTier)==6) {
                    if(!firstAffix) out<<','; firstAffix=false; out << Get<uint16_t>(a,affixId);
                }
                out << "]}";
            }
        }
        out << "]}"; return out.str();
    });
}
#endif
}
