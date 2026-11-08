// Selection and editing operations. All entry points run on the Unity thread.
void DrawSelectionMarkers(){
    const auto destroy=[](){if(selectionMarkers&&game::IsAlive(selectionMarkers->Get())){void* a[]{selectionMarkers->Get()};Invoke(Unity("Object","Destroy",1),nullptr,a);}selectionMarkers.reset();};
    if(!editing||selection.empty()||!holderRoot){destroy();return;}
    if(selectionMarkers&&game::IsAlive(selectionMarkers->Get())&&markerRevision==historyRevision&&(!markerRetry||GetTickCount64()<markerRetry))return;
    destroy();markerRevision=historyRevision;markerRetry=0;
    if(!selectionMarkerMaterial||!game::IsAlive(selectionMarkerMaterial->Get())){
        void* a[]{il2cpp::api().string_new("Sprites/Default")};void* shader=Invoke(Signature("Shader","Find",{"System.String"}),nullptr,a);
        if(!shader)return;selectionMarkerMaterial=std::make_unique<Root>(il2cpp::api().object_new(game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","Material")));
        void* b[]{shader};Invoke(Signature("Material",".ctor",{"UnityEngine.Shader"}),selectionMarkerMaterial->Get(),b);
    }
    selectionMarkers=std::make_unique<Root>(NewObject("EP selected objects"));Root parent(Invoke(Unity("GameObject","get_transform",0),selectionMarkers->Get()));
    void* owner[]{Invoke(Unity("GameObject","get_transform",0),holderRoot->Get())};Invoke(Signature("Transform","SetParent",{"UnityEngine.Transform"}),parent.Get(),owner);
    for(int id:selection){auto* i=Find(id);if(!i)continue;const auto bounds=Measure(ItemVisual(*i));if(!bounds.valid){if(i->waiting||i->settle>0)markerRetry=GetTickCount64()+250;continue;}
        Root go(NewObject("EP selection outline"));Root tr(Invoke(Unity("GameObject","get_transform",0),go.Get()));void* p[]{parent.Get()};Invoke(Signature("Transform","SetParent",{"UnityEngine.Transform"}),tr.Get(),p);
        Root type(TypeObject("UnityEngine.CoreModule.dll","UnityEngine","LineRenderer"));void* a[]{type.Get()};void* line=Invoke(Signature("GameObject","AddComponent",{"System.Type"}),go.Get(),a);
        const auto set=[&](const char* name,void* value){void* args[]{value};Invoke(Unity("LineRenderer",name,1),line,args);};
        bool yes=true;float width=.05f;int count=4;set("set_useWorldSpace",&yes);set("set_loop",&yes);set("set_positionCount",&count);set("set_startWidth",&width);set("set_endWidth",&width);
        ingame::ui::Color color=i->locked?ingame::ui::Color{.6f,.75f,1,.9f}:ingame::ui::Color{.25f,1,.5f,.9f};set("set_startColor",&color);set("set_endColor",&color);
        void* material[]{selectionMarkerMaterial->Get()};Invoke(Unity("Renderer","set_sharedMaterial",1),line,material);
        const auto corners=rules::Rectangle(bounds.c.x,bounds.c.z,bounds.e.x+.15f,bounds.e.z+.15f);
        for(int k=0;k<4;++k){Vec3 at{corners[k][0],monolith::HallGroundY()+.08f,corners[k][1]};void* point[]{&k,&at};Invoke(Unity("LineRenderer","SetPosition",2),line,point);}
    }
}
void LoadEditorOptions(){
    auto path=FilePath();if(path.empty())return;path+=L".editor";std::ifstream in(path);
    float grid=0,angle=15;int floors=0;
    if(in>>grid>>angle>>floors)if(std::isfinite(grid)&&grid>=0&&grid<=10&&std::isfinite(angle)&&angle>=1&&angle<=90){gridStep=grid;angleStep=angle;selectFloors=floors!=0;}
}
void SaveEditorOptions(){
    auto path=FilePath();if(path.empty())throw std::runtime_error("No preferences folder is available.");path+=L".editor";std::ostringstream out;out<<gridStep<<' '<<angleStep<<' '<<selectFloors<<'\n';const auto text=out.str();
    if(!WriteFile(path,&text))throw std::runtime_error("Could not save editor preferences.");++historyRevision;
}
void PickSelection(int id,bool toggle){
    auto* item=Find(id);if(!item||item->off)throw std::runtime_error("This object is not visible.");
    auto next=toggle?selection:std::set<int>{};
    if(toggle){if(next.count(id))next.erase(id);else next.insert(id);}
    else if(item->group){for(const auto& i:items)if(i.group==item->group&&!i.off)next.insert(i.id);}
    else next.insert(id);
    if(next.size()>32)throw std::runtime_error("Select at most 32 objects at a time.");
    if(!toggle||selection.empty())selectionPivot=id;
    selection=std::move(next);if(!selection.count(selectionPivot))selectionPivot=selection.empty()?-1:*selection.begin();++historyRevision;
}
std::vector<int> SelectedIds(){std::vector<int> ids;for(int id:selection)if(Find(id))ids.push_back(id);return ids;}
void RequireSelection(bool unlocked=true){
    if(selection.empty())throw std::runtime_error("Select an object first.");
    for(int id:selection){auto* i=Find(id);if(!i)throw std::runtime_error("Select the object again.");if(unlocked&&i->locked)throw std::runtime_error("Unlock the selected objects first.");}
}
void RequireCommittedSelection(){for(int id:selection)if(Uncommitted(id))throw std::runtime_error("Place new objects before grouping, aligning or locking them.");}
void DeleteSelection(){
    RequireSelection();const auto ids=SelectedIds();
    for(int id:ids)if(Find(id)->door==monolith::kHallOut)throw std::runtime_error("The way out stays. Deselect it before deleting.");
    EndPlacement(false);for(int id:ids)RemoveItem(id,false);selection.clear();Save();++historyRevision;
}
Item CopyItem(const Item& i){
    Item n;n.kind=i.kind;n.door=i.door;n.name=i.name;n.guid=i.guid;n.right=i.right;n.up=i.up;n.lift=i.lift;n.yaw=i.yaw;n.size=i.size;
    n.maxSpan=i.maxSpan;n.centered=i.centered;n.walkable=i.walkable;n.fit=false;return n;
}
void DuplicateSelection(){
    if(!PlayerInside())throw std::runtime_error("Enter the hideout before duplicating objects.");
    RequireSelection(false);const auto ids=SelectedIds();if(items.size()+ids.size()>rules::kMaxItems)throw std::runtime_error("The hideout has reached its 400 object limit.");
    std::vector<Item> copies;
    for(int id:ids){const auto* i=Find(id);if(i->door!=kNoDoor)throw std::runtime_error("Only decoration can be duplicated; services and gates stay unique.");
        if(Uncommitted(id))throw std::runtime_error("Place the new object before duplicating it.");
        auto n=CopyItem(*i);if(placement.id==id)SetItemPose(n,{placement.right,placement.up,placement.lift,placement.yaw,placement.size});
        for(const auto& p:placement.members)if(p.id==id)SetItemPose(n,{p.right,p.up,p.lift,p.yaw,p.size});copies.push_back(std::move(n));}
    if(!editing)SetEditing(true);EndPlacement(false);selection.clear();const int group=copies.size()>1?nextGroupId++:0;int first=-1;
    for(auto& n:copies){n.id=nextId++;n.group=group;const int id=n.id;items.push_back(std::move(n));StartLoad(items.back());selection.insert(id);uncommittedIds.insert(id);if(first<0)first=id;}
    BeginPlacement(first,true);UpdatePlacement(placement.right,placement.up,true);
}
void ApplyPoses(const std::map<int,rules::Pose>& poses){
    for(const auto& [id,p]:poses){auto* i=Find(id);if(!i||i->locked)throw std::runtime_error("Unlock the selected objects first.");if(!rules::FinitePlacement(p.x,p.z,p.lift,p.yaw,p.size)||!rules::Inside(groundShape,groundSize,p.x,p.z))throw std::runtime_error("Place the selection inside the hideout.");}
    CheckPoses(poses);EndPlacement(false);
    for(const auto& [id,p]:poses)if(auto* i=Find(id)){SetItemPose(*i,p);PlaceCarrier(*i);}
    Save();navAt=GetTickCount64()+400;doorsAt=GetTickCount64()+50;
}
void TransformSelection(int anchor,const rules::Pose& after){
    if(!PlayerInside())throw std::runtime_error("Enter the hideout before transforming objects.");
    auto* i=Find(anchor);if(!i)throw std::runtime_error("Select the object again.");
    if(!selection.count(anchor))PickSelection(anchor);RequireSelection();
    if(!rules::FinitePlacement(after.x,after.z,after.lift,after.yaw,after.size))throw std::runtime_error("Use finite coordinates, height -40 to 40 and scale 0.001 to 100.");
    if(!editing)SetEditing(true);
    if(held==anchor){placement.lift=after.lift;placement.yaw=after.yaw;placement.size=after.size;UpdatePlacement(after.x,after.z,true);return;}
    const auto before=ItemPose(*i);std::map<int,rules::Pose> poses;
    for(int id:selection)poses[id]=rules::Follow(ItemPose(*Find(id)),before,after);
    ApplyPoses(poses);
}
void AlignSelection(const std::string& axis){
    if(!PlayerInside())throw std::runtime_error("Enter the hideout before aligning objects.");
    RequireSelection();RequireCommittedSelection();if(selection.size()<2)throw std::runtime_error("Select at least two objects to align.");
    if(!editing)SetEditing(true);EndPlacement(false);const int anchor=SelectionAnchor();auto* pivot=Find(anchor);const auto target=Measure(ItemVisual(*pivot));if(!target.valid)throw std::runtime_error("Wait for the selected models to load.");
    const auto rect=[](const VisualBounds& b){std::array<float,4> out{1e9f,-1e9f,1e9f,-1e9f};for(int x:{-1,1})for(int z:{-1,1}){float r=0,u=0;monolith::HallLocal(b.c.x+x*b.e.x,b.c.z+z*b.e.z,&r,&u);out[0]=std::min(out[0],r);out[1]=std::max(out[1],r);out[2]=std::min(out[2],u);out[3]=std::max(out[3],u);}return out;};
    const auto edge=rect(target);std::map<int,rules::Pose> poses;
    for(int id:selection){auto* item=Find(id);const auto b=Measure(ItemVisual(*item));if(!b.valid)throw std::runtime_error("Wait for the selected models to load.");
        float x=0,z=0;const auto itemEdge=rect(b);
        if(axis=="left")x=edge[0]-itemEdge[0];
        else if(axis=="right")x=edge[1]-itemEdge[1];
        else if(axis=="x")x=(edge[0]+edge[1]-itemEdge[0]-itemEdge[1])*.5f;
        else if(axis=="top")z=edge[3]-itemEdge[3];
        else if(axis=="bottom")z=edge[2]-itemEdge[2];
        else if(axis=="z")z=(edge[2]+edge[3]-itemEdge[2]-itemEdge[3])*.5f;
        else throw std::runtime_error("Unknown alignment.");
        auto p=ItemPose(*item);p.x+=x;p.z+=z;poses[id]=p;}
    ApplyPoses(poses);
}
std::optional<std::string> ToolsCommand(const std::vector<std::string>& args){
    const auto& what=args[0];
    if(what=="select"){
        EndPlacement(false);PickSelection(std::stoi(args.at(1)),args.size()>2&&args[2]=="toggle");return State();
    }
    if(what=="options"){
        const auto key=args.at(1);const float value=std::stof(args.at(2));if(!std::isfinite(value))throw std::runtime_error("Use a finite value.");
        if(key=="grid"&&value>=0&&value<=10)gridStep=value;
        else if(key=="angle"&&value>=1&&value<=90)angleStep=value;
        else if(key=="floors"&&(value==0||value==1))selectFloors=value!=0;
        else throw std::runtime_error("Unknown editor option or value.");SaveEditorOptions();return State();
    }
    if(what=="transform"){
        TransformSelection(std::stoi(args.at(1)),{std::stof(args.at(2)),std::stof(args.at(3)),std::stof(args.at(4)),std::stof(args.at(5)),std::stof(args.at(6))});return State();
    }
    if(what=="objectlock"){
        const int id=std::stoi(args.at(1));if(Uncommitted(id))throw std::runtime_error("Place the new object before locking it.");EndPlacement(false);PickSelection(id);const bool on=args.at(2)=="1";
        for(int k:selection)Find(k)->locked=on;Save();++historyRevision;return State();
    }
    if(what=="remove"){
        const int id=std::stoi(args.at(1));if(!selection.count(id))PickSelection(id);DeleteSelection();return State();
    }
    if(what!="selection")return std::nullopt;
    const auto action=args.at(1);
    if(action=="clear"){EndPlacement(false);selection.clear();++historyRevision;return State();}
    if(action=="duplicate"){DuplicateSelection();return State();}
    RequireSelection(action!="lock");
    if(action=="move"){if(held>=0)return State();BeginPlacement(*selection.begin());UpdatePlacement(placement.right,placement.up,true);}
    else if(action=="delete")DeleteSelection();
    else if(action=="lock"){
        RequireCommittedSelection();
        const auto ids=SelectedIds();EndPlacement(false);const bool on=args.at(2)=="1";for(int id:ids)if(auto* i=Find(id))i->locked=on;Save();++historyRevision;
    }else if(action=="group"||action=="ungroup"){
        RequireCommittedSelection();
        if(action=="group"&&selection.size()<2)throw std::runtime_error("Select at least two objects to group.");
        EndPlacement(false);const int group=action=="group"?nextGroupId++:0;for(int id:selection)Find(id)->group=group;Save();++historyRevision;
    }else if(action=="align")AlignSelection(args.at(2));
    else throw std::runtime_error("Unknown selection action.");return State();
}
