#include "item_pages.hpp"
#include "managed.hpp"
#include "progression.hpp"
#include "ingame_window.hpp"
#include "inventory_tools.hpp"
#include <memory>
#include "report.hpp"
#include "hook.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace ep::itempages {
namespace {
using namespace managed;
struct V2 { int x, y; };
struct Stored { int where = 0, x = 0, y = 0, quantity = 1; std::string data; };  // where: 0 the grid, 1 the altar's slot
using Page = std::vector<Stored>;
struct Book {
    std::string id, name;
    int current[2]{1, 1};
    std::map<int, Page> pages[2];
    bool pending = false;
    int pendingKind = 0, pendingFrom = 0, pendingTo = 0;
    std::string signatureFrom, signatureTo;
    std::vector<std::string> held;  // items on the cursor when a page changed: the game's save does not hold them
};
struct Who { void* manager; std::string id, name; };
Book book;
bool bookLoaded = false;
bool heldThisRun = false;  // the book's held items were seen on the cursor in this run of the game
constexpr int kContextDefault = 0, kContextLoading = 1;

Who Loaded() {
    if (!game::IsOfflinePlay()) throw std::runtime_error(game::GateText());
    RequireSession(4);
    void* actor = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0));
    if (!actor || !game::IsAlive(actor)) throw std::runtime_error("enter a zone with an offline character first");
    void* tracker = Get<void*>(actor, Offset("Actor", "characterDataTracker"));
    void* data = Get<void*>(tracker, Offset("CharacterDataTracker", "charData"));
    void* manager = game::StaticObject(game::FindStaticField("LE.dll", "", "ItemContainersManager", "_instance"));
    if (!manager || !Value<bool>(Method(manager, "get_IsFullyInitializedAndLoaded", 0), manager)) throw std::runtime_error("the inventory is not ready");
    return {manager, Text(Invoke(Method(data, "get_Id", 0), data)), Text(Get<void*>(data, Offset("CharacterData", "<CharacterName>k__BackingField", "LE.Data")))};
}
std::filesystem::path Folder() {
    const auto root = ingame::view::PreferencesFolder();
    if (root.empty()) throw std::runtime_error("no folder for the item pages");
    return root / L"item-pages";
}
std::filesystem::path FileOf(const std::string& id) {
    for (const char ch : id) if (!isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') throw std::runtime_error("unexpected character id");
    return Folder() / (std::wstring(id.begin(), id.end()) + L".txt");
}
void Write(const Book& b) {
    std::filesystem::create_directories(Folder());
    const auto path = FileOf(b.id);
    auto temp = path; temp += L".new";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << "epochpact-item-pages 1\n" << "name " << b.name << "\n";
        for (int kind = 0; kind < 2; ++kind) out << "current " << kind << ' ' << b.current[kind] << "\n";
        if (b.pending) out << "pending " << b.pendingKind << ' ' << b.pendingFrom << ' ' << b.pendingTo << ' ' << b.signatureFrom << ' ' << b.signatureTo << "\n";
        for (int kind = 0; kind < 2; ++kind) for (const auto& [page, items] : b.pages[kind]) for (const auto& s : items)
            out << "item " << kind << ' ' << page << ' ' << s.where << ' ' << s.x << ' ' << s.y << ' ' << s.quantity << ' ' << s.data << "\n";
        for (const auto& data : b.held) out << "held " << data << "\n";
        out << "end\n";
        out.flush();
        if (!out) throw std::runtime_error("the item pages could not be written");
    }
    std::error_code ignored;
    if (std::filesystem::exists(path, ignored)) { auto backup = path; backup += L".bak"; std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, ignored); }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("the item pages could not be stored");
}
bool ReadFile(const std::filesystem::path& path, Book* b) {
    std::ifstream in(path, std::ios::binary);
    std::string word, version;
    if (!(in >> word >> version) || word != "epochpact-item-pages" || version != "1") return false;
    bool complete = false;
    while (in >> word) {
        if (word == "name") { in.get(); std::getline(in, b->name); while (!b->name.empty() && (b->name.back() == '\r' || b->name.back() == ' ')) b->name.pop_back(); }
        else if (word == "current") { int kind = 0, page = 1; in >> kind >> page; if (kind < 0 || kind > 1 || page < 1 || page > kPages) return false; b->current[kind] = page; }
        else if (word == "pending") { b->pending = true; in >> b->pendingKind >> b->pendingFrom >> b->pendingTo >> b->signatureFrom >> b->signatureTo; }
        else if (word == "item") { int kind = 0, page = 0; Stored s; in >> kind >> page >> s.where >> s.x >> s.y >> s.quantity >> s.data; if (!in || kind < 0 || kind > 1 || page < 1 || page > kPages) return false; b->pages[kind][page].push_back(std::move(s)); }
        else if (word == "held") { std::string data; in >> data; if (!in) return false; b->held.push_back(std::move(data)); }
        else if (word == "end") { complete = true; break; }
        else return false;
    }
    return complete;
}
std::vector<void*> Containers(const Who& c, int kind) {
    if (kind == kInventory) return {Get<void*>(c.manager, Offset("ItemContainersManager", "inventory"))};
    // the idols come off before the altar that makes their slots, and the altar goes on first
    return {Get<void*>(c.manager, Offset("ItemContainersManager", "idols")), Get<void*>(c.manager, Offset("ItemContainersManager", "idolsInventory"))};
}
std::string Base64(void* bytes) {
    void* a[]{bytes};
    return Text(Invoke(game::FindMethod("mscorlib.dll", "System", "Convert", "ToBase64String", 1), nullptr, a));
}
void* ItemFrom(const std::string& data) {
    Root text(il2cpp::api().string_new(data.c_str())); void* a[]{text.Get()};
    Root bytes(Invoke(game::FindMethod("mscorlib.dll", "System", "Convert", "FromBase64String", 1), nullptr, a));
    const auto* cls = game::FindClass("LE.dll", "", "ItemData");
    const il2cpp::Method* constructor = nullptr; void* iterator = nullptr;
    while (const auto* m = il2cpp::api().class_get_methods(cls, &iterator)) {
        if (std::string(il2cpp::api().method_get_name(m)) != ".ctor" || il2cpp::api().method_get_param_count(m) != 1) continue;
        char* name = il2cpp::api().type_get_name(il2cpp::api().method_get_param(m, 0));
        const bool match = name && std::string(name) == "System.Byte[]";
        if (name) il2cpp::api().free(name);
        if (match) { constructor = m; break; }
    }
    if (!constructor) throw std::runtime_error("the item constructor is missing");
    void* item = il2cpp::api().object_new(cls);
    Root pin(item); void* ca[]{bytes.Get()};
    Invoke(constructor, item, ca);
    // the game's containers hold the unpacked form: idols are refused without it, and it is what every later use of an item expects
    return Invoke(Method(pin.Get(), "getAsUnpacked", 0), pin.Get());
}
std::vector<std::pair<void*, void*>> Live(const Who& c, int kind) {
    std::vector<std::pair<void*, void*>> out;  // container, entry
    for (void* container : Containers(c, kind)) {
        void* list = Invoke(Method(container, "GetContent", 0), container);
        for (void* entry : Entries(list)) if (entry) out.emplace_back(container, entry);
    }
    return out;
}
Page Snapshot(const Who& c, int kind) {
    Page page;
    const auto containers = Containers(c, kind);
    for (const auto& [container, entry] : Live(c, kind)) {
        Stored s;
        s.where = kind == kIdols && container == containers[1] ? 1 : 0;
        const auto at = Value<V2>(Method(entry, "get_Position", 0), entry);
        s.x = at.x; s.y = at.y;
        s.quantity = Get<int>(entry, Offset("ItemContainerEntry", "quantity"));
        void* item = Get<void*>(entry, Offset("ItemContainerEntry", "data"));
        s.data = Base64(Get<void*>(item, Offset("ItemData", "id")));
        page.push_back(std::move(s));
    }
    return page;
}
std::string Signature(Page page) {
    std::sort(page.begin(), page.end(), [](const Stored& a, const Stored& b) { return std::tie(a.where, a.x, a.y, a.data) < std::tie(b.where, b.x, b.y, b.data); });
    uint64_t hash = 14695981039346656037ull;
    const auto mix = [&](const std::string& t) { for (unsigned char ch : t) { hash ^= ch; hash *= 1099511628211ull; } };
    for (const auto& s : page) mix(std::to_string(s.where) + "," + std::to_string(s.x) + "," + std::to_string(s.y) + "," + std::to_string(s.quantity) + "," + s.data + ";");
    std::ostringstream out; out << std::hex << hash << '-' << page.size();
    return out.str();
}
// A switch that a crash cut short: the save says which side of it the game is on.
void Recover(const Who& c) {
    if (!book.pending) return;
    const int kind = book.pendingKind;
    const auto now = Signature(Snapshot(c, kind));
    if (now == book.signatureTo) {
        book.pages[kind][book.pendingTo].clear();
        book.current[kind] = book.pendingTo;
        Log("item pages: an interrupted switch finished (page %d)", book.pendingTo);
    } else {
        // the game has the old page: its copy in the file goes, or it would be there twice. Anything else keeps the copy: a duplicate, never a loss.
        if (now == book.signatureFrom) book.pages[kind][book.pendingFrom].clear();
        else Log("item pages: after an interruption the game holds neither page; the stored items are kept");
        book.current[kind] = book.pendingFrom;
        Log("item pages: an interrupted switch went back (page %d)", book.pendingFrom);
    }
    book.pending = false;
    Write(book);
}
std::vector<std::string> OnCursor(const Who& c) {
    std::vector<std::string> out;
    void* cursor = Get<void*>(c.manager, Offset("ItemContainersManager", "cursor"));
    if (!cursor || !Value<bool>(Method(cursor, "HasContent", 0), cursor)) return out;
    for (void* entry : Entries(Invoke(Method(cursor, "GetContent", 0), cursor))) if (entry) out.push_back(Base64(Get<void*>(Get<void*>(entry, Offset("ItemContainerEntry", "data")), Offset("ItemData", "id"))));
    return out;
}
bool Known(const Who& c, const std::string& data) {
    for (int kind = 0; kind < 2; ++kind) {
        for (const auto& s : Snapshot(c, kind)) if (s.data == data) return true;
        for (const auto& [page, items] : book.pages[kind]) for (const auto& s : items) if (s.data == data) return true;
    }
    for (const auto& s : OnCursor(c)) if (s == data) return true;
    return false;
}
// The game was gone with an item in the hand and a page changed under it: the save cannot have the item, so it comes back. A duplicate at worst, never a loss.
void SettleHeld(const Who& c) {
    if (book.held.empty() || heldThisRun) return;
    size_t back = 0, waiting = 0;
    for (const auto& data : book.held) {
        if (Known(c, data)) continue;
        void* inventory = Containers(c, kInventory)[0];
        Stored s; s.data = data;
        bool added = false;
        game::Guarded([&] {
            void* item = ItemFrom(data);
            Root pin(item);
            int quantity = 1, context = kContextDefault;
            void* free[]{pin.Get(), &quantity, &context};
            added = Value<bool>(Method(inventory, "TryAddItem", 3), inventory, free);
        }, nullptr);
        if (added) ++back;
        else { book.pages[kInventory][book.current[kInventory] == 1 ? 2 : 1].push_back(std::move(s)); ++waiting; }
    }
    book.held.clear();
    Write(book);
    if (back) progression::SaveCurrent(c.id);
    if (back || waiting) Log("item pages: items left in the hand when the game stopped: %zu in the inventory, %zu on a waiting page", back, waiting);
}
void Open(const Who& c) {
    if (bookLoaded && book.id == c.id && book.name == c.name) { Recover(c); return; }
    heldThisRun = false;
    Book next; next.id = c.id; next.name = c.name;
    const auto path = FileOf(c.id);
    std::error_code ignored;
    if (std::filesystem::exists(path, ignored)) {
        Book found; found.id = c.id;
        bool good = ReadFile(path, &found);
        if (!good) { auto backup = path; backup += L".bak"; found = Book{}; found.id = c.id; good = ReadFile(backup, &found); if (good) Log("item pages: the file of %s was damaged, its backup is used", c.name.c_str()); }
        if (!good) throw std::runtime_error("the item pages file cannot be read; nothing was changed");
        if (found.name == c.name) next = std::move(found);
        else {
            // another character had this id: its items stay in a file of their own
            auto kept = path; kept += L"." + std::to_wstring(GetTickCount64()) + L".other";
            std::filesystem::rename(path, kept, ignored);
            Log("item pages: the file for id %s belongs to %s, kept apart", c.id.c_str(), found.name.c_str());
        }
    }
    book = std::move(next); bookLoaded = true;
    Recover(c);
    SettleHeld(c);
}
// A container's own word on why it refused, for the log.
std::string RefusalOf(void* container) {
    const auto* cls = il2cpp::api().object_get_class(container);
    const auto* field = il2cpp::api().class_get_field_from_name(cls, "lastReason");
    if (!field) return "-";
    const auto at = il2cpp::api().field_get_offset(field);
    const bool has = Get<bool>(container, at);
    return has ? std::to_string(Get<int>(container, at + 4)) : "none";
}
bool Add(void* container, const Stored& s) {
    void* item = ItemFrom(s.data);
    Root pin(item);
    int quantity = s.quantity;
    V2 at{s.x, s.y};
    // the player's way first; then as the game itself puts items back when a character loads
    for (int context : {kContextDefault, kContextLoading}) {
        void* args[]{pin.Get(), &quantity, &at, &context};
        if (Value<bool>(Method(container, "TryAddItem", 4), container, args)) { if (context != kContextDefault) Log("item pages: placed as when loading, at %d,%d", s.x, s.y); return true; }
        Log("item pages: refused at %d,%d in context %d: reason %s, item type %d", s.x, s.y, context, RefusalOf(container).c_str(), static_cast<int>(Get<uint8_t>(pin.Get(), Offset("ItemData", "itemType"))));
    }
    int context = kContextDefault;
    void* free[]{pin.Get(), &quantity, &context};
    return Value<bool>(Method(container, "TryAddItem", 3), container, free);
}
// The next page with room for an item picked up while the shown page is full. The stored items' places are read from their
// own sizes; a place the game refuses later still finds another (Add), and an item with no place keeps waiting.
V2 ItemSize(void* item) {
    Root list(Invoke(game::FindMethod("LE.dll", "", "ItemList", "get", 0)));
    int type = Get<uint8_t>(item, Offset("ItemData", "itemType"));
    void* a[]{&type};
    return Value<V2>(Method(list.Get(), "GetItemSizeFromBaseType", 1), list.Get(), a);
}
bool FindRoom(const Page& items, const V2& grid, const V2& size, V2* at) {
    if (grid.x <= 0 || grid.y <= 0 || size.x <= 0 || size.y <= 0) return false;
    std::vector<char> used(static_cast<size_t>(grid.x) * grid.y, 0);
    for (const auto& s : items) {
        void* item = ItemFrom(s.data);
        Root pin(item);
        const V2 z = ItemSize(pin.Get());
        for (int y = s.y; y < s.y + z.y && y < grid.y; ++y) for (int x = s.x; x < s.x + z.x && x < grid.x; ++x) if (x >= 0 && y >= 0) used[static_cast<size_t>(y) * grid.x + x] = 1;
    }
    // as the game fills a page: from the top left, column by column (y counts up from the bottom row)
    for (int x = 0; x + size.x <= grid.x; ++x) for (int y = grid.y - size.y; y >= 0; --y) {
        bool free = true;
        for (int b = y; free && b < y + size.y; ++b) for (int a = x; a < x + size.x; ++a) if (used[static_cast<size_t>(b) * grid.x + a]) { free = false; break; }
        if (free) { *at = {x, y}; return true; }
    }
    return false;
}
int Overflow(void* self, void* item) {
    if (!invtools::PagesOn() || !item) return 0;
    const auto c = Loaded();
    if (c.manager != self) return 0;
    const int type = Get<uint8_t>(item, Offset("ItemData", "itemType"));
    if (type > 33 && type != 41) return 0;  // gear, idols and altars: the items a page's grid takes
    Open(c);
    if (book.pending) return 0;
    void* inventory = Containers(c, kInventory)[0];
    const V2 grid = Value<V2>(Method(inventory, "GetSize", 0), inventory), size = ItemSize(item);
    const int current = book.current[kInventory];
    for (int step = 1; step < kPages; ++step) {
        const int page = (current - 1 + step) % kPages + 1;
        V2 at{};
        if (!FindRoom(book.pages[kInventory][page], grid, size, &at)) continue;
        Invoke(Method(item, "RebuildID", 0), item);
        Stored s;
        s.x = at.x; s.y = at.y;
        s.data = Base64(Get<void*>(item, Offset("ItemData", "id")));
        book.pages[kInventory][page].push_back(std::move(s));
        Write(book);
        Log("item pages: page %d is full, a picked up item went to page %d at %d,%d", current, page, at.x, at.y);
        return page;
    }
    return 0;
}
using PickupFn = bool (*)(void* self, void* item, void* position, const il2cpp::Method* method);
PickupFn originalPickup = nullptr;
bool Pickup(void* self, void* item, void* position, const il2cpp::Method* method) {
    if (originalPickup(self, item, position, method)) return true;
    int page = 0;
    game::Guarded([&] { page = Overflow(self, item); }, nullptr);
    if (page <= 0) return false;
    static uint64_t nextNotice = 0;
    static int noticedPage = 0;
    const uint64_t now = GetTickCount64();
    if (page != noticedPage || now >= nextNotice) {
        noticedPage = page; nextNotice = now + 4000;
        game::Guarded([&] { report::Notice("EpochPact", "This page is full. Items go to inventory page " + std::to_string(page) + "."); }, nullptr);
    }
    return true;  // the game takes the item off the ground
}
void InstallPickup() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    const auto ref = game::FindMethod("LE.dll", "", "ItemContainersManager", "attemptToPickupItem", 2);
    std::string why;
    if (!ref || !(hook::IsInstalled(ref.code) || hook::Install(ref.code, reinterpret_cast<void*>(&Pickup), reinterpret_cast<void**>(&originalPickup), &why)))
        Log("item pages: the pickup hook is out: %s", why.c_str());
}
// ---- Every idol page counts. The idols of the waiting idol pages give their stats through the game's own
// ItemEquipManager.UpdateStats, the call the idol grid makes for an idol it accepts (modifiers 0: no refracted slot).
// The altar of a waiting page gives its stats the same way (the game applies an altar by the same call); its slots do not matter. Nothing of it is saved: the stats are
// put on again after a switch, a load, or a reset of the game's list of equipped items.
struct Extra { std::unique_ptr<Root> item; };
std::vector<Extra> extras;
void* extrasEquip = nullptr;   // the ItemEquipManager they are on
std::string extrasKey;         // character, current idol page and the waiting idols they were made from
void UpdateItem(void* equip, void* item, bool adding) {
    bool unequipping = !adding; float none = 0;
    void* args[]{item, &adding, &unequipping, &none, &none, &none, &none};
    Invoke(game::FindMethod("LE.dll", "", "ItemEquipManager", "UpdateStats", 7), equip, args);
}
void TakeOffExtras() {
    if (extrasEquip && game::IsAlive(extrasEquip))
        for (auto& e : extras) if (e.item && e.item->Get()) game::Guarded([&] { UpdateItem(extrasEquip, e.item->Get(), false); }, nullptr);
    extras.clear(); extrasEquip = nullptr; extrasKey.clear();
}
std::string ExtrasKey(const Who& c) {
    std::string key = c.id + "|" + std::to_string(book.current[kIdols]);
    for (const auto& [page, items] : book.pages[kIdols]) {
        if (page == book.current[kIdols]) continue;
        for (const auto& s : items) key += "|" + std::to_string(page) + ":" + std::to_string(s.where) + ":" + s.data.substr(0, 64) + std::to_string(s.data.size());
    }
    return key;
}
bool ExtrasStillOn() {
    if (extras.empty() || !extrasEquip || !game::IsAlive(extrasEquip)) return !extras.empty() ? false : true;
    bool inList = false; void* args[]{extras.front().item->Get(), &inList};
    Invoke(game::FindMethod("LE.dll", "", "ItemEquipManager", "ItemIsEquippedWithStatsApplied", 2), extrasEquip, args);
    return inList;
}
size_t SyncExtras(const Who& c) {
    void* equip = Get<void*>(c.manager, Offset("ItemContainersManager", "itemEquipManager"));
    const bool on = invtools::IdolPagesAll() && equip && !book.pending;
    const std::string key = on ? ExtrasKey(c) : std::string();
    const bool still = equip == extrasEquip && ExtrasStillOn();
    if (on && still && key == extrasKey) return extras.size();
    if (still) TakeOffExtras();
    else { extras.clear(); extrasEquip = nullptr; extrasKey.clear(); }  // another character, or the game dropped them itself
    if (!on) return 0;
    for (const auto& [page, items] : book.pages[kIdols]) {
        if (page == book.current[kIdols]) continue;
        for (const auto& s : items) {
            game::Guarded([&] {
                Extra e; e.item = std::make_unique<Root>(ItemFrom(s.data));
                UpdateItem(equip, e.item->Get(), true);
                extras.push_back(std::move(e));
            }, nullptr);
        }
    }
    extrasEquip = equip; extrasKey = key;
    if (!extras.empty()) Log("item pages: %zu idols and altars of waiting idol pages give their stats", extras.size());
    return extras.size();
}
}  // namespace

std::string Switch(Kind kind, int page) {
    return Run([=]() -> std::string {
        if (page < 1 || page > kPages) throw std::runtime_error("no such page");
        const auto c = Loaded();
        Open(c);
        const int from = book.current[kind];
        if (from == page) return "{\"ok\":true,\"kind\":" + std::to_string(kind) + ",\"page\":" + std::to_string(page) + ",\"moved\":0}";
        if (kind == kIdols) TakeOffExtras();
        const auto containers = Containers(c, kind);
        const auto leaving = Snapshot(c, kind);
        Page arriving = book.pages[kind][page];
        // 1. The file first: both pages are safe in it before anything leaves the game.
        book.pages[kind][from] = leaving;
        book.pending = true; book.pendingKind = kind; book.pendingFrom = from; book.pendingTo = page;
        book.signatureFrom = Signature(leaving); book.signatureTo = Signature(arriving);
        book.held = OnCursor(c);
        heldThisRun = !book.held.empty();
        Write(book);
        // 2. Out of the game's containers, by the game's own calls.
        size_t removed = 0;
        std::string failure;
        for (const auto& [container, entry] : Live(c, kind)) {
            int quantity = Get<int>(entry, Offset("ItemContainerEntry", "quantity")), context = kContextDefault;
            void* args[]{entry, &quantity, &context};
            if (!Value<bool>(Method(container, "TryRemoveItem", 3), container, args)) { failure = "an item could not be taken out"; break; }
            ++removed;
        }
        if (!failure.empty()) {
            // back as it was: what is still in the game stays, what came out returns
            const auto still = Signature(Snapshot(c, kind));
            for (const auto& s : leaving) {
                bool there = false;
                for (const auto& t : Snapshot(c, kind)) if (t.where == s.where && t.x == s.x && t.y == s.y && t.data == s.data) there = true;
                if (!there) Add(containers[s.where], s);
            }
            (void)still;
            book.pages[kind][from].clear();
            book.pending = false;
            Write(book);
            if (kind == kIdols) SyncExtras(c);
            throw std::runtime_error(failure + "; nothing changed");
        }
        // 3. Into them: the altar first, because it makes the idols' slots.
        std::stable_sort(arriving.begin(), arriving.end(), [](const Stored& a, const Stored& b) { return a.where > b.where; });
        Page left;
        for (const auto& s : arriving) {
            void* container = containers[s.where < static_cast<int>(containers.size()) ? s.where : 0];
            bool added = false;
            game::Guarded([&] { added = Add(container, s); }, nullptr);
            if (!added) left.push_back(s);
        }
        // 4. The file says it is over, then the game saves.
        book.pages[kind][page] = left;
        book.current[kind] = page;
        book.pending = false;
        Write(book);
        progression::SaveCurrent(c.id);
        const size_t counting = kind == kIdols ? SyncExtras(c) : 0;
        Log("item pages: %s page %d -> %d, %zu out, %zu in, %zu waiting", kind == kInventory ? "inventory" : "idol", from, page, removed, arriving.size() - left.size(), left.size());
        std::ostringstream out;
        out << "{\"ok\":true,\"kind\":" << kind << ",\"from\":" << from << ",\"page\":" << page << ",\"moved\":" << removed << ",\"in\":" << arriving.size() - left.size() << ",\"waiting\":" << left.size() << ",\"idolsCounting\":" << counting << "}";
        return out.str();
    });
}
std::string Read(bool full) {
    return Run([=]() -> std::string {
        const auto c = Loaded();
        Open(c);
        std::ostringstream out;
        out << "{\"ok\":true,\"name\":" << Json(c.name) << ",\"kinds\":[";
        for (int kind = 0; kind < 2; ++kind) {
            const auto now = Snapshot(c, kind);
            out << (kind ? "," : "") << "{\"current\":" << book.current[kind] << ",\"live\":" << now.size() << ",\"signature\":\"" << Signature(now) << "\",\"pages\":[";
            for (int page = 1; page <= kPages; ++page) out << (page > 1 ? "," : "") << (page == book.current[kind] ? now.size() : book.pages[kind][page].size());
            out << "]";
            if (full) { out << ",\"stored\":{"; bool comma = false; for (const auto& [page, items] : book.pages[kind]) { out << (comma ? "," : "") << '"' << page << "\":\"" << Signature(items) << '"'; comma = true; } out << "}"; }
            out << "}";
        }
        out << "],\"pending\":" << Boolean(book.pending) << ",\"held\":" << book.held.size() << ",\"idolsAll\":" << Boolean(invtools::IdolPagesAll())
            << ",\"idolsCounting\":" << (extrasKey.rfind(c.id + "|", 0) == 0 ? extras.size() : 0) << "}";
        return out.str();
    });
}
std::string ToStash() {
    return Run([=]() -> std::string {
        const auto c = Loaded();
        Open(c);
        TakeOffExtras();
        void* holder = Get<void*>(c.manager, Offset("ItemContainersManager", "stash"));
        void* stash = holder ? Invoke(Method(holder, "get_ActiveContainer", 0), holder) : nullptr;
        if (!stash) throw std::runtime_error("the stash is not ready");
        const auto tabs = Entries(Invoke(Method(stash, "get_Containers", 0), stash));
        size_t moved = 0, left = 0;
        for (int kind = 0; kind < 2; ++kind) for (auto& [page, items] : book.pages[kind]) {
            Page keep;
            for (const auto& s : items) {
                bool placed = false;
                for (void* tab : tabs) {
                    if (!tab) continue;
                    void* item = ItemFrom(s.data);
                    Root pin(item);
                    int quantity = s.quantity, context = kContextDefault;
                    void* args[]{pin.Get(), &quantity, &context};
                    if (Value<bool>(Method(tab, "TryAddItem", 3), tab, args)) { placed = true; break; }
                }
                if (placed) ++moved; else { keep.push_back(s); ++left; }
                // one at a time in the file: a crash between two costs a duplicate at worst
            }
            items = std::move(keep);
        }
        Write(book);
        progression::SaveCurrent(c.id);
        SyncExtras(c);
        Log("item pages: %zu waiting items went to the stash, %zu had no room", moved, left);
        return "{\"ok\":true,\"moved\":" + std::to_string(moved) + ",\"noRoom\":" + std::to_string(left) + "}";
    });
}
int Current(Kind kind) {
    int page = 0;
    game::Guarded([&] { const auto c = Loaded(); Open(c); page = book.current[kind]; }, nullptr);
    return page;
}
int Count(Kind kind, int page) {
    int count = 0;
    game::Guarded([&] {
        const auto c = Loaded(); Open(c);
        count = static_cast<int>(page == book.current[kind] ? Snapshot(c, kind).size() : book.pages[kind][page].size());
    }, nullptr);
    return count;
}
void Frame() {
    // the first look at a character after it loads: a switch cut short by a crash is settled before the player touches anything
    static uint64_t next = 0;
    const uint64_t now = GetTickCount64();
    if (now < next) return;
    next = now + 2000;
    game::Guarded([] { InstallPickup(); }, nullptr);
    game::Guarded([] {
        void* actor = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0));
        if (!actor || !game::IsOfflinePlay()) return;
        const auto c = Loaded();
        if (!bookLoaded || book.id != c.id || book.name != c.name) Open(c);
        if (!book.held.empty() && OnCursor(c).empty()) {
            book.held.clear();
            heldThisRun = false;
            progression::SaveCurrent(c.id);
            Write(book);
        }
        SyncExtras(c);
    }, nullptr);
}
}  // namespace ep::itempages
