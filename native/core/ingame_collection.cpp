// Unique Atlas / Stash Assistant: the desktop collection-ui.js and serialized collection_backend
// composition, using the same read-only native commands. Markers change local preferences only.
#include "ingame_window.hpp"
#include "ingame_browser_rules.hpp"
#include "ingame_stat_rules.hpp"
#include <algorithm>
#include <cmath>
#include <memory>

namespace ep::ingame::view {
namespace {
using J = json::Value;
struct Browser {
    J data;
    std::string query, filter = "all", base = "all", selected;
    int page = 0, head = -1, results = -1, comparison = -1;
    bool busy = false;
    uint64_t token = 0, revision = 0;
};
Browser atlas, stash;
Browser& State(bool isAtlas) { return isAtlas ? atlas : stash; }
bool IsAtlas(const Section& s) { return (*s.spec)["mode"].Str() == "atlas"; }
void Refresh(bool isAtlas, bool head = true) {
    auto& s = State(isAtlas);
    if (head && s.head >= 0) Rebuild(s.head);
    if (s.results >= 0) Rebuild(s.results);
    if (s.comparison >= 0) Rebuild(s.comparison);
}
std::string Clean(const std::string& text) {
    std::string out; bool tag = false;
    for (char c : text) { if (c == '<') tag = true; else if (c == '>') tag = false; else if (!tag) out += c; }
    return out;
}
std::string MarkerKey(const Browser& s) { return "epochpact.collection.v1." + s.data["scope"].Str("none"); }
bool Contains(const J& values, const std::string& id) {
    return std::any_of(values.items.begin(), values.items.end(), [&](const J& v) { return model::Id(v) == id; });
}
J Markers(const Browser& s) { return Preference(MarkerKey(s)); }
void ToggleMarker(bool isAtlas, const std::string& kind, J value) {
    auto& s = State(isAtlas);
    if (s.data["scope"].Str().empty()) return;
    J markers = Markers(s); if (!markers.IsObject()) markers = model::Object();
    std::vector<J> values = markers[kind].items;
    const std::string id = model::Id(value);
    const auto at = std::find_if(values.begin(), values.end(), [&](const J& v) { return model::Id(v) == id; });
    if (at == values.end()) values.push_back(std::move(value)); else values.erase(at);
    model::Put(markers, kind, model::List(std::move(values)));
    const std::string packed = model::Pack(markers);
    if (!StorePreference(MarkerKey(s), &packed)) { Toast(Tr("Could not save the local marker. Your game items are unchanged."), true); return; }
    Refresh(isAtlas);
}
struct ScanTask { model::Scan scan; J catalog, identity; bool isAtlas = false; uint64_t token = 0; };
void FailScan(const std::shared_ptr<ScanTask>& task, const std::string& why) {
    auto& s = State(task->isAtlas); if (s.token != task->token) return;
    s.busy = false; s.data = J{}; s.selected.clear(); Refresh(task->isAtlas); Toast(PlayerError(why), true);
}
void ReadPage(const std::shared_ptr<ScanTask>& task) {
    Send("stashread " + std::to_string(task->scan.offset), [task](const std::string& reply) {
        auto& state = State(task->isAtlas); if (state.token != task->token) return;
        J page; std::string why;
        if (Refused(reply, &why) || !json::Parse(reply, page)) { FailScan(task, why.empty() ? Tr("Invalid collection page.") : why); return; }
        try {
            if (!model::SameOwner(page, task->identity)) throw std::runtime_error("The loaded character changed during the scan. Refresh again.");
            task->scan.Accept(page);
            if (!task->scan.complete) { ReadPage(task); return; }
            J data = task->isAtlas ? model::Atlas(task->catalog, task->scan.result) : model::Groups(task->scan.result);
            auto& peer = State(!task->isAtlas);
            if (peer.data.IsObject() && (!model::SameOwner(peer.data, data) || model::Pack(peer.data["revision"]) != model::Pack(data["revision"]) || peer.data["scope"].Str() != data["scope"].Str())) {
                peer.data = J{}; peer.selected.clear(); Refresh(!task->isAtlas);
            }
            state.data = std::move(data); state.selected.clear(); state.page = 0; state.busy = false; Refresh(task->isAtlas);
        } catch (const std::exception& e) { FailScan(task, e.what()); }
    });
}
void Scan(bool isAtlas) {
    auto& state = State(isAtlas); if (state.busy || !Eligible()) return;
    state.busy = true; state.selected.clear(); const uint64_t token = ++state.token; Refresh(isAtlas);
    auto task = std::make_shared<ScanTask>(); task->isAtlas = isAtlas; task->token = token;
    Send("identityread", [task](const std::string& reply) {
        if (State(task->isAtlas).token != task->token) return;
        std::string why;
        if (Refused(reply, &why) || !json::Parse(reply, task->identity) || !model::On(task->identity["offline"]) || model::Id(task->identity["player"]["id"]).empty()) {
            FailScan(task, why.empty() ? "Load an offline character and wait for the area to finish loading." : why); return;
        }
        if (!task->isAtlas) { ReadPage(task); return; }
        Send("atlasread", [task](const std::string& reply2) {
            if (State(task->isAtlas).token != task->token) return;
            std::string why;
            if (Refused(reply2, &why) || !json::Parse(reply2, task->catalog) || !model::SameOwner(task->catalog, task->identity)) {
                FailScan(task, why.empty() ? "The loaded character changed during the scan. Refresh again." : why); return;
            }
            ReadPage(task);
        });
    });
}
float Summary(void* parent, float y, const std::vector<std::pair<std::string,std::string>>& values) {
    const float w = (kPanelInner - 24) / 3;
    Style value{Face::Serif, 30, C("accent")}, label{Face::Body, 12, C("muted")};
    const float h = 38 + LineHeight(value) + 8 + LineHeight(label);
    for (size_t i = 0; i < values.size(); ++i) {
        const float x = kPanelPad + static_cast<float>(i) * (w + 12);
        Picture(parent, "Summary gradient", x, y, w, h, "grad-stat-value.png", false, kWhite);
        Outline(parent, "Summary frame", x, y, w, h, C("line"), 5);
        TextBox(parent, "Summary value", x+23, y+19, w-46, LineHeight(value), value, values[i].first);
        TextBox(parent, "Summary label", x+23, y+27+LineHeight(value), w-46, LineHeight(label), label, Tr(values[i].second));
    }
    return h;
}
void MakeHeader(Section& section) {
    const bool isAtlas = IsAtlas(section); State(isAtlas).head = section.id;
    section.reads.insert("identityread"); section.polls.insert("identityread");
    section.onData = [isAtlas](Section& s, const std::string&) {
        auto& state = State(isAtlas); const auto* identity = Cached("identityread");
        if (state.data.IsObject() && identity && !model::SameOwner(state.data, *identity)) {
            ++state.token; state.busy = false; state.data = J{}; state.selected.clear(); Refresh(isAtlas);
        } else if (!s.built) Rebuild(s.id);
    };
    section.build = [isAtlas](Section& sec, const Ctx& outer) {
        auto& state = State(isAtlas);
        auto panel = std::make_shared<Panel>(OpenPanel(outer.parent,kPadX,0,kPanelW)); sec.frame = panel; sec.after = 0;
        const Ctx c = outer.At(panel->rt.Get(),kPadX,0); float y = kPanelPad;
        Style eyebrow{Face::Body,10,C("accent")}; eyebrow.spacing = 2;
        TextBox(c.parent,"Eyebrow",kPanelPad,y,kPanelInner,13,eyebrow,Tr(isAtlas?"COLLECTION BOOK":"OWNED EQUIPMENT")); y += 20;
        auto scan = AddButton(c,kPanelPad+kPanelInner,y,Tr(state.busy?"Reading…":state.data.IsObject()?"↻ Refresh collection":"↻ Scan collection"),Look::Primary,[isAtlas]{Scan(isAtlas);},true,isAtlas?"atlas_read":"stash_read");
        SetEnabled(c,scan,!state.busy&&Eligible());
        y += std::max(H2(c.parent,kPanelPad,y,kPanelInner-scan.w-16,Tr(isAtlas?"Unique Atlas":"Stash Assistant")),scan.h)+14;
        const bool has = state.data.IsObject();
        y += Muted(c.parent,kPanelPad,y,kPanelInner,(has?state.data["player"]["name"].Str()+" · "+Fmt("{0} stash tabs",{Num(state.data["tabs"].Num())})+" · ":"")+
            Tr("Scans the loaded stash, bags and equipment. Includes the Forge and Eternity Cache. Other characters and stashes are outside this scan."))+24;
        if (!has) return y;
        const J prefs = Markers(state);
        y += Summary(c.parent,y,{{Num(state.data[isAtlas?"owned":"total"].Num()),isAtlas?"Owned types":"Equipment pieces"},
            {Num(isAtlas?state.data["total"].Num()-state.data["owned"].Num():state.data["duplicateGroups"].Num()),isAtlas?"Missing types":"Duplicate Unique groups"},
            {std::to_string(prefs[isAtlas?"wishlist":"protected"].items.size()),isAtlas?"On your wishlist":"Local protection markers"}})+24;
        const int id = sec.id; const float searchW = kPanelInner-(isAtlas?348:174);
        const float height = InputField(c,kPanelPad,y,searchW,Tr(isAtlas?"Search items":"Search groups"),state.query,Tr("Name, base type or affix…"),false,[isAtlas](const std::string& value){
            auto& s=State(isAtlas);s.query=value;s.page=0;const auto revision=++s.revision;
            After(180,[isAtlas,revision]{auto& b=State(isAtlas);if(b.revision==revision)Refresh(isAtlas,false);});
        },isAtlas?"atlas-search":"stash-search");
        const std::vector<std::string> filters=isAtlas?std::vector<std::string>{"all","owned","missing","wishlist","set"}:std::vector<std::string>{"all","duplicates","protected"};
        const auto at=std::find(filters.begin(),filters.end(),state.filter);
        SelectField(c,kPanelPad+searchW+14,y,160,Tr("Show"),isAtlas?std::vector<std::string>{Tr("All items"),Tr("Owned"),Tr("Missing"),Tr("Wishlist"),Tr("Set items")}:
            std::vector<std::string>{Tr("All equipment"),Tr("Duplicate Uniques"),Tr("Protected reminders")},at==filters.end()?0:static_cast<int>(at-filters.begin()),[isAtlas,filters,id](int index){
                auto& b=State(isAtlas);b.filter=filters[index];b.page=0;Rebuild(id);Refresh(isAtlas,false);
            },isAtlas?"atlas-filter":"stash-filter");
        if(isAtlas){
            std::set<std::string> bases;for(const auto& row:state.data["items"].items)bases.insert(row["baseType"].Str());
            std::vector<std::string> values{"all"},labels{Tr("All types")};int chosen=0;
            for(const auto& base:bases){if(base==state.base)chosen=static_cast<int>(values.size());values.push_back(base);labels.push_back(base);}
            SelectField(c,kPanelPad+searchW+188,y,160,Tr("Base type"),labels,chosen,[values,id](int index){atlas.base=values[index];atlas.page=0;Rebuild(id);Refresh(true,false);},"atlas-base");
        }
        return y+height+20;
    };
}
const std::vector<J>& Source(const Browser& s,bool isAtlas){return s.data[isAtlas?"items":"groups"].items;}
bool Match(const J& row,const Browser& state,bool isAtlas,const J& markers){
    std::string text=row["name"].Str()+" "+row["baseType"].Str();
    for(const auto& copy:row[isAtlas?"copies":"items"].items)for(const auto& affix:copy["affixes"].items)text+=" "+affix["name"].Str();
    if(model::Fold(text).find(model::Fold(Trim(state.query)))==std::string::npos)return false;
    if(isAtlas)return (state.base=="all"||row["baseType"].Str()==state.base)&&
        (state.filter=="all"||(state.filter=="owned"&&row["count"].Num()>0)||(state.filter=="missing"&&row["count"].Num()==0)||
        (state.filter=="wishlist"&&Contains(markers["wishlist"],model::Id(row["id"])))||(state.filter=="set"&&model::On(row["set"])));
    if(state.filter=="duplicates")return model::On(row["unique"])&&row["count"].Num()>1;
    if(state.filter=="protected")return std::any_of(row["items"].items.begin(),row["items"].items.end(),[&](const J& item){return Contains(markers["protected"],item["key"].Str());});
    return true;
}
void SelectCopy(bool isAtlas,const std::string& key){State(isAtlas).selected=key;Refresh(isAtlas,false);}
CardMaker EntryCard(bool isAtlas,J row){
    return [isAtlas,row=std::move(row)](const Ctx& outer,float x,float y,float w){
        auto& state=State(isAtlas);const J markers=Markers(state);
        const std::string key=model::Id(row[isAtlas?"id":"key"]);
        CardBox card=OpenBrowserCard(outer.parent,x,y,w,!isAtlas&&state.selected==key);const Ctx c=outer.At(card.node.Get(),x,y);
        float cy=19,badgeH=0;
        Badge(c.parent,isAtlas?w-60:w-19,cy,isAtlas?Tr(model::On(row["set"])?"Set":"Unique")+" · "+row["baseType"].Str():Tr(model::On(row["unique"])?"Unique / Set copies":"Same base comparison"),&badgeH);
        if(isAtlas)AddButton(c,w-16,cy-6,Contains(markers["wishlist"],key)?"★":"☆",Look::Quiet,[isAtlas,row]{ToggleMarker(isAtlas,"wishlist",row["id"]);},true,"atlas-wishlist-"+key);
        cy+=badgeH+16;Style title{Face::Serif,20,C("ink")};title.line=1.4f;title.wrap=true;
        cy+=Paragraph(c.parent,"Item name",19,cy,w-38,title,row["name"].Str())+16;
        if(!isAtlas){
            bool protectedItem=false;for(const auto& item:row["items"].items)protectedItem|=Contains(markers["protected"],item["key"].Str());
            cy+=Muted(c.parent,19,cy,w-38,Fmt(row["count"].Num()==1?"{0} copy":"{0} copies",{Num(row["count"].Num())})+(protectedItem?" · "+Tr("◆ Protected"):""),12)+14;
            Hit open;open.x=x;open.y=y;open.w=w;open.name="stash-group-"+key;open.action=[key]{SelectCopy(false,key);};open.h=cy+19;outer.Add(std::move(open));
        }else{
            Style status{Face::Body,12,row["count"].Num()>0?C("accent"):C("muted")};
            TextBox(c.parent,"Owned status",19,cy,w-38,20,status,row["count"].Num()>0?Fmt("✓ {0} owned",{Num(row["count"].Num())}):Tr("Not in this scan"));cy+=20;
            if(row["count"].Num()>0){
                cy+=Muted(c.parent,19,cy,w-38,model::On(row["set"])?Tr("Set item"):row["bestLP"].IsNull()?Tr("Legendary / Weaver"):Fmt("{0} LP best",{Num(row["bestLP"].Num())}),12)+4;
            }
            cy+=18;Solid(c.parent,"Item divider",19,cy,w-38,1,C("line"));cy+=15;
            cy+=Muted(c.parent,19,cy,w-38,Fmt("Level {0}",{Num(row["level"].Num())})+" · "+Tr(model::On(row["randomDrop"])?"Random drop eligible":"Special acquisition"),10)+12;
            if(row["count"].Num()>0)cy+=AddButton(c,19,cy,Tr("View copies →"),Look::Small,[key]{SelectCopy(true,key);},false,"atlas-copies-"+key).h;
        }
        CloseCard(card,w,cy+19);return card;
    };
}
void MakeResults(Section& section){
    const bool isAtlas=IsAtlas(section);State(isAtlas).results=section.id;
    section.build=[isAtlas](Section& sec,const Ctx& outer){
        auto& state=State(isAtlas);const Node body=New("Collection results",outer.parent,kPadX,0,kPanelW,1000);const Ctx c=outer.At(body.rt,kPadX,0);
        if(!state.data.IsObject()){
            float y=40;Style icon{Face::Symbol,45,C("gold")};icon.align=kCenter;
            TextBox(c.parent,"Collection emblem",kPanelPad,y,kPanelInner,55,icon,"✦");y+=70;
            y+=H3(c.parent,kPanelPad,y,kPanelInner,Tr("Your collection starts here"))+14;
            return y+EmptyText(c.parent,kPanelPad,y,kPanelInner,Tr("Load an offline character, then scan. Nothing is sold, moved or changed."))+50;
        }
        const J markers=Markers(state);std::vector<J> rows;
        for(const auto& row:Source(state,isAtlas))if(Match(row,state,isAtlas,markers))rows.push_back(row);
        std::sort(rows.begin(),rows.end(),[](const J& a,const J& b){return model::Fold(a["name"].Str())<model::Fold(b["name"].Str());});
        const int size=isAtlas?24:16,pages=std::max(1,static_cast<int>((rows.size()+size-1)/size));state.page=std::clamp(state.page,0,pages-1);
        float y=Muted(c.parent,kPanelPad,0,kPanelInner,Fmt(isAtlas?"{0} types · Snapshot":"{0} groups · Snapshot",{std::to_string(rows.size())}),11)+14;
        std::vector<CardMaker> cards;
        for(size_t i=static_cast<size_t>(state.page*size);i<std::min(rows.size(),static_cast<size_t>((state.page+1)*size));++i)cards.push_back(EntryCard(isAtlas,rows[i]));
        const int id=sec.id;const float gridTop=y;
        struct Footer {Pin node;std::vector<int> hits;float y=0,h=0;};auto footer=std::make_shared<Footer>();
        float grid=cards.empty()?EmptyText(c.parent,kPanelPad,y,kPanelInner,Tr("No matches. Try a different filter.")):
            ScrollCards(sec,c,kPanelPad,y,kPanelInner,0,std::move(cards),3,14,[footer,id,gridTop](float height){
                const float next=gridTop+height+24,delta=next-footer->y;footer->y=next;
                if(footer->node)Move(footer->node.Get(),kPanelPad,next);
                if(auto* s=FindSection(id)){for(int hit:footer->hits)if(static_cast<size_t>(hit)<s->hits.size())s->hits[hit].y+=delta;Resized(*s,next+footer->h+kPanelPad);}
            });
        y+=grid+24;const Node bar=New("Pagination",c.parent,kPanelPad,y,kPanelInner,50);footer->node.Set(bar.rt);footer->y=y;
        const Ctx fc=c.At(bar.rt,kPanelPad,y);const size_t first=fc.hits->size();
        auto previous=AddButton(fc,kPanelInner/2-85,0,Tr("← Previous"),Look::Normal,[isAtlas]{--State(isAtlas).page;Refresh(isAtlas,false);},true,isAtlas?"atlas-prev":"stash-prev");
        Style count{Face::Body,12,C("ink")};count.align=kCenter;
        TextBox(fc.parent,"Page",kPanelInner/2-80,0,160,previous.h,count,Fmt("Page {0} of {1}",{std::to_string(state.page+1),std::to_string(pages)}));
        auto next=AddButton(fc,kPanelInner/2+85,0,Tr("Next →"),Look::Normal,[isAtlas]{++State(isAtlas).page;Refresh(isAtlas,false);},false,isAtlas?"atlas-next":"stash-next");
        SetEnabled(fc,previous,state.page>0);SetEnabled(fc,next,state.page+1<pages);footer->h=std::max(previous.h,next.h);
        for(size_t i=first;i<fc.hits->size();++i)footer->hits.push_back(static_cast<int>(i));
        return y+footer->h+kPanelPad;
    };
}
std::string Potential(const J& item){
    if(item["kind"].Str()=="Legendary")return Tr("Legendary · LP used");
    if(item["kind"].Str()=="Set")return Tr("Set item");
    if(item["weaversWill"].Num()>0)return Fmt("Weaver’s Will {0}",{Num(item["weaversWill"].Num())});
    return item["uniqueId"].IsNull()?Fmt("{0} FP",{Num(item["fp"].Num())}):Fmt("{0} LP",{Num(item["lp"].Num())});
}
float Meter(void* parent,float x,float y,float w,double roll){
    Fill(parent,"Roll track",x,y,w,7,Hex("#ffffff12"),3);Fill(parent,"Roll",x,y,w*static_cast<float>(std::clamp(roll/255.,0.,1.)),7,C("accent"),3);return 7;
}
CardMaker CopyCard(bool isAtlas,J item){
    return [isAtlas,item=std::move(item)](const Ctx& outer,float x,float y,float w){
        const bool protectedItem=Contains(Markers(State(isAtlas))["protected"],item["key"].Str());
        CardBox card=OpenBrowserCard(outer.parent,x,y,w,protectedItem);const Ctx c=outer.At(card.node.Get(),x,y);float cy=21;
        cy+=H3(c.parent,21,cy,w-42,item["name"].Str())+12;
        cy+=AddButton(c,21,cy,Tr(protectedItem?"◆ Protected":"◇ Mark protected"),Look::Small,[isAtlas,item]{ToggleMarker(isAtlas,"protected",item["key"]);},false,"protect-"+item["key"].Str()).h+20;
        Style potential{Face::Body,21,C("gold")};cy+=Paragraph(c.parent,"Potential",21,cy,w-42,potential,Potential(item))+10;
        std::string place=item["source"].Str();if(item["tab"].Num(-1)>=0)place+=" · "+Fmt("Tab {0}",{Num(item["tab"].Num()+1)});
        place+=" · ("+Num(item["x"].Num()+1)+", "+Num(item["y"].Num()+1)+")";if(model::On(item["corrupted"]))place+=" · "+Tr("Corrupted");
        cy+=Muted(c.parent,21,cy,w-42,place)+20;
        for(const auto& affix:item["affixes"].items){
            Solid(c.parent,"Affix divider",21,cy,w-42,1,C("line"));cy+=10;
            Style text{Face::Body,12,C("ink")};text.wrap=true;
            const std::string name="T"+Num(affix["tier"].Num())+" "+Clean(affix["name"].Str("Affix "+Num(affix["id"].Num())))+(model::On(affix["sealed"])?" · "+Tr("Sealed"):"");
            cy+=Paragraph(c.parent,"Affix",21,cy,w-42,text,name)+6;
            cy+=Muted(c.parent,21,cy,w-42,Fmt("{0}% roll",{Num(std::round(affix["roll"].Num()/255*100))}),10)+8;
            cy+=Meter(c.parent,21,cy,w-42,affix["roll"].Num())+10;
        }
        if(item["affixes"].items.empty())cy+=Muted(c.parent,21,cy,w-42,Tr("No regular affixes."))+10;
        if(!item["uniqueMods"].items.empty()){
            cy+=Muted(c.parent,21,cy,w-42,Tr("Unique modifiers · actual game values"),11)+10;
            for(const auto& mod:item["uniqueMods"].items){
                Solid(c.parent,"Modifier divider",21,cy,w-42,1,C("line"));cy+=10;
                const int mode=static_cast<int>(mod["mode"].Num());
                cy+=Muted(c.parent,21,cy,w-42,Clean(mod["name"].Str("Unique effect"))+(mode==1?" · "+Tr("Increased"):mode==2?" · "+Tr("More"):""),12)+5;
                const std::string suffix=model::On(mod["percent"])?"%":"";const auto number=[](double n){return stats::Display(std::round(n*100)/100);};
                Style value{Face::BodySemi,12,C("gold")};cy+=Paragraph(c.parent,"Modifier value",21,cy,w-42,value,number(mod["value"].Num())+suffix)+6;
                if(model::On(mod["canRoll"])){
                    cy+=Muted(c.parent,21,cy,w-42,Fmt("Range {0}–{1}{2}",{number(mod["min"].Num()),number(mod["max"].Num()),suffix})+(model::On(mod["lessIsBetter"])?" · "+Tr("Lower is better"):""),10)+8;
                    cy+=Meter(c.parent,21,cy,w-42,mod["roll"].Num())+10;
                }else cy+=Muted(c.parent,21,cy,w-42,Tr("Fixed modifier"),10)+10;
            }
        }
        cy+=16+Muted(c.parent,21,cy+16,w-42,Tr("Roll % is position in the modifier’s range, not its stat value."),10,1.6f)+21;
        CloseCard(card,w,cy);return card;
    };
}
void MakeComparison(Section& section){
    const bool isAtlas=IsAtlas(section);State(isAtlas).comparison=section.id;
    section.build=[isAtlas](Section& sec,const Ctx& outer){
        auto& state=State(isAtlas);const J* selection=nullptr;
        for(const auto& row:Source(state,isAtlas))if(model::Id(row[isAtlas?"id":"key"])==state.selected){selection=&row;break;}
        if(!selection){sec.after=0;return 0.f;}
        sec.after=0;const Node body=New("Copy comparison",outer.parent,kPadX,0,kPanelW,1000);const Ctx c=outer.At(body.rt,kPadX,0);
        float y=24;Solid(c.parent,"Comparison divider",kPanelPad,0,kPanelInner,1,C("line"));
        y+=SectionHead(c,kPanelPad,y,kPanelInner,(*selection)["name"].Str(),{{Tr("Close comparison"),[isAtlas]{State(isAtlas).selected.clear();Refresh(isAtlas,false);}}})+14;
        y+=Muted(c.parent,kPanelPad,y,kPanelInner,Tr("No universal “best” score: compare LP and the modifiers your build needs. Protection is an EpochPact reminder and does not prevent selling in game."))+20;
        std::vector<J> items=(*selection)[isAtlas?"copies":"items"].items;
        std::stable_sort(items.begin(),items.end(),[](const J& a,const J& b){return a["lp"].Num()>b["lp"].Num();});
        std::vector<CardMaker> cards;for(const auto& item:items)cards.push_back(CopyCard(isAtlas,item));
        const int id=sec.id;const float top=y;
        return y+ScrollCards(sec,c,kPanelPad,y,kPanelInner,0,std::move(cards),3,16,[id,top](float h){if(auto* s=FindSection(id))Resized(*s,top+h+kPanelPad);})+kPanelPad;
    };
}
void MakeSearchHelp(Section& section){
    section.build=[](Section&,const Ctx& outer){
        Panel panel=OpenPanel(outer.parent,kPadX,0,kPanelW);const Ctx c=outer.At(panel.rt.Get(),kPadX,0);float y=kPanelPad;
        y+=H2(c.parent,kPanelPad,y,kPanelInner,Tr("In-game stash search"))+14;
        y+=Muted(c.parent,kPanelPad,y,kPanelInner,Tr("Copy a query and paste it into the game’s stash search box. These are search expressions, not gameplay commands."))+20;
        const std::vector<std::pair<std::string,std::string>> recipes{{"LP2+","Uniques with at least 2 Legendary Potential"},{"WW18+","Weaver uniques with at least 18 Weaver’s Will"},
            {"FP30+","Equipment with at least 30 Forging Potential"},{"exalted&2T6+","Exalted equipment with at least two tier 6 or higher affixes"},{"2T7","Items with at least two tier 7 affixes"},
            {"LP1+&boots","Boots with at least 1 Legendary Potential"},{"CoF&unique","Circle of Fortune tagged uniques"},{"2hsword&exalted","Exalted two-handed swords"}};
        for(const auto& [query,note]:recipes){
            const float h=60;Outline(c.parent,"Recipe",kPanelPad,y,kPanelInner,h,Hex("#92734035"),4);
            Style code{Face::BodySemi,13,C("gold")};TextBox(c.parent,"Query",kPanelPad+13,y+18,240,24,code,query);
            Muted(c.parent,kPanelPad+265,y+15,kPanelInner-370,Tr(note),12);
            AddButton(c,kPanelPad+kPanelInner-13,y+10,Tr("Copy"),Look::Normal,[query]{Toast(Tr(CopyText(query)?"Copied. Paste into the game’s stash search box.":"Clipboard unavailable. Select the query text and copy it manually."));},true,"copy-query-"+query);
            y+=h+10;
        }
        y+=Muted(c.parent,kPanelPad,y,kPanelInner,Tr("& combines requirements; | accepts either condition. Tooltip text searches use the game’s current language."))+kPanelPad;
        ClosePanel(panel,kPanelW,y);return y;
    };
}
} // namespace
bool ScanCollection(const std::string& page){
    if(page!="atlas"&&page!="stash")return false;
    if(State(page=="atlas").busy)return false;
    Scan(page=="atlas");return true;
}
std::string CollectionState(){
    J out=model::Object();
    for(bool isAtlas:{true,false}){
        const auto& s=State(isAtlas);J state=model::Object();
        model::Put(state,"busy",model::Flag(s.busy));model::Put(state,"snapshot",model::Flag(s.data.IsObject()));
        model::Put(state,"player",s.data["player"]);model::Put(state,"total",s.data["total"]);model::Put(state,"pages",s.data["pages"]);
        model::Put(state,"selected",model::Text(s.selected));model::Put(out,isAtlas?"atlas":"stash",std::move(state));
    }
    return model::Pack(out);
}
void RegisterCollectionSections(){
    RegisterSection("collection_head",MakeHeader);RegisterSection("collection_results",MakeResults);
    RegisterSection("collection_comparison",MakeComparison);RegisterSection("stash_search_help",MakeSearchHelp);
}
} // namespace ep::ingame::view
