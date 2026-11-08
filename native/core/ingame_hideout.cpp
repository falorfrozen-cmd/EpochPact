// The Hideout page: go there, the mouse editor, anything of the game to add, what stands there and the doors.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include "hideout.hpp"
#include "hideout_hud_rules.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace ep::ingame::view {
namespace {
using JValue = json::Value;
using assets::Face;

void PlaceButton(const Ctx& c,Button& b,float x,float y) {
    Move(b.node.rt,x,y);(*c.hits)[b.hit].x=c.ox+x;(*c.hits)[b.hit].y=c.oy+y;
}
// Use the real font-measured button sizes. Mixed primary/normal controls share
// a row centre; narrow viewports and translated labels flow onto another row.
float ButtonRows(const Ctx& c,std::vector<Button>& buttons,float x,float y,float width,float gap=8) {
    size_t first=0;float top=y;
    while(first<buttons.size()) {
        size_t last=first;float used=0,height=0;
        while(last<buttons.size()) {const auto& b=buttons[last];const float next=used+(last>first?gap:0)+b.w;
            if(last>first&&next>width)break;used=next;height=std::max(height,b.h);++last;}
        float left=x;for(size_t k=first;k<last;++k){auto& b=buttons[k];PlaceButton(c,b,left,top+(height-b.h)/2);left+=b.w+gap;}
        top+=height;first=last;if(first<buttons.size())top+=gap;
    }
    return top-y;
}
void Ellipsize(void* text) {
    int mode=1;void* args[]{&mode};managed::Invoke(Find("Unity.TextMeshPro.dll","TMPro","TMP_Text","set_overflowMode",1),text,args);
}

float InPanel(const Ctx& c, const std::function<float(const Ctx& in, float y)>& draw) {
    Panel p = OpenPanel(c.parent, kPadX, 0, kPanelW);
    const Ctx in = c.At(p.rt.Get(), kPadX, 0);
    const float h = draw(in, kPanelPad) + kPanelPad;
    ClosePanel(p, kPanelW, h);
    return h;
}
std::string Pretty(std::string name) {
    std::replace(name.begin(), name.end(), '_', ' ');
    return name;
}
struct Search { std::string filter, category = "all", sort = "az"; JValue rows; bool asked = false; int results = -1, page = 0; unsigned generation = 0; } search;
std::string hoverPreview, hoverTitle;
void HoverPreview(const std::string& key,const std::string& title) {hoverPreview=key;hoverTitle=title;hideout::RequestPreview(key,true);}
void PollInspectionPreview(const std::string& key){
    const auto status=hideout::PreviewStatus(key);
    const bool loading=status=="Queued"||status=="Loading model"||status=="Paused"||status=="Hover to preview"||status.rfind("Loading template ",0)==0;
    // Keep an actively hovered load at the front. A failed asset stays failed
    // until another explicit hover/click, rather than retrying every frame.
    hideout::RequestPreview(key,loading);
}
struct Thumb {Pin image,note;std::string key;float y=0,h=0;void* shown=nullptr;std::string status;};
struct Gallery {std::vector<Thumb> thumbs;};
void* RawPicture(void* parent,float x,float y,float w,float h) {
    auto node=New("Live model preview",parent,x,y,w,h);
    auto* image=Add(node.go,"UnityEngine.UI.dll","UnityEngine.UI","RawImage");Raycast(image,false);
    // A RawImage without a texture renders a white rectangle. Keep the dark
    // model well and loading caption visible until an actual preview exists.
    Tint(image,Alpha(kWhite,0));return image;
}
void ThumbCard(const Ctx& c,Gallery& gallery,float x,float y,float w,const std::string& key,const std::string& title) {
    Fill(c.parent,"Model well",x,y,w,w*2/3,Hex("#101d19"),3);Outline(c.parent,"Model frame",x,y,w,w*2/3,C("line"),3);
    Thumb t;t.key=key;t.y=y;t.h=w*2/3;t.image.Set(RawPicture(c.parent,x+1,y+1,w-2,w*2/3-2));
    Style st{Face::Body,10,C("muted")};st.align=kCenter;
    t.note.Set(TextBox(c.parent,"Preview status",x+6,y+w*2/3-22,w-12,20,st,Tr("Loading preview…")));Ellipsize(t.note.Get());
    Hit hit;hit.name="hideout-preview-"+key;hit.x=x;hit.y=y;hit.w=w;hit.h=w*2/3;
    hit.enter=[key,title]{HoverPreview(key,title);};hit.leave=[key]{if(hoverPreview==key){hoverPreview.clear();hoverTitle.clear();}};
    hit.action=[key,title]{HoverPreview(key,title);};c.Add(std::move(hit));gallery.thumbs.push_back(std::move(t));
}
void GalleryTick(Section& s) {
    const auto g=std::static_pointer_cast<Gallery>(s.live);if(!g)return;
    for(auto& t:g->thumbs) {
        if(!SectionSliceVisible(s.id,t.y,t.h))continue;
        hideout::RequestPreview(t.key);void* texture=hideout::PreviewTexture(t.key);
        if(texture!=t.shown && t.image && game::IsAlive(t.image.Get())){void* a[]{texture};managed::Invoke(UI("RawImage","set_texture",1),t.image.Get(),a);Tint(t.image.Get(),Alpha(kWhite,texture?1.f:0.f));t.shown=texture;}
        const auto status=texture?Tr("Ready"):hideout::PreviewStatus(t.key);
        if(status!=t.status && t.note && game::IsAlive(t.note.Get())){SetText(t.note.Get(),status);t.status=status;}
    }
}
void AskSearch() {
    const auto generation=++search.generation;
    std::string line = "hideout browse " + search.category+" "+search.sort+" "+std::to_string(search.page);
    std::string word;
    for (char ch : search.filter + " ") {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') { if (!word.empty()) line += " " + word; word.clear(); }
        else word += ch;
    }
    search.asked = true;
    Send(line, [generation](const std::string& reply) {
        if(generation!=search.generation)return;
        json::Parse(reply, search.rows);
        if (search.results >= 0) Rebuild(search.results);
    });
}
void DebounceSearch() {
    const auto generation=++search.generation;search.page=0;
    After(250,[generation]{if(generation==search.generation)AskSearch();});
}
void Do(const std::string& line) {
    Send(line, [](const std::string& reply) {
        JValue v; json::Parse(reply, v);
        if (v["ok"].IsBool() && !v["ok"].boolean) Toast(v["error"].Str(), true);
        else if(v["items"].IsArray()) Keep("hideoutread", reply);
        Read("hideoutread");
    });
}
void CloseHideoutGameMenus() {
    void* ui=game::StaticObject(game::FindStaticField("LE.dll","","UIBase","instance"));
    if(!ui||!game::IsAlive(ui))throw std::runtime_error("Game UI is not ready.");
    bool yes=true;void* args[]{&yes,&yes,&yes,&yes,&yes,&yes,&yes,&yes};
    managed::Invoke(managed::Method(ui,"closeAll",8),ui,args);
    void* panels=managed::Get<void*>(ui,managed::Offset("UIBase","_panelSystem"));
    if(panels)managed::Invoke(managed::Method(panels,"CloseAllPanels",0),panels);
}
#include "ingame_hideout_tools.inl"
#include "ingame_hideout_library.inl"
void MakeTabs(Section& s) {
    s.build=[](Section& sec,const Ctx& c){
        const int page=sec.page;const std::vector<std::pair<std::string,std::string>> tabs{{"templates","Templates"},{"library","Model library"},{"placed","Placed objects"},{"ground","Ground"}};
        std::vector<Button> buttons;
        for(const auto& [id,label]:tabs){auto b=AddButton(c,0,0,Tr(label),Look::Normal,[page,id]{ShowTab(page,id);hoverPreview.clear();},false,"hideout-tab-"+id);SetSelected(c,b,PageTab(page)==id);buttons.push_back(std::move(b));}
        return ButtonRows(c,buttons,kPadX,0,kPanelW);
    };s.onTab=[](Section& sec){Rebuild(sec.id);};
}
// Give long model names their own line above the wrapping action buttons.
struct RowButton { std::string label; std::string line; std::string name;Action action;bool enabled=true; };
float Row(const Ctx& in, float y, const std::string& text, const std::string& note, const std::vector<RowButton>& buttons) {
    Style name{Face::Body,13,C("ink")};name.wrap=true;
    const float textHeight=Paragraph(in.parent,"Placed model name",kPanelPad,y,kPanelInner,name,text);
    std::vector<Button> controls;
    for (const auto& button : buttons) {
        const std::string line = button.line;
        const auto action=button.action;
        Button b = AddButton(in, 0, 0, button.label, Look::Small, [line,action] {
            if(action){action();return;}
            if(line.rfind("hideout hold ",0)==0)Send(line,[](const std::string& reply){JValue v;json::Parse(reply,v);if(v["ok"].boolean){Read("hideoutread");Send("panel 0",[](const std::string&){});}else Toast(v["error"].Str(),true);});
            else Do(line);
        }, false, button.name);
        SetEnabled(in,b,button.enabled);
        controls.push_back(std::move(b));
    }
    (void)note;
    return textHeight+6+ButtonRows(in,controls,kPanelPad,y+textHeight+6,kPanelInner,6)+14;
}
void MakeMain(Section& s) {
    s.reads.insert("hideoutread");
    s.build = [](Section&, const Ctx& c) {
        return InPanel(c, [](const Ctx& in, float y) {
            const JValue* r = Cached("hideoutread");
            const bool hall = r && (*r)["hall"].IsBool() && (*r)["hall"].boolean, editing = r && (*r)["editing"].IsBool() && (*r)["editing"].boolean;
            y += H2(in.parent,kPanelPad,y,kPanelInner,Tr("Your hideout"))+6;
            y += Muted(in.parent,kPanelPad,y,kPanelInner,Tr(hall?"Local layout · No character-save changes · Hover pictures to inspect models":"Enable Boss doors, visit the Monolith Hub, then enter your hideout."),12)+8;
            std::vector<Button> buttons;
            const auto button = [&](const std::string& label, Action action, const std::string& name, Look look = Look::Normal) {
                buttons.push_back(AddButton(in,0,0,label,look,std::move(action),false,name));
            };
            button(Tr("Start again"), [] {
                Confirm(Tr("Start again"), Tr("Your hideout goes back to how it was at first. Everything you placed goes."), [] { Do("hideout reset"); });
            }, "hideout-reset");
            button(Tr("Editor tools"),[]{ShowEditorTools();},"hideout-tools");
            button(Tr(editing ? "Finish editing" : "Edit with the mouse"), [editing] {
                if(!editing)CloseHideoutGameMenus();
                Do(editing ? "hideout edit 0" : "hideout edit 1");
                if (!editing) Send("panel 0", [](const std::string&) {});
            }, "hideout-edit");
            button(Tr("Go to the hideout"), [] {
                Send("bossdoors 1",[](const std::string& reply){JValue setup;json::Parse(reply,setup);if(!setup["ok"].boolean){Toast(setup["error"].Str(),true);return;}
                    Send("hideout go",[](const std::string& answer){JValue v;json::Parse(answer,v);if(!v["ok"].boolean){Toast(v["error"].Str(),true);return;}Read("hideoutread");Send("panel 0",[](const std::string&){});});});
            }, "hideout-go", Look::Primary);
            std::reverse(buttons.begin(),buttons.end());
            y += ButtonRows(in,buttons,kPanelPad,y,kPanelInner)+12;
            y += Muted(in.parent, kPanelPad, y, kPanelInner, Tr("Click: pick up / drop · Q/E: rotate · Up/Down: scale · Page Up/Down: height · Delete: remove · End: finish")) + 2;
            return y;
        });
    };
}
// The ground: choices that apply at once. Buttons that do not fit a row go on to the next.
float Choices(const Ctx& in, float y, const std::string& title, const std::vector<std::pair<std::string, std::string>>& choices, int current) {
    y += H3(in.parent, kPanelPad, y, kPanelInner, title) + 6;
    std::vector<Button> buttons;
    for (size_t k = 0; k < choices.size(); ++k) {
        const std::string line = choices[k].second;
        Button b = AddButton(in,0,0,choices[k].first,Look::Normal,[line]{Do(line);},false,"hideout-ground-"+title+"-"+std::to_string(k));
        SetSelected(in,b,static_cast<int>(k)==current);buttons.push_back(std::move(b));
    }
    return y+ButtonRows(in,buttons,kPanelPad,y,kPanelInner,6)+12;
}
std::string templateName;
void RenameTemplate(const std::string& name){
    auto text=std::make_shared<std::string>(name);ShowCustomDialog(480,[text,name](const Ctx& c,float width){
        float y=20;y+=H2(c.parent,20,y,width-40,Tr("Rename template"))+10;
        y+=InputField(c,20,y,width-40,name,*text,Tr("New name"),false,[text](const std::string& value){*text=value;},"hideout-rename-name")+12;
        std::vector<Button> buttons;buttons.push_back(AddButton(c,0,0,Tr("Rename"),Look::Primary,[text,name]{const auto token=DialogToken();Send("hideout template rename "+name+"|"+*text,[token](const std::string& answer){JValue r;json::Parse(answer,r);if(!r["ok"].boolean){Toast(r["error"].Str(),true);return;}if(token==DialogToken())CloseDialog();Keep("hideoutread",answer);Read("hideoutread");});},false,"hideout-rename-apply"));
        buttons.push_back(AddButton(c,0,0,Tr("Cancel"),Look::Normal,[]{CloseDialog();},false,"hideout-rename-cancel"));y+=ButtonRows(c,buttons,20,y,width-40);return y+20;
    });
}
void MakeTemplates(Section& s) {
    s.reads.insert("hideoutread");
    s.build = [](Section& sec, const Ctx& c) {
        auto gallery=std::make_shared<Gallery>();sec.live=gallery;sec.widgetTick=GalleryTick;
        return InPanel(c, [gallery](const Ctx& in, float y) {
            y += H2(in.parent, kPanelPad, y, kPanelInner, Tr("Templates")) + 8;
            const JValue* r = Cached("hideoutread");
            if (!r) return y + EmptyText(in.parent, kPanelPad, y, kPanelInner, Tr("Nothing read yet."));
            y += Muted(in.parent, kPanelPad, y, kPanelInner, Tr("A template puts up a whole hideout at once. What you had can be brought back with Undo.")) + 10;
            // a row of buttons that wraps
            const auto wrap = [&](const std::string& title, const std::vector<std::pair<std::string, std::function<void()>>>& rows, const std::string& prefix) {
                y += H3(in.parent, kPanelPad, y, kPanelInner, title) + 6;
                float x = kPanelPad, row = 0;
                for (size_t k = 0; k < rows.size(); ++k) {
                    Button b = AddButton(in, x, y, rows[k].first, Look::Normal, rows[k].second, false, prefix + std::to_string(k));
                    if (x + b.w > kPanelPad + kPanelInner && x > kPanelPad) { y += row + 6; x = kPanelPad; row = 0; PlaceButton(in, b, x, y); }
                    x += b.w + 6; row = std::max(row, b.h);
                }
                y += row + 12;
            };
            JValue data;json::Parse(assets::Bytes("hideout-designs.json"),data);
            const auto& designs=data["designs"].items;
            const int columns=kPanelInner>=640?2:1;
            const float width=(kPanelInner-(columns-1)*16)/columns;
            float rowHeight=0;
            for(size_t k=0;k<designs.size();++k) {
                if(k && k%columns==0){y+=rowHeight+20;rowHeight=0;}
                const auto& d=designs[k];const auto key="t:"+std::to_string(k),label=d["label"].Str(),line="hideout template ready "+std::to_string(k);
                const float x=kPanelPad+(k%columns)*(width+16),top=y;
                ThumbCard(in,*gallery,x,y,width,key,label);float yy=y+width*2/3+10;
                yy+=H3(in.parent,x,yy,width,label)+6;
                yy+=Muted(in.parent,x,yy,width,d["description"].Str(),12)+8;
                yy+=Muted(in.parent,x,yy,width,d["shape"].Str()+" · "+std::to_string(static_cast<int>(d["size"].Num()))+" · "+d["texture"].Str()+" · "+std::to_string(d["items"].items.size())+" objects",10)+8;
                auto b=AddButton(in,x,yy,Tr("Use this template"),Look::Primary,[line,label]{Confirm(label,Tr("Replace the current hideout? Undo restores your previous layout."),[line]{Do(line);});},false,"hideout-ready-"+std::to_string(k));
                rowHeight=std::max(rowHeight,yy+b.h-top);
            }
            y+=rowHeight+24;
            const auto& saved=(*r)["savedDetails"].items;
            if(!saved.empty())y+=H3(in.parent,kPanelPad,y,kPanelInner,Tr("Your templates"))+10;
            rowHeight=0;
            for(size_t k=0;k<saved.size();++k){
                if(k&&k%columns==0){y+=rowHeight+20;rowHeight=0;}
                const auto name=saved[k]["name"].Str(),key=saved[k]["preview"].Str(),tag=std::to_string(k);const float x=kPanelPad+(k%columns)*(width+16),top=y;
                if(!key.empty())ThumbCard(in,*gallery,x,y,width,key,name);float yy=y+width*2/3+8;yy+=H3(in.parent,x,yy,width,name)+8;
                std::vector<Button> controls;
                controls.push_back(AddButton(in,0,0,Tr("Load"),Look::Normal,[name]{Confirm(name,Tr("Replace the current hideout? Undo restores your previous layout."),[name]{Do("hideout template load "+name);});},false,"hideout-saved-load-"+tag));
                controls.push_back(AddButton(in,0,0,Tr("Rename"),Look::Small,[name]{RenameTemplate(name);},false,"hideout-saved-rename-"+tag));
                controls.push_back(AddButton(in,0,0,Tr("Delete"),Look::Small,[name]{Confirm(Tr("Delete template")+": "+name,Tr("Delete this saved template permanently? Your current hideout stays as it is."),[name]{Do("hideout template remove "+name);});},false,"hideout-saved-delete-"+tag));
                yy+=ButtonRows(in,controls,x,yy,width,6);rowHeight=std::max(rowHeight,yy-top);
            }if(!saved.empty())y+=rowHeight+20;
            y += H3(in.parent, kPanelPad, y, kPanelInner, Tr("Save this hideout as a template")) + 6;
            Style st{Face::Body, 12, C("ink")};
            st.align = kLeft;
            const float h = LineHeight(st) + 22;
            MakeInput(in,kPanelPad,y,kPanelInner,h,st,InputLook{Hex("#080e0e"),Hex("#7b745a99")},templateName,Tr("Name: letters, digits, spaces"),false,
                      [](const std::string& t){templateName=t;},"hideout-template-name");y+=h+8;
            std::vector<Button> actions;
            actions.push_back(AddButton(in,0,0,Tr("Save"),Look::Primary,[] {
                if(templateName.empty())return;const auto name=templateName;bool exists=false;const auto lower=[](std::string value){std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;};
                if(const auto* r=Cached("hideoutread"))for(const auto& item:(*r)["saved"].items)exists|=lower(item.Str())==lower(name);
                if(exists)Confirm(Tr("Overwrite template")+": "+name,Tr("Replace this saved template with your current hideout?"),[name]{Do("hideout template overwrite "+name);});
                else Do("hideout template save "+name);
            },false,"hideout-template-save"));
            y+=ButtonRows(in,actions,kPanelPad,y,kPanelInner)+10;
            if ((*r)["canUndo"].IsBool() && (*r)["canUndo"].boolean) {
                float x = kPanelPad;
                Button undo = AddButton(in, x, y, Tr("Undo"), Look::Normal, [] { Do("hideout template undo"); }, false, "hideout-template-undo");
                y += undo.h + 10;
            }
            if((*r)["canRedo"].boolean){
                auto redo=AddButton(in,kPanelPad,y,Tr("Redo"),Look::Normal,[]{Do("hideout redo");},false,"hideout-template-redo");y+=redo.h+10;
            }
            y += Muted(in.parent, kPanelPad, y, kPanelInner, Tr("To share one, send a friend its file. It goes in this folder, and it shows up here:"), 12) + 4;
            return y + Muted(in.parent, kPanelPad, y, kPanelInner, (*r)["templatesFolder"].Str(), 12);
        });
    };
}
void MakeGround(Section& s) {
    s.reads.insert("hideoutread");
    s.build = [](Section&, const Ctx& c) {
        return InPanel(c, [](const Ctx& in, float y) {
            y += H2(in.parent, kPanelPad, y, kPanelInner, Tr("The ground")) + 8;
            const JValue* r = Cached("hideoutread");
            if (!r) return y + EmptyText(in.parent, kPanelPad, y, kPanelInner, Tr("Nothing read yet."));
            const int shape = static_cast<int>((*r)["shape"].Num(0)), texture = static_cast<int>((*r)["texture"].Num(0));
            const long long size = std::llround((*r)["size"].Num(38));
            const auto line = [](int a, long long b, int t) { return "hideout ground " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(t); };
            std::vector<std::pair<std::string, std::string>> shapes;
            const std::vector<std::string> shapeNames{Tr("Round"), Tr("Square"), Tr("Long hall"), Tr("Cross"), Tr("Three islands")};
            for (size_t k = 0; k < shapeNames.size(); ++k) shapes.push_back({shapeNames[k], line(static_cast<int>(k), size, texture)});
            y = Choices(in, y, Tr("Shape"), shapes, shape);
            const std::vector<std::pair<std::string, long long>> sizes{{Tr("Small"), 26}, {Tr("Medium"), 38}, {Tr("Large"), 55}, {Tr("Huge"), 75}};
            std::vector<std::pair<std::string, std::string>> sizeChoices; int sizeAt = -1;
            for (size_t k = 0; k < sizes.size(); ++k) { sizeChoices.push_back({sizes[k].first, line(shape, sizes[k].second, texture)}); if (sizes[k].second == size) sizeAt = static_cast<int>(k); }
            y = Choices(in, y, Tr("Size"), sizeChoices, sizeAt);
            std::vector<std::pair<std::string, std::string>> textures; int at = 0;
            for (const auto& name : (*r)["textures"].items) { textures.push_back({Tr(name.Str()), line(shape, size, at)}); ++at; }
            y = Choices(in, y, Tr("Texture"), textures, texture);
            return y + Muted(in.parent, kPanelPad, y, kPanelInner, Tr("The whole layout must fit a smaller ground. Move edge objects inward first or choose a larger size."), 12);
        });
    };
}
void MakeAdd(Section& s) {
    s.build = [](Section& sec, const Ctx& c) {
        const int sid = sec.id;
        return InPanel(c, [sid](const Ctx& in, float y) {
            y += H2(in.parent, kPanelPad, y, kPanelInner, Tr("Add anything of the game")) + 8;
            Style st{Face::Body, 12, C("ink")};
            st.align = kLeft;
            const float h = LineHeight(st) + 22;
            MakeInput(in, kPanelPad, y, kPanelInner, h, st, InputLook{Hex("#080e0e"), Hex("#7b745a99")}, search.filter, Tr("Search: statue, wall, tree, chest, fountain..."), false,
                      [](const std::string& t) { search.filter = t; DebounceSearch(); }, "hideout-search");
            y += h + 10;
            const std::vector<std::pair<std::string, std::string>> kinds{{"all", Tr("Everything")}, {"favorites",Tr("Favorites")},{"recent",Tr("Recently used")}, {"b", Tr("Buildings")}, {"n", Tr("Nature")}, {"p", Tr("Things")}, {"c", Tr("Creatures")}, {"o", Tr("Other")}};
            std::vector<Button> buttons;
            for (const auto& [key, label] : kinds) {
                const std::string k = key;
                Button b = AddButton(in,0,0,label,Look::Normal,[k,sid] {
                    search.category = k; if(k=="recent")search.sort="recent"; search.page=0; AskSearch(); Rebuild(sid);
                }, false, "hideout-kind-" + k);
                SetSelected(in,b,search.category==k);buttons.push_back(std::move(b));
            }
            y+=ButtonRows(in,buttons,kPanelPad,y,kPanelInner,6)+10;
            const std::vector<std::string> tags{"","floor","wall","column","statue","banner","light","altar","crystal","rock","tree","plant","furniture"};
            const std::vector<std::string> labels{Tr("Any type"),Tr("Floors & paths"),Tr("Walls"),Tr("Columns"),Tr("Statues"),Tr("Banners"),Tr("Lights (test effects)"),Tr("Altars"),Tr("Crystals"),Tr("Rocks"),Tr("Trees"),Tr("Plants"),Tr("Furniture")};
            int at=0;for(size_t k=1;k<tags.size();++k)if(search.category=="tag:"+tags[k])at=static_cast<int>(k);
            y+=SelectField(in,kPanelPad,y,kPanelInner,Tr("Model type (from asset names)"),labels,at,[tags,sid](int k){search.category=k?"tag:"+tags[k]:"all";search.page=0;AskSearch();Rebuild(sid);},"hideout-model-type");return y;
        });
    };
}
void MakeResults(Section& s) {
    s.build=[](Section& sec,const Ctx& c){
        search.results=sec.id;if(!search.asked)AskSearch();
        auto gallery=std::make_shared<Gallery>();sec.live=gallery;sec.widgetTick=GalleryTick;
        return InPanel(c,[gallery,sid=sec.id](const Ctx& in,float y){
            const auto& rows=search.rows["rows"].items;
            if(search.rows["fits"].IsBool()&&!search.rows["fits"].boolean)return y+NoteBox(in.parent,kPanelPad,y,kPanelInner,Tr("The asset catalog does not match this game build. Update EpochPact before placing models."));
            const int page=static_cast<int>(search.rows["page"].Num()),pages=static_cast<int>(search.rows["pages"].Num(1));
            std::vector<Button> navigation;
            navigation.push_back(AddButton(in,0,0,Tr("Previous"),Look::Small,[page]{search.page=std::max(0,page-1);AskSearch();},false,"hideout-page-previous"));
            navigation.push_back(AddButton(in,0,0,Tr("Next"),Look::Small,[page,pages]{search.page=std::min(pages-1,page+1);AskSearch();},false,"hideout-page-next"));
            navigation.push_back(AddButton(in,0,0,search.sort=="az"?"A \u2192 Z":search.sort=="za"?"Z \u2192 A":Tr("Recently used"),Look::Small,[]{search.sort=search.sort=="az"?"za":search.sort=="za"?"recent":"az";search.page=0;AskSearch();},false,"hideout-sort"));
            SetEnabled(in,navigation[0],page>0);SetEnabled(in,navigation[1],page+1<pages);
            y+=ButtonRows(in,navigation,kPanelPad,y,kPanelInner)+10;
            y+=Muted(in.parent,kPanelPad,y,kPanelInner,std::to_string(static_cast<int>(search.rows["total"].Num()))+" models · "+Tr("Page")+" "+std::to_string(page+1)+" / "+std::to_string(pages)+" · "+Tr("Hover a picture to inspect it. Add picks it up for placement."),12)+12;
            const int columns=std::max(1,std::min(3,static_cast<int>((kPanelInner+14)/250)));
            const float width=(kPanelInner-(columns-1)*14)/columns;float rowHeight=0;
            for(size_t k=0;k<rows.size();++k) {
                if(k && k%columns==0){y+=rowHeight+18;rowHeight=0;}
                const auto& r=rows[k];const auto index=std::to_string(static_cast<int>(r["i"].Num())),key="a:"+index,name=r["n"].Str();
                const float xx=kPanelPad+(k%columns)*(width+14),top=y;
                ThumbCard(in,*gallery,xx,y,width,key,name);float yy=y+width*2/3+8;
                yy+=Muted(in.parent,xx,yy,width,name,12)+6;
                auto add=AddButton(in,xx,yy,Tr("Add & place"),Look::Primary,[index]{
                    Send("hideout add "+index,[](const std::string& reply){JValue v;json::Parse(reply,v);if(v["ok"].boolean){Read("hideoutread");Send("panel 0",[](const std::string&){});}else Toast(v["error"].Str(),true);});
                },false,"hideout-add-"+index);
                yy+=add.h+6;const bool favorite=r["favorite"].boolean;
                auto star=AddButton(in,xx,yy,Tr(favorite?"Remove favorite":"Favorite"),Look::Small,[index,favorite]{Send("hideout favorite "+index+(favorite?" 0":" 1"),[](const std::string& reply){JValue value;json::Parse(reply,value);if(!value["ok"].boolean)Toast(value["error"].Str(),true);else AskSearch();});},false,"hideout-favorite-"+index);
                rowHeight=std::max(rowHeight,yy+star.h-top);
            }
            if(rows.empty())y+=EmptyText(in.parent,kPanelPad,y,kPanelInner,Tr("No models match these words and category."));
            return y+rowHeight;
        });
    };
}
void MakeItems(Section& s) {
    s.reads.insert("hideoutread");
    s.build = [](Section&, const Ctx& c) {
        return InPanel(c, [](const Ctx& in, float y) {
            y += H2(in.parent, kPanelPad, y, kPanelInner, Tr("What stands there")) + 10;
            const JValue* r = Cached("hideoutread");
            if (!r) return y + EmptyText(in.parent, kPanelPad, y, kPanelInner, Tr("Nothing read yet."));
            for (const auto& item : (*r)["items"].items) {
                const std::string id = std::to_string(static_cast<long long>(item["id"].Num(0)));
                const int door = static_cast<int>(item["door"].Num(0));
                const bool off = item["off"].IsBool() && item["off"].boolean;
                std::string name = Pretty(item["name"].Str());
                if (door > 0) name = Fmt("Boss door {0}", {std::to_string(door)});
                else if (door == -3) name = Tr("Way out");
                else if (door == 0) name = Tr("Difficulty door");
                else if (door == -1) name = Tr("Boss Rush door");
                if (door == -4) name = Tr("Stash");
                if (door == -5) name = Tr("Vendor");
                if (door == -6) name = Tr("Eternity Cache");
                if (item["failed"].IsBool() && item["failed"].boolean) name += " " + Tr("(not found)");
                if(item["locked"].boolean)name+=" · "+Tr("Locked");
                if(item["group"].Num()>0)name+=" · "+Tr("Group")+" "+std::to_string(static_cast<int>(item["group"].Num()));
                std::vector<RowButton> buttons;
                if (off) {
                    buttons.push_back({Tr("Bring back"), "hideout show " + id + " 1", "hideout-show-" + id});
                    y += Row(in, y, name + " " + Tr("(taken away)"), "", buttons);
                    continue;
                }
                y+=CheckField(in,kPanelPad,y,Tr("Select"),item["selected"].boolean,[id](bool){Do("hideout select "+id+" toggle");},"hideout-select-"+id)+5;
                buttons.push_back({Tr("Pick up"), "hideout hold " + id, "hideout-hold-" + id});
                buttons.push_back({Tr("Exact transform"),"","hideout-inspect-"+id,[id]{Send("hideout select "+id,[](const std::string& answer){JValue r;json::Parse(answer,r);if(r["ok"].boolean)ShowEditorTools();else Toast(r["error"].Str(),true);});}});
                buttons.push_back({Tr(item["locked"].boolean?"Unlock":"Lock"),"hideout objectlock "+id+(item["locked"].boolean?" 0":" 1"),"hideout-lock-"+id});
                buttons.push_back({Tr("Here"), "hideout here " + id, "hideout-here-" + id});
                buttons.push_back({"\xE2\x86\xBA", "hideout turn " + id + " -15", "hideout-left-" + id});
                buttons.push_back({"\xE2\x86\xBB", "hideout turn " + id + " 15", "hideout-right-" + id});
                buttons.push_back({"\xE2\x88\x92", "hideout size " + id + " 0.9", "hideout-small-" + id});
                buttons.push_back({"+", "hideout size " + id + " 1.1", "hideout-big-" + id});
                if (door != -3) buttons.push_back({Tr("Remove"), "hideout remove " + id, "hideout-remove-" + id});
                if(item["pending"].boolean)for(auto& b:buttons)if(b.line.rfind("hideout objectlock ",0)==0)b.enabled=false;
                if(item["locked"].boolean)for(auto& b:buttons)if(!b.action&&b.line.rfind("hideout objectlock ",0)!=0)b.enabled=false;
                y += Row(in, y, name, "", buttons);
            }
            return y;
        });
    };
}
}  // namespace
Action SuspendHideoutBuilderForLayout(){
    if(!builder.open||!StandaloneDialogOpen())return {};
    const auto query=builder.query,category=builder.category,sort=builder.sort;
    const bool placed=builder.placed;const int page=builder.page;
    // The host owns the impending canvas teardown. Invalidate old replies,
    // retaining only ordinary browser values, never old Unity pins or actions.
    ClearBuilderHistoryWait();builder.open=builder.opening=builder.pending=false;
    builder.bodyDirty=builder.wholeDirty=false;++builder.epoch;++builder.generation;
    hoverPreview.clear();hoverTitle.clear();
    return [query,category,sort,placed,page]{
        if(!hideout::Editing()||!hideout::PlayerInside())return;
        builder.query=query;builder.category=category;builder.sort=sort;builder.placed=placed;
        OpenHideoutBuilder(page);
    };
}
namespace {
struct HideoutHud {
    Pin parent,root,listener,caption,hint,tooltip,tooltipImage,tooltipTitle,tooltipNote;
    std::vector<Hit> hits;
    float scale=1,width=1600,height=900,x=0,y=0,w=0,h=0;
    bool edit=false,down=false,under=false,visible=false;
    int held=-2;
    unsigned long long historyRevision=0;
    void* shown=nullptr;
    std::string hover,shownCaption,shownHint;
} hud;
#include "ingame_hideout_shortcut.inl"
void OpenHideoutPage(const std::string& tab) {
    Send("panel 1",[tab](const std::string&){Navigate("hideout",tab);Read("hideoutread");});
}
std::string EditorHint() {
    if(hud.held<0&&hideout::SelectionCount()>0)return Tr("Ctrl-click: add/remove selection · Move selection to drag · Tools: align, group and exact values");
    if(hud.held<0)return Tr("Click a model to select it. The green copy follows your cursor; Q/E rotates it.");
    if(!hideout::PlacementValid()&&!hideout::PlacementIssue().empty())return Tr(hideout::PlacementIssue());
    return Tr(hideout::PlacementValid()?"Click to place · Right-click: cancel · Q/E: rotate · Delete: remove · Ctrl+Z/Y: undo/redo":"Move inside the hideout · Right-click: cancel · Q/E: rotate · Delete: remove");
}
void BuildHideoutBar(bool gameMenuOpen) {
    if(hud.root){void* a[]{GameObjectOf(hud.root.Get())};SetActive(a[0],false);managed::Invoke(U("Object","Destroy",1),nullptr,a);}
    hud.hits.clear();hud.edit=hideout::Editing()&&!gameMenuOpen;hud.held=hideout::Held();
    hud.historyRevision=hideout::HistoryRevision();
    hud.caption.Reset();hud.hint.Reset();hud.shownCaption.clear();hud.shownHint.clear();
    hud.w=std::min(hud.width-32,hud.edit?(hideout::SelectionCount()>0?820.f:720.f):220.f);hud.h=40;
    auto root=New("EpochPact hideout editor toolbar",hud.parent.Get(),0,0,hud.w,hud.h);hud.root.Set(root.rt);
    auto* canvas=AddCanvas(root.go);bool override=true;int order=30030;void* sorting[]{&override},*layer[]{&order};
    managed::Invoke(Find("UnityEngine.UIModule.dll","UnityEngine","Canvas","set_overrideSorting",1),canvas,sorting);
    managed::Invoke(Find("UnityEngine.UIModule.dll","UnityEngine","Canvas","set_sortingOrder",1),canvas,layer);
    hud.listener.Set(Add(root.go,"LE.dll","","UIMouseListener"));
    const auto opaque=Hex("#0a1813");void* background=Fill(root.rt,"Hideout toolbar",0,0,hud.w,hud.h,opaque,4);
    void* border=Outline(root.rt,"Gold frame",0,0,hud.w,hud.h,Hex("#b59b62"),4);
    Ctx c{&hud.hits,Area::Fixed,root.rt,0,0,-30};
    if(!hud.edit) {
        Style label{Face::Serif,15,C("ink")};label.align=kCenter;Ellipsize(TextBox(root.rt,"Edit Hideout",12,0,hud.w-24,hud.h,label,Tr(hideout::Editing()?"✎  Resume editing":"✎  Edit Hideout")));
        Hit hit;hit.name="hideout-hud-edit";hit.w=hud.w;hit.h=hud.h;hit.action=[]{CloseHideoutGameMenus();Do("hideout edit 1");};AddHover(hit,background,opaque,opaque);c.Add(std::move(hit));
    } else {
        constexpr float pad=12;Style title{Face::BodySemi,12,Hex("#cabd94")};title.align=kLeft;
        const float titleHeight=LineHeight(title)+4;hud.caption.Set(TextBox(root.rt,"Selected object",pad,pad,hud.w-2*pad,titleHeight,title,""));Ellipsize(hud.caption.Get());
        Style hint{Face::Body,11,C("muted")};hint.wrap=true;hint.line=1.4f;
        const float hintY=pad+titleHeight+4;void* hintText=TextBox(root.rt,"Placement instructions",pad,hintY,hud.w-2*pad,100,hint,EditorHint());hud.hint.Set(hintText);
        int lines=1;for(const auto& text:{EditorHint(),Tr("Click to place · Right-click: cancel · Q/E: rotate · Delete: remove · Ctrl+Z/Y: undo/redo"),Tr("Move inside the hideout · Right-click: cancel · Q/E: rotate · Delete: remove")}){SetText(hintText,text);lines=std::max(lines,TextLines(hintText));}
        const float hintHeight=lines*LineHeight(hint);Resize(TransformOf(hintText),hud.w-2*pad,hintHeight);hud.shownHint=EditorHint();SetText(hintText,hud.shownHint);
        float y=hintY+hintHeight+10;std::vector<Button> buttons;
        const auto button=[&](const std::string& label,Action action,const std::string& name,Look look=Look::Normal){buttons.push_back(AddButton(c,0,0,label,look,std::move(action),false,name));};
        button(Tr("Add objects"),[]{OpenHideoutBuilder();},"hideout-hud-library",Look::Primary);
        button(Tr("Templates"),[]{OpenHideoutPage("templates");},"hideout-hud-templates");
        button(Tr("Tools"),[]{OpenEditorTools();},"hideout-hud-tools");
        button(Tr("Undo"),[]{Do("hideout undo");},"hideout-hud-undo");
        SetEnabled(c,buttons.back(),hideout::CanUndo());
        button(Tr("Redo"),[]{Do("hideout redo");},"hideout-hud-redo");
        SetEnabled(c,buttons.back(),hideout::CanRedo());
        button(Tr("Cancel"),[]{Do("hideout cancel");},"hideout-hud-cancel");
        button(Tr("Done"),[]{Do("hideout edit 0");},"hideout-hud-done",Look::Primary);
        y+=ButtonRows(c,buttons,pad,y,hud.w-2*pad);
        if(hud.held>=0){y+=8;buttons.clear();const auto transform=[&](const std::string& label,const std::string& line,const std::string& name){buttons.push_back(AddButton(c,0,0,Tr(label),Look::Small,[line]{Do(line);},false,name));};
            const auto id=std::to_string(hud.held),angle=EditorNumber(hideout::AngleStep());transform("↶ "+angle+"°","hideout turn "+id+" -"+angle,"hideout-hud-rotate-left");transform("↷ "+angle+"°","hideout turn "+id+" "+angle,"hideout-hud-rotate-right");
            transform("Size −","hideout size "+id+" 0.9","hideout-hud-smaller");transform("Size +","hideout size "+id+" 1.1","hideout-hud-larger");
            transform("Height −","hideout lift "+id+" -0.25","hideout-hud-lower");transform("Height +","hideout lift "+id+" 0.25","hideout-hud-raise");
            transform("Place","hideout drop","hideout-hud-place");transform("Cancel object","hideout unhold","hideout-hud-unhold");
            if(hideout::CanRemoveHeld())transform("Delete object","hideout remove "+id,"hideout-hud-delete");
            if(hideout::SelectionDuplicable())transform("Duplicate","hideout selection duplicate","hideout-hud-duplicate");
            transform("Lock","hideout selection lock 1","hideout-hud-lock");SetEnabled(c,buttons.back(),!hideout::SelectionPending());
            y+=ButtonRows(c,buttons,pad,y,hud.w-2*pad);
        }
        else if(hideout::SelectionCount()>0){
            y+=8;buttons.clear();const auto selectionAction=[&](const std::string& label,const std::string& line,const std::string& key){buttons.push_back(AddButton(c,0,0,Tr(label),Look::Small,[line]{Do(line);},false,"hideout-hud-"+key));};
            if(!hideout::SelectionLocked())selectionAction("Move selection","hideout selection move","move-selection");
            if(hideout::SelectionDuplicable())selectionAction("Duplicate","hideout selection duplicate","duplicate");
            if(hideout::CanRemoveHeld())selectionAction("Delete selection","hideout selection delete","delete");
            selectionAction(hideout::SelectionLocked()?"Unlock":"Lock",std::string("hideout selection lock ")+(hideout::SelectionLocked()?"0":"1"),"lock");
            selectionAction("Clear selection","hideout selection clear","clear-selection");
            y+=ButtonRows(c,buttons,pad,y,hud.w-2*pad);
        }
        hud.h=y+pad;
    }
    Resize(root.rt,hud.w,hud.h);Resize(TransformOf(background),hud.w,hud.h);Resize(TransformOf(border),hud.w,hud.h);
    hud.x=(hud.width-hud.w)/2;hud.y=std::max(16.f,hud.height-190-hud.h);Move(root.rt,hud.x,hud.y);
    hud.down=true;
}
void TooltipStep() {
    const bool show=WindowOpen()&&!hoverPreview.empty();
    if(!hud.tooltip)return;
    SetActive(GameObjectOf(hud.tooltip.Get()),show);
    if(!show){hud.hover.clear();return;}
    const auto key=hoverPreview;PollInspectionPreview(key);
    if(hud.hover!=key){SetText(hud.tooltipTitle.Get(),hoverTitle);hud.hover=key;}
    auto* texture=hideout::PreviewTexture(key);
    if(texture!=hud.shown){void* a[]{texture};managed::Invoke(UI("RawImage","set_texture",1),hud.tooltipImage.Get(),a);Tint(hud.tooltipImage.Get(),Alpha(kWhite,texture?1.f:0.f));hud.shown=texture;}
    SetText(hud.tooltipNote.Get(),Tr("Static in-game model preview · ")+hideout::PreviewStatus(key));
}
}
void HideoutHudInit(void* parent,int width,int height) {
    ReleaseHideoutHud();hud.parent.Set(parent);hud.scale=std::clamp(height/900.f,.5f,2.f);hud.width=width/hud.scale;hud.height=height/hud.scale;
    ShortcutInit();
    auto tip=New("EpochPact model inspection",parent,std::max(10.f,hud.width-490),65,480,366);hud.tooltip.Set(tip.rt);
    auto* canvas=AddCanvas(tip.go);bool override=true;int order=30040;void* a[]{&override};void* b[]{&order};
    managed::Invoke(Find("UnityEngine.UIModule.dll","UnityEngine","Canvas","set_overrideSorting",1),canvas,a);managed::Invoke(Find("UnityEngine.UIModule.dll","UnityEngine","Canvas","set_sortingOrder",1),canvas,b);
    Fill(tip.rt,"Inspection background",0,0,480,366,Hex("#08110ffb"),4);Outline(tip.rt,"Inspection frame",0,0,480,366,Hex("#b59b62"),4);
    Style title{Face::Serif,17,C("ink")};title.align=kLeft;
    hud.tooltipTitle.Set(TextBox(tip.rt,"Preview title",12,5,456,28,title,""));Ellipsize(hud.tooltipTitle.Get());hud.tooltipImage.Set(RawPicture(tip.rt,8,35,464,304));
    Style note{Face::Body,10,C("muted")};hud.tooltipNote.Set(TextBox(tip.rt,"Preview result",12,340,456,22,note,""));
    SetActive(tip.go,false);
}
bool HideoutGameMenuOpen() {
    static const auto* instance=game::FindStaticField("LE.dll","","UIBase","instance");
    static const auto any=game::FindMethod("LE.dll","","UIBase","AnyPanelOpened",1);
    void* ui=game::StaticObject(instance);if(!ui||!any||!game::IsAlive(ui))return true;
    using Query=bool(*)(void*,int,const il2cpp::Method*);
    return reinterpret_cast<Query>(any.code)(ui,7,any.info); // left, centre or right menu; no scene scan
}
void HideoutHudFrame() {
    if(!hud.parent || !game::IsAlive(hud.parent.Get()))return;
    HideoutBuilderFrame();ShortcutFrame();
    TooltipStep();const bool inside=hideout::PlayerInside(),menuOpen=inside&&HideoutGameMenuOpen();
    const bool visible=inside&&!WindowOpen();
    if(!visible){
        if(hud.root && game::IsAlive(hud.root.Get()))SetActive(GameObjectOf(hud.root.Get()),false);
        if(hud.under && hud.listener && game::IsAlive(hud.listener.Get())){bool no=false;void* a[]{&no};managed::Invoke(Find("LE.dll","","UIMouseListener","set_underMouse",1),hud.listener.Get(),a);}
        hud.visible=hud.under=false;hideout::BlockEditorPointer(menuOpen||ShortcutUnderPointer(),menuOpen);return;
    }
    if(!hud.root || !game::IsAlive(hud.root.Get()) || hud.edit!=(hideout::Editing()&&!menuOpen) || hud.held!=hideout::Held()||hud.historyRevision!=hideout::HistoryRevision())BuildHideoutBar(menuOpen);
    SetActive(GameObjectOf(hud.root.Get()),true);hud.visible=true;
    if(hud.edit && hud.caption){const auto caption=hud.held>=0?hideout::HeldName()+" · "+EditorNumber(hideout::HeldRotation())+"°":hideout::SelectionCount()>0?std::to_string(hideout::SelectionCount())+" "+Tr("objects selected"):Tr("HIDEOUT EDITOR");
        if(caption!=hud.shownCaption){SetText(hud.caption.Get(),caption);hud.shownCaption=caption;}
        const auto hint=EditorHint();if(hint!=hud.shownHint)BuildHideoutBar(menuOpen);
    }
    POINT p{};RECT r{};HWND window=GetForegroundWindow();DWORD pid=0;if(window)GetWindowThreadProcessId(window,&pid);
    bool cursor=pid==GetCurrentProcessId() && GetCursorPos(&p) && ScreenToClient(window,&p) && GetClientRect(window,&r) && r.right>0 && r.bottom>0;
    const float x=cursor?p.x*hud.width/r.right:-10000,y=cursor?p.y*hud.height/r.bottom:-10000;
    const bool under=cursor && x>=hud.x && x<=hud.x+hud.w && y>=hud.y && y<=hud.y+hud.h;
    hideout::BlockEditorPointer(under||menuOpen||ShortcutUnderPointer(),menuOpen);
    if(under!=hud.under){bool on=under;void* a[]{&on};managed::Invoke(Find("LE.dll","","UIMouseListener","set_underMouse",1),hud.listener.Get(),a);hud.under=under;}
    const bool down=(GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON)?VK_RBUTTON:VK_LBUTTON)&0x8000)!=0;
    if(down&&!hud.down&&under)for(const auto& hit:hud.hits)if(x-hud.x>=hit.x && x-hud.x<=hit.x+hit.w && y-hud.y>=hit.y && y-hud.y<=hit.y+hit.h && hit.enabled){const auto action=hit.action;if(action)action();break;}
    hud.down=down;
}
void ReleaseHideoutHud() {ShortcutRelease();ReleaseHideoutBuilder();hud=HideoutHud{};hoverPreview.clear();hoverTitle.clear();hideout::BlockEditorPointer(false);}
#ifdef EPOCHPACT_TESTING
std::string HideoutHudMenuTest(const std::string& action) {
    void* ui=game::StaticObject(game::FindStaticField("LE.dll","","UIBase","instance"));
    if(!ui||!game::IsAlive(ui))throw std::runtime_error("Game UI is not ready.");
    if(action=="close")CloseHideoutGameMenus();
    else if(action=="builder-open")OpenHideoutBuilder();
    else if(action=="shortcut-click"){ShortcutClickTest();}
    else if(action=="builder-close")CloseHideoutBuilder();
    else if(action.rfind("click:",0)==0){
        if(!hud.visible)throw std::runtime_error("The toolbar must be visible.");
        const auto name=action.substr(6);
        for(const auto& h:hud.hits)if(h.name==name&&h.enabled){h.action();return "{\"ok\":true}";}
        throw std::runtime_error("No enabled toolbar action.");
    }
    else if(action=="edit-entry"){
        if(!hud.visible||hud.edit)throw std::runtime_error("The compact edit entry must be visible.");
        for(const auto& h:hud.hits)if(h.name=="hideout-hud-edit"){h.action();return "{\"ok\":true}";}
        throw std::runtime_error("No compact edit entry.");
    }
    else if(action=="factions")managed::Invoke(managed::Method(ui,"FactionsKeyDown",0),ui);
    else throw std::runtime_error("Unknown private menu fixture.");
    return "{\"ok\":true}";
}
std::string HideoutHudAudit() {
    std::ostringstream out;int outside=0,overlap=0;
    for(size_t k=0;k<hud.hits.size();++k){const auto& a=hud.hits[k];
        if(a.x<-.01f||a.y<-.01f||a.x+a.w>hud.w+.01f||a.y+a.h>hud.h+.01f)++outside;
        for(size_t j=0;j<k;++j){const auto& b=hud.hits[j];if(std::min(a.x+a.w,b.x+b.w)-std::max(a.x,b.x)>.01f&&std::min(a.y+a.h,b.y+b.h)-std::max(a.y,b.y)>.01f)++overlap;}}
    const bool onScreen=hud.x>=0&&hud.y>=0&&hud.x+hud.w<=hud.width+.01f&&hud.y+hud.h<=hud.height+.01f;
    out<<"{\"ok\":true,\"visible\":"<<managed::Boolean(hud.visible)<<",\"editing\":"<<managed::Boolean(hud.edit)<<",\"held\":"<<hud.held<<",\"w\":"<<hud.w<<",\"h\":"<<hud.h<<",\"onScreen\":"<<managed::Boolean(onScreen)<<",\"outsideButtons\":"<<outside<<",\"overlappingButtons\":"<<overlap<<",\"captionLines\":"<<(hud.caption?TextLines(hud.caption.Get()):0)<<",\"hintLines\":"<<(hud.hint?TextLines(hud.hint.Get()):0)<<",\"buttons\":[";
    for(size_t k=0;k<hud.hits.size();++k){const auto& h=hud.hits[k];out<<(k?",":"")<<"{\"name\":"<<managed::Json(h.name)<<",\"x\":"<<h.x<<",\"y\":"<<h.y<<",\"w\":"<<h.w<<",\"h\":"<<h.h<<'}';}
    return out.str()+"],\"builder\":"+HideoutBuilderAudit()+",\"shortcut\":"+ShortcutAudit()+"}";
}
#endif
void RegisterHideoutSections() {
    RegisterSection("hideout_main", MakeMain);
    RegisterSection("hideout_tabs", MakeTabs);
    RegisterSection("hideout_templates", MakeTemplates);
    RegisterSection("hideout_ground", MakeGround);
    RegisterSection("hideout_add", MakeAdd);
    RegisterSection("hideout_results", MakeResults);
    RegisterSection("hideout_items", MakeItems);
}
}  // namespace ep::ingame::view
