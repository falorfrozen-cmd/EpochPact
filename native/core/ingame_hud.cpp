// Native equivalents of feed.html and damage.html. Separate canvases, no external windows.
#include "ingame_window.hpp"
#include "ingame_hud_rules.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <cmath>
#include <cstdio>
#include <sstream>
#pragma comment(lib, "crypt32.lib")

namespace ep::ingame::view {
namespace {
using namespace model;
using assets::Face;
using managed::Invoke;
struct Cell { Pin text, bar; std::string source, field, shown; int element=-1; bool shortNumber=false; float width=0; };
struct LootTip { float y=0,h=0; J entry; };
struct Hud {
    bool damage=false,open=false,options=false,dirty=true,recap=false,down=false,resizing=false,dragging=false,chromeShown=true,underMouse=false,fixture=false,resumePending=false;
    int owner=-10,pressed=-1,deathIndex=0; size_t bodyHit=0; float x=18,y=100,w=430,h=300,scroll=0,contentH=0,startX=0,startY=0,startW=0,startH=0,topH=31;
    Pin root,canvas,body,content,chrome,listener; std::vector<Hit> hits; std::vector<Cell> cells;
    J optionsData,reply,entries,recapData; std::string structure,selected; uint64_t nextRead=0,nextRecap=0; bool reading=false;
    std::vector<LootTip> lootTips; Pin tooltip; int tip=-1;
};
Hud damage,feed;
Pin hudParent;
float scale=1,screenWidth=1600,screenHeight=900;
bool loaded=false;
std::map<std::string,int> iconTries;
std::map<int,int> affixTries;
std::map<int,J> affixes;
constexpr const char* metrics[]{"dps10","dps60","dps600","dps3600","average","combatAverage"};
constexpr const char* metricLabels[]{"10 s","60 s","10 min","60 min","Session","Combat"};
constexpr const char* styles[]{"quiet","bars","split","table"};
constexpr const char* styleLabels[]{"Quiet HUD","Pact Bars","Split Meter","Table"};
constexpr const char* categories[]{"normal","magic","rare","exalted","unique","set","legendary","gold","amber","ember","xp","shard","rune","glyph","material"};
constexpr const char* categoryLabels[]{"Normal","Magic","Rare","Exalted","Unique","Set","Legendary","Gold","Memory Amber","Soul Ember","XP motes","Affix shards","Runes","Glyphs","Other materials"};
std::string Key(const Hud& h) { return h.damage?"epochpact.damage-window.v1":"epochpact.loot-feed.v1"; }
std::string Name(const Hud& h) { return h.damage?"damage":"feed"; }
std::string DisplayNumber(double n,bool shortNumber=false) {
    if(!std::isfinite(n))return "—";char out[64];
    double unit=1;const char* suffix="";if(std::abs(n)>=999.5e6){unit=1e9;suffix="B";}else if(std::abs(n)>=1e6||(shortNumber&&std::abs(n)>=999.5e3)){unit=1e6;suffix="M";}else if(shortNumber&&std::abs(n)>=999.5){unit=1e3;suffix="k";}
    if(unit!=1){const auto v=std::abs(n/unit);std::snprintf(out,sizeof out,v>=99.95?"%.0f%s":v>=9.995?"%.1f%s":"%.2f%s",n/unit,suffix);return out;}
    std::string text=std::to_string(static_cast<long long>(std::llround(n))),result;for(size_t i=0;i<text.size();++i){if(i&&text[i]!='-'&&(text.size()-i)%3==0&&text[i-1]!='-')result+=',';result+=text[i];}return result;
}
bool ColorCode(const std::string& text) { return text.size()==7&&text[0]=='#'&&std::all_of(text.begin()+1,text.end(),[](unsigned char c){return std::isxdigit(c)!=0;}); }
Color InkColor(const Hud& h) {const auto text=h.optionsData["text"].Str("#eee6d5");return Hex(ColorCode(text)?text:"#eee6d5");}
float FontSize(const Hud& h){return static_cast<float>(std::clamp(h.optionsData["fontSize"].Num(13),10.,22.));}
int Index(const std::string& v,const char* const* values,int count,int fallback=0){for(int i=0;i<count;++i)if(v==values[i])return i;return fallback;}
void Save(Hud& h) {
    const auto text=Pack(h.optionsData);if(!StorePreference(Key(h),&text))Toast(Tr("Could not save local preferences."),true);
}
void SavePlace(Hud& h) {
    auto windows=Preference("epochpact.windows.v1");if(!windows.IsObject())windows=Object();auto state=windows[Name(h)];if(!state.IsObject())state=Object();
    Put(state,"open",Flag(h.open));auto native=Object();Put(native,"x",Number(h.x));Put(native,"y",Number(h.y));Put(native,"w",Number(h.w));Put(native,"h",Number(h.h));Put(state,"ingame",native);Put(windows,Name(h),state);
    const auto text=Pack(windows);if(!StorePreference("epochpact.windows.v1",&text))Toast(Tr("Could not save window position."),true);
}
void Clamp(Hud& h){
    const float base=h.damage?260.f:220.f,available=std::max(base,screenWidth-20);
    // A remembered compact HUD width must not squeeze nine table columns into
    // 300 px on startup. Apply the same limit on load, style changes and resize.
    const bool table=h.damage&&h.optionsData["style"].Str()=="table";
    const float minimumW=std::min(available,table?(On(h.optionsData["compact"])?560.f:900.f):base);
    h.w=std::clamp(h.w,minimumW,available);
    const float minimum=h.damage?180.f:120.f;
    h.h=std::clamp(h.h,minimum,std::max(minimum,screenHeight-20));
    h.x=std::clamp(h.x,0.f,std::max(0.f,screenWidth-h.w));h.y=std::clamp(h.y,0.f,std::max(0.f,screenHeight-h.h));
}
void DestroyNode(Pin& pin){if(pin){void* a[]{GameObjectOf(pin.Get())};SetActive(a[0],false);Invoke(U("Object","Destroy",1),nullptr,a);pin.Reset();}}
J State(const Hud& h) {
    if(h.optionsData["view"].Str()=="taken") {auto r=h.reply["taken"];Put(r,"ok",Flag(On(h.reply["ok"])));Put(r,"active",h.reply["active"]);Put(r,"elapsedSeconds",h.reply["elapsedSeconds"]);return r;}
    return h.reply;
}
J Model(const Hud& h){return DamageModel(State(h),h.optionsData["metric"].Str("dps10"),static_cast<int>(h.optionsData["element"].Num(-1)));}
std::string Label(const J& row){return row["name"].Str()=="You"?Tr("You"):DamageSourceLabel(row);}
bool Hidden(const Hud& h,const std::string& key){const auto& list=h.optionsData[h.optionsData["view"].Str()=="taken"?"takenHidden":"hidden"];return std::any_of(list.items.begin(),list.items.end(),[&](const J& v){return v.Str()==key;});}
std::string Selected(const Hud& h,const J& m) {
    for(const auto& r:m["sources"].items)if(!Hidden(h,r["name"].Str())&&r["name"].Str()==h.selected)return h.selected;
    if(h.optionsData["view"].Str()=="taken")return "__total";
    for(const auto& r:m["sources"].items)if(!Hidden(h,r["name"].Str())&&r["name"].Str()=="You")return "You";
    for(const auto& r:m["sources"].items)if(!Hidden(h,r["name"].Str()))return r["name"].Str();return "";
}
void AddText(Hud& h,void* parent,float x,float y,float w,float height,const std::string& text,float font=0,Color color={},bool right=false) {
    Style s{Face::Body,font?font:FontSize(h),color.a?color:InkColor(h)};s.align=right?kRight:kLeft;auto label=TextBox(parent,"HUD text",x,y,w,height,s,text);
    int ellipsis=1;void* overflow[]{&ellipsis};Invoke(T("set_overflowMode",1),label,overflow);
}
void ValueCell(Hud& h,void* parent,float x,float y,float w,float height,const std::string& source,const std::string& field,int element=-1,float font=0,bool right=true,bool shortNumber=false){
    Style s{Face::BodySemi,font?font:FontSize(h),InkColor(h)};s.align=right?kRight:kLeft;Cell cell;cell.text.Set(TextBox(parent,"HUD value",x,y,w,height,s,"—"));cell.source=source;cell.field=field;cell.element=element;cell.shortNumber=shortNumber;h.cells.push_back(std::move(cell));
}
void HideHud(Hud& h){h.open=false;h.underMouse=false;SavePlace(h);ForgetInputs(h.owner);if(h.listener){bool no=false;void* a[]{&no};Invoke(Find("LE.dll","","UIMouseListener","set_underMouse",1),h.listener.Get(),a);}if(h.root)SetActive(GameObjectOf(h.root.Get()),false);}
void Cycle(Hud& h,const std::string& key,const char* const* values,int count){Put(h.optionsData,key,Text(values[(Index(h.optionsData[key].Str(),values,count)+1)%count]));Save(h);h.dirty=true;}
void DamageAction(Hud& h,const std::string& action){
    if(!Eligible()){Toast(Tr("Load an offline character and wait for the area to finish loading."),true);return;}
    Send("damagesession "+action,[&h,action](const std::string& text){std::string why;if(Refused(text,&why)){Toast(PlayerError(why),true);return;}if(action=="start"||action=="stop"){Put(h.optionsData,"running",Flag(action=="start"));Save(h);}h.nextRead=0;});
}
float Strip(Hud& h,const Ctx& c,float x,float y,float w,const std::string& source,bool lines=false){
    const float height=lines?28.f:62.f,cellW=w/7;
    for(int i=0;i<7;++i){const float xx=lines?x:x+i*cellW,yy=lines?y+i*height:y;
        Picture(c.parent,"Damage type",xx+(lines?0:(cellW-18)/2),yy+2,18,18,"dmg-type-"+std::to_string(i)+".png",false,kWhite);
        AddText(h,c.parent,xx+(lines?24:0),yy+(lines?0:21),lines?w-95:cellW,18,Tr(kDamageTypes[i]),lines?FontSize(h):9);
        ValueCell(h,c.parent,lines?x+w-78:xx,yy+(lines?0:39),lines?78:cellW,20,source,"value",i,0,lines,true);
        Hit hit;hit.name="hud-type:"+std::to_string(i);hit.x=xx;hit.y=yy;hit.w=lines?w:cellW;hit.h=height;
        hit.action=[&h,i]{const int selected=static_cast<int>(h.optionsData["element"].Num(-1));Put(h.optionsData,"element",Number(selected==i?-1:i));Save(h);h.dirty=true;};c.Add(std::move(hit));
    }return lines?height*7:height;
}
float SourceRow(Hud& h,const Ctx& c,float x,float y,float w,const J& source,bool bar=true) {
    const float height=FontSize(h)+18;const auto key=source["name"].Str();
    void* fill=bar?Fill(c.parent,"Damage bar",x,y,0,height,Hex("#88bfa220"),3):nullptr;
    const auto icon=source["icon"].Str();
    if(!icon.empty()&&assets::RuntimeSprite(icon))Picture(c.parent,"Source portrait",x+5,y+(height-20)/2,20,20,"runtime:"+icon,false,kWhite);
    else Solid(c.parent,"Source marker",x+11,y+(height-6)/2,6,6,key=="You"?Hex("#cbb581"):Hex("#88bfa2"));
    AddText(h,c.parent,x+31,y,w-129,height,Label(source)+(source["live"].Num()>1?" ×"+Num(source["live"].Num()):""));
    ValueCell(h,c.parent,x+w-95,y,86,height,key,"value");
    if(fill){h.cells.back().bar.Set(TransformOf(fill));h.cells.back().width=w;}
    Hit hit;hit.name="hud-source:"+key;hit.x=x;hit.y=y;hit.w=w;hit.h=height;hit.action=[&h,key]{h.selected=key;h.dirty=true;};c.Add(std::move(hit));return height+4;
}
float Options(Hud& h,const Ctx& c,float y) {
    const float w=h.w-20;auto button=[&](const std::string& text,Action action){auto b=AddButton(c,10,y,text,Look::Small,std::move(action));y+=b.h+8;};
    if(h.damage){
        button(Tr("Style")+": "+styleLabels[Index(h.optionsData["style"].Str(),styles,4)],[&h]{Cycle(h,"style",styles,4);if(h.optionsData["style"].Str()=="table")h.w=std::min(screenWidth-20,900.f);Clamp(h);});
        if(h.optionsData["style"].Str()=="table") y+=CheckField(c,10,y,Tr("Compact table"),On(h.optionsData["compact"]),[&h](bool on){Put(h.optionsData,"compact",Flag(on));Clamp(h);Save(h);h.dirty=true;},"hud-compact")+8;
        button(Tr("Damage type")+": "+(h.optionsData["element"].Num(-1)<0?Tr("All damage"):Tr(kDamageTypes[std::clamp(static_cast<int>(h.optionsData["element"].Num()),0,6)])),[&h]{Put(h.optionsData,"element",Number(static_cast<int>(h.optionsData["element"].Num(-1))+1>6?-1:h.optionsData["element"].Num(-1)+1));Save(h);h.dirty=true;});
        auto model=Model(h);const auto key=h.optionsData["view"].Str()=="taken"?"takenHidden":"hidden";
        for(const auto& row:model["sources"].items){const auto name=row["name"].Str();
            y+=CheckField(c,10,y,Label(row),!Hidden(h,name),[&h,name,key](bool on){auto list=h.optionsData[key];if(!list.IsArray())list=List();list.items.erase(std::remove_if(list.items.begin(),list.items.end(),[&](const J& v){return v.Str()==name;}),list.items.end());if(!on)list.items.push_back(Text(name));Put(h.optionsData,key,list);Save(h);h.dirty=true;},"hud-show:"+name)+6;
        }
    }else{
        auto setAll=[&h](bool on){auto show=Object();for(const auto* key:categories)Put(show,key,Flag(on));Put(h.optionsData,"show",show);Save(h);h.dirty=true;};
        auto all=AddButton(c,10,y,Tr("All"),Look::Small,[setAll]{setAll(true);});auto none=AddButton(c,20+all.w,y,Tr("None"),Look::Small,[setAll]{setAll(false);});y+=std::max(all.h,none.h)+12;
        for(size_t i=0;i<std::size(categories);++i){
            if(i==0||i==7||i==11){AddText(h,c.parent,10,y,w,24,Tr(i==0?"Items":i==7?"Resources":"Materials"),13);y+=28;}
            const auto* category=categories[i];y+=CheckField(c,10,y,Tr(categoryLabels[i]),!h.optionsData["show"][category].IsBool()||On(h.optionsData["show"][category]),[&h,key=std::string(category)](bool on){auto show=h.optionsData["show"];if(!show.IsObject())show=Object();Put(show,key,Flag(on));Put(h.optionsData,"show",show);Save(h);h.dirty=true;},"hud-feed-show:"+std::string(category))+6;
        }
    }
    y+=10;const auto colors=std::make_shared<std::pair<std::string,std::string>>(h.optionsData["background"].Str("#091310"),h.optionsData["text"].Str("#eee6d5"));
    y+=InputField(c,10,y,w,Tr("Background colour"),colors->first,"#091310",false,[colors](const std::string& v){colors->first=v;},"hud-background")+8;
    y+=InputField(c,10,y,w,Tr("Text colour"),colors->second,"#eee6d5",false,[colors](const std::string& v){colors->second=v;},"hud-text")+8;
    button(Tr("Apply colours"),[&h,colors]{if(!ColorCode(colors->first)||!ColorCode(colors->second)){Toast(Tr("Use six hexadecimal digits, for example #091310."),true);return;}Put(h.optionsData,"background",Text(colors->first));Put(h.optionsData,"text",Text(colors->second));Save(h);h.dirty=true;});
    button(Tr("Background opacity")+": "+Num(h.optionsData["backgroundOpacity"].Num(85))+"%",[&h]{Put(h.optionsData,"backgroundOpacity",Number(static_cast<int>(h.optionsData["backgroundOpacity"].Num(85)+5)%105));Save(h);h.dirty=true;});
    button(Tr("Text size")+": "+Num(FontSize(h)),[&h]{Put(h.optionsData,"fontSize",Number(FontSize(h)>=22?10:FontSize(h)+1));Save(h);h.dirty=true;});
    static const char* spacing[]{"compact","normal","roomy"};button(Tr("Row spacing")+": "+h.optionsData["spacing"].Str("normal"),[&h]{Cycle(h,"spacing",spacing,3);});
    button(Tr("Top bar")+": "+Tr(h.optionsData["bar"].Str()=="always"?"Always":"On hover"),[&h]{Put(h.optionsData,"bar",Text(h.optionsData["bar"].Str()=="always"?"hover":"always"));Save(h);h.dirty=true;});
    return y;
}
float DamageBody(Hud& h,const Ctx& c) {
    auto m=Model(h);float y=10;const auto style=h.optionsData["view"].Str()=="taken"?"bars":h.optionsData["style"].Str("quiet");const auto selected=Selected(h,m);
    if(!On(m["ok"])||m["sources"].items.empty()){AddText(h,c.parent,10,y,h.w-20,55,Tr(Eligible()?"No damage recorded yet. Start the session and fight.":"Waiting for an offline character…"));return 65;}
    const auto find=[&](const std::string& key)->const J*{for(const auto& r:m["sources"].items)if(r["name"].Str()==key)return &r;return nullptr;};
    if(style=="quiet"){
        const auto* source=find(selected);AddText(h,c.parent,10,y,h.w-125,26,source?Label(*source):Tr("All attackers"));ValueCell(h,c.parent,h.w-115,y,105,26,selected,"value",-1,20);y+=34;
        y+=Strip(h,c,10,y,h.w-20,selected)+10;
        auto roster=AddButton(c,10,y,Tr("Allies")+(On(h.optionsData["roster"])?" ⌃":" ⌄"),Look::Small,[&h]{Put(h.optionsData,"roster",Flag(!On(h.optionsData["roster"])));Save(h);h.dirty=true;});
        ValueCell(h,c.parent,h.w-115,y,105,roster.h,"__allies","value");y+=roster.h+8;
        if(On(h.optionsData["roster"]))for(const auto& r:m["sources"].items)if(r["name"].Str()!="You"&&!Hidden(h,r["name"].Str()))y+=SourceRow(h,c,10,y,h.w-20,r);
    }else if(style=="split"){
        const float col=(h.w-30)/2;AddText(h,c.parent,10,y,col,24,Tr("You"));AddText(h,c.parent,col+20,y,col,24,Tr("Allies"));y+=30;
        Strip(h,c,10,y,col,"You",true);float right=y;
        for(const auto& r:m["sources"].items)if(r["name"].Str()!="You"&&!Hidden(h,r["name"].Str()))right+=SourceRow(h,c,col+20,right,col,r);
        right+=10+Strip(h,c,col+20,right+10,col,selected=="You"?"__allies":selected,true);y=std::max(y+196,right);
    }else if(style=="table"){
        const std::vector<std::pair<std::string,std::string>> columns=On(h.optionsData["compact"])?std::vector<std::pair<std::string,std::string>>{{"dps10","DPS 10 s"},{"dps60","DPS 60 s"},{"combatAverage","Avg in combat"},{"total","Total damage"}}:
            std::vector<std::pair<std::string,std::string>>{{"dps10","DPS 10 s"},{"dps60","DPS 60 s"},{"dps600","DPS 10 min"},{"dps3600","DPS 60 min"},{"combatAverage","Avg in combat"},{"average","Avg session"},{"total","Total damage"},{"hits","Hits"},{"maxHit","Biggest hit"}};
    const float first=std::max(110.f,(h.w-20)*.24f),col=(h.w-20-first)/static_cast<float>(columns.size());
        AddText(h,c.parent,10,y,first,24,Tr("Source"),11);for(size_t i=0;i<columns.size();++i)AddText(h,c.parent,10+first+i*col,y,col,24,Tr(columns[i].second),10);y+=26;
        for(const auto& r:m["sources"].items)if(!Hidden(h,r["name"].Str())){AddText(h,c.parent,10,y,first-6,26,Label(r),11);for(size_t i=0;i<columns.size();++i)ValueCell(h,c.parent,10+first+i*col,y,col-4,26,r["name"].Str(),columns[i].first,-1,11);y+=28;}
    }else{
        auto rows=m["sources"].items;std::stable_sort(rows.begin(),rows.end(),[](const J& a,const J& b){return a["dps"].Num(-1)>b["dps"].Num(-1);});
        AddText(h,c.parent,10,y,h.w-20,22,Tr(h.optionsData["view"].Str()=="taken"?"Damage taken by attacker":"Damage by source"),11);y+=26;
        for(const auto& r:rows)if(!Hidden(h,r["name"].Str()))y+=SourceRow(h,c,10,y,h.w-20,r);
        y+=10;const auto* row=find(selected);AddText(h,c.parent,10,y,h.w-20,22,Tr("Damage types")+" · "+(row?Label(*row):Tr("All attackers")),11);y+=26;y+=Strip(h,c,10,y,h.w-20,selected);
    }
    if(h.reply["unsplitHits"].Num()>0){AddText(h,c.parent,10,y+6,h.w-20,30,Tr("Some hits are not split by damage type."),10);y+=36;}return y+10;
}
Color LootColor(const std::string& category) {if(category=="unique")return Hex("#d99248");if(category=="set")return Hex("#68c47c");if(category=="exalted")return Hex("#c091ee");if(category=="rare")return Hex("#e5d66d");if(category=="magic")return Hex("#79b6e8");if(category=="legendary")return Hex("#e56c6c");return Hex("#eee6d5");}
float LootBody(Hud& h,const Ctx& c){
    const auto rows=LootRows(h.entries,h.optionsData["show"]);float y=8;const auto space=h.optionsData["spacing"].Str("normal");const float gap=space=="compact"?1.f:space=="roomy"?8.f:4.f;
    if(rows.items.empty()){AddText(h,c.parent,10,y,h.w-20,35,Tr(h.entries.items.empty()?"Waiting for pickups…":"No pickups in the categories you show."));return 50;}
    for(const auto& e:rows.items){const float start=y;const auto kind=e["kind"].Str();std::string label=kind=="gold"?"+"+DisplayNumber(e["gold"].Num())+" "+Tr("Gold"):e["name"].Str("Item");
        if(kind=="amber"&&e["gold"].Num()>0)label+=" ×"+DisplayNumber(e["gold"].Num());if(e["count"].Num()>1)label+=" ("+Num(e["count"].Num())+" "+Tr("pickups")+")";
        if(!e["tiers"].Str().empty()){std::istringstream tiers(e["tiers"].Str());std::string tier;while(tiers>>tier)label+=" T"+tier;}
        AddText(h,c.parent,10,y,h.w-20,FontSize(h)+9,label,0,LootColor(e["category"].Str()));y+=FontSize(h)+9;
        if(e["ruleNumber"].Num()>0){AddText(h,c.parent,10,y,h.w-20,16,Tr("Rule")+" "+Num(e["ruleNumber"].Num())+" · "+e["rule"].Str(),10,Hex("#aebdb7"));y+=17;}y+=gap;
        h.lootTips.push_back({start,y-start,e});
    }return y+8;
}
void Update(Hud& h);
void Draw(Hud& h) {
    ForgetInputs(h.owner);DestroyNode(h.tooltip);h.tip=-1;h.lootTips.clear();DestroyNode(h.body);h.hits.clear();h.cells.clear();h.chrome.Reset();h.content.Reset();
    if(!h.open){if(h.root)SetActive(GameObjectOf(h.root.Get()),false);return;}
    Clamp(h);SetActive(GameObjectOf(h.root.Get()),true);Move(h.root.Get(),h.x,h.y);Resize(h.root.Get(),h.w,h.h);
    const auto body=New("HUD body",h.root.Get(),0,0,h.w,h.h);h.body.Set(body.rt);
    const auto bg=h.optionsData["background"].Str("#091310");Fill(body.rt,"Background",0,0,h.w,h.h,Alpha(Hex(ColorCode(bg)?bg:"#091310"),static_cast<float>(std::clamp(h.optionsData["backgroundOpacity"].Num(85),0.,100.)/100)),3);
    Outline(body.rt,"Border",0,0,h.w,h.h,Hex("#92734055"),3);
    h.topH=31.f;
    auto chrome=New("HUD controls",body.rt,0,0,h.w,h.topH);h.chrome.Set(chrome.rt);h.chromeShown=true;auto bar=Solid(chrome.rt,"Top bar",0,0,h.w,h.topH,Hex("#07120fdd"));
    Ctx top{&h.hits,Area::Fixed,chrome.rt,0,0,h.owner};AddText(h,chrome.rt,8,0,h.damage?55.f:h.w-80,30,Tr(h.damage?"Damage":"Recent pickups"),12);
    auto close=AddButton(top,h.w-5,1,"−",Look::Small,[&h]{HideHud(h);},true,"hud-hide");
    auto gear=AddButton(top,h.w-close.w-8,1,"⚙",Look::Small,[&h]{h.options=!h.options;h.dirty=true;h.scroll=0;if(h.options)h.h=std::max(h.h,450.f);},true,"hud-options");
    if(h.damage){
        float x=62,y=1;const float firstLimit=h.w-close.w-gear.w-19;
        auto flow=[&](const std::string& label,Action action,const std::string& name){auto b=AddButton(top,0,0,label,Look::Small,std::move(action),false,name);if(x+b.w>(y<2?firstLimit:h.w-8)){x=8;y+=b.h+2;}Move(b.node.rt,x,y);h.hits[b.hit].x=x;h.hits[b.hit].y=y;x+=b.w+4;h.topH=std::max(h.topH,y+b.h+1);};
        const bool active=On(h.reply["active"]);flow(active?"||":"▶",[&h,active]{DamageAction(h,active?"stop":"start");},"hud-session");
        flow("↺",[&h]{DamageAction(h,"reset");},"hud-reset");
        flow(h.optionsData["view"].Str()=="taken"?Tr("Taken"):Tr("Dealt"),[&h]{Put(h.optionsData,"view",Text(h.optionsData["view"].Str()=="taken"?"dealt":"taken"));h.selected.clear();Save(h);h.dirty=true;},"hud-view");
        const int metric=Index(h.optionsData["metric"].Str(),metrics,6);flow(metricLabels[metric],[&h]{Cycle(h,"metric",metrics,6);},"hud-metric");
        flow("☠",[&h]{h.recap=!h.recap;h.options=false;h.scroll=0;h.nextRecap=0;h.dirty=true;},"hud-recap");
    }
    Resize(chrome.rt,h.w,h.topH);Resize(TransformOf(bar),h.w,h.topH);
    const float foot=h.damage?52.f:30.f;
    const float topY=h.topH+1;h.bodyHit=h.hits.size();
    auto viewport=New("HUD viewport",body.rt,0,topY,h.w,std::max(1.f,h.h-topY-foot));Clip(viewport);auto content=New("HUD contents",viewport.rt,0,-h.scroll,h.w,1000);h.content.Set(content.rt);
    Ctx c{&h.hits,Area::Fixed,content.rt,0,topY,h.owner};h.contentH=h.recap?DrawHudRecap(c,10,8,h.w-20,&h.deathIndex,[&h]{h.dirty=true;}):h.options?Options(h,c,8):h.damage?DamageBody(h,c):LootBody(h,c);Resize(content.rt,h.w,h.contentH);
    h.scroll=std::clamp(h.scroll,0.f,std::max(0.f,h.contentH-(h.h-topY-foot)));Move(content.rt,0,-h.scroll);
    for(size_t i=h.bodyHit;i<h.hits.size();++i)h.hits[i].y-=h.scroll;
    const float fy=h.h-foot;Solid(body.rt,"Footer border",8,fy,h.w-16,1,Hex("#92734055"));
    if(h.damage){
        AddText(h,body.rt,10,fy+3,h.w-130,18,Tr(h.optionsData["view"].Str()=="taken"?"DAMAGE TAKEN":"TOTAL DPS"),10,Hex("#cbb581"));
#ifdef EPOCHPACT_TESTING
        if(h.fixture)AddText(h,body.rt,h.w-126,fy+3,100,18,"PREVIEW DATA",8,Hex("#eeaa66"),true);
#endif
        ValueCell(h,body.rt,10,fy+20,h.w-20,29,"__total","value",-1,25);
        Solid(body.rt,"Recording",h.w-19,fy+8,6,6,On(h.reply["active"])?Hex("#88bfa2"):Hex("#555b58"));
    }else{double gold=0;for(const auto& e:h.entries.items)if(e["kind"].Str()=="gold")gold+=e["gold"].Num();AddText(h,body.rt,10,fy+3,h.w-20,24,Tr("Gold")+": "+DisplayNumber(gold),11);}
    AddText(h,body.rt,h.w-15,h.h-18,13,17,"◢",11,Hex("#aebdb7"));h.dirty=false;Update(h);
}
void Update(Hud& h){
    if(!h.damage)return;const auto m=Model(h);double largest=0;for(const auto& r:m["sources"].items)if(!Hidden(h,r["name"].Str()))largest=std::max(largest,r["dps"].Num());
    for(auto& cell:h.cells){J value;std::vector<J> sum;
        if(cell.source=="__total"&&cell.element<0)value=m["total"];
        else for(const auto& source:m["sources"].items){
            const auto name=source["name"].Str();if(cell.source!="__total"&&cell.source!="__allies"&&name!=cell.source)continue;if(cell.source=="__allies"&&name=="You")continue;
            J n;
            if(cell.field!="value")n=Finite(source[cell.field]);else if(cell.element<0)n=source["dps"];else if(source["values"].IsArray())n=source["values"].items[cell.element];sum.push_back(n);
        }
        if(value.IsNull())value=Sum(sum);const auto text=DisplayNumber(value.IsNumber()?value.number:NAN,cell.shortNumber);
        if(text!=cell.shown){SetText(cell.text.Get(),text);cell.shown=text;}
        if(cell.bar)Resize(cell.bar.Get(),largest>0&&value.IsNumber()?cell.width*static_cast<float>(std::clamp(value.number/largest,0.,1.)):0,FontSize(h)+18);
    }
}
bool Cursor(float* x,float* y){const auto window=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid!=GetCurrentProcessId())return false;POINT p{};RECT rect{};if(!GetCursorPos(&p)||!ScreenToClient(window,&p)||!GetClientRect(window,&rect)||rect.right<=0||rect.bottom<=0)return false;*x=p.x*screenWidth/rect.right;*y=p.y*screenHeight/rect.bottom;return p.x>=0&&p.y>=0&&p.x<rect.right&&p.y<rect.bottom;}
void Tooltip(Hud& h,int index) {
    if(index==h.tip)return;DestroyNode(h.tooltip);h.tip=index;
    if(index<0||index>=static_cast<int>(h.lootTips.size()))return;
    const auto& e=h.lootTips[index].entry;
    std::string text=e["name"].Str("Item");
    if(!e["tiers"].Str().empty())text+="\n"+e["tiers"].Str();
    if(e["ruleNumber"].Num()>0)text+="\n"+Tr("Rule")+" "+Num(e["ruleNumber"].Num())+" · "+e["rule"].Str();
    const int id=static_cast<int>(e["subtype"].Num(-1));
    if(e["kind"].Str()=="material"&&e["type"].Num()==101&&affixes.count(id)){
        const auto& info=affixes[id];text+="\n"+info["name"].Str()+" · "+Tr(info["type"].Str());
        if(info["level"].Num()>0)text+=" · "+Tr("Level")+" "+Num(info["level"].Num());
        if(!info["rollsOn"].items.empty()){text+="\n"+Tr("Rolls on")+": ";bool first=true;for(const auto& item:info["rollsOn"].items){if(!first)text+=", ";first=false;text+=item.Str();}}
        for(const auto& tier:info["tiers"].items){text+="\nT"+Num(tier["tier"].Num())+"  "+tier["min"].Str();if(tier["min"].Str()!=tier["max"].Str())text+=" – "+tier["max"].Str();if(!tier["min2"].Str().empty()){text+=" / "+tier["min2"].Str();if(tier["min2"].Str()!=tier["max2"].Str())text+=" – "+tier["max2"].Str();}}
        if(info["rollsOn"].items.size()>1&&!info["valuesOn"].Str().empty())text+="\n"+Tr("Values for "+info["valuesOn"].Str());
    }
    const float w=std::min(360.f,screenWidth-20),x=h.x+h.w+w+8<=screenWidth?h.w+8:std::max(-h.x, -w-8);
    const auto node=New("Loot details",h.root.Get(),x,0,w,500);h.tooltip.Set(node.rt);
    auto background=Fill(node.rt,"Tooltip background",0,0,w,500,Hex("#091310f5"),3);auto border=Outline(node.rt,"Tooltip border",0,0,w,500,Hex("#927340"),3);
    const float height=Muted(node.rt,12,10,w-24,text,12)+20;Resize(node.rt,w,height);Resize(TransformOf(background),w,height);Resize(TransformOf(border),w,height);
    const float y=std::clamp(h.topH+h.lootTips[index].y-h.scroll, -h.y+8,std::max(-h.y+8,screenHeight-h.y-height-8));Move(node.rt,x,y);
}
float Wheel(){static const auto method=game::FindMethod("UnityEngine.InputLegacyModule.dll","UnityEngine","Input","get_mouseScrollDelta",0);return method?reinterpret_cast<V2(*)(const il2cpp::Method*)>(method.code)(method.info).y:0;}
void Input(Hud& h,bool blocked){
    if(!h.root||!h.open)return;float x=-1,y=-1;const bool front=Cursor(&x,&y);const bool over=front&&x>=h.x&&y>=h.y&&x<h.x+h.w&&y<h.y+h.h;
    const bool interact=over&&!blocked;
    if(interact!=h.underMouse){bool under=interact;void* args[]{&under};Invoke(Find("LE.dll","","UIMouseListener","set_underMouse",1),h.listener.Get(),args);h.underMouse=interact;}
    const bool chrome=h.options||h.dragging||interact||h.optionsData["bar"].Str()=="always";
    if(chrome!=h.chromeShown){SetActive(GameObjectOf(h.chrome.Get()),chrome);h.chromeShown=chrome;}
    const bool down=front&&!blocked&&(GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON)?VK_RBUTTON:VK_LBUTTON)&0x8000);
    if(down&&!h.down&&over){
        h.pressed=-1;const float px=x-h.x,py=y-h.y;
        for(size_t i=0;i<h.hits.size();++i){const auto& hit=h.hits[i];if(i>=h.bodyHit&&(py<h.topH+1||py>=h.h-(h.damage?52.f:30.f)))continue;if(hit.enabled&&px>=hit.x&&py>=hit.y&&px<hit.x+hit.w&&py<hit.y+hit.h){h.pressed=static_cast<int>(i);break;}}
        h.startX=x-h.x;h.startY=y-h.y;h.startW=h.w;h.startH=h.h;
        h.resizing=px>=h.w-18&&py>=h.h-20;h.dragging=!h.resizing&&h.pressed<0&&py<h.topH;
    }else if(down&&h.down){if(h.dragging){h.x=x-h.startX;h.y=y-h.startY;Clamp(h);Move(h.root.Get(),h.x,h.y);}else if(h.resizing){h.w=h.startW+x-h.x-h.startX;h.h=h.startH+y-h.y-h.startY;Clamp(h);}}
    else if(!down&&h.down){if(h.dragging||h.resizing){h.dirty=h.resizing;SavePlace(h);}else if(interact&&h.pressed>=0&&h.pressed<static_cast<int>(h.hits.size())){const Hit hit=h.hits[h.pressed];const float px=x-h.x,py=y-h.y;if(px>=hit.x&&px<hit.x+hit.w&&py>=hit.y&&py<hit.y+hit.h&&hit.action)hit.action();}h.resizing=h.dragging=false;h.pressed=-1;}
    h.down=down;
    if(interact&&!down){const float wheel=Wheel();if(wheel){const float foot=h.damage?52.f:30.f;const float next=std::clamp(h.scroll-wheel*60,0.f,std::max(0.f,h.contentH-(h.h-h.topH-1-foot))),delta=h.scroll-next;h.scroll=next;Move(h.content.Get(),0,-next);for(size_t i=h.bodyHit;i<h.hits.size();++i)h.hits[i].y+=delta;}}
    if(!h.damage){int tip=-1;const float py=y-h.y-h.topH-1+h.scroll;if(interact&&!down&&!h.options&&y-h.y>h.topH&&y-h.y<h.h-30)for(size_t i=0;i<h.lootTips.size();++i)if(py>=h.lootTips[i].y&&py<h.lootTips[i].y+h.lootTips[i].h){tip=static_cast<int>(i);break;}Tooltip(h,tip);}
}
void DescribeSources(Hud& h) {
    int requested=0;
    const auto current=Model(h);
    for(const auto& source:current["sources"].items){const auto key=source["icon"].Str();if(requested>=4||!DamageIconKey(key)||assets::RuntimeSprite(key)||iconTries[key]>=3)continue;++requested;++iconTries[key];
        Send("damageicon "+key,[&h,key](const std::string& text){J reply;if(!json::Parse(text,reply)||!On(reply["ok"])||reply["key"].Str()!=key)return;const auto encoded=reply["png"].Str();if(encoded.empty()||encoded.size()>200000)return;
            DWORD count=0;if(!CryptStringToBinaryA(encoded.data(),static_cast<DWORD>(encoded.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,nullptr,&count,nullptr,nullptr)||count>150000)return;
            std::string bytes(count,'\0');if(!CryptStringToBinaryA(encoded.data(),static_cast<DWORD>(encoded.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,reinterpret_cast<BYTE*>(bytes.data()),&count,nullptr,nullptr))return;bytes.resize(count);
            try{if(assets::RuntimeSprite(key,bytes)&&!h.options)h.dirty=true;}catch(const std::exception& e){Log("damage source icon: %s",e.what());}
        });
    }
}
void DescribeShards(Hud& h){
    int requested=0;
    for(const auto& entry:h.entries.items){if(requested>=4)break;const auto number=entry["subtype"].Num(-1);if(entry["kind"].Str()!="material"||entry["type"].Num()!=101||number<0||number>1000000||std::floor(number)!=number)continue;const int id=static_cast<int>(number);if(affixes.count(id)||affixTries[id]>=3)continue;++affixTries[id];++requested;
        Send("affixinfo "+std::to_string(id),[&h,id](const std::string& text){J reply;if(json::Parse(text,reply)&&On(reply["ok"])){affixes[id]=reply;h.tip=-1;}});
    }
}
void Poll(Hud& h){
    if(!h.open||h.fixture||h.reading||GetTickCount64()<h.nextRead)return;h.nextRead=GetTickCount64()+(h.damage?(On(h.reply["active"])?1000:3000):2000);h.reading=true;
    Send(h.damage?"damagesession":"lootlog",[&h](const std::string& text){h.reading=false;J reply;if(!json::Parse(text,reply))return;
        if(h.damage){h.reply=reply;const auto m=Model(h);J shape=List();for(const auto& r:m["sources"].items)shape.items.push_back(List({r["name"],r["label"],r["live"]}));const auto key=Pack(shape)+Pack(reply["ok"])+Pack(reply["active"]);if(key!=h.structure){h.structure=key;if(!h.options)h.dirty=true;}}
        else{h.entries=MergeLoot(h.entries,reply);const auto key=Pack(LootRows(h.entries,h.optionsData["show"]));if(key!=h.structure){h.structure=key;h.dirty=true;}}
        if(h.damage){if(!h.dirty)Update(h);DescribeSources(h);if(h.open&&h.resumePending&&Eligible()&&On(reply["ok"])&&reply["active"].IsBool()){h.resumePending=false;if(!On(reply["active"]))DamageAction(h,"start");}}else DescribeShards(h);
    });
    if(h.damage&&h.recap&&GetTickCount64()>=h.nextRecap){h.nextRecap=GetTickCount64()+3000;Send("deathrecap",[&h](const std::string& text){Keep("deathrecap",text);h.dirty=true;});}
}
void Load(Hud& h,bool isDamage,void* parent){
    h=Hud{};h.damage=isDamage;h.owner=isDamage?-10:-11;h.w=isDamage?430.f:330.f;h.h=isDamage?300.f:240.f;h.x=isDamage?screenWidth-h.w-18:18;h.y=100;
    h.optionsData=Preference(Key(h));if(!h.optionsData.IsObject())h.optionsData=Object();
    if(h.optionsData["backgroundOpacity"].IsNull()&&h.optionsData["opacity"].IsNumber())Put(h.optionsData,"backgroundOpacity",h.optionsData["opacity"]);
    if(isDamage&&h.optionsData["compact"].IsNull())Put(h.optionsData,"compact",Flag(true));
    if(!isDamage)Put(h.optionsData,"show",FeedShown(h.optionsData));
    const auto entry=Preference("epochpact.windows.v1")[Name(h)];h.open=On(entry["open"]);const auto native=entry["ingame"];
    if(native.IsObject()){h.x=static_cast<float>(native["x"].Num(h.x));h.y=static_cast<float>(native["y"].Num(h.y));h.w=static_cast<float>(native["w"].Num(h.w));h.h=static_cast<float>(native["h"].Num(h.h));}Clamp(h);
    auto root=New(isDamage?"Damage HUD":"Loot HUD",parent,h.x,h.y,h.w,h.h);h.root.Set(root.rt);
    h.listener.Set(Add(root.go,"LE.dll","","UIMouseListener"));
    Draw(h);h.resumePending=isDamage&&On(h.optionsData["running"]);
}
} // namespace
void HudInit(void* parent,int width,int height){
    scale=std::clamp(height/900.f,.5f,2.f);screenWidth=width/scale;screenHeight=height/scale;
    const auto root=New("EpochPact HUD",parent,0,0,0,0);hudParent.Set(root.rt);const auto canvas=AddCanvas(root.go);int overlay=0,order=29990;void* a[]{&overlay};void* b[]{&order};void* z[]{&scale};
    Invoke(Find("UnityEngine.UIModule.dll","UnityEngine","Canvas","set_renderMode",1),canvas,a);Invoke(Find("UnityEngine.UIModule.dll","UnityEngine","Canvas","set_sortingOrder",1),canvas,b);Invoke(Find("UnityEngine.UIModule.dll","UnityEngine","Canvas","set_scaleFactor",1),canvas,z);
    Add(root.go,"UnityEngine.UI.dll","UnityEngine.UI","GraphicRaycaster");Load(damage,true,root.rt);Load(feed,false,root.rt);loaded=true;
}
bool HudActive(){return loaded&&(damage.open||feed.open);}
bool HudShown(const std::string& name){return name=="damage"?damage.open:feed.open;}
void ToggleHud(const std::string& name){auto& h=name=="damage"?damage:feed;if(h.open){HideHud(h);return;}h.open=true;h.dirty=true;h.nextRead=0;h.resumePending=h.damage&&On(h.optionsData["running"]);SavePlace(h);}
void HudStep(bool blocked){if(!loaded)return;for(auto* h:{&damage,&feed}){if(h->dirty)Draw(*h);if(!h->open)continue;Input(*h,blocked);Poll(*h);if(h->dirty)Draw(*h);}}
void ReleaseHud(){damage=Hud{};feed=Hud{};hudParent.Reset();iconTries.clear();affixTries.clear();affixes.clear();loaded=false;}
std::string HudState(){
    J result=Object();for(const auto* h:{&damage,&feed}){J state=Object(),cells=List();Put(state,"open",Flag(h->open));Put(state,"fixture",Flag(h->fixture));Put(state,"style",h->optionsData["style"]);Put(state,"w",Number(h->w));Put(state,"h",Number(h->h));
        for(const auto& cell:h->cells){auto row=Object();Put(row,"source",Text(cell.source));Put(row,"field",Text(cell.field));Put(row,"element",Number(cell.element));Put(row,"text",Text(cell.shown));cells.items.push_back(row);}Put(state,"cells",cells);
#ifdef EPOCHPACT_TESTING
        auto icons=List();const auto model=Model(*h);for(const auto& row:model["sources"].items){const auto key=row["icon"].Str();if(DamageIconKey(key)&&assets::RuntimeSprite(key))icons.items.push_back(Text(key));}Put(state,"iconsLoaded",icons);
#endif
        Put(result,Name(*h),state);}return Pack(result);
}
#ifdef EPOCHPACT_TESTING
bool HudFixture(const std::string& style){
    if(style!="quiet"&&style!="bars"&&style!="split"&&style!="table"&&style!="options")return false;
    auto& h=damage;h.fixture=true;h.open=true;h.options=style=="options";h.recap=false;h.scroll=0;h.selected="Fire Skeleton";h.optionsData=Object();Put(h.optionsData,"style",Text(style=="options"?"bars":style));Put(h.optionsData,"metric",Text("dps10"));Put(h.optionsData,"roster",Flag(true));Put(h.optionsData,"bar",Text("always"));
    J state=Object(),sources=List();Put(state,"ok",Flag(true));Put(state,"active",Flag(true));Put(state,"elapsedSeconds",Number(25));Put(state,"combatSeconds",Number(10));
    for(int n=0;n<3;++n){J row=Object(),elements=Object();Put(row,"name",Text(n==0?"You":n==1?"Fire Skeleton":"Cold Golem"));Put(row,"live",Number(n==1?4:1));
        if(n==0)Put(row,"icon",Text("c:3:0")); // exercise the real read-only portrait path
        const double dps=n==0?1234:n==1?4567:2345;Put(row,"total",Number(dps*25));Put(row,"hits",Number(123+n));Put(row,"maxHit",Number(dps*3));
        for(const auto* key:metrics){Put(row,key,Number(dps));auto values=List();for(int i=0;i<7;++i)values.items.push_back(Number(dps*(i+1)/28));Put(elements,key,values);}auto totals=List();for(int i=0;i<7;++i)totals.items.push_back(Number(dps*25*(i+1)/28));Put(elements,"total",totals);Put(row,"elements",elements);sources.items.push_back(row);
    }
    Put(state,"types",sources);h.reply=state;h.w=style=="table"?920:520;h.h=style=="quiet"?380:style=="table"?290:540;h.x=screenWidth-h.w-20;h.y=90;h.dirty=true;DescribeSources(h);
    auto& f=feed;f.fixture=true;f.open=true;f.entries=List();f.optionsData=Object();Put(f.optionsData,"bar",Text("always"));Put(f.optionsData,"backgroundOpacity",Number(85));
    for(int i=0;i<6;++i){J row=Object();Put(row,"kind",Text(i==0?"gold":i==1?"material":"item"));Put(row,"name",Text(i==1?"Health Shard":i==2?"Chronostasis":i==3?"Apathy's Maw":i==4?"Exalted Runic Helm":"Rare Jade Ring"));Put(row,"seq",Number(i));Put(row,"gold",Number(i==0?12500:0));Put(row,"rarity",Text(i==2?"Legendary":i==3?"Unique":i==4?"Exalted":"Rare"));Put(row,"tiers",Text(i>=4?"7 6 5 5":""));f.entries.items.push_back(row);}f.w=350;f.h=310;f.x=20;f.y=90;f.dirty=true;return true;
}
#endif
} // namespace ep::ingame::view
