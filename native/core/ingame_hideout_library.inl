// The dedicated Hideout Builder. It uses the shared native dialog/input host,
// but has its own catalog, selection, pagination and refresh state. It never
// navigates the main F6 pages or commits an object merely by inspecting it.
struct HideoutBuilder {
    bool open=false, opening=false, pending=false, bodyDirty=false, wholeDirty=false, placed=false, restoring=false, catalogPending=false;
    Pin restoreActor;std::string restorePlayerId;DWORD restoreProcess=0;uint64_t restoreUntil=0;
    unsigned epoch=0,generation=0;
    int page=0,pageSize=6,loadedPage=-1;
    std::string query,category="all",sort="az",previewKey,previewName,shownKey,shownStatus,shownName;
    JValue rows,snapshot,inspected;
    bool inspectedPlaced=false;
    Pin parent,body,title,image,status,detail;
    void* shownTexture=nullptr;
    Ctx ctx;
    size_t headerHits=0;
    float bodyTop=0,bodyHeight=0,bodyWidth=0,inspectX=0,inspectWidth=0;
    bool inlineInspector=false;
    std::shared_ptr<Gallery> gallery;
    struct Frame {Pin graphic;std::string key;};
    std::vector<Frame> frames;
    Button primary,secondary,undo,redo;
} builder;
void DrawHideoutBuilder();
void BuilderBody();
void AskBuilder();
void ClearBuilderHistoryWait(){builder.restoring=false;builder.restoreActor.Reset();builder.restorePlayerId.clear();builder.restoreUntil=0;builder.restoreProcess=0;}
void CloseHideoutBuilder(){
    ClearBuilderHistoryWait();builder.open=false;builder.opening=false;++builder.epoch;++builder.generation;builder.pending=false;
    hoverPreview.clear();hoverTitle.clear();CloseStandaloneDialog();
}
bool BuilderHistoryOwner(void** actor,std::string* id){
    try{
        if(!game::IsOfflinePlay())return false;const auto session=managed::SessionState();
        if(session.state!=4||session.transitioning)return false;
        void* service=managed::Invoke(game::FindMethod("LE.dll","LE.Services","ServiceProvider","get_SceneService",0));
        if(!service||managed::Value<bool>(managed::Method(service,"IsLoading",0),service)||
           managed::Text(managed::Invoke(managed::Method(service,"get_CurrentScene",0),service))!="MonolithHub")return false;
        *actor=managed::Invoke(game::FindMethod("LE.dll","","PlayerFinder","getPlayerActor",0));
        if(!*actor||!game::IsAlive(*actor))return false;
        void* tracker=managed::Get<void*>(*actor,managed::Offset("Actor","characterDataTracker"));
        void* data=managed::Get<void*>(tracker,managed::Offset("CharacterDataTracker","charData"));
        if(!data||!managed::Value<bool>(game::FindMethod("LE.dll","LE.Data","CharacterData","get_IsOffline",0),data))return false;
        *id=managed::Text(managed::Invoke(managed::Method(data,"get_Id",0),data));return !id->empty();
    }catch(const std::exception&){return false;}
}
bool BuilderHistoryWaitValid(){
    if(!builder.restoring||GetTickCount64()>=builder.restoreUntil||GetCurrentProcessId()!=builder.restoreProcess)return false;
    void* actor=nullptr;std::string id;
    return BuilderHistoryOwner(&actor,&id)&&actor==builder.restoreActor.Get()&&id==builder.restorePlayerId;
}
bool BeginBuilderHistoryWait(){
    void* actor=nullptr;std::string id;if(!BuilderHistoryOwner(&actor,&id))return false;
    builder.restoreActor.Set(actor);builder.restorePlayerId=id;builder.restoreProcess=GetCurrentProcessId();
    builder.restoreUntil=GetTickCount64()+10000;builder.restoring=true;return true;
}
bool BuilderLive(){return builder.open&&StandaloneDialogOpen()&&(hideout::PlayerInside()||BuilderHistoryWaitValid());}
std::string BuilderLower(std::string text){
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return text;
}
bool BuilderMatches(const std::string& name){
    const auto lower=BuilderLower(name);std::istringstream words(BuilderLower(builder.query));std::string word;
    while(words>>word)if(lower.find(word)==std::string::npos)return false;return true;
}
std::string BuilderItemName(const JValue& item){
    const int door=static_cast<int>(item["door"].Num(-100));
    if(door>0)return Fmt("Boss door {0}",{std::to_string(door)});
    if(door==-3)return Tr("Way out");if(door==0)return Tr("Difficulty door");if(door==-1)return Tr("Boss Rush door");
    if(door==-4)return Tr("Stash");if(door==-5)return Tr("Vendor");if(door==-6)return Tr("Eternity Cache");
    return Pretty(item["name"].Str());
}
std::string BuilderModelKey(const std::string& name){
    static std::map<std::string,int> indices;
    if(indices.empty()){
        std::istringstream input(std::string(assets::Bytes("hideout-catalog.tsv")));std::string line;int index=0;
        while(std::getline(input,line)){
            if(line.empty()||line[0]=='#')continue;if(line.back()=='\r')line.pop_back();
            size_t at=0;for(int k=0;k<3&&at!=std::string::npos;++k){at=line.find('\t',at);if(at!=std::string::npos)++at;}
            if(at!=std::string::npos)indices.emplace(line.substr(at),index);++index;
        }
    }
    const auto it=indices.find(name);return it==indices.end()?"":"a:"+std::to_string(it->second);
}
void BuilderInspect(const JValue& row,bool placed){
    builder.inspected=row;builder.inspectedPlaced=placed;
    builder.previewName=placed?BuilderItemName(row):row["n"].Str();
    builder.previewKey=placed?BuilderModelKey(row["name"].Str()):"a:"+std::to_string(static_cast<int>(row["i"].Num()));
    if(!builder.previewKey.empty())hideout::RequestPreview(builder.previewKey,true);
    if(!builder.inlineInspector&&!builder.previewKey.empty())HoverPreview(builder.previewKey,builder.previewName);
    for(auto& frame:builder.frames)if(frame.graphic&&game::IsAlive(frame.graphic.Get()))Tint(frame.graphic.Get(),frame.key==builder.previewKey?C("accent"):C("line"));
}
void BuilderCommand(const std::string& line,bool returnToWorld=false,int selectId=-1){
    if(!BuilderLive()||builder.pending||builder.restoring)return;
    if((line=="hideout undo"||line=="hideout redo")&&!BeginBuilderHistoryWait()){
        Toast(Tr("Wait until your offline character is ready before changing layout history."),true);return;
    }
    builder.pending=true;const auto epoch=builder.epoch;
    const auto complete=[epoch,returnToWorld](const std::string& answer){
        JValue r;if(!json::Parse(answer,r)||!r["ok"].boolean){
            if(builder.epoch==epoch){builder.pending=false;ClearBuilderHistoryWait();Toast(r["error"].Str().empty()?Tr("The action could not be completed."):r["error"].Str(),true);}return;
        }
        if(builder.epoch!=epoch||!BuilderLive())return;builder.pending=false;
        if(r["items"].IsArray())builder.snapshot=r;
        if(returnToWorld){CloseHideoutBuilder();return;}
        if(builder.placed){builder.bodyDirty=true;}
        else AskBuilder();
    };
    if(selectId<0){Send(line,complete);return;}
    // A row action targets its own item/group, never the Ctrl-selection that
    // happened to be active in the world when the builder was opened.
    Send("hideout select "+std::to_string(selectId),[epoch,line,complete](const std::string& answer){
        if(builder.epoch!=epoch||!BuilderLive())return;
        JValue selected;if(!json::Parse(answer,selected)||!selected["ok"].boolean){complete(answer);return;}
        if(selected["items"].IsArray())builder.snapshot=selected;
        Send(line,complete); // one action, only after selection succeeded; never retry
    });
}
int BuilderGroupCount(const JValue& item){
    const int group=static_cast<int>(item["group"].Num());if(!group)return 1;
    return static_cast<int>(std::count_if(builder.snapshot["items"].items.begin(),builder.snapshot["items"].items.end(),[group](const JValue& row){return row["group"].Num()==group&&!row["off"].boolean;}));
}
bool BuilderGroupFlag(const JValue& item,const std::string& flag){
    const int group=static_cast<int>(item["group"].Num());
    for(const auto& row:builder.snapshot["items"].items)if(!row["off"].boolean&&(group?row["group"].Num()==group:row["id"].Num()==item["id"].Num())){
        if(flag=="locked"&&row["locked"].boolean)return true;
        if(flag=="exit"&&row["door"].Num()==-3)return true;
        if(flag=="pending"&&row["pending"].boolean)return true;
    }return false;
}
void BuilderPrimary(){
    if(builder.previewName.empty())return;
    if(builder.inspectedPlaced){
        const auto& item=builder.inspected;const auto id=std::to_string(static_cast<int>(item["id"].Num()));
        if(item["off"].boolean)BuilderCommand("hideout show "+id+" 1");
        else if(item["pending"].boolean||BuilderGroupFlag(item,"pending"))CloseHideoutBuilder();
        else if(!BuilderGroupFlag(item,"locked"))BuilderCommand("hideout hold "+id,true,static_cast<int>(item["id"].Num()));
    }else BuilderCommand("hideout add "+std::to_string(static_cast<int>(builder.inspected["i"].Num())),true);
}
void BuilderSecondary(){
    if(builder.previewName.empty())return;
    if(builder.inspectedPlaced){
        const auto& item=builder.inspected;if(item["off"].boolean||BuilderGroupFlag(item,"locked")||BuilderGroupFlag(item,"exit"))return;
        if(item["pending"].boolean||BuilderGroupFlag(item,"pending")){BuilderCommand("hideout unhold");return;}
        const int id=static_cast<int>(item["id"].Num());
        BuilderCommand("hideout remove "+std::to_string(id),false,id);
    }else BuilderCommand("hideout favorite "+std::to_string(static_cast<int>(builder.inspected["i"].Num()))+(builder.inspected["favorite"].boolean?" 0":" 1"));
}
void AskBuilder(){
    if(!BuilderLive())return;
    const auto epoch=builder.epoch,generation=++builder.generation;
    if(builder.placed){
        Send("hideout read",[epoch,generation](const std::string& answer){
            if(!BuilderLive()||builder.epoch!=epoch||builder.generation!=generation)return;
            JValue r;if(!json::Parse(answer,r)||!r["ok"].boolean){Toast(r["error"].Str(),true);return;}
            builder.snapshot=r;builder.bodyDirty=true;
        });return;
    }
    builder.catalogPending=true;
    const int slices=12/builder.pageSize;const int backendPage=builder.page/slices;
    std::string command="hideout browse "+builder.category+" "+builder.sort+" "+std::to_string(backendPage);
    std::istringstream words(builder.query);std::string word;while(words>>word)command+=" "+word;
    Send(command,[epoch,generation](const std::string& answer){
        if(!BuilderLive()||builder.epoch!=epoch||builder.generation!=generation)return;
        JValue r;if(!json::Parse(answer,r)||!r["ok"].boolean){Toast(r["error"].Str(),true);return;}
        builder.rows=r;builder.catalogPending=false;builder.loadedPage=static_cast<int>(r["page"].Num());
        const int pages=std::max(1,(static_cast<int>(r["total"].Num())+builder.pageSize-1)/builder.pageSize);
        builder.page=std::min(builder.page,pages-1);builder.bodyDirty=true;
    });
}
void BuilderQueryChanged(const std::string& query){
    builder.query=query;builder.page=0;builder.catalogPending=true;const auto epoch=builder.epoch,generation=++builder.generation;
    After(250,[epoch,generation]{if(BuilderLive()&&builder.epoch==epoch&&builder.generation==generation)AskBuilder();});
}
void BuilderPage(int delta,int total){
    const int pages=std::max(1,(total+builder.pageSize-1)/builder.pageSize),next=std::clamp(builder.page+delta,0,pages-1);
    if(next==builder.page)return;builder.page=next;++builder.generation;
    if(builder.placed||(!builder.catalogPending&&next/(12/builder.pageSize)==builder.loadedPage))builder.bodyDirty=true;else AskBuilder();
}
void BuilderDrawInspector(const Ctx& c,float x,float y,float width){
    Fill(c.parent,"Object inspection",x,y,width,builder.bodyHeight-55,Hex("#0b1915"),4);
    Outline(c.parent,"Object inspection frame",x,y,width,builder.bodyHeight-55,C("line"),4);
    Style name{Face::Serif,17,C("ink")};name.wrap=true;
    builder.title.Set(TextBox(c.parent,"Inspected object",x+14,y+12,width-28,52,name,""));Ellipsize(builder.title.Get());
    Fill(c.parent,"Inspection well",x+12,y+70,width-24,(width-24)*2/3,Hex("#06100d"),3);
    builder.image.Set(RawPicture(c.parent,x+12,y+70,width-24,(width-24)*2/3));
    float yy=y+78+(width-24)*2/3;
    Style status{Face::Body,11,C("muted")};status.wrap=true;
    builder.status.Set(TextBox(c.parent,"Model loading state",x+14,yy,width-28,40,status,""));Ellipsize(builder.status.Get());yy+=45;
    builder.primary=AddButton(c,x+14,yy,Tr("Add & place"),Look::Primary,BuilderPrimary,false,"hideout-builder-inspect-primary");yy+=builder.primary.h+8;
    builder.secondary=AddButton(c,x+14,yy,Tr("Favorite"),Look::Small,BuilderSecondary,false,"hideout-builder-inspect-secondary");yy+=builder.secondary.h+12;
    Style detail{Face::Body,11,C("muted")};detail.wrap=true;
    builder.detail.Set(TextBox(c.parent,"Placement details",x+14,yy,width-28,90,detail,""));
}
void BuilderBody(){
    if(!BuilderLive()||!builder.parent||!game::IsAlive(builder.parent.Get()))return;
    // Only the results area is replaced. The focused search input and its caret
    // survive asynchronous replies, pagination and model-thumbnail completion.
    // A mouse release must not resolve an old row's hit index to a new model.
    InvalidateDialogHits(builder.headerHits);
    if(builder.body&&game::IsAlive(builder.body.Get())){void* a[]{GameObjectOf(builder.body.Get())};SetActive(a[0],false);managed::Invoke(U("Object","Destroy",1),nullptr,a);}
    builder.body.Reset();builder.title.Reset();builder.image.Reset();builder.status.Reset();builder.detail.Reset();builder.primary=Button{};builder.secondary=Button{};
    builder.ctx.hits->resize(builder.headerHits);builder.frames.clear();builder.gallery=std::make_shared<Gallery>();
    auto root=New("Hideout builder results",builder.parent.Get(),0,builder.bodyTop,builder.bodyWidth,builder.bodyHeight);builder.body.Set(root.rt);
    float wx=0,wy=0;WindowPoint(builder.ctx,0,builder.bodyTop,&wx,&wy);
    Ctx c{builder.ctx.hits,Area::Dialog,root.rt,wx,wy,kCustomDialogSection};
    const float pad=20,inner=builder.bodyWidth-2*pad,gap=18;
    const float side=builder.inlineInspector?292.f:0;
    const float grid=inner-(builder.inlineInspector?side+gap:0);
    const int columns=builder.pageSize==4?2:3;const float card=(grid-(columns-1)*12)/columns;
    const float picture=card*2/3,cardHeight=picture+80;
    std::vector<JValue> rows;
    if(builder.placed){
        for(const auto& item:builder.snapshot["items"].items)if(BuilderMatches(BuilderItemName(item)+" "+item["name"].Str()))rows.push_back(item);
    }else rows=builder.rows["rows"].items;
    const int total=builder.placed?static_cast<int>(rows.size()):static_cast<int>(builder.rows["total"].Num());
    const int pages=std::max(1,(total+builder.pageSize-1)/builder.pageSize);builder.page=std::clamp(builder.page,0,pages-1);
    const int first=builder.placed?builder.page*builder.pageSize:(builder.page%(12/builder.pageSize))*builder.pageSize;
    const int last=std::min(static_cast<int>(rows.size()),first+builder.pageSize);
    bool preserved=false;
    for(int k=first;k<last;++k){const auto& r=rows[k];if(builder.inspectedPlaced==builder.placed&&(builder.placed?r["id"].Num()==builder.inspected["id"].Num():r["i"].Num()==builder.inspected["i"].Num())){BuilderInspect(r,builder.placed);preserved=true;break;}}
    if(!preserved&&first<last)BuilderInspect(rows[first],builder.placed);
    if(first==last){builder.previewName.clear();builder.previewKey.clear();}
    if(builder.placed){
        for(int k=first;k<last;++k){
            const auto item=rows[k];const std::string id=std::to_string(static_cast<int>(item["id"].Num()));const float top=(k-first)*92.f;
            const int count=BuilderGroupCount(item);const bool locked=BuilderGroupFlag(item,"locked");
            Fill(root.rt,"Placed object row",pad,top,grid,84,Hex("#0c1b16"),3);
            Outline(root.rt,"Placed object frame",pad,top,grid,84,C("line"),3);
            Style name{Face::BodySemi,13,C("ink")};name.wrap=false;
            Ellipsize(TextBox(root.rt,"Placed object name",pad+12,top+8,grid-24,24,name,BuilderItemName(item)));
            const std::string info=(item["off"].boolean?Tr("Removed"):locked?Tr("Locked"):Tr("Placed"))+(count>1?" · "+Tr("Group")+" "+std::to_string(static_cast<int>(item["group"].Num()))+" ("+std::to_string(count)+")":"")+" · X "+EditorNumber(item["right"].Num())+" · Y "+EditorNumber(item["up"].Num());
            auto action=AddButton(c,pad+grid-12,top+40,Tr(locked?(count>1?"Unlock group":"Unlock"):(count>1?"Delete group":"Delete")),Look::Small,[item,id,locked]{BuilderInspect(item,true);if(locked)BuilderCommand("hideout objectlock "+id+" 0");else BuilderSecondary();},true,"hideout-builder-remove-"+id);SetEnabled(c,action,locked||(!item["off"].boolean&&!BuilderGroupFlag(item,"exit")));
            auto move=AddButton(c,pad+grid-action.w-20,top+40,Tr(item["off"].boolean?"Restore":count>1?"Move group":"Move"),Look::Small,[item]{BuilderInspect(item,true);BuilderPrimary();},true,"hideout-builder-move-"+id);SetEnabled(c,move,!locked);
            const float infoWidth=std::max(30.f,grid-action.w-move.w-56);Style note{Face::Body,10,C("muted")};note.align=kLeft;
            Ellipsize(TextBox(root.rt,"Placed object state",pad+12,top+43,infoWidth,28,note,info));
            Hit inspect;inspect.name="hideout-builder-object-"+id;inspect.x=pad;inspect.y=top;inspect.w=grid;inspect.h=35;inspect.enter=[item]{BuilderInspect(item,true);};inspect.action=inspect.enter;c.Add(std::move(inspect));
        }
    }else{
        if(builder.rows["fits"].IsBool()&&!builder.rows["fits"].boolean){NoteBox(root.rt,pad,0,grid,Tr("The asset catalog does not match this game build. Update EpochPact before placing models."));}
        else for(int k=first;k<last;++k){
            const auto row=rows[k];const int local=k-first;const float x=pad+(local%columns)*(card+12),y=(local/columns)*(cardHeight+12);
            const auto index=std::to_string(static_cast<int>(row["i"].Num())),key="a:"+index;
            ThumbCard(c,*builder.gallery,x,y,card,key,row["n"].Str());
            auto& hit=builder.ctx.hits->back();hit.name="hideout-builder-preview-"+index;hit.enter=[row]{BuilderInspect(row,false);};hit.leave={};hit.action=hit.enter;
            Style name{Face::Body,12,C("ink")};name.wrap=true;Ellipsize(TextBox(root.rt,"Model name",x,y+picture+6,card,32,name,Pretty(row["n"].Str())));
            auto add=AddButton(c,x,y+picture+42,Tr("Place"),Look::Small,[row,index]{BuilderInspect(row,false);BuilderCommand("hideout add "+index,true);},false,"hideout-builder-add-"+index);
            auto star=AddButton(c,x+card,y+picture+42,row["favorite"].boolean?"★":"☆",Look::Small,[row,index]{BuilderInspect(row,false);BuilderCommand("hideout favorite "+index+(row["favorite"].boolean?" 0":" 1"));},true,"hideout-builder-favorite-"+index);
            (void)add;(void)star;
        }
    }
    if(!total)EmptyText(root.rt,pad,20,grid,Tr(builder.opening?"Loading your hideout…":builder.placed?"No placed objects match this search.":"No models match these words and filter."));
    if(builder.inlineInspector)BuilderDrawInspector(c,pad+grid+gap,0,side);
    const float footer=builder.bodyHeight-42;
    auto prev=AddButton(c,pad,footer,Tr("Previous"),Look::Small,[total]{BuilderPage(-1,total);},false,"hideout-builder-previous");SetEnabled(c,prev,builder.page>0);
    auto next=AddButton(c,pad+prev.w+8,footer,Tr("Next"),Look::Small,[total]{BuilderPage(1,total);},false,"hideout-builder-next");SetEnabled(c,next,builder.page+1<pages);
    Muted(root.rt,pad+prev.w+next.w+30,footer+5,std::max(80.f,grid-prev.w-next.w-35),Tr("Page")+" "+std::to_string(builder.page+1)+" / "+std::to_string(pages)+" · "+std::to_string(total)+" "+Tr(builder.placed?"objects":"models"),11);
    builder.bodyDirty=false;builder.shownKey.clear();builder.shownTexture=nullptr;builder.shownStatus.clear();builder.shownName.clear();
}
void DrawHideoutBuilder(){
    if(!builder.open)return;
    builder.body.Reset();builder.gallery.reset();builder.frames.clear();builder.parent.Reset();builder.primary=Button{};builder.secondary=Button{};
    ShowStandaloneCustomDialog(1160,[](const Ctx& c,float width){
        builder.ctx=c;builder.parent.Set(c.parent);builder.bodyWidth=width;builder.pageSize=width<720?4:6;builder.inlineInspector=width>=1050;
        constexpr float pad=20;const float inner=width-2*pad;
        Style eyebrow{Face::BodySemi,10,C("accent")};TextBox(c.parent,"Builder eyebrow",pad,14,inner,18,eyebrow,Tr("YOUR HIDEOUT · BUILD & ARRANGE"));
        auto close=AddButton(c,width-pad,24,Tr("Back to editing"),Look::Normal,CloseHideoutBuilder,true,"hideout-builder-close");
        H2(c.parent,pad,35,std::max(80.f,inner-close.w-15),Tr("Hideout Builder"));
        float y=83;
        std::vector<Button> tabs;
        for(const auto& tab:std::vector<std::pair<bool,std::string>>{{false,"Objects"},{true,"Placed objects"}}){
            auto b=AddButton(c,0,0,Tr(tab.second),Look::Small,[placed=tab.first]{builder.placed=placed;builder.page=0;builder.inspected=JValue{};builder.wholeDirty=true;AskBuilder();},false,tab.first?"hideout-builder-placed":"hideout-builder-library");SetSelected(c,b,builder.placed==tab.first);tabs.push_back(std::move(b));}
        auto undo=AddButton(c,0,0,Tr("Undo"),Look::Small,[]{BuilderCommand("hideout undo");},false,"hideout-builder-undo");SetEnabled(c,undo,hideout::CanUndo());builder.undo.hit=undo.hit;builder.undo.group.Set(undo.group.Get());builder.undo.label.Set(undo.label.Get());builder.undo.labelColor=undo.labelColor;tabs.push_back(std::move(undo));
        auto redo=AddButton(c,0,0,Tr("Redo"),Look::Small,[]{BuilderCommand("hideout redo");},false,"hideout-builder-redo");SetEnabled(c,redo,hideout::CanRedo());builder.redo.hit=redo.hit;builder.redo.group.Set(redo.group.Get());builder.redo.label.Set(redo.label.Get());builder.redo.labelColor=redo.labelColor;tabs.push_back(std::move(redo));
        y+=ButtonRows(c,tabs,pad,y,inner)+12;
        if(builder.placed){y+=InputField(c,pad,y,inner,Tr("Find placed objects"),builder.query,Tr("Search your layout…"),false,BuilderQueryChanged,"hideout-builder-search")+12;}
        else{
            const bool stacked=width<800;const float field=stacked?inner:(inner-24)*.53f,filter=stacked?(inner-12)*.62f:(inner-24)*.29f,sort=stacked?inner-filter-12:inner-field-filter-24;
            const float line=InputField(c,pad,y,field,Tr("Find a model"),builder.query,Tr("Search: statue, wall, tree, chest…"),false,BuilderQueryChanged,"hideout-builder-search");
            if(stacked)y+=line+10;
            const float fx=stacked?pad:pad+field+12;
            const std::vector<std::pair<std::string,std::string>> filters{{"all","All models"},{"favorites","Favorites"},{"recent","Recently used"},{"b","Buildings"},{"n","Nature"},{"p","Props"},{"c","Creatures"},{"o","Other"},{"tag:floor","Floors & paths"},{"tag:wall","Walls"},{"tag:column","Columns"},{"tag:statue","Statues"},{"tag:banner","Banners"},{"tag:light","Lights (test effects)"},{"tag:altar","Altars"},{"tag:crystal","Crystals"},{"tag:rock","Rocks"},{"tag:tree","Trees"},{"tag:plant","Plants"},{"tag:furniture","Furniture"}};
            std::vector<std::string> labels;int at=0;for(size_t k=0;k<filters.size();++k){labels.push_back(Tr(filters[k].second));if(filters[k].first==builder.category)at=static_cast<int>(k);}
            float h=SelectField(c,fx,y,filter,Tr("Filter"),labels,at,[filters](int k){builder.category=filters[k].first;builder.page=0;builder.wholeDirty=true;AskBuilder();},"hideout-builder-filter");
            const std::vector<std::string> sorts{"A → Z","Z → A",Tr("Recently used")};const int order=builder.sort=="za"?1:builder.sort=="recent"?2:0;
            h=std::max(h,SelectField(c,fx+filter+12,y,sort,Tr("Sort"),sorts,order,[](int k){builder.sort=k==1?"za":k==2?"recent":"az";builder.page=0;builder.wholeDirty=true;AskBuilder();},"hideout-builder-sort"));y+=std::max(stacked?0.f:line,h)+12;
        }
        y+=Muted(c.parent,pad,y,inner,Tr("Hover or click a picture to inspect. Add & place picks up a green copy; right-click cancels placement."),11)+12;
        builder.bodyTop=y;const float grid=inner-(builder.inlineInspector?310.f:0),card=(grid-(builder.pageSize==4?12:24))/(builder.pageSize==4?2:3);
        builder.bodyHeight=std::max(builder.inlineInspector?560.f:470.f,builder.placed?builder.pageSize*92.f+55:2*(card*2/3+92)+55);builder.headerHits=c.hits->size();builder.bodyDirty=true;
        return y+builder.bodyHeight+12;
    });
    // The host has now converted header hits to window coordinates; body hits
    // use WindowPoint and can subsequently refresh without recreating inputs.
    BuilderBody();builder.wholeDirty=false;
}
void OpenHideoutBuilder(int page=0){
    if(!hideout::PlayerInside()){Toast(Tr("Enter the hideout before adding or arranging objects."),true);return;}
    if(builder.open&&StandaloneDialogOpen()){CloseHideoutBuilder();return;}
    builder.open=true;builder.opening=true;++builder.epoch;builder.pending=false;builder.page=std::max(0,page);builder.loadedPage=-1;hoverPreview.clear();hoverTitle.clear();
    builder.rows=JValue{};builder.snapshot=JValue{};builder.inspected=JValue{};builder.previewKey.clear();builder.previewName.clear();builder.catalogPending=true;
    // Make F6/Escape cancellation available immediately; never leave an unseen
    // opening request that can replace a later window when its read completes.
    DrawHideoutBuilder();const auto epoch=builder.epoch,generation=builder.generation;
    Send("hideout read",[epoch,generation,page](const std::string& answer){
        if(builder.epoch!=epoch||!BuilderLive())return;JValue r;
        if(!json::Parse(answer,r)||!r["ok"].boolean){CloseHideoutBuilder();Toast(r["error"].Str(),true);return;}
        builder.snapshot=r;builder.opening=false;if(builder.generation==generation)builder.page=std::max(0,page);
        builder.bodyDirty=builder.placed;AskBuilder();
    });
}
void HideoutBuilderFrame(){
    if(!builder.open)return;
    if(builder.restoring){
        // A layout-history rebuild can briefly remove the floor. Preserve this
        // dialog only for our own pending undo/redo, in the exact same owner.
        if(!hideout::Editing()||!BuilderHistoryWaitValid()){CloseHideoutBuilder();return;}
        if(builder.pending||!hideout::PlayerInside())return;
        ClearBuilderHistoryWait();builder.bodyDirty=true;
    }
    if(!hideout::PlayerInside()||!hideout::Editing()){
        if(StandaloneDialogOpen())CloseStandaloneDialog();builder.open=false;++builder.epoch;++builder.generation;return;
    }
    if(!StandaloneDialogOpen()){
        builder.open=builder.opening=false;++builder.epoch;++builder.generation;hoverPreview.clear();hoverTitle.clear();return;
    }
    if(builder.wholeDirty){DrawHideoutBuilder();}
    else if(builder.bodyDirty)BuilderBody();
    SetEnabled(builder.ctx,builder.undo,hideout::CanUndo()&&!builder.pending&&!builder.restoring);
    SetEnabled(builder.ctx,builder.redo,hideout::CanRedo()&&!builder.pending&&!builder.restoring);
    if(builder.gallery)for(auto& t:builder.gallery->thumbs){
        if(!DialogSliceVisible(builder.bodyTop+t.y,t.h))continue;
        hideout::RequestPreview(t.key);void* texture=hideout::PreviewTexture(t.key);
        if(texture!=t.shown&&t.image&&game::IsAlive(t.image.Get())){void* a[]{texture};managed::Invoke(UI("RawImage","set_texture",1),t.image.Get(),a);Tint(t.image.Get(),Alpha(kWhite,texture?1.f:0.f));t.shown=texture;}
        const auto status=texture?Tr("Ready"):hideout::PreviewStatus(t.key);if(status!=t.status&&t.note&&game::IsAlive(t.note.Get())){SetText(t.note.Get(),status);t.status=status;}
    }
    if(!builder.inlineInspector||!builder.title||!game::IsAlive(builder.title.Get()))return;
    if(builder.previewName!=builder.shownName){SetText(builder.title.Get(),builder.previewName.empty()?Tr("Choose an object"):Pretty(builder.previewName));builder.shownName=builder.previewName;}
    void* texture=nullptr;if(!builder.previewKey.empty()){PollInspectionPreview(builder.previewKey);texture=hideout::PreviewTexture(builder.previewKey);}
    if(texture!=builder.shownTexture&&builder.image&&game::IsAlive(builder.image.Get())){void* a[]{texture};managed::Invoke(UI("RawImage","set_texture",1),builder.image.Get(),a);Tint(builder.image.Get(),Alpha(kWhite,texture?1.f:0.f));builder.shownTexture=texture;}
    const std::string status=builder.previewName.empty()?Tr("Inspect a model before placing it."):builder.previewKey.empty()?Tr("Native hideout station · use Move to inspect its position"):hideout::PreviewStatus(builder.previewKey);
    if(status!=builder.shownStatus){SetText(builder.status.Get(),status);builder.shownStatus=status;}
    const bool has=!builder.previewName.empty(),placed=builder.inspectedPlaced,locked=placed&&BuilderGroupFlag(builder.inspected,"locked"),off=placed&&builder.inspected["off"].boolean;
    const int groupCount=placed?BuilderGroupCount(builder.inspected):1;
    if(builder.primary.label)SetText(builder.primary.label.Get(),Tr(placed?(off?"Restore object":groupCount>1?"Move group":"Move object"):"Add & place"));
    if(builder.secondary.label)SetText(builder.secondary.label.Get(),Tr(placed?(groupCount>1?"Delete group":"Delete object"):builder.inspected["favorite"].boolean?"Remove favorite":"Favorite"));
    SetEnabled(builder.ctx,builder.primary,has&&!locked&&!builder.pending&&!builder.restoring);
    SetEnabled(builder.ctx,builder.secondary,has&&!builder.pending&&!builder.restoring&&(!placed||(!off&&!locked&&!BuilderGroupFlag(builder.inspected,"exit"))));
    if(builder.detail&&game::IsAlive(builder.detail.Get())){
        const std::string detail=placed?"X "+EditorNumber(builder.inspected["right"].Num())+" · Y "+EditorNumber(builder.inspected["up"].Num())+"\n"+Tr("Rotation")+" "+EditorNumber(builder.inspected["yaw"].Num())+"° · "+Tr("Scale")+" "+EditorNumber(builder.inspected["size"].Num())+"\n"+(groupCount>1?Tr("Group")+" "+std::to_string(static_cast<int>(builder.inspected["group"].Num()))+" · "+std::to_string(groupCount)+" "+Tr("objects")+"\n":"")+(locked?Tr("Unlock this object before moving or deleting it."):Tr("Completed changes can be restored with Undo.")):Tr("Static game model. Place the green preview in the world, then click to confirm. Q/E rotates it.");
        SetText(builder.detail.Get(),detail);
    }
}
void ReleaseHideoutBuilder(){
    const auto epoch=builder.epoch+1,generation=builder.generation+1;builder=HideoutBuilder{};builder.epoch=epoch;builder.generation=generation;
}
#ifdef EPOCHPACT_TESTING
std::string HideoutBuilderAudit(){
    std::ostringstream out;out<<"{\"open\":"<<managed::Boolean(builder.open&&StandaloneDialogOpen())<<",\"placed\":"<<managed::Boolean(builder.placed)<<",\"query\":"<<managed::Json(builder.query)<<",\"category\":"<<managed::Json(builder.category)<<",\"sort\":"<<managed::Json(builder.sort)<<",\"page\":"<<builder.page<<",\"pageSize\":"<<builder.pageSize<<",\"thumbnails\":"<<(builder.gallery?builder.gallery->thumbs.size():0)<<",\"inspected\":"<<managed::Json(builder.previewName)<<'}';return out.str();
}
#endif
