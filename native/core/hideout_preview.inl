// Included inside hideout.cpp's private namespace. All Unity work is main-thread
// only. A single isolated scene is assembled incrementally, photographed once,
// then destroyed. No layout/save commands are used by this service.
struct Preview {
    std::unique_ptr<Root> texture;
    std::string status = "Queued";
    uint64_t touched = 0;
    uint64_t requestedAt = 0;
    bool failed = false;
    bool renderTexture = true;
};
std::map<std::string, Preview> previews;
std::map<std::string, VisualBounds> previewMeasured;
std::deque<std::string> previewQueue;
std::string previewKey;
std::string previewPriority;
uint64_t previewPriorityUntil=0;
#ifdef EPOCHPACT_TESTING
uint64_t previewTestDelay=0,previewPreemptions=0;
bool previewArtReview=false;
#endif
std::unique_ptr<Root> previewRoot, previewCamera, previewCameraObject;
std::vector<std::unique_ptr<Root>> previewOwned;
size_t previewPart = 0;
int previewMissing = 0;
int previewMaterialFixes = 0;
bool previewFloorReady = false;
uint64_t previewStarted = 0, previewSerial = 0;
uint64_t previewProgress = 0;
constexpr Vec3 kPreviewOrigin{20000,-4000,20000};
void* GameTransform(void* go) {return Invoke(Unity("GameObject","get_transform",0),go);}
void DisposePreviewObject(void* o) {
    if (!o || !game::IsAlive(o)) return;
    void* a[]{o}; Invoke(Signature("Object","DestroyImmediate",{"UnityEngine.Object"}),nullptr,a);
}
void DropPreviewTexture(Preview& p) {
    if(!p.texture)return;
    // A GC handle keeps the managed wrapper alive, not its native Unity object.
    // Scene/resource unloading can already have destroyed the cached texture.
    auto texture=std::move(p.texture);p.renderTexture=false;
    if(!game::IsAlive(texture->Get()))return;
    if(std::string(il2cpp::api().class_get_name(il2cpp::api().object_get_class(texture->Get())))=="RenderTexture")
        Invoke(Unity("RenderTexture","Release",0),texture->Get());
    DisposePreviewObject(texture->Get());
}
bool TemplatePreview(const std::string& key) {return key.rfind("t:",0)==0||key.rfind("s:",0)==0;}
int PreviewWidth(const std::string& key) {return TemplatePreview(key)?960:480;}
int PreviewHeight(const std::string& key) {return TemplatePreview(key)?640:320;}
// Locally rendered thumbnails, never shipped game assets. A fixed-size format
// avoids image decoders, rejects corrupt data and bounds disk use to 128 cards.
std::filesystem::path PreviewCacheFile(const std::string& key) {
    const auto folder=ingame::view::PreferencesFolder();if(folder.empty())return {};
    const auto cache=folder/L"hideout-preview-cache";const DWORD attrs=GetFileAttributesW(cache.c_str());
    if(attrs!=INVALID_FILE_ATTRIBUTES && (attrs&FILE_ATTRIBUTE_REPARSE_POINT))return {};
    uint64_t hash=1469598103934665603ull;
    for(unsigned char c:std::string(ingame::assets::Bytes("hideout-designs.json"))+key+std::to_string(catalogBundles)+"preview-v4")hash=(hash^c)*1099511628211ull;
    return cache/(std::to_string(hash)+".rgba");
}
void* PreviewPixelsTexture(const unsigned char* bytes,int w,int h) {
    auto& api=il2cpp::api();Root texture(api.object_new(game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","Texture2D")));
    int format=4;bool no=false;void* ctor[]{&w,&h,&format,&no};Invoke(Signature("Texture2D",".ctor",{"System.Int32","System.Int32","UnityEngine.TextureFormat","System.Boolean"}),texture.Get(),ctor);
    Root data(api.array_new(game::FindClass("mscorlib.dll","System","Byte"),w*h*4));std::memcpy(static_cast<char*>(data.Get())+0x20,bytes,w*h*4);
    void* a[]{data.Get()};Invoke(Signature("Texture2D","LoadRawTextureData",{"System.Byte[]"}),texture.Get(),a);bool yes=true;void* apply[]{&no,&yes};Invoke(Signature("Texture2D","Apply",{"System.Boolean","System.Boolean"}),texture.Get(),apply);
    return texture.Get();
}
bool LoadPreviewCache(const std::string& key,Preview& p) {
    const auto file=PreviewCacheFile(key);if(file.empty())return false;
    int w=PreviewWidth(key),h=PreviewHeight(key);
    std::error_code error;if(std::filesystem::is_symlink(file,error)||std::filesystem::file_size(file,error)!=16+w*h*4)return false;
    std::ifstream input(file,std::ios::binary);uint32_t header[4]{};input.read(reinterpret_cast<char*>(header),16);
    if(header[0]!=0x45505056u||header[1]!=4||header[2]!=static_cast<uint32_t>(w)||header[3]!=static_cast<uint32_t>(h))return false;
    std::vector<unsigned char> bytes(w*h*4);input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());if(!input)return false;
    p.texture=std::make_unique<Root>(PreviewPixelsTexture(bytes.data(),w,h));p.renderTexture=false;p.status="Ready";return true;
}
void SavePreviewCache(const std::string& key,void* rendered) {
    const auto path=PreviewCacheFile(key);if(path.empty())return;
    int w=PreviewWidth(key),h=PreviewHeight(key);
    void* old=Invoke(Unity("RenderTexture","get_active",0));std::unique_ptr<Root> previous=old?std::make_unique<Root>(old):nullptr;void* active[]{rendered};Invoke(Unity("RenderTexture","set_active",1),nullptr,active);
    std::unique_ptr<Root> pixels;
    try {
        auto& api=il2cpp::api();pixels=std::make_unique<Root>(api.object_new(game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","Texture2D")));
        int format=4;bool no=false;void* ctor[]{&w,&h,&format,&no};Invoke(Signature("Texture2D",".ctor",{"System.Int32","System.Int32","UnityEngine.TextureFormat","System.Boolean"}),pixels->Get(),ctor);
        struct Rect{float x,y,w,h;} rect{0,0,static_cast<float>(w),static_cast<float>(h)};int zero=0;void* read[]{&rect,&zero,&zero,&no};Invoke(Signature("Texture2D","ReadPixels",{"UnityEngine.Rect","System.Int32","System.Int32","System.Boolean"}),pixels->Get(),read);
        Root data(Invoke(Unity("Texture2D","GetPixels32",0),pixels->Get()));
        if(data.Get()&&Get<uintptr_t>(data.Get(),0x18)==static_cast<uintptr_t>(w*h)){std::error_code error;std::filesystem::create_directories(path.parent_path(),error);
            if(!error){auto temp=path;temp+=L".tmp";std::ofstream out(temp,std::ios::binary|std::ios::trunc);const uint32_t header[]{0x45505056u,4,static_cast<uint32_t>(w),static_cast<uint32_t>(h)};out.write(reinterpret_cast<const char*>(header),16);out.write(static_cast<char*>(data.Get())+0x20,w*h*4);out.close();if(out)MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);}
            std::vector<std::filesystem::directory_entry> files;for(const auto& f:std::filesystem::directory_iterator(path.parent_path(),error))if(f.is_regular_file()&&!f.is_symlink()&&f.path().extension()==".rgba")files.push_back(f);
            std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return a.last_write_time()<b.last_write_time();});for(size_t i=0;files.size()>128&&i<files.size()-128;++i)std::filesystem::remove(files[i].path(),error);
        }
    } catch(const std::exception& e){Log("preview cache: %s",e.what());}
    void* restore[]{previous?previous->Get():nullptr};Invoke(Unity("RenderTexture","set_active",1),nullptr,restore);if(pixels)DisposePreviewObject(pixels->Get());
}
void DropPreviewScene() {
    if (previewRoot) DisposePreviewObject(previewRoot->Get());
    previewRoot.reset();
    for (auto& owned:previewOwned) if (owned) DisposePreviewObject(owned->Get());
    previewOwned.clear();
    // Reuse a bounded warm bundle pool between thumbnails. Disposing every
    // bundle after every picture made adjacent cards reload the same data.
    while(previewBundles.size()>12){auto at=previewBundles.begin();const int file=at->first;
        for(auto r=previewAssetRequests.begin();r!=previewAssetRequests.end();) {const auto entry=byGuid.find(r->first);if(entry!=byGuid.end()&&catalog[entry->second].file==file)r=previewAssetRequests.erase(r);else ++r;}
        if(at->second&&at->second->Get())try{Invoke(Method(at->second->Get(),"Dispose",0),at->second->Get());}catch(...){}previewBundles.erase(at);}
    previewPart=0;previewMissing=0;previewMaterialFixes=0;previewFloorReady=false;previewKey.clear();
}
std::map<std::string,J> savedPreviewDesigns;
const J& PreviewDesign(const std::string& key){
    if(key.rfind("t:",0)==0){const auto index=std::stoul(key.substr(2));if(index>=Designs().items.size())throw std::runtime_error("Unknown template.");return Designs().items[index];}
    auto it=savedPreviewDesigns.find(key);if(it==savedPreviewDesigns.end()){if(savedPreviewDesigns.size()>=32)savedPreviewDesigns.clear();it=savedPreviewDesigns.emplace(key,SavedDesign(key)).first;}return it->second;
}
void WarmPreviewKey(const std::string& key,size_t limit) {
    if(previewBundles.size()>=limit)return;
    if(key.rfind("a:",0)==0){const auto k=static_cast<size_t>(std::stoul(key.substr(2)));if(k<catalog.size())WarmBundle(catalog[k].guid,true);return;}
    if(key.rfind("t:",0)!=0&&key.rfind("s:",0)!=0)return;const auto& d=PreviewDesign(key);
    for(const auto& t:kTextures)if(d["texture"].Str()==t.label&&t.prefab){auto it=byName.find(t.prefab);if(it!=byName.end())WarmBundle(catalog[it->second].guid,true);}
    for(const auto& part:d["items"].items){if(previewBundles.size()>=limit)break;const auto guid=part["guid"].Str();if(!guid.empty())WarmBundle(guid,true);}
}
void SetVector(void* tr,const char* property,Vec3 value) {void* a[]{&value};Invoke(Unity("Transform",property,1),tr,a);}
void PreviewLayer(void* go) {
    bool all=true;Root t(TypeObject("UnityEngine.CoreModule.dll","UnityEngine","Transform"));void* a[]{t.Get(),&all};
    Root list(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),go,a));
    int layer=31;void* p[]{&layer};
    for (uintptr_t k=0,n=list.Get()?Get<uintptr_t>(list.Get(),0x18):0;k<n;++k) {
        void* tr=Get<void*>(list.Get(),0x20+k*8);if (!tr)continue;
        void* child=Invoke(Unity("Component","get_gameObject",0),tr);Invoke(Unity("GameObject","set_layer",1),child,p);
    }
}
void* PreviewCopy(void* source,Vec3 at,float yaw,float size,float span) {
    using namespace ingame::ui;
    if (!previewRoot) return nullptr;
    void* holder=NewObject("EP preview object");Root rt(Invoke(Unity("GameObject","get_transform",0),holder));
    bool no=false;void* off[]{&no};Invoke(Unity("GameObject","SetActive",1),holder,off);
    void* parent=GameTransform(previewRoot->Get());void* a[]{parent};Invoke(Signature("Transform","SetParent",{"UnityEngine.Transform"}),rt.Get(),a);
    SetVector(rt.Get(),"set_localPosition",at);
    const float angle=yaw*3.14159265f/360;Quat q{0,std::sin(angle),0,std::cos(angle)};void* ra[]{&q};Invoke(Unity("Transform","set_localRotation",1),rt.Get(),ra);
    void* ca[]{source,rt.Get(),&no};void* copy=Invoke(Signature("Object","Instantiate",{"UnityEngine.Object","UnityEngine.Transform","System.Boolean"}),nullptr,ca);
    if (!copy) {DisposePreviewObject(holder);return nullptr;}
    void* ct=GameTransform(copy);SetVector(ct,"set_localPosition",{});
    previewMaterialFixes+=Sanitize(copy,false);PreviewLayer(copy);
    bool yes=true;void* on[]{&yes};Invoke(Unity("GameObject","SetActive",1),copy,on);Invoke(Unity("GameObject","SetActive",1),holder,on);
    auto b=Measure(copy);
    if (!b.valid) {DisposePreviewObject(holder);return nullptr;}
    if(previewKey.rfind("a:",0)==0)previewMeasured[previewKey]=b;
    // Match live placement: scale relative to the original prefab, with the same cap.
    const float largest=2*std::max({b.e.x,b.e.y,b.e.z});
    if (span>0 && largest*size>span) size=span/largest;
    SetVector(rt.Get(),"set_localScale",{size,size,size});CenterCopy(copy,rt.Get());
    return holder;
}
void* FirstMaterial(void* go) {
    bool all=true;Root t(TypeObject("UnityEngine.CoreModule.dll","UnityEngine","MeshRenderer"));void* a[]{t.Get(),&all};
    Root list(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),go,a));
    for (uintptr_t k=0,n=list.Get()?Get<uintptr_t>(list.Get(),0x18):0;k<n;++k) {
        void* r=Get<void*>(list.Get(),0x20+k*8);if (!r) continue;
        void* m=Invoke(Unity("Renderer","get_sharedMaterial",0),r);if (m)return m;
    }
    return nullptr;
}
void PreviewFloor(const J& design,void* material) {
    using namespace ingame::ui;
    const int shape=DesignShape(design);const float size=static_cast<float>(design["size"].Num(38));
    const GroundTexture* texture=&kTextures[0];for(const auto& t:kTextures)if(design["texture"].Str()==t.label)texture=&t;
    const int half=static_cast<int>(std::ceil(rules::MaxExtent(shape,size)+(texture->natural?30.f:3.f)));
    std::vector<Vec3> points;std::vector<int> indices;std::vector<V2> uvs;
    constexpr float step=2;
    struct Vertex {rules::Point p;V2 uv;};
    for (int z=-half;z<half;z+=2)for (int x=-half;x<half;x+=2) {
        const rules::Point raw[4]{{float(x),float(z)},{x+step,float(z)},{x+step,z+step},{float(x),z+step}};
        Vertex corners[4];float distance[4];
        for(int k=0;k<4;++k){corners[k].p=rules::GridPoint(raw[k].x,raw[k].z,texture->natural);corners[k].uv={texture->u0+rules::Mirrored(raw[k].x,texture->period)*(texture->u1-texture->u0),texture->v0+rules::Mirrored(raw[k].z,texture->period)*(texture->v1-texture->v0)};distance[k]=rules::Distance(shape,size,corners[k].p.x,corners[k].p.z);}
        std::vector<Vertex> polygon;
        for(int k=0;k<4;++k){const int j=(k+1)%4;if(distance[k]>=0)polygon.push_back(corners[k]);if((distance[k]>=0)!=(distance[j]>=0)){const float t=distance[k]/(distance[k]-distance[j]);polygon.push_back({{corners[k].p.x+(corners[j].p.x-corners[k].p.x)*t,corners[k].p.z+(corners[j].p.z-corners[k].p.z)*t},{corners[k].uv.x+(corners[j].uv.x-corners[k].uv.x)*t,corners[k].uv.y+(corners[j].uv.y-corners[k].uv.y)*t}});}}
        const int begin=static_cast<int>(points.size());
        for(const auto& v:polygon){points.push_back({v.p.x,-.05f,v.p.z});uvs.push_back(v.uv);}
        for(int k=1;k+1<static_cast<int>(polygon.size());++k)indices.insert(indices.end(),{begin,begin+k+1,begin+k});
    }
    auto& api=il2cpp::api();Root mesh(api.object_new(game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","Mesh")));
    Invoke(Signature("Mesh",".ctor",{}),mesh.Get());
    Root vertices(api.array_new(game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","Vector3"),points.size()));
    Root triangles(api.array_new(game::FindClass("mscorlib.dll","System","Int32"),indices.size()));
    Root uv(api.array_new(game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","Vector2"),uvs.size()));
    std::memcpy(static_cast<char*>(vertices.Get())+0x20,points.data(),points.size()*sizeof(Vec3));
    std::memcpy(static_cast<char*>(triangles.Get())+0x20,indices.data(),indices.size()*sizeof(int));
    std::memcpy(static_cast<char*>(uv.Get())+0x20,uvs.data(),uvs.size()*sizeof(V2));
    void* va[]{vertices.Get()};void* ta[]{triangles.Get()};void* ua[]{uv.Get()};
    Invoke(Signature("Mesh","set_vertices",{"UnityEngine.Vector3[]"}),mesh.Get(),va);Invoke(Signature("Mesh","set_triangles",{"System.Int32[]"}),mesh.Get(),ta);Invoke(Signature("Mesh","set_uv",{"UnityEngine.Vector2[]"}),mesh.Get(),ua);
    Invoke(Unity("Mesh","RecalculateNormals",0),mesh.Get());Invoke(Unity("Mesh","RecalculateBounds",0),mesh.Get());
    void* go=NewObject("EP preview ground");void* parent[]{GameTransform(previewRoot->Get())};Invoke(Signature("Transform","SetParent",{"UnityEngine.Transform"}),GameTransform(go),parent);SetVector(GameTransform(go),"set_localPosition",{});
    void* filter=Add(go,"UnityEngine.CoreModule.dll","UnityEngine","MeshFilter");void* ma[]{mesh.Get()};Invoke(Unity("MeshFilter","set_sharedMesh",1),filter,ma);
    void* renderer=Add(go,"UnityEngine.CoreModule.dll","UnityEngine","MeshRenderer");void* mat[]{material};Invoke(Unity("Renderer","set_sharedMaterial",1),renderer,mat);PreviewLayer(go);
    previewOwned.push_back(std::make_unique<Root>(mesh.Get()));
}
void PreviewCameraReady() {
    using namespace ingame::ui;
    if (previewCamera && game::IsAlive(previewCamera->Get()))return;
    void* go=NewObject("EP library camera");void* cam=Add(go,"UnityEngine.CoreModule.dll","UnityEngine","Camera");Enable(cam,false);
    int clear=2,mask=static_cast<int>(0x80000000u);bool no=false,yes=true;float nearPlane=.1f,farPlane=2000;
    void* c[]{&clear};void* m[]{&mask};void* n[]{&no};void* y[]{&yes};void* a[]{&nearPlane};void* b[]{&farPlane};
    Invoke(Unity("Camera","set_clearFlags",1),cam,c);Invoke(Unity("Camera","set_cullingMask",1),cam,m);Invoke(Unity("Camera","set_orthographic",1),cam,y);
    Invoke(Unity("Camera","set_useOcclusionCulling",1),cam,n);Invoke(Unity("Camera","set_allowHDR",1),cam,n);Invoke(Unity("Camera","set_nearClipPlane",1),cam,a);Invoke(Unity("Camera","set_farClipPlane",1),cam,b);
    Color color=Hex("#101d19");void* co[]{&color};Invoke(Unity("Camera","set_backgroundColor",1),cam,co);
    previewCameraObject=std::make_unique<Root>(go);previewCamera=std::make_unique<Root>(cam);
}
void BeginPreview(const std::string& key) {
    using namespace ingame::ui;
    previewKey=key;previewStarted=previewProgress=GetTickCount64();previewPart=0;WarmPreviewKey(key,12);
    previewRoot=std::make_unique<Root>(NewObject("EpochPact isolated static preview"));SetVector(GameTransform(previewRoot->Get()),"set_position",kPreviewOrigin);
    void* lamp=NewObject("EP preview light");void* parent[]{GameTransform(previewRoot->Get())};Invoke(Signature("Transform","SetParent",{"UnityEngine.Transform"}),GameTransform(lamp),parent);
    void* light=Add(lamp,"UnityEngine.CoreModule.dll","UnityEngine","Light");int type=1,mask=static_cast<int>(0x80000000u);float intensity=1.15f;
    void* t[]{&type};void* m[]{&mask};void* i[]{&intensity};Invoke(Unity("Light","set_type",1),light,t);Invoke(Unity("Light","set_cullingMask",1),light,m);Invoke(Unity("Light","set_intensity",1),light,i);
    Vec3 angles{55,-35,0};void* a[]{&angles};Invoke(Unity("Transform","set_eulerAngles",1),GameTransform(lamp),a);
    previews[key].status="Loading model";
}
void Photograph() {
    using namespace ingame::ui;
    auto b=Measure(previewRoot->Get());if (!b.valid)throw std::runtime_error("No static mesh: effect or script-only asset (test separately)");
    PreviewCameraReady();void* cam=previewCamera->Get();
    const bool design=TemplatePreview(previewKey);
    const float radius=std::max({b.e.x,b.e.y,b.e.z,1.f});
    Vec3 target=b.c,at{target.x+radius*.35f,target.y+radius*(design?2.f:1.1f),target.z-radius*2.1f};
    SetVector(TransformOf(cam),"set_position",at);void* look[]{&target};Invoke(Signature("Transform","LookAt",{"UnityEngine.Vector3"}),TransformOf(cam),look);
    const auto right=Value<Vec3>(Unity("Transform","get_right",0),TransformOf(cam)),up=Value<Vec3>(Unity("Transform","get_up",0),TransformOf(cam));
    const float horizontal=std::abs(right.x)*b.e.x+std::abs(right.y)*b.e.y+std::abs(right.z)*b.e.z;
    const float vertical=std::abs(up.x)*b.e.x+std::abs(up.y)*b.e.y+std::abs(up.z)*b.e.z;
    float orthographic=std::max({horizontal/1.5f,vertical,.2f})*1.12f;void* ortho[]{&orthographic};Invoke(Unity("Camera","set_orthographicSize",1),cam,ortho);
    auto& api=il2cpp::api();Root texture(api.object_new(game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","RenderTexture")));
    int w=PreviewWidth(previewKey),h=PreviewHeight(previewKey),depth=24;void* ctor[]{&w,&h,&depth};Invoke(Signature("RenderTexture",".ctor",{"System.Int32","System.Int32","System.Int32"}),texture.Get(),ctor);
    Invoke(Unity("RenderTexture","Create",0),texture.Get());void* a[]{texture.Get()};Invoke(Unity("Camera","set_targetTexture",1),cam,a);
    Invoke(Unity("Camera","Render",0),cam);void* clear[]{nullptr};Invoke(Unity("Camera","set_targetTexture",1),cam,clear);
    auto& result=previews[previewKey];result.texture=std::make_unique<Root>(texture.Get());result.renderTexture=true;result.status=previewMissing?"Static preview: "+std::to_string(previewMissing)+" models unavailable":previewMaterialFixes?"Ready · game stone finish on placeholder surfaces":"Ready";
    bool cacheComplete=!previewMissing;
#ifdef EPOCHPACT_TESTING
    // Private art evidence can include a missing scene NPC/door. Its status is
    // preserved in the test report; never cache incomplete public previews.
    cacheComplete|=previewArtReview;
#endif
    if(cacheComplete)SavePreviewCache(previewKey,texture.Get());
    DropPreviewScene();
}
void PreviewStep() {
    bool allowed=game::IsOfflinePlay(),visible=ingame::view::WindowOpen();
#ifdef EPOCHPACT_TESTING
    // Isolated, script-free renderer only; no player/world/save operations.
    allowed|=previewArtReview;visible|=previewArtReview;
#endif
    if (!visible || !allowed) {
        if (previewRoot)DropPreviewScene();previewQueue.clear();
        for (auto& [_,p]:previews) if (!p.texture && !p.failed){p.status="Paused";p.requestedAt=0;}
        return;
    }
    if(!previewKey.empty()&&!previewQueue.empty()&&rules::PreemptPreview(
        previewQueue.front()==previewPriority&&GetTickCount64()<=previewPriorityUntil,
        previewQueue.front()==previewKey,GetTickCount64()-previewProgress)){
#ifdef EPOCHPACT_TESTING
        ++previewPreemptions;
#endif
        const auto suspended=previewKey;DropPreviewScene();previews[suspended].status="Queued";previewQueue.push_back(suspended);
    }
    if (previewKey.empty()) {
        if (previewQueue.empty())return;
        const auto key=previewQueue.front();previewQueue.pop_front();BeginPreview(key);
    }
    const std::string key=previewKey;
#ifdef EPOCHPACT_TESTING
    if(GetTickCount64()-previewStarted<previewTestDelay)return;
#endif
    try {
        if (GetTickCount64()-previewStarted>20000)throw std::runtime_error("Model is taking too long. Choose another model, or hover again to retry.");
        bool waiting=false;
        if (key.rfind("a:",0)==0) {
            const auto index=static_cast<size_t>(std::stoul(key.substr(2)));if (index>=catalog.size())throw std::runtime_error("Unknown catalog entry");
            void* asset=OpenAndTakePrefab(catalog[index].guid,&waiting,true);if (waiting)return;if (!asset)throw std::runtime_error("Model unavailable in this game build");
            if (!PreviewCopy(asset,{},0,1,14))throw std::runtime_error("No static mesh: effect-only asset (test separately)");Photograph();
        } else if (key.rfind("t:",0)==0||key.rfind("s:",0)==0) {
            const auto& d=PreviewDesign(key);const auto& parts=d["items"].items;
            if (!previewFloorReady) {
                void* mat=nullptr;
                for (const auto& t:kTextures) if (d["texture"].Str()==t.label && t.prefab) {
                    const auto it=byName.find(t.prefab);if (it!=byName.end()) {void* floor=OpenAndTakePrefab(catalog[it->second].guid,&waiting,true);if (waiting)return;if (floor)mat=FirstMaterial(floor);}break;
                }
                if (!mat)throw std::runtime_error("Ground material unavailable");
                PreviewFloor(d,mat);
                previewFloorReady=true;
            }
            // Small bounded batches avoid a full frame of delay per object.
            const auto budget=GetTickCount64();int batch=0;
            while (previewPart<parts.size() && batch++<6 && GetTickCount64()-budget<5) {
                const auto& p=parts[previewPart];void* source=nullptr;
                const auto guid=p["guid"].Str();
                if (!guid.empty())source=OpenAndTakePrefab(guid,&waiting,true);
                else if (static_cast<int>(p["kind"].Num())==kDoor)source=monolith::HallPreviewDoor();
                else {const auto it=sceneSources.find(p["name"].Str());if (it!=sceneSources.end())source=it->second;}
                if (waiting)return;
                Vec3 at{static_cast<float>(p["right"].Num()),static_cast<float>(p["lift"].Num()),static_cast<float>(p["up"].Num())};
                const float size=static_cast<float>(p["size"].Num(1)),span=static_cast<float>(p["maxSpan"].Num());
                if (!source || !PreviewCopy(source,at,static_cast<float>(p["yaw"].Num()),size,static_cast<int>(p["kind"].Num())==kDoor?3:span))++previewMissing;
                ++previewPart;previewProgress=GetTickCount64();previews[key].status="Loading template "+std::to_string(previewPart)+" / "+std::to_string(parts.size());
            }
            if(previewPart<parts.size())return;
            Photograph();
        } else throw std::runtime_error("Invalid preview key");
    } catch (const std::exception& e) {previews[key].status=e.what();previews[key].failed=true;Log("hideout preview %s: %s",key.c_str(),e.what());DropPreviewScene();}
}
