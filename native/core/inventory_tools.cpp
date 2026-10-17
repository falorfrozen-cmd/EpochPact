#include "inventory_tools.hpp"
#include "common.hpp"
#include "game.hpp"
#include "lifecycle.hpp"
#include "mainthread.hpp"
#include "managed.hpp"
#include <windows.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

// Automatic sorting uses the game's own ItemContainer.TrySort, the call behind the
// inventory's and the stash's Sort buttons (it has the game's own short throttle):
// the inventory when it opens, a stash tab when it is shown. Only item positions
// change; the game saves them as it does after its Sort button.
namespace ep::invtools {
namespace {
using namespace managed;
bool inventoryOn = false, allTabs = false;
int sellKeep = 1;
std::set<int> tabs;
uint64_t nextTick = 0, inventorySorts = 0, tabSorts = 0, faults = 0;
int lastSortedTab = -1;
std::string lastError;
// What was open at the last check, so each opening (or tab change) sorts once.
bool inventoryWasOpen = false, stashWasOpen = false;
void* lastContainer = nullptr;
int lastIndex = -1;

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

void Tick() {
    if (!game::IsOfflinePlay()) return;
    const auto session = SessionState();
    if (session.state != 4 || session.transitioning) { inventoryWasOpen = stashWasOpen = false; lastIndex = -1; return; }
    static const il2cpp::Field* uiField = game::FindStaticField("LE.dll", "", "UIBase", "instance");
    void* ui = game::StaticObject(uiField);
    if (!ui || !game::IsAlive(ui)) return;
    static const auto instance = game::FindMethod("LE.dll", "", "ItemContainersManager", "get_Instance", 0);
    void* manager = Invoke(instance);
    if (!manager || !Value<bool>(Method(manager, "get_IsFullyInitializedAndLoaded", 0), manager)) return;
    const bool inventoryOpen = Value<bool>(Method(ui, "IsInventoryOpen", 0), ui), stashOpen = Value<bool>(Method(ui, "IsStashOpen", 0), ui);
    if (inventoryOn && inventoryOpen && !inventoryWasOpen && TrySort(Get<void*>(manager, F("ItemContainersManager", "inventory")))) ++inventorySorts;
    inventoryWasOpen = inventoryOpen;
    if (!stashOpen) { stashWasOpen = false; lastIndex = -1; return; }
    if (!allTabs && tabs.empty()) return;
    void* holder = Get<void*>(manager, F("ItemContainersManager", "stash"));
    void* container = holder ? Invoke(Method(holder, "get_CurrentContainer", 0), holder) : nullptr;
    if (!container) return;
    const int index = Get<int>(container, F("TabbedItemContainer", "currentlyActiveTab"));
    if (stashWasOpen && container == lastContainer && index == lastIndex) return;
    stashWasOpen = true; lastContainer = container; lastIndex = index;
    void* stash = Invoke(Method(container, "get_LinkedStash", 0), container);
    void* list = stash ? Get<void*>(stash, F("Stash", "Tabs", "LE.Data")) : nullptr;
    if (!list || index < 0 || index >= Value<int>(Method(list, "get_Count", 0), list)) return;
    int at = index; void* a[]{&at};
    void* tab = Invoke(Method(list, "get_Item", 1), list, a);
    if (!tab) return;
    const int id = Get<int>(tab, F("StashTab", "_tabId", "LE.Data"));
    if (!allTabs && !tabs.count(id)) return;
    const auto containers = Entries(Invoke(Method(container, "get_Containers", 0), container));
    if (static_cast<size_t>(index) < containers.size() && TrySort(containers[index])) { ++tabSorts; lastSortedTab = id; }
}
}  // namespace

// Runs on the convenience frame callback (the game's UI update), at most every 250 ms.
void Frame() {
    if (lifecycle::Stopping() || !(inventoryOn || allTabs || !tabs.empty())) return;
    const auto now = GetTickCount64();
    if (now < nextTick) return;
    nextTick = now + 250;
    std::string why;
    if (!game::Guarded([] { Tick(); }, &why)) { ++faults; lastError = why; nextTick = now + 5000; }
}
namespace {
std::string State() {
    std::ostringstream out;
    out << "{\"ok\":true,\"autosort\":" << Boolean(inventoryOn) << ",\"autosortAll\":" << Boolean(allTabs) << ",\"sellKeep\":" << sellKeep << ",\"tabs\":[";
    bool comma = false;
    for (int tab : tabs) { if (comma) out << ','; comma = true; out << tab; }
    out << "],\"inventorySorts\":" << inventorySorts << ",\"tabSorts\":" << tabSorts << ",\"lastSortedTab\":" << lastSortedTab
        << ",\"faults\":" << faults << ",\"error\":" << Json(lastError) << '}';
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
