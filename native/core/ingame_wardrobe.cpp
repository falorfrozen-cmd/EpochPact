// The Wardrobe page (F6; player request 2026-10-29 "Vanity - transmog", and the owner's "like Diablo 4's wardrobe:
// see how every item looks before wearing it"). A mirror of the loaded character (ingame_mirror.cpp), the seven
// slots with the pictures of the looks they show, and every look that fits the selected slot's item in a grid of the
// game's own item pictures, with filters and a search. A click tries a look on at once (transmogpreview: worn, never
// stored, and taken off by itself once the page is left); Apply keeps every look being tried on, Cancel takes them
// off. The transmogread reply is the state; every command answers with it.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include "transmog.hpp"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ep::ingame::view {
namespace {
using JValue = json::Value;
using managed::Invoke;
using managed::Root;
using managed::Value;

constexpr const char* kSlots[]{"helmet", "body", "gloves", "boots", "belt", "weapon", "offhand"};
constexpr const char* kFilters[]{"all", "base", "unique", "set", "owned"};
constexpr int kRows = 4;                 // grid rows on screen
constexpr float kTileH = 104, kTileGap = 7, kMirrorW = 360, kMirrorH = 450, kColumnGap = 24;

std::string SlotName(int i) {
    switch (i) {
        case 0: return Tr("Helmet");
        case 1: return Tr("Body armour");
        case 2: return Tr("Gloves");
        case 3: return Tr("Boots");
        case 4: return Tr("Belt");
        case 5: return Tr("Weapon");
        default: return Tr("Off-hand");
    }
}
std::string FilterName(int i) {
    switch (i) {
        case 0: return Tr("All");
        case 1: return Tr("Base items");
        case 2: return Tr("Uniques");
        case 3: return Tr("Sets");
        default: return Tr("In your bags");
    }
}
bool Flag(const JValue& v) { return v.IsBool() && v.boolean; }
std::string Lower(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}
Color KindColor(const JValue& look) {
    if (Flag(look["set"])) return Hex("#68c47c");
    if (Flag(look["unique"])) return Hex("#d99248");
    return Hex("#d8d0bb");
}

// ------------------------------------------------------------------- the game's item pictures
// ItemData.GetItemSprite (the picture the inventory draws for an item) as a LoadRef of the game's asset bundles:
// ready at once when its bundle is loaded, else a few frames later. Kept while the window lives.
struct Icon { std::unique_ptr<Root> ref, sprite; int state = -1, frames = 0; };  // state 0 loading, 1 ready, -1 none
std::map<std::string, Icon> icons;
struct Waiting { Pin image; std::string key; };
std::vector<Waiting> waiting;

void ShowSprite(void* image, void* sprite) {
    void* sa[]{sprite};
    Invoke(UI("Image", "set_sprite", 1), image, sa);
    bool keep = true;
    void* pa[]{&keep};
    Invoke(UI("Image", "set_preserveAspect", 1), image, pa);
    Tint(image, kWhite);
}
void Load(const JValue& look) {
    const std::string key = look["key"].Str();
    if (key.empty() || icons.count(key) || icons.size() > 2500) return;
    Icon& icon = icons[key];
    std::string why;
    game::Guarded([&] {
        try {
            const auto* get = Find("LE.dll", "", "ItemData", "GetItemSprite", 5, false);
            const auto* load = Find("LE.Addressables.dll", "LE.AssetBundles", "SoftRefExtensions", "CreateLoadRefTypeless", 3, false);
            if (!get || !load) return;
            uint8_t type = static_cast<uint8_t>(look["type"].Num());
            uint16_t sub = static_cast<uint16_t>(look["sub"].Num()), uid = static_cast<uint16_t>(look["uid"].Num());
            bool unique = Flag(look["unique"]);
            int context = 0;  // ItemUIContext.Default
            void* a[]{&type, &sub, &unique, &uid, &context};
            void* soft = Invoke(get, nullptr, a);
            if (!soft) return;
            Root caller(il2cpp::api().string_new("EpochPact wardrobe"));
            int line = 0;
            void* b[]{soft, caller.Get(), &line};
            void* ref = Invoke(load, nullptr, b);
            if (!ref) return;
            icon.ref = std::make_unique<Root>(ref);
            icon.state = 0;
        } catch (const std::exception&) {}
    }, &why);
}
// Each frame while the page is shown: loads that finished, and their pictures into the images waiting for them.
void PollIcons() {
    int blocked = 0;
    for (auto& [key, icon] : icons) {
        if (icon.state != 0) continue;
        std::string why;
        game::Guarded([&] {
            try {
                void* ref = icon.ref->Get();
                int status = Value<int>(managed::Method(ref, "get_Status", 0), ref);  // AssetStatus: 0 error, 1 loading, 2 loaded
                // a picture still loading after a while is waited for, two a frame
                if (status == 1 && ++icon.frames > 45 && blocked < 2) {
                    ++blocked;
                    Invoke(managed::Method(ref, "BlockForLoad", 0), ref);
                    status = Value<int>(managed::Method(ref, "get_Status", 0), ref);
                }
                if (status == 2) {
                    void* sprite = Invoke(managed::Method(ref, "get_TypelessAssetOrNull", 0), ref);
                    const bool isSprite = sprite && std::string(il2cpp::api().class_get_name(il2cpp::api().object_get_class(sprite))) == "Sprite";
                    if (isSprite) icon.sprite = std::make_unique<Root>(sprite);
                    icon.state = isSprite ? 1 : -1;
                } else if (status != 1) {
                    icon.state = -1;
                }
            } catch (const std::exception&) { icon.state = -1; }
        }, &why);
        if (icon.state == 0 && !why.empty()) icon.state = -1;
    }
    for (size_t i = 0; i < waiting.size();) {
        Waiting& w = waiting[i];
        const auto it = icons.find(w.key);
        const bool alive = w.image && game::IsAlive(w.image.Get());
        if (alive && it != icons.end() && it->second.state == 0) { ++i; continue; }
        if (alive && it != icons.end() && it->second.state == 1) game::Guarded([&] { ShowSprite(w.image.Get(), it->second.sprite->Get()); }, nullptr);
        waiting.erase(waiting.begin() + static_cast<std::ptrdiff_t>(i));
    }
}
// A look's picture in a size x size box: drawn now when loaded, else when it arrives.
void ItemPicture(void* parent, float x, float y, float size, const JValue& look) {
    Node n = New("Picture", parent, x, y, size, size);
    void* image = Add(n.go, "UnityEngine.UI.dll", "UnityEngine.UI", "Image");
    Raycast(image, false);
    Tint(image, Color{1, 1, 1, 0});
    Load(look);
    const auto it = icons.find(look["key"].Str());
    if (it == icons.end()) return;
    if (it->second.state == 1) ShowSprite(image, it->second.sprite->Get());
    else if (it->second.state == 0) waiting.push_back({Pin(image), look["key"].Str()});
}

// --------------------------------------------------------------------------------- the view
struct State {
    std::string slot, filter = "all", query, signature;
    int row = 0;
    float yaw = 0;
    bool closeUp = false, dragging = false, busy = false;
    float dragFrom = 0, yawFrom = 0;
    int section = -1;
    // the grid, built again alone while typing and scrolling: its node, the place it is drawn in and the
    // section's hits before its own (the grid's hits come last)
    Pin grid, gridParent;
    size_t gridHits = 0;
    float ox = 0, oy = 0, gx = 0, gy = 0, gw = 0, gh = 0;
};
State st;

std::string IdOf(const JValue* v) { return !v ? "" : v->IsString() ? v->text : v->IsNumber() ? Num(v->number) : ""; }
// The looks of the loaded character (the inventory read names the character the page shows).
const JValue* Looks() {
    const JValue* s = Cached("transmogread");
    if (!s || !(*s)["slots"].IsArray()) return nullptr;
    const JValue* inv = Cached("inventoryread");
    if (inv && !IdOf(inv->Path("player.id")).empty() && IdOf(inv->Path("player.id")) != IdOf(s->Path("player.id"))) return nullptr;
    return s;
}
const JValue* SlotOf(const JValue& data, const std::string& key) {
    for (const auto& x : data["slots"].items)
        if (x["slot"].Str() == key) return &x;
    return nullptr;
}
bool Equipped(const JValue* slot) { return slot && (*slot)["item"].IsObject(); }
// What a slot shows: the look being tried on, else the stored look, else the item's own.
const JValue& Shown(const JValue& slot) {
    if (slot["preview"].IsObject()) return slot["preview"];
    if (slot["look"].IsObject() && Flag(slot["look"]["shown"])) return slot["look"];
    return slot["item"];
}
// What the page is built from; the same signature means nothing to draw again.
std::string Signature() {
    const JValue* data = Looks();
    if (!data) return "none";
    std::string sig = IdOf(data->Path("player.id"));
    for (const auto& x : (*data)["slots"].items)
        sig += "|" + x["item"]["key"].Str() + "," + x["look"]["key"].Str() + "," + x["preview"]["key"].Str() + "," + std::to_string(x["owned"].items.size()) +
               "," + std::to_string(x["options"].items.size());
    return sig;
}
// A base item made for another class: the game has no art of it for this one, the character would not change.
bool Shows(const JValue& look) { return !look["art"].IsBool() || look["art"].boolean; }
// The item's own look counts even when it is made for another class (anyArt).
bool Matches(const JValue& look, const std::string& filter, const std::set<std::string>& owned, bool anyArt = false) {
    if (!anyArt && !Shows(look)) return false;
    const bool unique = Flag(look["unique"]), set = Flag(look["set"]);
    if (filter == "base") return !unique;
    if (filter == "unique") return unique && !set;
    if (filter == "set") return set;
    if (filter == "owned") return owned.count(look["key"].Str()) > 0;
    return true;
}
std::set<std::string> OwnedOf(const JValue& slot) {
    std::set<std::string> owned;
    for (const auto& k : slot["owned"].items) owned.insert(k.Str());
    owned.insert(slot["item"]["key"].Str());  // the item worn is in your bags too
    return owned;
}
// The looks the grid shows: the filter's and the search's, the item's own first.
std::vector<const JValue*> Visible(const JValue& slot) {
    const auto owned = OwnedOf(slot);
    const std::string query = Lower(Trim(st.query)), own = slot["item"]["key"].Str();
    std::vector<const JValue*> out;
    for (const auto& o : slot["options"].items) {
        if (!Matches(o, st.filter, owned, o["key"].Str() == own)) continue;
        if (!query.empty() && Lower(o["name"].Str()).find(query) == std::string::npos) continue;
        out.push_back(&o);
    }
    std::stable_partition(out.begin(), out.end(), [&](const JValue* o) { return (*o)["key"].Str() == own; });
    return out;
}

// Commands: one at a time; each answers with the looks, kept as the transmogread reply.
void Wear(const std::string& line, const std::string& done = "") {
    if (st.busy) return;
    st.busy = true;
    Send(line, [done](const std::string& reply) {
        st.busy = false;
        std::string why;
        if (Refused(reply, &why)) {
            Toast(PlayerError(why), true);
            Forget("transmogread");
            Read("transmogread");
            return;
        }
        if (!done.empty()) Toast(done);
        Keep("transmogread", reply);
    });
}

void DestroyNode(Pin& pin) {
    if (!pin) return;
    void* a[]{GameObjectOf(pin.Get())};
    SetActive(a[0], false);
    Invoke(U("Object", "Destroy", 1), nullptr, a);
    pin.Reset();
}

// A small label on a tile's corner: Trying on, Worn, Own look.
void Chip(void* parent, float x, float y, const std::string& text, Color color) {
    Style s{Face::BodySemi, 8.5f, color};
    s.upper = true;
    s.spacing = .5f;
    s.align = kCenter;
    const float w = TextAdvance(Face::BodySemi, 8.5f, Upper(text), .5f) + 10;
    Fill(parent, "Chip", x, y, w, 15, Hex("#081210e6"), 2);
    Outline(parent, "Chip border", x, y, w, 15, Alpha(color, .7f), 2);
    TextBox(parent, "Chip text", x, y, w, 15, s, text);
}

// ---------------------------------------------------------------------------------- the grid
void BuildGrid(Section& s);
// Work after the current input is handled (a click's hit list is not changed under it).
void Later(void (*work)()) { After(0, work); }
void RebuildGrid() {
    Section* s = FindSection(st.section);
    if (!s || !st.gridParent || !game::IsAlive(st.gridParent.Get())) return;
    DestroyNode(st.grid);
    if (s->hits.size() > st.gridHits) s->hits.resize(st.gridHits);
    BuildGrid(*s);
}
void Tile(const Ctx& g, float x, float y, float w, const JValue& look, const JValue& slot) {
    const std::string key = look["key"].Str(), slotKey = slot["slot"].Str();
    const bool own = key == slot["item"]["key"].Str();
    const bool stored = slot["look"].IsObject() && slot["look"]["key"].Str() == key;
    const bool previewing = slot["preview"].IsObject() && slot["preview"]["key"].Str() == key;
    const bool worn = !slot["preview"].IsObject() && (stored || (own && !slot["look"].IsObject()));
    const Color kind = KindColor(look), back = previewing ? Hex("#14302a") : Hex("#0d1716");
    void* fill = Fill(g.parent, "Tile", x, y, w, kTileH, back, 4);
    Outline(g.parent, "Tile border", x, y, w, kTileH, previewing ? C("accent") : worn ? C("gold") : Alpha(kind, .38f), 4);
    if (previewing || worn) Outline(g.parent, "Tile ring", x + 1, y + 1, w - 2, kTileH - 2, Alpha(previewing ? C("accent") : C("gold"), .45f), 4);
    const float size = 60;
    ItemPicture(g.parent, x + (w - size) / 2, y + 9, size, look);
    Style ns{Face::Body, 10.5f, kind};
    ns.align = kTop;
    ns.wrap = true;
    ns.line = 1.25f;
    void* name = TextBox(g.parent, "Name", x + 5, y + 73, w - 10, kTileH - 76, ns, look["name"].Str());
    int ellipsis = 1;
    void* oa[]{&ellipsis};
    Invoke(T("set_overflowMode", 1), name, oa);
    if (previewing) Chip(g.parent, x + 4, y + 4, Tr("Trying on"), C("accent"));
    else if (worn) Chip(g.parent, x + 4, y + 4, Tr("Worn"), C("gold"));
    else if (own) Chip(g.parent, x + 4, y + 4, Tr("Own look"), Hex("#aebdb7"));
    Hit hit;
    hit.kind = HitKind::Action;
    hit.x = x;
    hit.y = y;
    hit.w = w;
    hit.h = kTileH;
    hit.name = "wardrobe-look-" + key;
    AddHover(hit, fill, back, previewing ? Hex("#1a3a33") : Hex("#173029"));
    const bool backToWorn = slot["preview"].IsObject() && (stored || (own && !slot["look"].IsObject()));
    hit.action = [slotKey, key, previewing, worn, backToWorn] {
        if (previewing || worn) return;
        // the look the slot wears anyway: the try-on ends
        if (backToWorn) Wear("transmogpreview " + slotKey + " none");
        else Wear("transmogpreview " + slotKey + " " + key);
    };
    g.Add(std::move(hit));
}
void BuildGrid(Section& s) {
    Node node = New("Looks grid", st.gridParent.Get(), st.gx, st.gy, st.gw, st.gh);
    st.grid.Set(node.rt);
    const Ctx g{&s.hits, Area::Main, node.rt, st.ox + st.gx, st.oy + st.gy, s.id};
    const JValue* data = Looks();
    const JValue* slot = data ? SlotOf(*data, st.slot) : nullptr;
    if (!Equipped(slot)) {
        EmptyText(node.rt, 0, 40, st.gw, Tr("Equip an item in this slot to choose its look."));
        return;
    }
    const auto list = Visible(*slot);
    const float barW = 8;
    const int columns = std::max(3, static_cast<int>((st.gw - barW - 6 + kTileGap) / (88 + kTileGap)));
    const float tileW = (st.gw - barW - 6 - (columns - 1) * kTileGap) / columns;
    const int total = static_cast<int>(list.size()), rows = (total + columns - 1) / columns, last = std::max(0, rows - kRows);
    st.row = std::clamp(st.row, 0, last);
    const float areaH = kRows * kTileH + (kRows - 1) * kTileGap;
    if (!total) {
        EmptyText(node.rt, 0, 60, st.gw - barW, Tr(Trim(st.query).empty() ? "No look of this kind fits the item." : "No look matches your search."));
    }
    for (int r = 0; r < kRows; ++r)
        for (int col = 0; col < columns; ++col) {
            const int index = (st.row + r) * columns + col;
            if (index >= total) break;
            Tile(g, col * (tileW + kTileGap), r * (kTileH + kTileGap), tileW, *list[static_cast<size_t>(index)], *slot);
        }
    // the scrollbar: where these rows are among all
    const float trackX = st.gw - barW;
    Fill(node.rt, "Scroll track", trackX, 0, barW, areaH, Hex("#0b1312"), 3);
    if (rows > kRows) {
        const float thumbH = std::max(28.f, areaH * kRows / rows), thumbY = (areaH - thumbH) * st.row / std::max(1, last);
        Fill(node.rt, "Scroll thumb", trackX + 1, thumbY, barW - 2, thumbH, Alpha(C("gold"), .55f), 3);
    }
    // below: which of them are shown, and the page buttons
    const float fy = areaH + 10;
    const int first = total ? st.row * columns + 1 : 0, lastShown = std::min(total, (st.row + kRows) * columns);
    Style cs{Face::Body, 12, C("muted")};
    cs.align = kLeft;
    TextBox(node.rt, "Count", 0, fy, st.gw - 120, 26, cs, total ? Fmt("{0}–{1} of {2} looks", {Num(first), Num(lastShown), Num(total)}) : Fmt("{0} looks", {Num(0)}));
    Button down = AddButton(g, st.gw, fy, "\xE2\x96\xBC", Look::Small, [] { ++st.row; Later(RebuildGrid); }, true, "wardrobe-down");
    Button up = AddButton(g, st.gw - down.w - 6, fy, "\xE2\x96\xB2", Look::Small, [] { st.row = std::max(0, st.row - 1); Later(RebuildGrid); }, true, "wardrobe-up");
    if (st.row >= last) SetEnabled(g, down, false);
    if (st.row <= 0) SetEnabled(g, up, false);
}

// ------------------------------------------------------------------------------- the parts
float Head(const Ctx& in, float y, bool enabled, bool read) {
    Style h2{Face::Serif, 27, C("ink")};
    const float lh = LineHeight(h2);
    Button b = AddButton(in, kPanelPad + kPanelInner, y, Tr(read ? "Refresh" : "Read looks"), Look::Normal, [] {
        Forget("transmogread");
        Read("transmogread");
    }, true, "transmog_read");
    if (!enabled) SetEnabled(in, b, false);
    const float row = std::max(lh, b.h);
    Move(b.node.rt, kPanelPad + kPanelInner - b.w, y + (row - b.h) / 2);
    (*in.hits)[static_cast<size_t>(b.hit)].y = in.oy + y + (row - b.h) / 2;
    TextBox(in.parent, "Title", kPanelPad, y + (row - lh) / 2, kPanelInner - b.w - 15, lh, h2, Tr("Wardrobe"));
    return row;
}
// The seven slots: the picture of the look each shows, its name, a mark when a look is stored or tried on.
float SlotStrip(const Ctx& in, float y, const JValue& data) {
    const float gap = 8, w = (kPanelInner - 6 * gap) / 7, box = 64, h = 124;
    const int sid = in.section;
    for (int i = 0; i < 7; ++i) {
        const JValue* slot = SlotOf(data, kSlots[i]);
        const bool has = Equipped(slot), selected = st.slot == kSlots[i];
        const float x = kPanelPad + i * (w + gap);
        void* back = Fill(in.parent, "Slot", x, y, w, h, selected ? Hex("#80e2b116") : Hex("#0a121100"), 4);
        Outline(in.parent, "Slot border", x, y, w, h, selected ? C("accent") : Alpha(C("line"), .55f), 4);
        const float bx = x + (w - box) / 2, by = y + 10;
        Fill(in.parent, "Slot box", bx, by, box, box, Hex("#0b1312"), 4);
        const JValue* shown = has ? &Shown(*slot) : nullptr;
        Outline(in.parent, "Slot box border", bx, by, box, box, shown ? Alpha(KindColor(*shown), .6f) : Alpha(C("line"), .4f), 4);
        if (shown) ItemPicture(in.parent, bx + 4, by + 4, box - 8, *shown);
        if (has && slot->Find("preview") && (*slot)["preview"].IsObject()) Chip(in.parent, x + 4, y + 4, Tr("Trying on"), C("accent"));
        else if (has && (*slot)["look"].IsObject()) {
            Style mark{Face::Symbol, 13, C("gold")};
            mark.align = kCenter;
            TextBox(in.parent, "Look mark", x + w - 20, y + 4, 16, 16, mark, "\xE2\x9C\xA6");
        }
        Style name{Face::BodySemi, 12, has ? C("gold") : C("muted")};
        name.align = kTop;
        TextBox(in.parent, "Slot name", x + 4, by + box + 8, w - 8, 18, name, SlotName(i));
        Style look{Face::Body, 10.5f, shown ? KindColor(*shown) : C("muted")};
        look.align = kTop;
        void* text = TextBox(in.parent, "Slot look", x + 5, by + box + 27, w - 10, 16, look, shown ? (*shown)["name"].Str() : Tr("Empty"));
        int ellipsis = 1;
        void* oa[]{&ellipsis};
        Invoke(T("set_overflowMode", 1), text, oa);
        Hit hit;
        hit.kind = HitKind::Action;
        hit.x = x;
        hit.y = y;
        hit.w = w;
        hit.h = h;
        hit.enabled = has;
        hit.name = std::string("wardrobe-slot-") + kSlots[i];
        AddHover(hit, back, selected ? Hex("#80e2b116") : Hex("#0a121100"), Hex("#80e2b10e"));
        hit.action = [sid, key = std::string(kSlots[i])] {
            if (st.slot == key) return;
            st.slot = key;
            st.row = 0;
            Rebuild(sid);
        };
        in.Add(std::move(hit));
    }
    return h;
}
// The mirror: the character live, turned by dragging or the buttons, whole or close.
float Mirror(const Ctx& in, float x, float y) {
    Fill(in.parent, "Mirror back", x, y, kMirrorW, kMirrorH, Hex("#0b1312"), 4);
    {
        Node n = New("Mirror", in.parent, x + 1, y + 1, kMirrorW - 2, kMirrorH - 2);
        void* raw = Add(n.go, "UnityEngine.UI.dll", "UnityEngine.UI", "RawImage");
        Raycast(raw, false);
        std::string why;
        game::Guarded([&] {
            try {
                void* a[]{MirrorTexture()};
                Invoke(UI("RawImage", "set_texture", 1), raw, a);
            } catch (const std::exception&) {}
        }, &why);
    }
    // a soft floor light and the frame
    Fill(in.parent, "Mirror floor", x + 1, y + kMirrorH - 70, kMirrorW - 2, 69, Hex("#80e2b108"), 4);
    Outline(in.parent, "Mirror border", x, y, kMirrorW, kMirrorH, Hex("#b9955188"), 4);
    Outline(in.parent, "Mirror inner", x + 4, y + 4, kMirrorW - 8, kMirrorH - 8, Hex("#b9955122"), 3);
    Picture(in.parent, "Corner", x + 6, y + 6, 13, 13, "corner.png", false, C("gold"));
    void* br = Picture(in.parent, "Corner", x + kMirrorW - 6, y + kMirrorH - 6, 13, 13, "corner.png", false, C("gold"));
    Turn(TransformOf(br), 180);
    Style hint{Face::Body, 11, Alpha(C("muted"), .85f)};
    hint.align = kCenter;
    TextBox(in.parent, "Mirror hint", x, y + kMirrorH - 26, kMirrorW, 18, hint, Tr("Drag to turn"));
    Hit turn;
    turn.kind = HitKind::Track;
    turn.x = x;
    turn.y = y;
    turn.w = kMirrorW;
    turn.h = kMirrorH;
    turn.name = "wardrobe-mirror";
    turn.drag = [](float px) {
        if (!st.dragging) {
            st.dragging = true;
            st.dragFrom = px;
            st.yawFrom = st.yaw;
        }
        st.yaw = std::fmod(st.yawFrom + (px - st.dragFrom) * .8f + 720.f, 360.f);
    };
    turn.release = [] { st.dragging = false; };
    in.Add(std::move(turn));
    float by = y + kMirrorH + 10, bx = x;
    const int sid = in.section;
    Button left = AddButton(in, bx, by, "\xE2\x86\xBA " + Tr("Turn left"), Look::Quiet, [] { st.yaw = std::fmod(st.yaw - 45 + 360.f, 360.f); }, false, "mirror-left");
    bx += left.w + 6;
    Button front = AddButton(in, bx, by, Tr("Front"), Look::Quiet, [] { st.yaw = 0; }, false, "mirror-front");
    bx += front.w + 6;
    Button right = AddButton(in, bx, by, Tr("Turn right") + " \xE2\x86\xBB", Look::Quiet, [] { st.yaw = std::fmod(st.yaw + 45, 360.f); }, false, "mirror-right");
    Button zoom = AddButton(in, x + kMirrorW, by, Tr(st.closeUp ? "Full body" : "Close-up"), Look::Quiet, [sid] {
        st.closeUp = !st.closeUp;
        Rebuild(sid);
    }, true, "mirror-zoom");
    return by + std::max({left.h, front.h, right.h, zoom.h});
}
// Above the grid: the filters with their counts, the search.
float Filters(Section& s, const Ctx& in, float x, float y, float w, const JValue& slot) {
    const auto owned = OwnedOf(slot);
    const int sid = s.id;
    float bx = x, by = y, rowH = 0;
    for (int i = 0; i < 5; ++i) {
        int count = 0;
        for (const auto& o : slot["options"].items) count += Matches(o, kFilters[i], owned);
        const std::string label = FilterName(i) + "  " + Num(count);
        Button b = AddButton(in, bx, by, label, Look::Small, [sid, f = std::string(kFilters[i])] {
            if (st.filter == f) return;
            st.filter = f;
            st.row = 0;
            Rebuild(sid);
        }, false, std::string("wardrobe-filter-") + kFilters[i]);
        if (bx + b.w > x + w && bx > x) {  // the next line
            bx = x;
            by += rowH + 6;
            Move(b.node.rt, bx, by);
            (*in.hits)[static_cast<size_t>(b.hit)].x = in.ox + bx;
            (*in.hits)[static_cast<size_t>(b.hit)].y = in.oy + by;
        }
        SetSelected(in, b, st.filter == kFilters[i]);
        bx += b.w + 6;
        rowH = std::max(rowH, b.h);
    }
    by += rowH + 10;
    int other = 0;
    for (const auto& o : slot["options"].items) other += !Shows(o);
    if (other) by += Muted(in.parent, x, by, w - 8, Fmt("{0} base items made for other classes are left out: the game has no art of them for your class.", {Num(other)}), 12, 1.45f) + 8;
    Style is{Face::Body, 13, C("ink")};
    MakeInput(in, x, by, w - 8, 34, is, InputLook{Hex("#080e0e"), Hex("#7b745a99")}, st.query, Tr("Search looks…"), false, [](const std::string& text) {
        if (text == st.query) return;
        st.query = text;
        st.row = 0;
        Later(RebuildGrid);
    }, "wardrobe-search");
    return by + 34;
}
// Below: what is tried on or worn in the selected slot, Apply / Cancel / Own look, Reset all looks.
float ActionBar(const Ctx& in, float y, const JValue& data, bool eligible) {
    int previews = 0, stored = 0;
    std::string tried;
    for (const auto& x : data["slots"].items) {
        if (x["preview"].IsObject()) {
            ++previews;
            tried = x["preview"]["name"].Str();
        }
        if (x["look"].IsObject()) ++stored;
    }
    const JValue* slot = SlotOf(data, st.slot);
    Solid(in.parent, "Bar line", kPanelPad, y, kPanelInner, 1, Alpha(C("line"), .5f));
    y += 14;
    std::string text;
    if (previews == 1) text = Fmt("Trying on {0}. Apply keeps it.", {tried});
    else if (previews > 1) text = Fmt("Trying on {0} looks. Apply keeps them all.", {Num(previews)});
    else if (Equipped(slot) && (*slot)["look"].IsObject()) text = Fmt("{0} wears the look of {1}.", {(*slot)["item"]["name"].Str(), (*slot)["look"]["name"].Str()});
    else if (Equipped(slot)) text = Tr("Click a look to try it on.");
    float x = kPanelPad;
    if (previews) {
        Button apply = AddButton(in, x, y, Tr(previews == 1 ? "Apply look" : "Apply looks"), Look::Primary, [] { Wear("transmog all preview", Tr("Looks kept.")); }, false,
                                 "wardrobe-apply");
        x += apply.w + 8;
        Button cancel = AddButton(in, x, y, Tr("Cancel"), Look::Normal, [] { Wear("transmogpreview clear all"); }, false, "wardrobe-cancel");
        x += cancel.w + 14;
        if (!eligible) { SetEnabled(in, apply, false); SetEnabled(in, cancel, false); }
    } else if (Equipped(slot) && (*slot)["look"].IsObject()) {
        Button own = AddButton(in, x, y, Tr("Own look"), Look::Normal, [key = st.slot] { Wear("transmog " + key + " none", Tr("Look changed.")); }, false, "wardrobe-own");
        if (!eligible) SetEnabled(in, own, false);
        x += own.w + 14;
    }
    Button reset = AddButton(in, kPanelPad + kPanelInner, y, Tr("Reset all looks"), Look::Normal, [] { Wear("transmog all none", Tr("Look changed.")); }, true, "transmog-reset");
    if (!stored || !eligible) SetEnabled(in, reset, false);
    Style ts{Face::Body, 13, previews ? C("accent") : C("muted")};
    ts.align = kLeft;
    TextBox(in.parent, "Bar text", x, y, std::max(60.f, kPanelPad + kPanelInner - reset.w - 14 - x), reset.h, ts, text);
    return 14 + reset.h;
}

float Build(Section& s, const Ctx& c) {
    st.section = s.id;
    st.signature = Signature();
    Panel p = OpenPanel(c.parent, kPadX, 0, kPanelW);
    const Ctx in = c.At(p.rt.Get(), kPadX, 0);
    const JValue* data = Looks();
    std::string reason;
    const bool eligible = Eligible(&reason);
    float y = kPanelPad;
    y += Head(in, y, eligible, data != nullptr) + 12;
    y += Muted(in.parent, kPanelPad, y, kPanelInner,
               Tr("Every look that fits your equipped items, with the game's own art. Click one to try it on in the mirror; Apply keeps it, Cancel takes it "
                  "off. A look must be an item of the same type, and it stays through zone changes and restarts until you choose the item's own look "
                  "again. Display only: your items and their stats do not change.")) + 18;
    if (!data) {
        y += EmptyText(in.parent, kPanelPad, y, kPanelInner, eligible ? Tr("Read the looks of the loaded character.") : reason) + 10;
        st.grid.Reset();
        st.gridParent.Reset();
        ClosePanel(p, kPanelW, y + kPanelPad);
        return y + kPanelPad;
    }
    // the selected slot: one with an item
    if (!Equipped(SlotOf(*data, st.slot))) {
        st.slot.clear();
        for (const char* key : {"body", "helmet", "weapon", "gloves", "boots", "belt", "offhand"})
            if (Equipped(SlotOf(*data, key))) { st.slot = key; break; }
    }
    y += SlotStrip(in, y, *data) + 18;
    const float top = y, rx = kPanelPad + kMirrorW + kColumnGap, rw = kPanelInner - kMirrorW - kColumnGap;
    const float mirrorBottom = Mirror(in, kPanelPad, y);
    const JValue* slot = SlotOf(*data, st.slot);
    float gy = top;
    if (Equipped(slot)) gy = Filters(s, in, rx, top, rw, *slot) + 12;
    const float gridH = kRows * kTileH + (kRows - 1) * kTileGap + 10 + 30;
    y = std::max(mirrorBottom, gy + gridH) + 18;
    y += ActionBar(in, y, *data, eligible);
    // the grid comes last: its hits are the section's last, so it can be built again alone
    s.wheels.push_back({in.ox + rx, in.oy + gy, rw, gridH, [](float notches) {
        st.row = std::max(0, st.row - static_cast<int>(std::lround(notches)));
        Later(RebuildGrid);
    }});
    st.gridParent.Set(p.rt.Get());
    st.ox = in.ox;
    st.oy = in.oy;
    st.gx = rx;
    st.gy = gy;
    st.gw = rw;
    st.gh = gridH;
    st.gridHits = s.hits.size();
    BuildGrid(s);
    y += kPanelPad;
    ClosePanel(p, kPanelW, y);
    return y;
}

void MakeWardrobe(Section& s) {
    s.reads.insert("inventoryread");
    s.reads.insert("transmogread");
    // every frame while the page is shown: the mirror, the try-ons kept on, the pictures that arrived
    s.tick = [](Section&) {
        std::string why;
        game::Guarded([&] {
            try { MirrorRender(st.yaw, st.closeUp); } catch (const std::exception&) {}
        }, &why);
        transmog::KeepPreview();
        PollIcons();
    };
    s.onData = [](Section& self, const std::string&) {
        if (self.built && Signature() == st.signature) return;
        Rebuild(self.id);
    };
    s.build = Build;
}
}  // namespace

void ReleaseWardrobe(bool dispose) {
    waiting.clear();
    for (auto& [key, icon] : icons)
        if (dispose && icon.ref) game::Guarded([&] { try { Invoke(managed::Method(icon.ref->Get(), "Dispose", 0), icon.ref->Get()); } catch (const std::exception&) {} }, nullptr);
    icons.clear();
    st.grid.Reset();
    st.gridParent.Reset();
}

void RegisterWardrobeSections() { RegisterSection("wardrobe", MakeWardrobe); }

}  // namespace ep::ingame::view
