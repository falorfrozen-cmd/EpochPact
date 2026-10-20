#pragma once
#include "ingame_browser_rules.hpp"
#include <array>
namespace ep::ingame::model {
inline constexpr const char* kDamageTypes[]{"Physical","Fire","Cold","Lightning","Necrotic","Void","Poison"};
inline bool DamageIconKey(const std::string& key) {
    if (key.size() < 3 || key.size() > 40 || (key[0] != 'a' && key[0] != 'c') || key[1] != ':') return false;
    int separators = 0, digits = 0;
    for (size_t i=2; i<key.size(); ++i) {
        const char c=key[i];
        if (c==':' && key[0]=='c' && !separators && digits) { ++separators; digits=0; }
        else if (c>='0' && c<='9') ++digits;
        else return false;
    }
    return digits && separators == (key[0]=='c' ? 1 : 0);
}
inline J FeedShown(const J& options) {
    J show=Object();
    for (const auto* key : {"normal","magic","rare","exalted","unique","set","legendary","gold","amber","ember","xp","shard","rune","glyph","material"}) {
        bool visible=true;
        if (std::string(key)=="gold" && options["gold"].IsBool()) visible=On(options["gold"]);
        if ((std::string(key)=="shard" || std::string(key)=="rune" || std::string(key)=="glyph" || std::string(key)=="material") && options["materials"].IsBool()) visible=On(options["materials"]);
        if (options["show"][key].IsBool()) visible=On(options["show"][key]);
        Put(show,key,Flag(visible));
    }
    return show;
}
inline J Finite(const J& v) { return v.IsNumber() && std::isfinite(v.number) ? v : J{}; }
inline J Sum(const std::vector<J>& values) { double n=0; bool have=false; for(const auto& v:values)if(v.IsNumber()&&std::isfinite(v.number)){n+=v.number;have=true;} return have?Number(n):J{}; }
inline J DamageTypes(const J& source,const std::string& metric,const J& state) {
    J result=List(); const auto& elements=source["elements"];
    if(!elements.IsObject())return {};
    const bool average=metric=="average"||metric=="combatAverage";
    const auto& values=elements[average?"total":metric]; if(!values.IsArray())return {};
    const double seconds=state[metric=="average"?"elapsedSeconds":"combatSeconds"].Num();
    for(int i=0;i<7;++i){auto v=Finite(i<static_cast<int>(values.items.size())?values.items[i]:J{});if(average&&v.IsNumber())v=Number(seconds>0?v.number/seconds:0);result.items.push_back(v);}return result;
}
inline J DamageModel(const J& state,std::string metric="dps10",int element=-1) {
    static const std::set<std::string> metrics{"dps10","dps60","dps600","dps3600","average","combatAverage"};if(!metrics.count(metric))metric="dps10";
    if(element<0||element>6)element=-1;
    J result=Object(),sources=List();std::vector<J> totals;
    for(const auto& raw:state["types"].items){
        if(raw["name"].Str().empty())continue;
        J source=raw;auto values=DamageTypes(raw,metric,state);Put(source,"values",values);
        auto dps=element<0?Finite(raw[metric]):values.IsArray()?values.items[element]:J{};
        Put(source,"dps",dps);totals.push_back(dps);sources.items.push_back(std::move(source));
    }
    Put(result,"sources",sources);Put(result,"total",Sum(totals));Put(result,"active",Flag(On(state["active"])));
    Put(result,"ok",Flag(state.IsObject()&&state["types"].IsArray()&&(!state["ok"].IsBool()||On(state["ok"]))));
    return result;
}
inline std::string LootCategory(const J& e) {
    const auto kind=e["kind"].Str(),name=e["name"].Str();if(kind=="gold")return "gold";
    if(kind=="amber")return name=="Soul Ember"?"ember":name=="XP mote"?"xp":"amber";
    if(kind=="material"){const auto type=e["type"].Num();return type==101?"shard":type==102?"rune":type==103?"glyph":"material";}
    const auto rarity=Fold(e["rarity"].Str());static const std::set<std::string> rarities{"normal","magic","rare","exalted","unique","set","legendary"};return rarities.count(rarity)?rarity:"normal";
}
inline J MergeLoot(J entries,const J& reply) {
    if(!entries.IsArray())entries=List();std::set<std::string> seen;
    for(const auto& e:entries.items)seen.insert(e["session"].Str()+":"+Id(e["seq"]));
    for(auto e:reply["entries"].items)if(e["seq"].IsNumber()&&std::isfinite(e["seq"].number)){
        const auto key=reply["session"].Str()+":"+Id(e["seq"]);if(!seen.insert(key).second)continue;
        Put(e,"session",Text(reply["session"].Str()));entries.items.push_back(std::move(e));
    }
    if(entries.items.size()>300)entries.items.erase(entries.items.begin(),entries.items.end()-300);return entries;
}
inline J LootRows(const J& entries,const J& shown) {
    J out=List();std::string lastKey;
    for(auto it=entries.items.rbegin();it!=entries.items.rend();++it){
        const auto& e=*it;const auto category=LootCategory(e);if(shown[category].IsBool()&&!On(shown[category]))continue;
        const auto kind=e["kind"].Str();const auto key=kind=="gold"?"gold":(kind=="amber"||kind=="material")?kind+":"+e["name"].Str():"";
        if(!key.empty()&&lastKey==key&&!out.items.empty()){auto& last=out.items.back();Put(last,"gold",Number(last["gold"].Num()+e["gold"].Num()));Put(last,"count",Number(last["count"].Num()+1));}
        else {if(out.items.size()>=40)break;auto row=e;Put(row,"count",Number(1));Put(row,"category",Text(category));out.items.push_back(std::move(row));lastKey=key;}
    }return out;
}
} // namespace ep::ingame::model
