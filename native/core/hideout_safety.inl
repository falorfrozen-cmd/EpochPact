// Bounds are sampled from the original once. Route checks run only on a drop,
// never in the mouse-follow frame and never through a scene-wide object scan.
bool FootprintInside(const VisualBounds& bounds,const rules::Pose& before,const rules::Pose& after,int shape,float size){
    if(!bounds.valid)return false;std::array<rules::Pose,4> corners;int k=0;
    for(const auto& point:rules::Rectangle(bounds.c.x,bounds.c.z,bounds.e.x,bounds.e.z)){
        float x=0,z=0;monolith::HallLocal(point[0],point[1],&x,&z);corners[k++]=rules::Follow({x,z,0,0,1},before,after);
    }
    for(int n=0;n<4;++n){const auto& a=corners[n];const auto& b=corners[(n+1)%4];
        const float length=std::hypot(b.x-a.x,b.z-a.z);if(length>4*rules::MaxExtent(shape,size))return false;
        const int count=std::max(1,static_cast<int>(std::ceil(length/.25f)));
        for(int j=0;j<=count;++j){const float t=j/float(count);if(rules::Distance(shape,size,a.x+(b.x-a.x)*t,a.z+(b.z-a.z)*t)<.1f)return false;}
    }return true;
}
void SyncHideoutPhysics(){Invoke(game::FindMethod("UnityEngine.PhysicsModule.dll","UnityEngine","Physics","SyncTransforms",0));}
struct RouteState {bool source=false;std::set<int> reachable;};
RouteState ReachableStations(){
    struct NavHit {Vec3 position,normal;float distance;int mask,hit;};
    const auto sample=Signature("NavMesh","SamplePosition",{"UnityEngine.Vector3","UnityEngine.AI.NavMeshHit&","System.Single","System.Int32"},"UnityEngine.AI","UnityEngine.AIModule.dll");
    const auto calculate=Signature("NavMesh","CalculatePath",{"UnityEngine.Vector3","UnityEngine.Vector3","System.Int32","UnityEngine.AI.NavMeshPath"},"UnityEngine.AI","UnityEngine.AIModule.dll");
    Root path(il2cpp::api().object_new(game::FindClass("UnityEngine.AIModule.dll","UnityEngine.AI","NavMeshPath")));Invoke(Signature("NavMeshPath",".ctor",{},"UnityEngine.AI","UnityEngine.AIModule.dll"),path.Get());
    const auto find=[&](Vec3 p,float range,Vec3& result){NavHit h{};int mask=-1;void* a[]{&p,&h,&range,&mask};if(!Value<bool>(sample,nullptr,a))return false;result=h.position;return true;};
    RouteState result;Vec3 start{};result.source=find(PlayerPlace(),1,start);if(!result.source)return result;
    for(const auto& item:items)if(!item.off&&item.door!=kNoDoor){
        for(int k=0;k<9;++k){const float angle=k*6.2831853f/8;Vec3 p=World(item);p.y=monolith::HallGroundY();if(k<8){p.x+=2*std::cos(angle);p.z+=2*std::sin(angle);}Vec3 target{};if(!find(p,1.25f,target))continue;
            int mask=-1;void* a[]{&start,&target,&mask,path.Get()};if(Value<bool>(calculate,nullptr,a)&&Value<int>(Method(path.Get(),"get_status",0),path.Get())==0){result.reachable.insert(item.id);break;}
        }
    }return result;
}
void CheckPoses(const std::map<int,rules::Pose>& poses){
    if(!PlayerInside())throw std::runtime_error("Enter the hideout before placing objects.");
    std::map<int,rules::Pose> before;std::map<int,bool> active;bool doors=false;
    for(const auto& [id,pose]:poses){auto* item=Find(id);if(!item||item->locked)throw std::runtime_error("Unlock the selected objects first.");
        VisualBounds bounds;bool cached=false;
        if(placement.id==id){bounds=placement.bounds;cached=true;}for(const auto& p:placement.members)if(p.id==id){bounds=p.bounds;cached=true;}
        if(!cached)bounds=Measure(ItemVisual(*item));
        if(!rules::FinitePlacement(pose.x,pose.z,pose.lift,pose.yaw,pose.size)||!FootprintInside(bounds,ItemPose(*item),pose,groundShape,groundSize))throw std::runtime_error("The whole model must fit inside the hideout edge. Move it inward or reduce its scale.");
        before[id]=ItemPose(*item);if(Alive(*item))active[id]=Value<bool>(Unity("GameObject","get_activeSelf",0),item->carrier->Get());doors|=item->kind==kDoor;
    }
    if(navAt||doorsAt){if(doorsAt)monolith::HallRebuildDoors();SyncHideoutPhysics();monolith::HallRebuildNavMesh();navAt=doorsAt=0;}
    const auto baseline=ReachableStations();
    const auto restore=[&](){for(const auto& [id,pose]:before)if(auto* i=Find(id)){SetItemPose(*i,pose);PlaceCarrier(*i);if(Alive(*i)&&active.count(id)){bool on=active[id];void* a[]{&on};Invoke(Unity("GameObject","SetActive",1),i->carrier->Get(),a);}}if(doors)monolith::HallRebuildDoors();SyncHideoutPhysics();monolith::HallRebuildNavMesh();};
    try{
        for(const auto& [id,pose]:poses){auto* i=Find(id);SetItemPose(*i,pose);PlaceCarrier(*i);if(Alive(*i)){bool on=true;void* a[]{&on};Invoke(Unity("GameObject","SetActive",1),i->carrier->Get(),a);}}
        if(doors)monolith::HallRebuildDoors();SyncHideoutPhysics();monolith::HallRebuildNavMesh();const auto after=ReachableStations();
        if(baseline.source&&!after.source)throw std::runtime_error("This position would trap the player. Leave a clear walking path.");
        for(int id:baseline.reachable)if(!after.reachable.count(id))throw std::runtime_error("This position blocks access to a service or gate. Leave a wider walking path.");
    }catch(...){restore();throw;}
    restore();
}
