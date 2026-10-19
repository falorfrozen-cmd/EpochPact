// Execute the real auto-pickup detour, snapshot walk and managed pickup calls
// against a controlled game facade. This is not a live Last Epoch test.
#include <functional>
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
bool inventoryOpen = false;
int tickCalls = 0, pickups = 0, roots = 0;
bool materialLabels = false;
int hoverField, uiField, listField, labelClass, pickupMethod;
void* hovered = nullptr;
void* activeItem = nullptr;
struct Ui { void* cls{}; void* monitor{}; void* system{}; } ui;
int card;
struct LegacyManager {void* cls{};void* monitor{};bool active=false;} legacyManager;
struct TooltipSystem {void* cls{};void* monitor{};void* rect{};void* cardObject=&card;void* manager=&legacyManager;bool locked=false;} tooltipSystem;
void* GameObject(void* object,const ep::il2cpp::Method*){return object;}
bool Visible(void*,const ep::il2cpp::Method*){return cardVisible;}
bool InventoryOpen(void*,const ep::il2cpp::Method*){return inventoryOpen;}
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
    if(!std::strcmp(name,"IsInventoryOpen"))return {&fixture::pickupMethod,reinterpret_cast<void*>(&fixture::InventoryOpen)};
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
bool IsMaterial(void*) { return fixture::materialLabels; }
bool RuleFor(void*, RuleMatch*) { return false; }
int MaximumWeaversWill() { return 28; }
}
namespace mainthread {
bool RunSteps(std::function<bool()> step, unsigned, std::string*) { for (int i = 0; i < 1000; ++i) if (step()) return true; return false; }
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
    // A ground label picked up under the cursor: the field still names that hidden label (and the
    // pool hands it out again for a later drop) with no tooltip behind it.
    hovered=&labels[0];Due();Tick(nullptr,0,nullptr);
    Check(pickups==6,"a newly highlighted item pauses before its tooltip opens");
    ep::items::g_highlightSince-=1000;Due();Tick(nullptr,0,nullptr);
    Check(pickups==8&&ep::items::g_staleHighlight.load()>0,"a highlight left with no tooltip behind it stops pausing");
    legacyManager.active=true;Due();Tick(nullptr,0,nullptr);legacyManager.active=false;
    Check(pickups==8,"the same item hovered again with its tooltip open pauses");
    hovered=&labels[1];Due();Tick(nullptr,0,nullptr);
    Check(pickups==8,"another highlighted item starts a new short pause");
    hovered=nullptr;Due();Tick(nullptr,0,nullptr);hovered=&labels[1];Due();Tick(nullptr,0,nullptr);
    Check(pickups==10,"a highlight cleared and set again starts a new short pause");
    hovered=nullptr;
    ep::items::SetAuto(0);modernMetadata=false;ep::items::Init();
    Check(ep::items::SetAuto(1).find("hover state was not found")!=std::string::npos,"missing V2 metadata refuses activation");
    modernMetadata=true;
    ep::items::SetAuto(0); collectable = true; hoverReadable = false; ep::items::Init();
    Check(ep::items::SetAuto(1).find("hover state was not found") != std::string::npos &&
          !ep::hook::IsInstalled(reinterpret_cast<void*>(&Tick)), "missing hover metadata refuses activation");
    // Inventory full: equipment labels wait, materials continue, explicit sweep retries.
    ep::items::SetAuto(0); hoverReadable = true; ep::items::Init(); ep::items::SetAuto(1);
    ep::items::g_inventoryFull = true; const int fullBefore = pickups; Due(); Tick(nullptr, 0, nullptr);
    Check(pickups == fullBefore && ep::items::g_fullSkipped.load() >= 2, "full inventory skips equipment labels instead of retrying them");
    materialLabels = true; Due(); Tick(nullptr, 0, nullptr);
    Check(pickups == fullBefore + 2, "materials are still collected while the inventory is full");
    materialLabels = false;
    // Opened and closed while an item tooltip stays open, so the hover pause is on throughout.
    legacyManager.active = true; inventoryOpen = true; Tick(nullptr, 0, nullptr);
    Check(ep::items::g_inventoryFull.load(), "the full pause stays while the inventory is open");
    inventoryOpen = false; Tick(nullptr, 0, nullptr);
    Check(!ep::items::g_inventoryFull.load(), "closing the inventory clears the full pause even while a tooltip pauses collection");
    legacyManager.active = false;
    // Collection off (hook out) while the player empties the inventory, then on again.
    ep::items::g_inventoryFull = true; ep::items::SetAuto(0);
    inventoryOpen = true; Tick(nullptr, 0, nullptr); inventoryOpen = false; Tick(nullptr, 0, nullptr);
    Check(ep::items::g_inventoryFull.load(), "with collection off nothing sees the inventory");
    ep::items::SetAuto(1);
    Check(!ep::items::g_inventoryFull.load(), "turning collection on again retries equipment");
    ep::items::g_inventoryFull = true; const int retryBefore = pickups; Due(); Tick(nullptr, 0, nullptr);
    Check(pickups == retryBefore, "equipment waits again once the full pause is set again");
    Check(ep::items::SetInterval(0.1).find("between 0.25 and 10") != std::string::npos &&
          ep::items::SetInterval(2).find("\"intervalSeconds\":2.00") != std::string::npos, "pickup interval is bounded and reported");
    const auto intervalScan = ep::items::g_lastScan.load(); Tick(nullptr, 0, nullptr);
    Check(ep::items::g_lastScan.load() != intervalScan, "a new interval starts with an immediate scan");
    const int slowBefore = pickups; Tick(nullptr, 0, nullptr);
    Check(pickups == slowBefore, "the chosen interval rate-limits the following scan");
    ep::items::g_inventoryFull = true;
    const auto now = ep::items::PickupNow();
    Check(now.find("\"ok\":true") != std::string::npos && !ep::items::g_inventoryFull.load() && pickups > slowBefore,
          "explicit pick-up-now clears the full pause and collects every loaded drop");
    ep::items::SetInterval(0.25);
    {
        // A big drop: 300 labels, the first 270 unwanted. Each scan still looks at
        // no more than 256, but continues where the last one stopped.
        struct BigArray { void* cls{}; void* monitor{}; void* bounds{}; uintptr_t length = 300; void* data[300]{}; };
        struct BigList { void* cls{}; void* monitor{}; void* items{}; int count = 300; };
        static BigArray big; static BigList bigList; static Label many[300];
        for (int i = 0; i < 300; ++i) big.data[i] = &many[i];
        bigList.items = &big;
        auto wanted = [&](void* label) { return static_cast<Label*>(label) - many >= 270; };
        void* out[256]; unsigned long long cursor = 0;
        const int first = ep::items::SnapshotList(&bigList, out, 256, wanted, &cursor);
        const int second = ep::items::SnapshotList(&bigList, out, 256, wanted, &cursor);
        Check(first == 0 && second == 30, "items behind 256 unwanted labels are reached on the next scan");
        int examined = 0; cursor = 0;
        for (int scan = 0; scan < 3; ++scan) ep::items::SnapshotList(&bigList, out, 256, [&](void*) { ++examined; return false; }, &cursor);
        Check(examined == 3 * 256, "every scan still looks at no more than 256 labels");
        unsigned long long fromStart = 0;
        Check(ep::items::SnapshotList(&bigList, out, 256, wanted) == 0, "without a cursor a scan starts at the front, as before");
        auto w = ep::items::rules::NextWindow(300, 256, fromStart);
        auto w2 = ep::items::rules::NextWindow(300, 256, fromStart);
        Check(w.start == 0 && w.count == 256 && w2.start == 256 && w2.count == 256 && fromStart == 212, "the window wraps around the list");
        unsigned long long small = 77;
        auto w3 = ep::items::rules::NextWindow(2, 256, small);
        Check(w3.start == 0 && w3.count == 2 && small == 0, "a short list is read whole from the front");
        unsigned long long empty = 5;
        Check(ep::items::rules::NextWindow(0, 256, empty).count == 0 && empty == 0, "an empty list resets the cursor");
    }
    std::printf("items_test: %d/%d passed\n", passed, total);
    return total - passed;
}
