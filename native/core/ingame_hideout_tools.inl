void ShowEditorTools();
void EditorAction(const std::string& line,bool world=false){
    const auto token=DialogToken();Send(line,[world,token](const std::string& answer){JValue r;json::Parse(answer,r);if(!r["ok"].boolean){Toast(r["error"].Str(),true);return;}
        Keep("hideoutread",answer);Read("hideoutread");if(!WindowOpen()||token!=DialogToken())return;CloseDialog();
        if(world)Send("panel 0",[](const std::string&){});else ShowEditorTools();});
}
std::string EditorNumber(double n){std::ostringstream out;out.precision(7);out<<n;return out.str();}
float EditorValue(std::string text){
    try{std::replace(text.begin(),text.end(),',','.');size_t used=0;const float value=std::stof(text,&used);
        if(used==text.size()&&std::isfinite(value))return value;
    }catch(const std::exception&){}
    throw std::runtime_error("Enter a valid number in every transform field.");
}
void ShowEditorTools(){
    const auto token=DialogToken();Send("hideout read",[token](const std::string& answer){
        if(!WindowOpen()||token!=DialogToken())return;
        JValue r;if(!json::Parse(answer,r)||!r["ok"].boolean){Toast(r["error"].Str(),true);return;}
        const int count=static_cast<int>(r["selectionCount"].Num()),moving=static_cast<int>(r["held"].Num(-1));
        const int anchor=hideout::SelectionAnchor();const JValue* chosen=nullptr;bool locked=false,pending=false;for(const auto& i:r["items"].items)if(i["selected"].boolean){if(!chosen||i["id"].Num()==anchor)chosen=&i;locked|=i["locked"].boolean;pending|=i["pending"].boolean;}
        const int id=chosen?static_cast<int>((*chosen)["id"].Num()):-1;
        auto fields=std::make_shared<std::array<std::string,5>>();
        if(chosen){const auto& values=id==moving?r["preview"]:*chosen;const char* keys[]{"right","up","lift","yaw","size"};for(int k=0;k<5;++k)(*fields)[k]=EditorNumber(values[keys[k]].Num());}
        const std::string name=chosen?Pretty((*chosen)["name"].Str()):Tr("No objects selected");
        const bool canRemove=hideout::CanRemoveHeld(),canDuplicate=hideout::SelectionDuplicable();
        ShowCustomDialog(660,[r,fields,id,count,moving,name,locked,pending,canRemove,canDuplicate](const Ctx& c,float width){
            constexpr float pad=20;const float w=width-2*pad,half=(w-16)/2;float y=20;
            y+=H2(c.parent,pad,y,w,Tr("Hideout tools"))+6;
            y+=Muted(c.parent,pad,y,w,count>1?std::to_string(count)+" "+Tr("objects selected")+" · "+name+" ("+Tr("anchor")+")":name,12)+12;
            if(id>=0){
                const std::array<std::string,5> labels{Tr("Right / X"),Tr("Forward / Z"),Tr("Height"),Tr("Rotation (degrees)"),Tr("Scale")};
                float row=0;for(int k=0;k<5;++k){if(k&&k%2==0){y+=row+8;row=0;}
                    row=std::max(row,InputField(c,pad+(k%2)*(half+16),y,half,labels[k],(*fields)[k],"0",true,[fields,k](const std::string& value){(*fields)[k]=value;},"hideout-exact-"+std::to_string(k)));}
                y+=row+10;
                auto apply=AddButton(c,pad,y,Tr(moving>=0?"Set preview transform":"Apply transform"),Look::Primary,[fields,id,moving]{
                    try{std::string line="hideout transform "+std::to_string(id);for(const auto& field:*fields)line+=" "+EditorNumber(EditorValue(field));EditorAction(line,moving>=0);}
                    catch(const std::exception& e){Toast(Tr(e.what()),true);}
                },false,"hideout-exact-apply");SetEnabled(c,apply,!locked);y+=apply.h+12;
                std::vector<Button> actions;
                const auto action=[&](const std::string& label,const std::string& line,const std::string& key,bool enabled=true,bool world=false){
                    auto b=AddButton(c,0,0,Tr(label),Look::Small,[line,world]{EditorAction(line,world);},false,"hideout-tools-"+key);SetEnabled(c,b,enabled);actions.push_back(std::move(b));};
                action("Move selection","hideout selection move","move",!locked,true);
                action("Duplicate","hideout selection duplicate","duplicate",canDuplicate,true);
                action(locked?"Unlock":"Lock",std::string("hideout selection lock ")+(locked?"0":"1"),"lock",!pending);
                action("Delete","hideout selection delete","delete",canRemove,true);
                action("Group","hideout selection group","group",!locked&&!pending&&count>1);
                action("Ungroup","hideout selection ungroup","ungroup",!locked&&!pending);
                y+=ButtonRows(c,actions,pad,y,w)+12;
                if(pending)y+=Muted(c.parent,pad,y,w,Tr("Place new objects before grouping, aligning or locking them."),12)+8;
                if(count>1){
                    y+=H3(c.parent,pad,y,w,Tr("Align to selection anchor"))+6;actions.clear();
                    for(const auto& a:std::vector<std::pair<std::string,std::string>>{{"Left edges","left"},{"Right edges","right"},{"Front edges","top"},{"Back edges","bottom"},{"X centers","x"},{"Z centers","z"}})
                        action(a.first,"hideout selection align "+a.second,"align-"+a.second,!locked&&!pending);
                    y+=ButtonRows(c,actions,pad,y,w)+12;
                }
            }else y+=Muted(c.parent,pad,y,w,Tr("Ctrl-click models or use the Placed objects checkboxes to select up to 32 objects."),12)+12;
            y+=H3(c.parent,pad,y,w,Tr("Placement options"))+6;
            const std::vector<std::string> gridNames{Tr("Off"),"0.25","0.5","1","2","5"};const std::array<float,6> grids{0,.25f,.5f,1,2,5};
            int grid=0;for(int k=0;k<6;++k)if(std::abs(r["grid"].Num()-grids[k])<.001)grid=k;
            float h=SelectField(c,pad,y,half,Tr("Grid snap"),gridNames,grid,[grids](int k){EditorAction("hideout options grid "+EditorNumber(grids[k]));},"hideout-grid");
            const std::vector<std::string> angleNames{"1°","5°","15°","45°"};const std::array<float,4> angles{1,5,15,45};int angle=2;
            for(int k=0;k<4;++k)if(std::abs(r["angle"].Num()-angles[k])<.001)angle=k;
            h=std::max(h,SelectField(c,pad+half+16,y,half,Tr("Q/E rotation step"),angleNames,angle,[angles](int k){EditorAction("hideout options angle "+EditorNumber(angles[k]));},"hideout-angle"));y+=h+10;
            y+=CheckField(c,pad,y,Tr("Select floor pieces with the mouse"),r["selectFloors"].boolean,[](bool on){EditorAction(std::string("hideout options floors ")+(on?"1":"0"));},"hideout-floor-picking")+12;
            auto back=AddButton(c,pad,y,Tr("Back to editing"),Look::Primary,[]{CloseDialog();Send("panel 0",[](const std::string&){});},false,"hideout-tools-back");y+=back.h;
            return y+20;
        });
    });
}
void OpenEditorTools(){Send("panel 1",[](const std::string&){ShowEditorTools();});}
