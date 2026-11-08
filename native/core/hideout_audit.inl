// Private, read-only checks. Queries the game's actual baked NavMesh and counts
// any residual scripts; does not teleport, interact, grant items or write saves.
std::string AuditGrid(int row,int count) {
    if(!PlayerInside())throw std::runtime_error("Enter the disposable test hideout first.");
    constexpr float step=.75f;const int half=static_cast<int>(std::ceil((rules::MaxExtent(groundShape,groundSize)+2)/step)),side=half*2+1;
    if(row<0 || row>=side || count<1 || count>8)throw std::runtime_error("Invalid audit grid slice.");
    const auto sample=Signature("NavMesh","SamplePosition",{"UnityEngine.Vector3","UnityEngine.AI.NavMeshHit&","System.Single","System.Int32"},"UnityEngine.AI","UnityEngine.AIModule.dll");
    struct NavHit {Vec3 position,normal;float distance;int mask,hit;};
    std::ostringstream out;out<<"{\"ok\":true,\"side\":"<<side<<",\"origin\":"<<-half*step<<",\"step\":"<<step<<",\"row\":"<<row<<",\"rows\":[";
    for(int z=row;z<std::min(side,row+count);++z){std::string line;for(int x=0;x<side;++x){const float right=(x-half)*step,up=(z-half)*step;if(rules::Distance(groundShape,groundSize,right,up)<0){line+='.';continue;}Vec3 p{};monolith::HallWorld(right,up,&p.x,&p.z);p.y=monolith::HallGroundY();NavHit hit{};float range=.12f;int mask=-1;void* a[]{&p,&hit,&range,&mask};const bool found=Value<bool>(sample,nullptr,a);line+=found && std::abs(hit.position.y-p.y)<.15f && std::hypot(hit.position.x-p.x,hit.position.z-p.z)<.1f?'#':'.';}out<<(z==row?"":",")<<Json(line);}
    return out.str()+"]}";
}
std::string AuditHideout() {
    if (!PlayerInside() || !holderRoot) throw std::runtime_error("Enter the disposable test hideout first.");
    struct NavHit {Vec3 position,normal;float distance;int mask,hit;};
    const auto sample=Signature("NavMesh","SamplePosition",{"UnityEngine.Vector3","UnityEngine.AI.NavMeshHit&","System.Single","System.Int32"},"UnityEngine.AI","UnityEngine.AIModule.dll");
    const auto calculate=Signature("NavMesh","CalculatePath",{"UnityEngine.Vector3","UnityEngine.Vector3","System.Int32","UnityEngine.AI.NavMeshPath"},"UnityEngine.AI","UnityEngine.AIModule.dll");
    Root path(il2cpp::api().object_new(game::FindClass("UnityEngine.AIModule.dll","UnityEngine.AI","NavMeshPath")));
    Invoke(Signature("NavMeshPath",".ctor",{},"UnityEngine.AI","UnityEngine.AIModule.dll"),path.Get());
    const auto find=[&](Vec3 p,float range,Vec3* result) {NavHit h{};int mask=-1;void* a[]{&p,&h,&range,&mask};const bool yes=Value<bool>(sample,nullptr,a);if(yes)*result=h.position;return yes;};
    Vec3 start{};const bool from=find(PlayerPlace(),3,&start);
    std::ostringstream out;out<<"{\"ok\":true,\"sourceOnNavMesh\":"<<Boolean(from)<<",\"items\":[";
    bool first=true;int failed=0,pending=0,scripts=0,unreachable=0,outside=0;
    for(const auto& i:items) {
        if(!first)out<<',';first=false;
        if(i.failed)++failed;if(i.waiting)++pending;
        out<<"{\"id\":"<<i.id<<",\"name\":"<<Json(i.name)<<",\"door\":"<<i.door<<",\"failed\":"<<Boolean(i.failed)<<",\"waiting\":"<<Boolean(i.waiting);
        if(void* visual=ItemVisual(i)) {
            out<<",\"instance\":"<<Value<int>(Unity("Object","GetInstanceID",0),visual);
            const auto b=Measure(visual);out<<",\"width\":"<<b.e.x*2<<",\"height\":"<<b.e.y*2<<",\"depth\":"<<b.e.z*2;
            bool all=true;Root type(TypeObject("UnityEngine.CoreModule.dll","UnityEngine","MonoBehaviour"));void* a[]{type.Get(),&all};
            Root components(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),visual,a));
            if(i.kind!=kDoor)scripts+=components.Get()?static_cast<int>(Get<uintptr_t>(components.Get(),0x18)):0;
            bool boundsInside=true;
            if(b.valid && !i.off)for(int x:{-1,1})for(int z:{-1,1}) {
                float right=0,up=0;monolith::HallLocal(b.c.x+x*b.e.x,b.c.z+z*b.e.z,&right,&up);
                if(rules::Distance(groundShape,groundSize,right,up)<0)boundsInside=false;
            }
            if(!boundsInside)++outside;out<<",\"visualBoundsInside\":"<<Boolean(boundsInside);
        }
        if(i.door!=kNoDoor && !i.off) {
            bool reachable=false;
            for(int k=0;k<9 && from && !reachable;++k) {
                const float angle=k*6.2831853f/8;Vec3 target=World(i);target.y=monolith::HallGroundY();
                if(k<8){target.x+=std::cos(angle)*2;target.z+=std::sin(angle)*2;}
                Vec3 snapped{};if(!find(target,1.25f,&snapped))continue;
                int mask=-1;void* a[]{&start,&snapped,&mask,path.Get()};
                reachable=Value<bool>(calculate,nullptr,a) && Value<int>(Method(path.Get(),"get_status",0),path.Get())==0;
            }
            if(!reachable)++unreachable;out<<",\"reachable\":"<<Boolean(reachable);
        }
        out<<'}';
    }
    out<<"],\"failed\":"<<failed<<",\"pending\":"<<pending<<",\"remainingScripts\":"<<scripts<<",\"unreachableStationsOrDoors\":"<<unreachable<<",\"visualBoundsOutside\":"<<outside<<'}';return out.str();
}
