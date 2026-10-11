// Execute the real auto-pickup detour, snapshot walk and managed pickup calls
// against a controlled game facade. This is not a live Last Epoch test.
#include "../core/items.cpp"
#include <cstring>
#include <stdexcept>

namespace fixture {
ep::il2cpp::Api api;
bool offline = true, hoverReadable = true, hoverFault = false, hoverAlive = true;
bool hoverOnPickup = false, collectable = true;
bool aliveReadable = true;
bool modernMetadata = true, modernFault = false, uiFault = false;
bool uiAlive = true, systemAlive = true, closeOnTick = false;
bool cardVisible=true;
int tickCalls = 0, pickups = 0, roots = 0;
int hoverField, uiField, listField, labelClass, pickupMethod;
void* hovered = nullptr;
void* activeItem = nullptr;
struct Ui { void* cls{}; void* monitor{}; void* system{}; } ui;
int card;
struct LegacyManager {void* cls{};void* monitor{};bool active=false;} legacyManager;
struct TooltipSystem {void* cls{};void* monitor{};void* rect{};void* cardObject=&card;void* manager=&legacyManager;bool locked=false;} tooltipSystem;
void* GameObject(void* object,const ep::il2cpp::Method*){return object;}
bool Visible(void*,const ep::il2cpp::Method*){return cardVisible;}
void* ActiveTooltip(void*, const ep::il2cpp::Method*) {
    if (modernFault) throw std::runtime_error("unreadable modern tooltip fixture");
    return activeItem;
}
struct Array { void* cls{}; void* monitor{}; void* bounds{}; uintptr_t length = 2; void* data[2]{}; } array;
struct List { void* cls{}; void* monitor{}; void* items = &array; int count = 2; } list;
struct DList { void* cls{}; void* monitor{}; void* inner = &list; } dlist;
struct Label { void* cls = &labelClass; void* monitor{}; } labels[2];

__declspec(noinline) void Tick(void*, float delta, const ep::il2cpp::Method*) {
    ++tickCalls;
    if (closeOnTick) activeItem = nullptr;
    if (delta < 0) tickCalls += 100;
}
void* Invoke(const ep::il2cpp::Method* method, void*, void**, void** exception) {
    *exception = nullptr;
    if (method == &pickupMethod) {
        ++pickups;
        if (hoverOnPickup) hovered = &labels[0];
    }
    return nullptr;
}
int total = 0, passed = 0;
void Check(bool ok, const char* name) {
    ++total; if (ok) ++passed;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", name);
}
void Due() { ep::items::g_lastScan = GetTickCount() - 750; }
}

namespace ep {
void Log(const char*, ...) {}
namespace il2cpp { const Api& api() { return fixture::api; } }
namespace game {
using il2cpp::Field;
using il2cpp::Class;
MethodRef FindMethod(const char*, const char*, const char* cls, const char* name, int) {
    if (!std::strcmp(name,"get_ActiveItemTooltipItem")) return fixture::modernMetadata ? MethodRef{&fixture::pickupMethod,reinterpret_cast<void*>(&fixture::ActiveTooltip)} : MethodRef{};
    if(!std::strcmp(name,"get_gameObject"))return {&fixture::pickupMethod,reinterpret_cast<void*>(&fixture::GameObject)};
    if(!std::strcmp(name,"get_activeInHierarchy"))return {&fixture::pickupMethod,reinterpret_cast<void*>(&fixture::Visible)};
    if (!std::strcmp(cls, "DistantItemPickupHandler")) return {&fixture::pickupMethod, reinterpret_cast<void*>(&fixture::Tick)};
    if (!std::strcmp(name, "requestPickup")) return {&fixture::pickupMethod, reinterpret_cast<void*>(&fixture::Invoke)};
    return {};
}
const Field* FindStaticField(const char*, const char*, const char*, const char* field) {
    if (!std::strcmp(field, "highlightedTooltipItem")) return fixture::hoverReadable ? &fixture::hoverField : nullptr;
    if (!std::strcmp(field, "instance")) return &fixture::uiField;
    if (!std::strcmp(field, "pickableGroundLabelList")) return &fixture::listField;
    return nullptr;
}
const Class* FindClass(const char*, const char*, const char*) { return &fixture::labelClass; }
size_t FieldOffset(const char*, const char*, const char*, const char* field) { return !std::strcmp(field,"_itemTooltip")?0x18:!std::strcmp(field,"_tooltipItemManager")?0x20:!std::strcmp(field,"<TooltipLockActive>k__BackingField")?0x28:!std::strcmp(field,"active")?0x10:!std::strcmp(field,"_tooltipSystem")||!std::strcmp(field,"_activeHoveredItemRect")?0x10:0; }
void* StaticObject(const Field* field) {
    if (field == &fixture::hoverField) {
        if (fixture::hoverFault) throw std::runtime_error("unreadable hover fixture");
        return fixture::hovered;
    }
    if (field == &fixture::listField) return &fixture::dlist;
    if (field == &fixture::uiField) {
        if(fixture::uiFault)throw std::runtime_error("unreadable UI fixture");
        return &fixture::ui;
    }
    return nullptr;
}
bool IsOfflinePlay(bool* known) { if (known) *known = true; return fixture::offline; }
std::string GateText() { return "fixture gate"; }
bool IsAlive(void* obj) { return obj && (obj != fixture::hovered || fixture::hoverAlive) && (obj!=&fixture::ui||fixture::uiAlive) && (obj!=&fixture::tooltipSystem||fixture::systemAlive); }
bool TryStaticObject(const Field* field, void** value) {
    *value = nullptr;
    return Guarded([&] { *value = StaticObject(field); }, nullptr);
}
bool TryIsAlive(void* obj, bool* alive) {
    *alive = false;
    if (!fixture::aliveReadable) return false;
    *alive = IsAlive(obj);
    return true;
}
}
namespace smartloot {
bool CanCollect() { return fixture::offline && fixture::collectable; }
bool Accept(void*) { return true; }
bool Category(int) { return true; }
}
}

int main() {
    using namespace fixture;
    struct Location{int x,y,z;};Location position{12,3,4},player{0,1,2};
    {ep::items::rules::BorrowLocation borrow(position,player);Check(position.x==0&&position.y==1,"native range check borrows only the selected drop location");}
    Check(position.x==12&&position.y==3&&position.z==4,"borrowed location is restored after pickup");
    try{ep::items::rules::BorrowLocation borrow(position,player);throw std::runtime_error("inventory rejected");}catch(...){}
    Check(position.x==12&&position.y==3&&position.z==4,"failed pickup restores the original drop location");
    api.runtime_invoke = Invoke;
    api.gchandle_new = [](void* object, bool) -> uintptr_t { ++roots; return reinterpret_cast<uintptr_t>(object); };
    api.gchandle_free = [](uintptr_t) { --roots; };
    array.data[0] = &labels[0]; array.data[1] = &labels[1];
    ui.system=&tooltipSystem;
    ep::items::Init();
    Check(ep::items::SetAuto(1).find("-> on") != std::string::npos, "enable the actual pickup hook");
    hovered = &labels[0]; Due();
    const auto before = ep::items::g_lastScan.load();
    for (int i = 0; i < 100; ++i) Tick(nullptr, 0, nullptr);
    Check(pickups == 0 && tickCalls == 0, "hover pauses both mod and native distant pickup ticks");
    Check(ep::items::g_lastScan.load() == before, "hover does not consume the next pickup interval");
    hovered = nullptr; Tick(nullptr, 0, nullptr);
    Check(pickups == 2 && roots == 0, "leaving hover resumes real managed pickups and releases roots");
    Tick(nullptr, 0, nullptr);
    Check(pickups == 2, "collection remains rate limited after a successful scan");
    offline = false; Due(); Tick(nullptr, 0, nullptr);
    Check(pickups == 2, "online mode never collects items");
    offline = true; hoverFault = true; Due(); Tick(nullptr, 0, nullptr);
    Check(pickups == 2 && roots == 0, "unreadable hover state pauses safely");
    hoverFault = false; hovered = &labels[0]; aliveReadable = false; Due(); Tick(nullptr, 0, nullptr);
    Check(pickups == 2, "unreadable Unity lifetime state also pauses collection");
    aliveReadable = true; hovered = nullptr; hoverOnPickup = true; Due(); Tick(nullptr, 0, nullptr);
    Check(pickups == 3 && roots == 0, "tooltip appearing during pickup stops the remaining calls");
    hoverOnPickup = false; hoverAlive = false; Due(); Tick(nullptr, 0, nullptr);
    Check(pickups == 4, "destroyed hover object does not permanently pause collection");
    hovered = nullptr; collectable = false; Due(); Tick(nullptr, 0, nullptr);
    Check(pickups == 4, "unloaded character does not collect");
    collectable=true;hoverAlive=true;activeItem=&labels[0];Due();
    const auto modernBefore=ep::items::g_lastScan.load();
    for(int i=0;i<100;++i)Tick(nullptr,0,nullptr);
    Check(pickups==4 && ep::items::g_lastScan.load()==modernBefore,"V2/container tooltip pauses pickup with no legacy hover and preserves timer");
    closeOnTick=true;Tick(nullptr,0,nullptr);closeOnTick=false;
    Check(pickups==4&&activeItem==&labels[0],"hover prevents native distant pickup from dismissing the tooltip");
    activeItem=nullptr;
    tooltipSystem.rect=&labels[0];Tick(nullptr,0,nullptr);
    Check(pickups==4,"pending asynchronous tooltip pauses pickup before its card is ready");
    tooltipSystem.rect=nullptr;
    activeItem=&labels[0];cardVisible=false;
    legacyManager.active=true;Tick(nullptr,0,nullptr);
    Check(pickups==4,"legacy manager card pauses pickup even when V2 card is inactive");
    legacyManager.active=false;tooltipSystem.locked=true;Tick(nullptr,0,nullptr);
    Check(pickups==4,"controller tooltip lock pauses pickup");
    tooltipSystem.locked=false;
    Tick(nullptr,0,nullptr);
    Check(pickups==6,"hidden V2 card resumes collection even when native getter retains the last item");
    modernFault=true;Due();Tick(nullptr,0,nullptr);modernFault=false;
    Check(pickups==6,"unreadable V2 tooltip fails closed");
    uiFault=true;Due();Tick(nullptr,0,nullptr);uiFault=false;
    Check(pickups==6,"unreadable UI singleton fails closed");
    uiAlive=false;Due();Tick(nullptr,0,nullptr);uiAlive=true;
    Check(pickups==6,"destroyed UI pauses collection");
    systemAlive=false;Due();Tick(nullptr,0,nullptr);systemAlive=true;
    Check(pickups==6,"destroyed tooltip system pauses collection");
    ep::items::SetAuto(0);modernMetadata=false;ep::items::Init();
    Check(ep::items::SetAuto(1).find("hover state was not found")!=std::string::npos,"missing V2 metadata refuses activation");
    modernMetadata=true;
    ep::items::SetAuto(0); collectable = true; hoverReadable = false; ep::items::Init();
    Check(ep::items::SetAuto(1).find("hover state was not found") != std::string::npos &&
          !ep::hook::IsInstalled(reinterpret_cast<void*>(&Tick)), "missing hover metadata refuses activation");
    std::printf("items_test: %d/%d passed\n", passed, total);
    return total - passed;
}
