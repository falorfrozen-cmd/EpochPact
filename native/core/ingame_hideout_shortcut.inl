// A small native HUD shortcut, bound to the real minimap frame. No scene scan,
// simulated hotkey, menu activation or save action is needed to draw it.
struct HideoutShortcut {
    Pin root,listener,frame,tip,tipText,map,camera,corners;
    hideout::hudrules::IconLayout layout;
    bool visible=false,enabled=false,under=false,down=false,busy=false,covered=false;
    uint64_t probeAt=0,busyAt=0,cooldownUntil=0,messageUntil=0;
    void* owner=nullptr;
    std::string message,playerId,error;
    hideout::hudrules::Rect minimapPixels;
} shortcut;
uint64_t shortcutEpoch=0;

void ShortcutRelease(){++shortcutEpoch;shortcut=HideoutShortcut{};}
bool ShortcutUnderPointer(){return shortcut.visible&&shortcut.under;}
void ShortcutMessage(const std::string& text){shortcut.message=Tr(text);shortcut.messageUntil=GetTickCount64()+9000;}
bool ShortcutPlayer(std::string* id=nullptr){
    if(!Eligible()||!game::IsOfflinePlay())return false;
    void* actor=managed::Invoke(Find("LE.dll","","PlayerFinder","getPlayerActor",0));
    if(!actor||!game::IsAlive(actor))return false;
    void* health=managed::Get<void*>(actor,managed::Offset("Actor","health"));
    if(!health||managed::Get<float>(health,managed::Offset("BaseHealth","currentHealth"))<=0)return false;
    void* tracker=managed::Get<void*>(actor,managed::Offset("Actor","characterDataTracker"));
    if(!tracker)return false;void* data=managed::Get<void*>(tracker,managed::Offset("CharacterDataTracker","charData"));
    if(!data||!managed::Value<bool>(Find("LE.dll","LE.Data","CharacterData","get_IsOffline",0),data))return false;
    if(id)*id=managed::Text(managed::Invoke(managed::Method(data,"get_Id",0),data));return true;
}
void ShortcutTravel(){
    if(!shortcut.visible||!shortcut.enabled||shortcut.busy)return;
    std::string player;if(!ShortcutPlayer(&player)||player!=shortcut.playerId)return;
    shortcut.busy=true;shortcut.busyAt=GetTickCount64();const auto epoch=shortcutEpoch;
    Send("bossdoors 1",[epoch,player](const std::string& answer){
        if(epoch!=shortcutEpoch)return;JValue r;json::Parse(answer,r);
        if(!r["ok"].boolean){shortcut.busy=false;ShortcutMessage(PlayerError(r["error"].Str()));return;}
        std::string current;if(!ShortcutPlayer(&current)||current!=player){shortcut.busy=false;ShortcutMessage("The loaded character changed. Try again.");return;}
        Send("hideout go",[epoch](const std::string& result){
            if(epoch!=shortcutEpoch)return;JValue value;json::Parse(result,value);shortcut.busy=false;
            shortcut.cooldownUntil=GetTickCount64()+3000;
            if(!value["ok"].boolean)ShortcutMessage(PlayerError(value["error"].Str()));
        });
    });
}
// The inventory, character, skill and other windows on the right, and the large ones in the centre, lie over
// the minimap corner; the shortcut's own canvas sorts above them, so it hides while one is open (UIBase.AnyPanelOpened).
bool ShortcutCovered(){
    static const il2cpp::Method* any=Find("LE.dll","","UIBase","AnyPanelOpened",1,false);
    void* ui=game::StaticObject(game::FindStaticField("LE.dll","","UIBase","instance"));
    if(!any||!ui||!game::IsAlive(ui))return false;
    int mask=2|4;void* a[]{&mask};  // PanelsMask Center | Right
    try{return managed::Value<bool>(any,ui,a);}catch(...){return false;}
}
void ShortcutStroke(void* parent,float x,float y,float endX,float endY,Color color,float thickness=1.8f){
    const float dx=endX-x,dy=endY-y;void* stroke=Solid(parent,"Hideout emblem",x,y,std::hypot(dx,dy),thickness,color);
    Turn(TransformOf(stroke),-std::atan2(dy,dx)*180.f/3.14159265f);
}
void ShortcutInit(){
    ShortcutRelease();if(!hud.parent)return;
    auto root=New("EpochPact Hideout travel",hud.parent.Get(),0,0,38,38);shortcut.root.Set(root.rt);
    auto* canvas=AddCanvas(root.go);bool yes=true;int order=30025;void* a[]{&yes},*b[]{&order};
    managed::Invoke(Find("UnityEngine.UIModule.dll","UnityEngine","Canvas","set_overrideSorting",1),canvas,a);
    managed::Invoke(Find("UnityEngine.UIModule.dll","UnityEngine","Canvas","set_sortingOrder",1),canvas,b);
    Add(root.go,"UnityEngine.UI.dll","UnityEngine.UI","GraphicRaycaster");
    shortcut.listener.Set(Add(root.go,"LE.dll","","UIMouseListener"));
    Raycast(Fill(root.rt,"Hideout emblem background",0,0,38,38,Hex("#091611f5"),5),true);
    shortcut.frame.Set(Outline(root.rt,"Hideout emblem border",0,0,38,38,Hex("#b59b62"),5));
    Outline(root.rt,"Inset frame",3,3,32,32,Hex("#415442"),3);
    const auto gold=Hex("#ead5a0"),jade=Hex("#78d9b0");
    ShortcutStroke(root.rt,8,19,19,9,gold,2);ShortcutStroke(root.rt,19,9,30,19,gold,2);
    ShortcutStroke(root.rt,11,18,11,30,gold);ShortcutStroke(root.rt,27,18,27,30,gold);ShortcutStroke(root.rt,10,30,28,30,gold);
    ShortcutStroke(root.rt,16,29,16,22,jade);ShortcutStroke(root.rt,16,22,19,19,jade);
    ShortcutStroke(root.rt,19,19,22,22,jade);ShortcutStroke(root.rt,22,22,22,29,jade);
    auto tip=New("Hideout shortcut tooltip",hud.parent.Get(),0,0,228,66);shortcut.tip.Set(tip.rt);
    auto* tipCanvas=AddCanvas(tip.go);order=30045;void* c[]{&order};
    managed::Invoke(Find("UnityEngine.UIModule.dll","UnityEngine","Canvas","set_overrideSorting",1),tipCanvas,a);
    managed::Invoke(Find("UnityEngine.UIModule.dll","UnityEngine","Canvas","set_sortingOrder",1),tipCanvas,c);
    Fill(tip.rt,"Shortcut tooltip background",0,0,228,66,Hex("#091611fa"),4);Outline(tip.rt,"Shortcut tooltip frame",0,0,228,66,Hex("#b59b62"),4);
    Style text{Face::Body,12,Hex("#ead5a0")};text.wrap=true;text.align=kLeft;
    shortcut.tipText.Set(TextBox(tip.rt,"Hideout travel hint",12,6,204,54,text,""));
    shortcut.corners.Set(il2cpp::api().array_new(game::FindClass("UnityEngine.CoreModule.dll","UnityEngine","Vector3"),4));
    SetActive(root.go,false);SetActive(tip.go,false);
}
void* ShortcutMap(void* ui){
    // UIPanel is a managed record. Its instance is the real GameObject; never
    // call Component methods on the UIPanel record or instantiate a new panel.
    void* panel=managed::Get<void*>(ui,managed::Offset("UIBase","minimapPanel"));
    void* object=panel?managed::Get<void*>(panel,managed::Offset("UIPanel","instance")):nullptr;
    if(!object||!game::IsAlive(object))return nullptr;
    void* tr=managed::Invoke(U("GameObject","get_transform",0),object);
    // DMM's bounded config path identifies the actual frame if the UIPanel
    // instance is only a full-screen container. No FindObjectsOfType is used.
    void* dmm=game::StaticObject(game::FindStaticField("LE.dll","DMM","DMMap","Instance"));
    if(dmm)for(void* config:managed::Entries(managed::Get<void*>(dmm,managed::Offset("DMMap","configs","DMM")))){
        if(!config||managed::Text(managed::Get<void*>(config,managed::Offset("DMMapConfig","name","DMM")))!="Minimap")continue;
        void* p=managed::Get<void*>(config,managed::Offset("DMMapConfig","uiPosition","DMM"));
        for(int depth=0;p&&depth<4;++depth){
            if(managed::Text(managed::Invoke(U("Object","get_name",0),GameObjectOf(p)))=="Minimap")return p;
            p=managed::Invoke(U("Transform","get_parent",0),p);
        }
    }
    return tr;
}
void ShortcutBindCamera(){
    shortcut.camera.Reset();managed::Root type(Type("UnityEngine.UIModule.dll","UnityEngine","Canvas"));void* a[]{type.Get()};
    // This Unity player strips the Type-only GetComponentInParent overload.
    // Walk the bounded UI ancestor chain using its retained GetComponent API.
    void* canvas=nullptr;void* ancestor=shortcut.map.Get();
    for(int depth=0;ancestor&&depth<16&&!canvas;++depth){
        canvas=managed::Invoke(Sig("UnityEngine.CoreModule.dll","UnityEngine","Component","GetComponent",{"System.Type"}),ancestor,a);
        if(!canvas)ancestor=managed::Invoke(U("Transform","get_parent",0),ancestor);
    }
    if(!canvas)return;void* root=managed::Invoke(Find("UnityEngine.UIModule.dll","UnityEngine","Canvas","get_rootCanvas",0),canvas);
    if(root)canvas=root;
    if(managed::Value<int>(Find("UnityEngine.UIModule.dll","UnityEngine","Canvas","get_renderMode",0),canvas)!=0)
        shortcut.camera.Set(managed::Invoke(Find("UnityEngine.UIModule.dll","UnityEngine","Canvas","get_worldCamera",0),canvas));
}
void ShortcutProbe(){
    shortcut.probeAt=GetTickCount64()+250;shortcut.layout={};shortcut.enabled=false;
    shortcut.error.clear();shortcut.minimapPixels={};
    void* ui=game::StaticObject(game::FindStaticField("LE.dll","","UIBase","instance"));
    if(!ui||!game::IsAlive(ui)||!ShortcutPlayer(&shortcut.playerId))return;
    const bool visible=managed::Get<bool>(ui,managed::Offset("UIBase","uiEnabled"));
    const bool overlay=managed::Get<bool>(ui,managed::Offset("UIBase","overlayMapOpen"));
    if(!hideout::hudrules::CanTravelButton(true,true,false,WindowOpen(),hideout::Editing(),false,visible,overlay))return;
    if(shortcut.owner!=ui||!shortcut.map||!game::IsAlive(shortcut.map.Get())){
        shortcut.owner=ui;shortcut.map.Set(ShortcutMap(ui));if(!shortcut.map)return;ShortcutBindCamera();
    }
    if(!managed::Value<bool>(U("GameObject","get_activeInHierarchy",0),GameObjectOf(shortcut.map.Get())))return;
    void* corners[]{shortcut.corners.Get()};managed::Invoke(U("RectTransform","GetWorldCorners",1),shortcut.map.Get(),corners);
    const float screenWidth=hud.width*hud.scale,screenHeight=hud.height*hud.scale;
    float left=screenWidth,top=screenHeight,right=0,bottom=0;
    for(int k=0;k<4;++k){auto point=managed::Get<V3>(shortcut.corners.Get(),0x20+k*sizeof(V3));void* args[]{shortcut.camera.Get(),&point};
        const auto at=managed::Value<V2>(Find("UnityEngine.UIModule.dll","UnityEngine","RectTransformUtility","WorldToScreenPoint",2),nullptr,args);
        left=std::min(left,at.x);right=std::max(right,at.x);top=std::min(top,screenHeight-at.y);bottom=std::max(bottom,screenHeight-at.y);
    }
    shortcut.minimapPixels={left,top,right-left,bottom-top};
    shortcut.layout=hideout::hudrules::TravelIconLayout(screenWidth,screenHeight,hud.scale,shortcut.minimapPixels,true);
    shortcut.enabled=shortcut.layout.visible&&!hideout::PlayerInside()&&!shortcut.busy&&GetTickCount64()>=shortcut.cooldownUntil;
}
void ShortcutFrame(){
    if(!shortcut.root||!game::IsAlive(shortcut.root.Get()))return;const auto now=GetTickCount64();
    if(shortcut.busy&&now-shortcut.busyAt>20000){shortcut.busy=false;ShortcutMessage("Hideout travel did not finish. Try again.");}
    if(now>=shortcut.probeAt)try{ShortcutProbe();}catch(const std::exception& e){shortcut.error=e.what();shortcut.layout={};shortcut.enabled=false;shortcut.map.Reset();shortcut.probeAt=now+1000;}
    shortcut.covered=shortcut.layout.visible&&ShortcutCovered();
    shortcut.visible=shortcut.layout.visible&&!WindowOpen()&&!hideout::Editing()&&Eligible()&&mapview::HideoutButton()&&!shortcut.covered;
    SetActive(GameObjectOf(shortcut.root.Get()),shortcut.visible);
    if(shortcut.visible){
        Move(shortcut.root.Get(),shortcut.layout.icon.x,shortcut.layout.icon.y);
        // Follow the native minimap's scale, including its compact control row.
        // The mod's independent text/HUD scale must not enlarge this emblem
        // into the quest tracker below it.
        const float factor=shortcut.layout.icon.w/38.f;V3 scale{factor,factor,1};void* a[]{&scale};
        managed::Invoke(U("Transform","set_localScale",1),shortcut.root.Get(),a);
    }
    POINT p{};RECT box{};HWND window=GetForegroundWindow();DWORD pid=0;if(window)GetWindowThreadProcessId(window,&pid);
    const bool cursor=pid==GetCurrentProcessId()&&GetCursorPos(&p)&&ScreenToClient(window,&p)&&GetClientRect(window,&box)&&box.right>0&&box.bottom>0;
    const float x=cursor?p.x*hud.width/box.right:-10000,y=cursor?p.y*hud.height/box.bottom:-10000;
    const auto r=shortcut.layout.icon;shortcut.under=shortcut.visible&&cursor&&x>=r.x&&x<=r.x+r.w&&y>=r.y&&y<=r.y+r.h;
    bool under=shortcut.under;void* a[]{&under};managed::Invoke(Find("LE.dll","","UIMouseListener","set_underMouse",1),shortcut.listener.Get(),a);
    Tint(shortcut.frame.Get(),Hex(shortcut.under?"#f0d298":shortcut.enabled?"#b59b62":"#687369"));
    const bool down=cursor&&(GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON)?VK_RBUTTON:VK_LBUTTON)&0x8000)!=0;
    if(down&&!shortcut.down&&shortcut.under)ShortcutTravel();shortcut.down=down;
    const bool error=shortcut.messageUntil>now;
    const bool tip=shortcut.visible&&(shortcut.under||error);SetActive(GameObjectOf(shortcut.tip.Get()),tip);
    if(tip){Move(shortcut.tip.Get(),std::clamp(r.x-238.f,8.f,std::max(8.f,hud.width-236.f)),std::clamp(r.y,8.f,std::max(8.f,hud.height-74.f)));
        SetText(shortcut.tipText.Get(),error?shortcut.message:Tr(shortcut.busy||now<shortcut.cooldownUntil?"Travelling to Hideout…":hideout::PlayerInside()?"You are in your Hideout":"Hideout\nTravel to your personal sanctuary"));}
}
#ifdef EPOCHPACT_TESTING
std::string ShortcutAudit(){
    const auto r=shortcut.layout.icon,m=shortcut.minimapPixels;std::ostringstream out;out<<"{\"visible\":"<<managed::Boolean(shortcut.visible)<<",\"enabled\":"<<managed::Boolean(shortcut.enabled)<<",\"busy\":"<<managed::Boolean(shortcut.busy)<<",\"covered\":"<<managed::Boolean(shortcut.covered)<<",\"turnedOn\":"<<managed::Boolean(mapview::HideoutButton())<<",\"bound\":"<<managed::Boolean(bool(shortcut.map))<<",\"x\":"<<r.x<<",\"y\":"<<r.y<<",\"w\":"<<r.w<<",\"h\":"<<r.h<<",\"minimap\":["<<m.x<<','<<m.y<<','<<m.w<<','<<m.h<<"],\"error\":"<<managed::Json(shortcut.error)<<'}';return out.str();
}
std::string ShortcutClickTest(){
    if(!shortcut.visible||!shortcut.enabled)throw std::runtime_error("The ready Hideout shortcut must be visible.");
    ShortcutTravel();return "{\"ok\":true}";
}
#endif
