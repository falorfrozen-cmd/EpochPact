// A placement is a transaction. Selected originals stay still until commit.
struct Placement {
    int id=-1;bool added=false,valid=false;float right=0,up=0,yaw=0,size=1,lift=0;
    float sourceYaw=0,sourceSize=1;Vec3 sourceScale{1,1,1};Quat sourceRotation{};
    rules::Pose initial,rejectedPose;bool rejected=false;std::string rejection;VisualBounds bounds;bool tinted=false,lastValid=false;
    std::unique_ptr<Root> ghost;std::vector<std::pair<std::unique_ptr<Root>,bool>> materials;std::string shader;
    std::vector<Placement> members;
} placement;
rules::Pose ItemPose(const Item& i){return {i.right,i.up,i.lift,i.yaw,i.size};}
void SetItemPose(Item& i,const rules::Pose& p){i.right=p.x;i.up=p.z;i.lift=p.lift;i.yaw=p.yaw;i.size=p.size;}
void DisposeGhost(Placement& p){
    if(p.ghost&&game::IsAlive(p.ghost->Get())){void* a[]{p.ghost->Get()};Invoke(Signature("Object","DestroyImmediate",{"UnityEngine.Object"}),nullptr,a);}p.ghost.reset();
    for(auto& [material,tint]:p.materials)if(material&&game::IsAlive(material->Get())){void* a[]{material->Get()};Invoke(Signature("Object","DestroyImmediate",{"UnityEngine.Object"}),nullptr,a);}p.materials.clear();
}
void DestroyPlacementVisual(){for(auto& p:placement.members)DisposeGhost(p);DisposeGhost(placement);}
void* ItemVisual(const Item& i){return Alive(i)?i.carrier->Get():i.kind==kDoor?monolith::HallDoorVisual(i.door):nullptr;}
void UpdatePlacement(float right,float up,bool valid);
#include "hideout_safety.inl"
bool PlacementReady(){
    if(placement.id<0||!placement.valid||!placement.ghost)return false;
    for(const auto& p:placement.members)if(!p.valid||!p.ghost)return false;
    return true;
}
void EndPlacement(bool commit){
    if(placement.id<0){held=-1;return;}
    const bool apply=commit&&PlacementReady();std::vector<Placement*> all{&placement};for(auto& p:placement.members)all.push_back(&p);
    if(apply){std::map<int,rules::Pose> poses;for(auto* p:all)poses[p->id]={p->right,p->up,p->lift,p->yaw,p->size};
        try{CheckPoses(poses);}catch(const std::exception& e){placementIssue=e.what();placement.rejected=true;placement.rejection=placementIssue;placement.rejectedPose={placement.right,placement.up,placement.lift,placement.yaw,placement.size};UpdatePlacement(placement.right,placement.up,true);throw;}}
    for(auto* p:all){
        Item* i=Find(p->id);if(!i)continue;
        if(apply){
            SetItemPose(*i,{p->right,p->up,p->lift,p->yaw,p->size});
            if(Alive(*i)){bool yes=true;void* a[]{&yes};Invoke(Unity("GameObject","SetActive",1),i->carrier->Get(),a);}PlaceCarrier(*i);
        }else if(p->added){
            if(Alive(*i)){void* a[]{i->carrier->Get()};Invoke(Signature("Object","DestroyImmediate",{"UnityEngine.Object"}),nullptr,a);}
            const int id=i->id;items.erase(std::remove_if(items.begin(),items.end(),[id](const Item& x){return x.id==id;}),items.end());selection.erase(id);
        }
        uncommittedIds.erase(p->id);
    }
    DestroyPlacementVisual();placement=Placement{};held=-1;uncommittedNew=-1;placementIssue.clear();
    if(apply){Save();navAt=GetTickCount64()+400;doorsAt=GetTickCount64()+50;}
    ++historyRevision;
}
void BeginPlacement(int id,bool added){
    if(!editing)SetEditing(true);
    EndPlacement(false);Item* i=Find(id);if(!i)return;
    if(!selection.count(id))PickSelection(id);
    if(selection.empty())selection.insert(id);
    if(selection.size()>32)throw std::runtime_error("Select at most 32 objects at a time.");
    for(int k:selection)if(auto* item=Find(k);item&&item->locked)throw std::runtime_error("Unlock the selected objects before moving them.");
    const auto prepare=[&](Placement& p,const Item& item){
        p.id=item.id;p.added=Uncommitted(item.id)||(added&&item.id==id);p.initial=ItemPose(item);p.right=item.right;p.up=item.up;
        p.yaw=item.yaw;p.size=item.size;p.lift=item.lift;p.sourceYaw=item.yaw;p.sourceSize=item.size;
        if(p.added)uncommittedIds.insert(item.id);
    };
    held=selectionPivot=id;prepare(placement,*i);uncommittedNew=placement.added?id:-1;
    for(int k:selection)if(k!=id)if(auto* item=Find(k);item&&!item->off){placement.members.emplace_back();prepare(placement.members.back(),*item);}
    ++historyRevision;
}
void MakePlacementVisual(const Item& item,Placement& p) {
    if(p.ghost || item.waiting || item.settle)return;
    void* source=ItemVisual(item);if(!source)return;
    if(p.added&&p.initial.size!=item.size){const float ratio=item.size/std::max(.001f,p.initial.size);p.size*=ratio;p.initial.size=item.size;p.sourceSize=item.size;}
    p.bounds=Measure(source);if(!p.bounds.valid){p.valid=false;placementIssue="This asset has no static model to place.";return;}
    Root original(Invoke(Unity("GameObject","get_transform",0),source));
    p.sourceRotation=Value<Quat>(Unity("Transform","get_rotation",0),original.Get());
    p.sourceScale=Value<Vec3>(Unity("Transform","get_lossyScale",0),original.Get());
    Root staging(NewObject("EP inactive placement staging"));bool no=false,yes=true;void* off[]{&no},*on[]{&yes};
    Invoke(Unity("GameObject","SetActive",1),staging.Get(),off);
    Root st(Invoke(Unity("GameObject","get_transform",0),staging.Get()));void* a[]{source,st.Get(),&no};
    Root clone(Invoke(Signature("Object","Instantiate",{"UnityEngine.Object","UnityEngine.Transform","System.Boolean"}),nullptr,a));
    Sanitize(clone.Get(),false);
    void* shader=nullptr;
    for(const char* name:{"Sprites/Default","UI/Default","Hidden/Internal-Colored"}){
        void* fa[]{il2cpp::api().string_new(name)};shader=Invoke(Signature("Shader","Find",{"System.String"}),nullptr,fa);
        if(shader&&Value<bool>(Unity("Shader","get_isSupported",0),shader)){p.shader=name;break;}shader=nullptr;
    }
    if(!shader)throw std::runtime_error("No supported shader for the placement preview.");
    bool all=true;Root type(TypeObject("UnityEngine.CoreModule.dll","UnityEngine","Renderer"));void* ra[]{type.Get(),&all};
    Root renderers(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),clone.Get(),ra));
    for(uintptr_t k=0,n=renderers.Get()?Get<uintptr_t>(renderers.Get(),0x18):0;k<n;++k){void* r=Get<void*>(renderers.Get(),0x20+k*8);
        Root mats(Invoke(Unity("Renderer","get_sharedMaterials",0),r));
        const auto kind=std::string(il2cpp::api().class_get_name(il2cpp::api().object_get_class(r)));
        if(kind!="MeshRenderer"&&kind!="SkinnedMeshRenderer"){void* a[]{&no};Invoke(Unity("Renderer","set_enabled",1),r,a);continue;}
        for(int m=0,count=mats.Get()?static_cast<int>(Get<uintptr_t>(mats.Get(),0x18)):0;m<count;++m){void* original=Get<void*>(mats.Get(),0x20+8*m);
            // Glow/outline alpha planes must not become giant solid green quads.
            const bool effect=original&&Value<int>(Unity("Material","get_renderQueue",0),original)>2500;
            auto material=std::make_unique<Root>(il2cpp::api().object_new(game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","Material")));void* ma[]{shader};Invoke(Signature("Material",".ctor",{"UnityEngine.Shader"}),material->Get(),ma);
            if(original){void* texture=Invoke(Unity("Material","get_mainTexture",0),original);if(texture){void* a[]{texture};Invoke(Unity("Material","set_mainTexture",1),material->Get(),a);}}
            int queue=3000;void* qa[]{&queue};Invoke(Unity("Material","set_renderQueue",1),material->Get(),qa);
            if(effect){ingame::ui::Color clear{0,0,0,0};void* a[]{&clear};Invoke(Unity("Material","set_color",1),material->Get(),a);}
            void* a[]{material->Get(),&m};Invoke(Signature("Array","SetValue",{"System.Object","System.Int32"},"System","mscorlib.dll"),mats.Get(),a);
            p.materials.emplace_back(std::move(material),!effect);
        }
        void* a[]{mats.Get()};Invoke(Unity("Renderer","set_sharedMaterials",1),r,a);int shadows=0;void* b[]{&shadows};Invoke(Unity("Renderer","set_shadowCastingMode",1),r,b);void* c[]{&no};Invoke(Unity("Renderer","set_receiveShadows",1),r,c);
    }
    Root tr(Invoke(Unity("GameObject","get_transform",0),clone.Get()));void* parent[]{holderRoot?Invoke(Unity("GameObject","get_transform",0),holderRoot->Get()):nullptr};
    Invoke(Signature("Transform","SetParent",{"UnityEngine.Transform"}),tr.Get(),parent);
    Invoke(Unity("GameObject","SetActive",1),clone.Get(),on);p.ghost=std::make_unique<Root>(clone.Get());
    void* dispose[]{staging.Get()};Invoke(Signature("Object","DestroyImmediate",{"UnityEngine.Object"}),nullptr,dispose);
    if(p.added && Alive(item))Invoke(Unity("GameObject","SetActive",1),item.carrier->Get(),off);
}
void UpdateGhost(Placement& p,const rules::Pose& pose,bool valid){
    Item* item=Find(p.id);if(!item)return;
    p.right=pose.x;p.up=pose.z;p.lift=pose.lift;p.yaw=pose.yaw;p.size=pose.size;p.valid=valid;
    MakePlacementVisual(*item,p);if(!p.ghost)return;
    Root tr(Invoke(Unity("GameObject","get_transform",0),p.ghost->Get()));Vec3 at{};monolith::HallWorld(p.right,p.up,&at.x,&at.z);at.y=monolith::HallGroundY()+p.lift+.035f;
    void* a[]{&at};Invoke(Unity("Transform","set_position",1),tr.Get(),a);
    const auto b=p.sourceRotation;auto q=rules::YawOnBase(p.yaw-p.sourceYaw,{b.x,b.y,b.z,b.w});void* r[]{&q};Invoke(Unity("Transform","set_rotation",1),tr.Get(),r);
    const float ratio=p.size/std::max(.001f,p.sourceSize);Vec3 scale{p.sourceScale.x*ratio,p.sourceScale.y*ratio,p.sourceScale.z*ratio};void* s[]{&scale};Invoke(Unity("Transform","set_localScale",1),tr.Get(),s);
    if(!p.tinted||p.lastValid!=valid){
        ingame::ui::Color color=valid?ingame::ui::Color{.12f,1,.35f,.48f}:ingame::ui::Color{1,.12f,.1f,.48f};void* ca[]{&color};
        for(auto& [material,tint]:p.materials)if(tint)Invoke(Unity("Material","set_color",1),material->Get(),ca);
        p.tinted=true;p.lastValid=valid;
    }
}
void UpdatePlacement(float right,float up,bool valid){
    if(!Find(placement.id))return;
    const rules::Pose pose{right,up,placement.lift,placement.yaw,placement.size};placementIssue.clear();
    std::vector<rules::Pose> poses;for(const auto& p:placement.members)poses.push_back(rules::Follow(p.initial,placement.initial,pose));
    if(auto* item=Find(placement.id))MakePlacementVisual(*item,placement);
    for(auto& p:placement.members)if(auto* item=Find(p.id))MakePlacementVisual(*item,p);
    valid=valid&&rules::FinitePlacement(pose.x,pose.z,pose.lift,pose.yaw,pose.size)&&FootprintInside(placement.bounds,placement.initial,pose,groundShape,groundSize);
    for(size_t k=0;k<poses.size();++k){const auto& p=poses[k];valid=valid&&rules::FinitePlacement(p.x,p.z,p.lift,p.yaw,p.size)&&FootprintInside(placement.members[k].bounds,placement.members[k].initial,p,groundShape,groundSize);}
    if(placement.rejected){const auto& r=placement.rejectedPose;if(std::abs(pose.x-r.x)+std::abs(pose.z-r.z)+std::abs(pose.lift-r.lift)+std::abs(pose.yaw-r.yaw)+std::abs(pose.size-r.size)<.001f){valid=false;placementIssue=placement.rejection;}else placement.rejected=false;}
    if(!valid&&placementIssue.empty())placementIssue="Keep the whole selection inside the hideout edge; wait for models to finish loading.";
    UpdateGhost(placement,pose,valid);for(size_t k=0;k<poses.size();++k)UpdateGhost(placement.members[k],poses[k],valid);
}
bool CursorGround(Vec3& origin,Vec3& direction,float& right,float& up) {
    POINT p{};RECT box{};HWND window=GetForegroundWindow();
    if(!GetCursorPos(&p)||!ScreenToClient(window,&p)||!GetClientRect(window,&box)||box.right<=0||box.bottom<=0)return false;
    void* camera=Invoke(Unity("Camera","get_main",0));if(!camera)return false;
    const int w=Value<int>(Unity("Screen","get_width",0)),h=Value<int>(Unity("Screen","get_height",0));
    Vec3 screen{p.x*float(w)/box.right,(box.bottom-p.y)*float(h)/box.bottom,0};void* a[]{&screen};struct Ray{Vec3 origin,direction;};
    const auto ray=Value<Ray>(Signature("Camera","ScreenPointToRay",{"UnityEngine.Vector3"}),camera,a);origin=ray.origin;direction=ray.direction;
    float x=0,z=0;if(!rules::GroundRay({origin.x,origin.y,origin.z},{direction.x,direction.y,direction.z},monolith::HallGroundY(),x,z))return false;
    monolith::HallLocal(x,z,&right,&up);return rules::Inside(groundShape,groundSize,right,up);
}
Item* PickAtRay(Vec3 origin,Vec3 direction) {
    Item* picked=nullptr;float nearest=1e9f;
    for(auto& i:items){if(i.off||i.locked||(!selectFloors&&rules::CatalogTag(rules::CatalogLower(i.name),"floor")))continue;void* visual=ItemVisual(i);if(!visual)continue;auto b=Measure(visual);if(!b.valid)continue;
        float t=rules::RayBounds({origin.x,origin.y,origin.z},{direction.x,direction.y,direction.z},{b.c.x,b.c.y,b.c.z},{b.e.x,b.e.y,b.e.z});
        if(t>=0&&t<nearest){nearest=t;picked=&i;}}
    return picked;
}
