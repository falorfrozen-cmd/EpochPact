// Execute the real auto-pickup detour, snapshot walk and managed pickup calls
// against a controlled game facade. This is not a live Last Epoch test.
#include "../core/items.cpp"
#include <cstring>
#include <stdexcept>

namespace fixture {
ep::il2cpp::Api api;
bool offline = true, hoverReadable = true, hoverFault = false, hoverAlive = true;
bool hoverOnPickup = false, collectable = true;
int tickCalls = 0, pickups = 0, roots = 0;
int hoverField, listField, labelClass, pickupMethod;
void* hovered = nullptr;
struct Array { void* cls{}; void* monitor{}; void* bounds{}; uintptr_t length = 2; void* data[2]{}; } array;
struct List { void* cls{}; void* monitor{}; void* items = &array; int count = 2; } list;
struct DList { void* cls{}; void* monitor{}; void* inner = &list; } dlist;
struct Label { void* cls = &labelClass; void* monitor{}; } labels[2];

__declspec(noinline) void Tick(void*, float delta, const ep::il2cpp::Method*) {
    ++tickCalls;
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
    if (!std::strcmp(cls, "DistantItemPickupHandler")) return {&fixture::pickupMethod, reinterpret_cast<void*>(&fixture::Tick)};
    if (!std::strcmp(name, "requestPickup")) return {&fixture::pickupMethod, reinterpret_cast<void*>(&fixture::Invoke)};
    return {};
}
const Field* FindStaticField(const char*, const char*, const char*, const char* field) {
    if (!std::strcmp(field, "highlightedTooltipItem")) return fixture::hoverReadable ? &fixture::hoverField : nullptr;
    if (!std::strcmp(field, "pickableGroundLabelList")) return &fixture::listField;
    return nullptr;
}
const Class* FindClass(const char*, const char*, const char*) { return &fixture::labelClass; }
size_t FieldOffset(const char*, const char*, const char*, const char*) { return 0; }
void* StaticObject(const Field* field) {
    if (field == &fixture::hoverField) {
        if (fixture::hoverFault) throw std::runtime_error("unreadable hover fixture");
        return fixture::hovered;
    }
    if (field == &fixture::listField) return &fixture::dlist;
    return nullptr;
}
bool IsOfflinePlay(bool* known) { if (known) *known = true; return fixture::offline; }
std::string GateText() { return "fixture gate"; }
bool IsAlive(void* obj) { return obj && (obj != fixture::hovered || fixture::hoverAlive); }
}
namespace smartloot {
bool CanCollect() { return fixture::offline && fixture::collectable; }
bool Accept(void*) { return true; }
bool Category(int) { return true; }
}
}

int main() {
    using namespace fixture;
    api.runtime_invoke = Invoke;
    api.gchandle_new = [](void* object, bool) -> uintptr_t { ++roots; return reinterpret_cast<uintptr_t>(object); };
    api.gchandle_free = [](uintptr_t) { --roots; };
    array.data[0] = &labels[0]; array.data[1] = &labels[1];
    ep::items::Init();
    Check(ep::items::SetAuto(1).find("-> on") != std::string::npos, "enable the actual pickup hook");
    hovered = &labels[0]; Due();
    const auto before = ep::items::g_lastScan.load();
    for (int i = 0; i < 100; ++i) Tick(nullptr, 0, nullptr);
    Check(pickups == 0 && tickCalls == 100, "hover pauses mod pickup while vanilla ticks continue");
    Check(ep::items::g_lastScan.load() == before, "hover does not consume the next pickup interval");
    hovered = nullptr; Tick(nullptr, 0, nullptr);
    Check(pickups == 2 && roots == 0, "leaving hover resumes real managed pickups and releases roots");
    Tick(nullptr, 0, nullptr);
    Check(pickups == 2, "collection remains rate limited after a successful scan");
    offline = false; Due(); Tick(nullptr, 0, nullptr);
    Check(pickups == 2, "online mode never collects items");
    offline = true; hoverFault = true; Due(); Tick(nullptr, 0, nullptr);
    Check(pickups == 2 && roots == 0, "unreadable hover state pauses safely");
    hoverFault = false; hoverOnPickup = true; Due(); Tick(nullptr, 0, nullptr);
    Check(pickups == 3 && roots == 0, "tooltip appearing during pickup stops the remaining calls");
    hoverOnPickup = false; hoverAlive = false; Due(); Tick(nullptr, 0, nullptr);
    Check(pickups == 4, "destroyed hover object does not permanently pause collection");
    hovered = nullptr; collectable = false; Due(); Tick(nullptr, 0, nullptr);
    Check(pickups == 4, "unloaded character does not collect");
    ep::items::SetAuto(0); collectable = true; hoverReadable = false; ep::items::Init();
    Check(ep::items::SetAuto(1).find("hover state was not found") != std::string::npos &&
          !ep::hook::IsInstalled(reinterpret_cast<void*>(&Tick)), "missing hover metadata refuses activation");
    std::printf("items_test: %d/%d passed\n", passed, total);
    return total - passed;
}
