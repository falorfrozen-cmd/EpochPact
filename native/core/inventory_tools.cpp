#include "inventory_tools.hpp"
#include "common.hpp"
#include "game.hpp"
#include "lifecycle.hpp"
#include "mainthread.hpp"
#include "managed.hpp"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

// Automatic sorting uses the game's own ItemContainer.TrySort, the call behind the
// inventory's and the stash's Sort buttons (it has the game's own short throttle):
// the inventory when it opens, a stash tab when it is shown. Only item positions
// change; the game saves them as it does after its Sort button.
//
// A window draws its items over a few frames after it opens (ItemContainerUI's
// CreateItemsAsyncLoop works from the item list it had when it began), and Sort takes every
// item out and puts it back. Sorting during that drawing left pictures of the old items
// behind: they looked present but could not be hovered or moved, and other items were drawn
// over them (player report on 0.1.2-dev.35). So a sort waits until the windows showing the
// container have finished drawing, and afterwards, as after Stash all and Sell all, a window
// whose pictures no longer match its container is redrawn with the game's ReDrawWholeContainer.
// Once a second while the inventory or stash is open, the same check repairs pictures left
// behind by anything else.
namespace ep::invtools {
namespace {
using namespace managed;
bool inventoryOn = false, allTabs = false;
int sellKeep = 1;
std::set<int> tabs;
uint64_t nextTick = 0, nextCheck = 0, inventorySorts = 0, tabSorts = 0, faults = 0, redraws = 0, sortsWithoutWindow = 0, repairs = 0;
int staleChecks = 0;
int lastSortedTab = -1;
std::string lastError;
// What was open at the last check, so each opening (or tab change) sorts once.
bool inventoryWasOpen = false, stashWasOpen = false;
void* lastContainer = nullptr;
int lastIndex = -1;
// A sort waiting for its windows: checks counts the ticks since it was requested, ready the
// consecutive ticks at which every window had finished drawing. Two ready ticks also cover a
// window that opened this frame and starts drawing only on the next one.
struct Pending { bool active = false; int checks = 0, ready = 0, tab = -1; };
Pending inventoryPending, tabPending;
constexpr int kReadyTicks = 2, kNoWindowTicks = 8;

std::wstring Path() { return PluginDir() + L"auto-sort.txt"; }
void Load() {
    std::ifstream in(Path());
    std::string key;
    long long value = 0;
    while (in >> key >> value) {
        if (key == "inventory") inventoryOn = value != 0;
        else if (key == "all") allTabs = value != 0;
        else if (key == "sellkeep" && value >= 0 && value <= 2) sellKeep = static_cast<int>(value);
        else if (key == "tab" && value >= 0 && value < 1000000 && tabs.size() < 1000) tabs.insert(static_cast<int>(value));
    }
}
void Save() {
    std::ofstream out(Path(), std::ios::trunc);
    out << "inventory " << (inventoryOn ? 1 : 0) << "\nall " << (allTabs ? 1 : 0) << "\nsellkeep " << sellKeep << "\n";
    for (int tab : tabs) out << "tab " << tab << "\n";
    if (!out) throw std::runtime_error("The automatic sorting settings could not be saved.");
}
size_t F(const char* cls, const char* field, const char* ns = "") {
    static std::map<std::string, size_t> fields;
    const std::string key = std::string(ns) + ":" + cls + ":" + field;
    if (auto it = fields.find(key); it != fields.end()) return it->second;
    return fields.emplace(key, Offset(cls, field, ns)).first->second;
}
bool TrySort(void* container) { return container && Value<bool>(Method(container, "TrySort", 0), container); }

// The ItemContainer whose items a window shows. The inventory window holds the inventory
// itself; the stash window holds the stash holder, whose current stash is tabbed: its active
// tab is the container shown.
void* Shown(void* view) {
    void* c = Invoke(Method(view, "get_Container", 0), view);
    for (int depth = 0; c && depth < 3; ++depth) {
        const auto* cls = il2cpp::api().object_get_class(c);
        if (il2cpp::api().class_get_method_from_name(cls, "get_CurrentContainer", 0)) { c = Invoke(Method(c, "get_CurrentContainer", 0), c); continue; }
        if (il2cpp::api().class_get_method_from_name(cls, "get_CurrentlyActiveTab", 0)) {
            const int tab = Value<int>(Method(c, "get_CurrentlyActiveTab", 0), c);
            const auto list = Entries(Invoke(Method(c, "get_Containers", 0), c));
            c = tab >= 0 && static_cast<size_t>(tab) < list.size() ? list[static_cast<size_t>(tab)] : nullptr;
            continue;
        }
        return c;
    }
    return nullptr;
}
// The open item windows (ItemContainerUI) that show one of these containers.
std::vector<void*> Views(const std::vector<void*>& containers) {
    static const auto find = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2);
    auto* klass = game::FindClass("LE.dll", "", "ItemContainerUI");
    if (!find || !klass) throw std::runtime_error("Item windows unavailable.");
    Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(klass)));
    bool inactive = false; void* a[]{type.Get(), &inactive};
    Root array(Invoke(find, nullptr, a));
    const auto n = Get<uintptr_t>(array.Get(), 0x18);
    if (n > 4096) throw std::runtime_error("Item window count outside supported bounds.");
    std::vector<void*> out;
    for (uintptr_t i = 0; i < n; ++i) {
        void* view = Get<void*>(array.Get(), 0x20 + 8 * i);
        if (!view || !game::IsAlive(view)) continue;
        void* shown = nullptr;
        try { shown = Shown(view); } catch (const std::exception&) { continue; }  // a window without a readable container
        if (shown && std::find(containers.begin(), containers.end(), shown) != containers.end()) out.push_back(view);
    }
    return out;
}
// Finished drawing: initialised, and not inside the asynchronous drawing.
bool Drawn(void* view) {
    return Get<bool>(view, F("ItemContainerUI", "initialised")) && !Get<bool>(view, F("ItemContainerUI", "_initializing"));
}
// The window's pictures (currentlyShown) are not exactly the container's items.
bool Stale(void* view) {
    void* shown = Shown(view);
    if (!shown) return false;
    Root content(Invoke(Method(shown, "GetContent", 0), shown));
    const auto entries = Entries(content.Get());
    const auto pictures = Entries(Get<void*>(view, F("ItemContainerUI", "currentlyShown")));
    if (entries.size() != pictures.size()) return true;
    const std::set<void*> present(entries.begin(), entries.end());
    for (void* ui : pictures) if (!ui || !present.count(Get<void*>(ui, F("InventoryItemUI", "<EntryRef>k__BackingField")))) return true;
    return false;
}
int RedrawViews(const std::vector<void*>& views) {
    int count = 0;
    for (void* view : views) if (Drawn(view) && Stale(view)) { Invoke(Method(view, "ReDrawWholeContainer", 0), view); ++count; }
    redraws += count;
    return count;
}
// One tick of a waiting sort; true when it may sort now.
bool Settled(Pending& p, const std::vector<void*>& views) {
    ++p.checks;
    if (views.empty()) { p.ready = 0; if (p.checks < kNoWindowTicks) return false; ++sortsWithoutWindow; return true; }
    p.ready = std::all_of(views.begin(), views.end(), Drawn) ? p.ready + 1 : 0;
    return p.ready >= kReadyTicks;
}
void* Manager() {
    static const auto instance = game::FindMethod("LE.dll", "", "ItemContainersManager", "get_Instance", 0);
    void* manager = Invoke(instance);
    return manager && Value<bool>(Method(manager, "get_IsFullyInitializedAndLoaded", 0), manager) ? manager : nullptr;
}
void* StashContainer(void* manager) {
    void* holder = Get<void*>(manager, F("ItemContainersManager", "stash"));
    return holder ? Invoke(Method(holder, "get_CurrentContainer", 0), holder) : nullptr;
}

// The inventory, the stash holder's current stash and its tabs.
std::vector<void*> InventoryAndStash(void* manager) {
    void* stash = StashContainer(manager);
    std::vector<void*> containers{Get<void*>(manager, F("ItemContainersManager", "inventory")), stash};
    if (stash) for (void* tab : Entries(Invoke(Method(stash, "get_Containers", 0), stash))) containers.push_back(tab);
    return containers;
}
// Once a second while the inventory or stash is open: a window whose pictures have not matched
// its items at two checks in a row is redrawn, whatever left the pictures behind. Not while an
// item is held on the cursor, and not while a window is still drawing.
void CheckWindows(void* manager) {
    void* cursor = Get<void*>(manager, F("ItemContainersManager", "cursor"));
    if (cursor && Value<bool>(Method(cursor, "HasContent", 0), cursor)) { staleChecks = 0; return; }
    std::vector<void*> stale;
    for (void* view : Views(InventoryAndStash(manager))) if (Drawn(view) && Stale(view)) stale.push_back(view);
    if (stale.empty()) { staleChecks = 0; return; }
    if (++staleChecks < 2) return;
    staleChecks = 0;
    repairs += RedrawViews(stale);
}
void SortTick(void* manager, bool inventoryOpen, bool stashOpen) {
    if (!inventoryOpen) inventoryPending = {};
    else if (!inventoryWasOpen && inventoryOn) inventoryPending = {true};
    inventoryWasOpen = inventoryOpen;
    if (inventoryPending.active) {
        void* inventory = Get<void*>(manager, F("ItemContainersManager", "inventory"));
        const auto views = Views({inventory});
        if (Settled(inventoryPending, views)) {
            inventoryPending = {};
            if (TrySort(inventory)) { ++inventorySorts; RedrawViews(views); }
        }
    }
    if (!stashOpen) { stashWasOpen = false; lastIndex = -1; tabPending = {}; return; }
    if (!allTabs && tabs.empty()) return;
    void* container = StashContainer(manager);
    if (!container) return;
    const int index = Get<int>(container, F("TabbedItemContainer", "currentlyActiveTab"));
    if (!stashWasOpen || container != lastContainer || index != lastIndex) {
        stashWasOpen = true; lastContainer = container; lastIndex = index; tabPending = {};
        void* stash = Invoke(Method(container, "get_LinkedStash", 0), container);
        void* list = stash ? Get<void*>(stash, F("Stash", "Tabs", "LE.Data")) : nullptr;
        if (!list || index < 0 || index >= Value<int>(Method(list, "get_Count", 0), list)) return;
        int at = index; void* a[]{&at};
        void* tab = Invoke(Method(list, "get_Item", 1), list, a);
        if (!tab) return;
        const int id = Get<int>(tab, F("StashTab", "_tabId", "LE.Data"));
        if (!allTabs && !tabs.count(id)) return;
        tabPending = {true, 0, 0, id};
    }
    if (!tabPending.active) return;
    const auto containers = Entries(Invoke(Method(container, "get_Containers", 0), container));
    if (static_cast<size_t>(index) >= containers.size()) { tabPending = {}; return; }
    const auto views = Views({containers[index]});
    if (!Settled(tabPending, views)) return;
    const int id = tabPending.tab;
    tabPending = {};
    if (TrySort(containers[index])) { ++tabSorts; lastSortedTab = id; RedrawViews(views); }
}
void Tick(uint64_t now) {
    if (!game::IsOfflinePlay()) return;
    const auto session = SessionState();
    if (session.state != 4 || session.transitioning) { inventoryWasOpen = stashWasOpen = false; lastIndex = -1; inventoryPending = tabPending = {}; staleChecks = 0; return; }
    static const il2cpp::Field* uiField = game::FindStaticField("LE.dll", "", "UIBase", "instance");
    void* ui = game::StaticObject(uiField);
    if (!ui || !game::IsAlive(ui)) return;
    void* manager = Manager();
    if (!manager) return;
    const bool inventoryOpen = Value<bool>(Method(ui, "IsInventoryOpen", 0), ui), stashOpen = Value<bool>(Method(ui, "IsStashOpen", 0), ui);
    if (inventoryOn || allTabs || !tabs.empty()) SortTick(manager, inventoryOpen, stashOpen);
    if (!inventoryOpen && !stashOpen) staleChecks = 0;
    else if (now >= nextCheck) { nextCheck = now + 1000; CheckWindows(manager); }
}
}  // namespace

namespace {
bool InventoryDrawn() {
    // A window that cannot be read must not block Stash all or Sell all: they then move the
    // items as before this check existed.
    bool drawn = true;
    std::string why;
    if (!game::Guarded([&] {
            void* manager = Manager();
            if (!manager) return;
            const auto views = Views({Get<void*>(manager, F("ItemContainersManager", "inventory"))});
            drawn = std::all_of(views.begin(), views.end(), Drawn);
        }, &why)) { ++faults; lastError = why; }
    return drawn;
}
}  // namespace
bool InventorySettled(int& drawnFrames, int& waits) {
    if (drawnFrames >= 10) return true;
    drawnFrames = InventoryDrawn() ? drawnFrames + 1 : 0;
    if (++waits > 600) throw std::runtime_error("The inventory did not finish opening.");
    return drawnFrames >= 10;
}
int RedrawStale() {
    void* manager = Manager();
    if (!manager) return 0;
    return RedrawViews(Views(InventoryAndStash(manager)));
}

// Runs on the convenience frame callback (the game's UI update), at most every 250 ms.
void Frame() {
    if (lifecycle::Stopping()) return;
    const auto now = GetTickCount64();
    if (now < nextTick) return;
    nextTick = now + 250;
    std::string why;
    if (!game::Guarded([now] { Tick(now); }, &why)) { ++faults; lastError = why; nextTick = now + 5000; }
}
namespace {
std::string State() {
    std::ostringstream out;
    out << "{\"ok\":true,\"autosort\":" << Boolean(inventoryOn) << ",\"autosortAll\":" << Boolean(allTabs) << ",\"sellKeep\":" << sellKeep << ",\"tabs\":[";
    bool comma = false;
    for (int tab : tabs) { if (comma) out << ','; comma = true; out << tab; }
    out << "],\"inventorySorts\":" << inventorySorts << ",\"tabSorts\":" << tabSorts << ",\"lastSortedTab\":" << lastSortedTab
        << ",\"windowRedraws\":" << redraws << ",\"staleRepairs\":" << repairs << ",\"sortsWithoutWindow\":" << sortsWithoutWindow;
    // The open inventory and stash windows: finished drawing, and whether their pictures match.
    std::string windows = "null";
    game::Guarded([&] {
        void* manager = Manager();
        if (!manager) return;
        std::ostringstream w; w << '[';
        bool next = false;
        for (void* view : Views(InventoryAndStash(manager))) {
            if (next) w << ','; next = true;
            const bool drawn = Drawn(view);
            w << "{\"type\":" << Json(il2cpp::api().class_get_name(il2cpp::api().object_get_class(view))) << ",\"drawn\":" << Boolean(drawn)
              << ",\"stale\":" << Boolean(drawn && Stale(view)) << '}';
        }
        windows = w.str() + ']';
    }, nullptr);
    out << ",\"windows\":" << windows << ",\"faults\":" << faults << ",\"error\":" << Json(lastError) << '}';
    return out.str();
}
}  // namespace

bool Init() {
    try { Load(); } catch (...) { return false; }
    return true;
}
std::string Read() { return Run([] { return State(); }); }
std::string Set(const std::string& key, int value) {
    return Run([=] {
        if ((key == "autosort" || key == "autosortall") && (value == 0 || value == 1)) (key == "autosort" ? inventoryOn : allTabs) = value == 1;
        else if (key == "sellkeep" && value >= 0 && value <= 2) sellKeep = value;
        else throw std::runtime_error("use invset autosort|autosortall 0|1 or invset sellkeep 0|1|2");
        Save();
        // Switched on while the inventory or stash is open: sorted at the next check.
        inventoryWasOpen = stashWasOpen = false; lastIndex = -1; lastError.clear();
        return State();
    });
}
std::string SetTab(int tabId, bool on) {
    return Run([=] {
        if (tabId < 0 || tabId >= 1000000) throw std::runtime_error("use invsorttab <tab ID> 0|1");
        if (on && !tabs.count(tabId) && tabs.size() >= 1000) throw std::runtime_error("Too many stash tabs are chosen.");
        if (on) tabs.insert(tabId); else tabs.erase(tabId);
        Save();
        stashWasOpen = false; lastIndex = -1;
        return State();
    });
}
bool KeepWhenSelling(void* item) {
    if (sellKeep == 0 || !item) return false;
    if (Value<bool>(Method(item, "isUniqueSetOrLegendary", 0), item)) return true;
    return sellKeep >= 2 && Value<bool>(Method(item, "isExaltedItem", 0), item);
}
}  // namespace ep::invtools
