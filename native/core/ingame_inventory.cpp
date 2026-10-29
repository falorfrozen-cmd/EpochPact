// The Inventory page (app.js renderPage "inventory"): the shortcuts (stash, vendor, Stash all,
// Sell all), Automatic sorting and Sell all (a setting panel whose tail lists the stash tabs to
// sort) and Stash tabs from loot filter (stashRulesPanel). Game-save actions confirm first and run
// only for the character their data was read for.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace ep::ingame::view {
namespace {
using JValue = json::Value;
using assets::Face;

const Control& Ctl(const std::string& id) {
    static const Control missing;
    const Control* c = ControlById(id);
    return c ? *c : missing;
}
std::string ViewReason() {
    std::string reason;
    Eligible(&reason);
    return reason;
}
std::string SaveReason(const std::string& snapshot) {
    std::string reason;
    if (!Eligible(&reason)) return reason;
    if (!snapshot.empty() && !Cached(snapshot)) return Tr("Refresh live data to enable this action for the loaded character.");
    return "";
}
std::string IdOf(const JValue* v) { return !v ? "" : v->IsString() ? v->text : v->IsNumber() ? Num(v->number) : ""; }
float InPanel(const Ctx& c, const std::function<float(const Ctx& in, float y)>& draw) {
    Panel p = OpenPanel(c.parent, kPadX, 0, kPanelW);
    const Ctx in = c.At(p.rt.Get(), kPadX, 0);
    const float h = draw(in, kPanelPad) + kPanelPad;
    ClosePanel(p, kPanelW, h);
    return h;
}
// .section-head: the h2 and a button at the right, centred on the taller of the two
float HeadWithButton(const Ctx& in, float y, const std::string& title, const std::string& label, Action action, const std::string& name, bool enabled) {
    Style h2{Face::Serif, 27, C("ink")};
    const float lh = LineHeight(h2);
    Button b = AddButton(in, kPanelPad + kPanelInner, y, label, Look::Normal, std::move(action), true, name);
    if (!enabled) SetEnabled(in, b, false);
    const float row = std::max(lh, b.h);
    Move(b.node.rt, kPanelPad + kPanelInner - b.w, y + (row - b.h) / 2);
    (*in.hits)[b.hit].y = in.oy + y + (row - b.h) / 2;
    TextBox(in.parent, "Title", kPanelPad, y + (row - lh) / 2, kPanelInner - b.w - 15, lh, h2, title);
    return row;
}
std::string SaveMessage(const Control& c) { return Tr(c.description) + " " + Tr("This action changes the game save. The backend creates a backup before applying it."); }

// ------------------------------------------------------------------------- inventory shortcuts
void MakeInventoryShortcuts(Section& s) {
    s.reads.insert("inventoryread");
    s.build = [](Section&, const Ctx& c) {
        return InPanel(c, [](const Ctx& in, float y) {
            y += HeadWithButton(in, y, Tr("Inventory shortcuts"), Tr("Refresh inventory"), [] { Read("inventoryread"); }, "inventory_read", true) + 16;
            const JValue* r = Cached("inventoryread");
            const JValue* items = r ? r->Find("inventoryItems") : nullptr;
            y += NoteBox(in.parent, kPanelPad, y, kPanelInner,
                         items && items->IsNumber()
                             ? Fmt("{0} inventory items. Stash all uses the game's placement: tabs whose stash priority matches an item first, otherwise the selected "
                                   "tab; items that do not fit stay in your inventory.", {Num(items->number)})
                             : Tr("Refresh inventory for the loaded character.")) + 18;
            const auto card = [](const std::string& id) -> CardMaker {
                return [id](const Ctx& cc, float x, float yy, float w) {
                    const Control& ctl = Ctl(id);
                    const bool save = ctl.lifetime == "game_save";
                    const std::string action = id.substr(id.find('_') + 1);
                    ActionRun run;
                    run.command = "inventoryaction {save_id} " + action;
                    run.snapshot = save ? "inventoryread" : "";
                    if (save) {
                        run.confirmTitle = Tr(ctl.label);
                        run.confirmMessage = SaveMessage(ctl);
                    }
                    run.success = [action](const JValue& v) -> std::string {
                        const bool opened = v["opened"].IsBool() && v["opened"].boolean;
                        if (action == "stash") return Tr(opened ? "Stash opened in the game." : "Stash closed in the game.");
                        if (action == "vendor") return Tr(opened ? "Vendor opened in the game." : "Vendor closed in the game.");
                        if (action == "stashall") {
                            const double remaining = v["remaining"].Num(0);
                            return remaining > 0 ? Fmt("Moved {0} items to your stash. {1} items stayed in your inventory: the stash had no room for them. Free stash "
                                                       "space or add a stash tab, then try again.", {Num(v["moved"].Num(0)), Num(remaining)})
                                                 : Fmt("Moved {0} items to your stash. Your inventory is empty; equipped items were not moved.", {Num(v["moved"].Num(0))});
                        }
                        // sellall
                        const long long gold = std::llround(v["gold"].Num(0));
                        std::string grouped;
                        const std::string digits = std::to_string(gold < 0 ? -gold : gold);
                        for (size_t i = 0; i < digits.size(); ++i) {
                            if (i && (digits.size() - i) % 3 == 0) grouped += ',';
                            grouped += digits[i];
                        }
                        const double refused = v["refused"].Num(0);
                        return Fmt("Sold {0} items for {1} gold. {2} items kept{3}. Sold items are in the vendor's buyback list.",
                                   {Num(v["sold"].Num(0)), grouped, Num(v["kept"].Num(0)), refused > 0 ? ", " + Num(refused) + " the vendor does not buy" : ""});
                    };
                    return ActionCard(cc, x, yy, w, ctl, ActionCaption(ctl), save, [run] { RunAction(run); }, save ? SaveReason("inventoryread") : ViewReason());
                };
            };
            return y + CardGrid(in, kPanelPad, y, kPanelInner, {card("inventory_stash"), card("inventory_vendor"), card("inventory_stashall"), card("inventory_sellall")});
        });
    };
}

std::string Counted(double n, const char* one, const char* many);
// ---------------------------------------------------------------------------- item categories
// app.js categoriesPanel: each category with its tick (its picked-up items go straight to its tab), how many
// of its items are in the inventory and in its tab, Open tab and Show only; the in-game column choice; Set up
// category tabs, Distribute ticked categories and Open crafting materials. The catread reply is the state.
const char* const kCategoryKeys[]{"unique", "exalted", "idol", "special", "weapon", "armour", "jewellery", "other"};
const char* const kCategoryNames[]{"Unique & Set", "Exalted", "Idols", "Keys & special", "Weapons", "Armour", "Jewellery", "Other"};
std::string CategoryName(const std::string& key) {
    for (size_t i = 0; i < std::size(kCategoryKeys); ++i) if (key == kCategoryKeys[i]) return Tr(kCategoryNames[i]);
    return key;
}
// For the loaded character only (the inventory read names the character the page shows).
const JValue* CurrentCategories() {
    const JValue* s = Cached("catread");
    if (!s || !(*s)["categories"].IsArray()) return nullptr;
    const JValue* inv = Cached("inventoryread");
    if (inv && !IdOf(inv->Path("player.id")).empty() && IdOf(inv->Path("player.id")) != IdOf(s->Path("player.id"))) return nullptr;
    return s;
}
bool ColumnShown() {
    const auto p = Preference("epochpact.categories.v1");
    return !p.IsObject() || !p["column"].IsBool() || p["column"].boolean;
}
void CategoryCommand(const std::string& line) {
    Send(line, [](const std::string& reply) {
        std::string why;
        if (Refused(reply, &why)) Toast(PlayerError(why), true);
        Forget("catread");
        Read("catread");
    });
}
void CategorySave(const std::string& kind) {
    const JValue* s = Cached("catread");
    ActionRun run;
    run.command = kind == "setup" ? "catsetup {save_id}" : "catsort {save_id}";
    run.expectId = s ? IdOf(s->Path("player.id")) : "";
    run.confirmTitle = Tr(Ctl(kind == "setup" ? "category_setup" : "category_sort").label);
    const JValue* inv = Cached("inventoryread");
    const std::string name = inv && inv->Path("player.name") ? inv->Path("player.name")->Str() : "";
    run.confirmMessage = kind == "setup" ? Tr("Create the missing category tabs, free of gold, in a stash folder named EpochPact. A recovery backup is created first.")
                         : name.empty() ? Tr("Move the loaded character's inventory items of the ticked categories into their tabs. Items that do not fit stay in the inventory. A recovery backup is created first.")
                                        : Fmt("Move {0}'s inventory items of the ticked categories into their tabs. Items that do not fit stay in the inventory. A recovery backup is created first.", {name});
    run.success = [kind](const JValue& v) {
        std::vector<std::string> parts;
        if (kind == "setup") {
            parts.push_back(Counted(v["created"].Num(0), "1 tab created", "{0} tabs created"));
            if (v["atLimit"].Num(0)) parts.push_back(Fmt("{0} over the game's tab limit", {Num(v["atLimit"].Num(0))}));
        } else {
            parts.push_back(Counted(v["moved"].Num(0), "1 item moved", "{0} items moved"));
            if (v["full"].Num(0)) parts.push_back(Fmt("{0} did not fit", {Num(v["full"].Num(0))}));
        }
        std::string out;
        for (const auto& p : parts) out += (out.empty() ? "" : ", ") + p;
        return out + ".";
    };
    run.reread = {"catread"};
    RunAction(run);
}
void MakeItemCategories(Section& s) {
    s.reads.insert("inventoryread");
    s.reads.insert("catread");
    s.build = [](Section&, const Ctx& c) {
        return InPanel(c, [](const Ctx& in, float y) {
            const JValue* st = CurrentCategories();
            const bool eligible = ViewReason().empty();
            y += HeadWithButton(in, y, Tr("Item categories"), Tr(st ? "Refresh" : "Read categories"), [] { Forget("catread"); Read("catread"); }, "categories_read",
                                eligible) + 16;
            y += Muted(in.parent, kPanelPad, y, kPanelInner,
                       Tr("Eight categories, each with its own stash tab in a folder named EpochPact. Tick a category to send its items straight to its tab when you "
                          "pick them up; the others stay in your inventory. In the game a column beside the inventory has the same ticks, opens a category's tab "
                          "with one click and can show one category in the inventory with the others faint.")) + 10;
            y += 14;  // .actions > label.inline-check
            CheckField(in, kPanelPad, y, Tr("Show the category column beside the inventory in the game"), ColumnShown(), [](bool on) {
                const std::string text = std::string("{\"column\":") + (on ? "true" : "false") + "}";
                if (!StorePreference("epochpact.categories.v1", &text)) Toast(Tr("Could not save local preferences."), true);
            }, "categories-column");
            y += 18 + 18;
            if (!st) return y + EmptyText(in.parent, kPanelPad, y, kPanelInner, Tr("Read the categories of the loaded character.")) + 10;
            const auto& list = (*st)["categories"].items;
            const std::string filter = (*st)["filter"].Str();
            bool missing = false;
            for (const auto& cat : list) if (cat["tab"].IsNull()) missing = true;
            if (!((*st)["stashLoaded"].IsBool() && (*st)["stashLoaded"].boolean))
                y += NoteBox(in.parent, kPanelPad, y, kPanelInner, Tr("Open the stash once in the game to count the items in each tab.")) + 18;
            if (missing)
                y += NoteBox(in.parent, kPanelPad, y, kPanelInner, Tr("Set up the category tabs first: picked-up items of a category without a tab stay in your inventory.")) + 18;
            // table.categories-table: Picked-up items go to the tab | Inventory | Stash tab | In the tab | (buttons)
            const std::vector<std::string> head{Tr("Picked-up items go to the tab"), Tr("Inventory"), Tr("Stash tab"), Tr("In the tab"), ""};
            const std::vector<float> share{.3f, .11f, .21f, .11f, .27f};
            std::vector<float> widths;
            for (float f : share) widths.push_back(kPanelInner * f);
            const float headH = LineHeight(Style{Face::Body, 12}) + 21, rowH = 46;
            {
                Style th{Face::Body, 12, C("gold")};
                th.align = kLeft;
                float cx = kPanelPad;
                for (size_t i = 0; i < head.size(); ++i) {
                    TextBox(in.parent, "Head", cx + 12, y, widths[i] - 24, headH - 1, th, head[i]);
                    cx += widths[i];
                }
                Solid(in.parent, "Head border", kPanelPad, y + headH - 1, kPanelInner, 1, Hex("#6c755144"));
                y += headH;
            }
            for (const auto& cat : list) {
                const std::string key = cat["key"].Str();
                const bool hasTab = !cat["tab"].IsNull(), only = filter == key;
                Style td{Face::Body, 12, C("ink")};
                td.align = kLeft;
                float cx = kPanelPad;
                CheckField(in, cx + 12, y + (rowH - 18) / 2, CategoryName(key), cat["auto"].IsBool() && cat["auto"].boolean, [key](bool on) {
                    CategoryCommand("catauto " + key + (on ? " 1" : " 0"));
                }, "category-auto-" + key);
                cx += widths[0];
                TextBox(in.parent, "Cell", cx + 12, y, widths[1] - 24, rowH - 1, td, Num(cat["inventory"].Num(0)));
                cx += widths[1];
                TextBox(in.parent, "Cell", cx + 12, y, widths[2] - 24, rowH - 1, td, hasTab ? cat["tab"]["name"].Str() : "\xE2\x80\x94");
                cx += widths[2];
                TextBox(in.parent, "Cell", cx + 12, y, widths[3] - 24, rowH - 1, td, cat["stored"].IsNumber() ? Num(cat["stored"].number) : "\xE2\x80\x94");
                cx += widths[3];
                Button open = AddButton(in, cx + 12, y, Tr("Open tab"), Look::Quiet, [key] { CategoryCommand("catopen " + key); }, false, "category-open-" + key);
                Move(open.node.rt, cx + 12, y + (rowH - open.h) / 2);
                (*in.hits)[open.hit].y = in.oy + y + (rowH - open.h) / 2;
                if (!hasTab || !eligible) SetEnabled(in, open, false);
                Button show = AddButton(in, cx + 12 + open.w + 6, y, Tr(only ? "Show all" : "Show only"), Look::Quiet,
                                        [key, only] { CategoryCommand("catshow " + (only ? std::string("none") : key)); }, false, "category-show-" + key);
                Move(show.node.rt, cx + 18 + open.w, y + (rowH - show.h) / 2);
                (*in.hits)[show.hit].y = in.oy + y + (rowH - show.h) / 2;
                if (!eligible) SetEnabled(in, show, false);
                Solid(in.parent, "Row border", kPanelPad, y + rowH - 1, kPanelInner, 1, Hex("#6c755144"));
                y += rowH;
            }
            // .actions
            y += 18;
            float x = kPanelPad;
            const std::string saveReason = SaveReason("inventoryread");
            if (missing) {
                Button setup = AddButton(in, x, y, Tr("Set up category tabs"), Look::Primary, [] { CategorySave("setup"); }, false, "category_setup");
                if (!saveReason.empty()) SetEnabled(in, setup, false);
                x += setup.w + 10;
            }
            Button sort = AddButton(in, x, y, Tr("Distribute ticked categories"), Look::Normal, [] { CategorySave("sort"); }, false, "category_sort");
            if (!saveReason.empty()) SetEnabled(in, sort, false);
            x += sort.w + 10;
            Button materials = AddButton(in, x, y, Tr("Open crafting materials"), Look::Normal, [] {
                Send("catmaterials", [](const std::string& reply) {
                    std::string why;
                    if (Refused(reply, &why)) { Toast(PlayerError(why), true); return; }
                    JValue v;
                    json::Parse(reply, v);
                    Toast(Tr(v["open"].IsBool() && v["open"].boolean ? "Crafting materials opened in the game." : "Crafting materials closed in the game."));
                });
            }, false, "category_materials");
            if (!eligible) SetEnabled(in, materials, false);
            return y + std::max(sort.h, materials.h);
        });
    };
}

// ---------------------------------------------------------------------------- item looks
// app.js looksPanel: each visible slot of the loaded character with its item and a select of the looks that fit,
// the item's own look first; a choice sends "transmog <slot> <look|none>". The transmogread reply is the state.
const char* const kLookSlots[]{"helmet", "body", "gloves", "boots", "belt", "weapon", "offhand"};
const char* const kLookSlotNames[]{"Helmet", "Body armour", "Gloves", "Boots", "Belt", "Weapon", "Off-hand"};
std::string LookSlotName(const std::string& key) {
    for (size_t i = 0; i < std::size(kLookSlots); ++i) if (key == kLookSlots[i]) return Tr(kLookSlotNames[i]);
    return key;
}
// For the loaded character only (the inventory read names the character the page shows).
const JValue* CurrentLooks() {
    const JValue* s = Cached("transmogread");
    if (!s || !(*s)["slots"].IsArray()) return nullptr;
    const JValue* inv = Cached("inventoryread");
    if (inv && !IdOf(inv->Path("player.id")).empty() && IdOf(inv->Path("player.id")) != IdOf(s->Path("player.id"))) return nullptr;
    return s;
}
void LookCommand(const std::string& line) {
    Send(line, [](const std::string& reply) {
        std::string why;
        if (Refused(reply, &why)) Toast(PlayerError(why), true);
        else Toast(Tr("Look changed."));
        Forget("transmogread");
        Read("transmogread");
    });
}
// The look list of a slot: the item's own look first, then base items, Uniques ("Unique · ") and Sets ("Set · ").
std::string LookLabel(const JValue& o) {
    const std::string name = o["name"].Str();
    if (o["set"].IsBool() && o["set"].boolean) return "Set \xC2\xB7 " + name;
    if (o["unique"].IsBool() && o["unique"].boolean) return "Unique \xC2\xB7 " + name;
    return name;
}
float MirrorYaw(int sectionId) {
    Section* s = FindSection(sectionId);
    if (!s) return 0.f;
    const auto it = s->state.find("yaw");
    return it == s->state.end() ? 0.f : std::strtof(it->second.c_str(), nullptr);
}
void TurnMirror(int sectionId, float by) {
    if (Section* s = FindSection(sectionId)) {
        const float yaw = by == 0 ? 0 : std::fmod(MirrorYaw(sectionId) + by + 360.f, 360.f);
        s->state["yaw"] = std::to_string(yaw);
    }
}
void MakeItemLooks(Section& s) {
    s.reads.insert("inventoryread");
    s.reads.insert("transmogread");
    // the mirror: one frame of the character each frame while the page is shown
    s.tick = [](Section& self) {
        const float yaw = MirrorYaw(self.id);
        std::string why;
        game::Guarded([&] {
            try { MirrorRender(yaw); } catch (const std::exception&) {}
        }, &why);
    };
    s.build = [](Section&, const Ctx& c) {
        return InPanel(c, [](const Ctx& in, float y) {
            const JValue* st = CurrentLooks();
            const bool eligible = ViewReason().empty();
            const int sid = in.section;
            y += HeadWithButton(in, y, Tr("Item looks"), Tr(st ? "Refresh" : "Read looks"), [] { Forget("transmogread"); Read("transmogread"); }, "transmog_read",
                                eligible) + 16;
            y += Muted(in.parent, kPanelPad, y, kPanelInner,
                       Tr("Make an equipped item look like another item of the same type: any base item or Unique of that type, with the game's own art. "
                          "The look stays when you change zone or equip another item of that type, until you choose the item's own look again. "
                          "Shop cosmetics are not touched.")) + 16;
            // the mirror (left) and its turn buttons
            const float mirrorW = 320, mirrorH = 400, gap = 24, top = y;
            Fill(in.parent, "Mirror back", kPanelPad, y, mirrorW, mirrorH, Hex("#0b1312"), 4);
            {
                Node n = New("Mirror", in.parent, kPanelPad + 1, y + 1, mirrorW - 2, mirrorH - 2);
                void* raw = Add(n.go, "UnityEngine.UI.dll", "UnityEngine.UI", "RawImage");
                Raycast(raw, false);
                std::string why;
                game::Guarded([&] {
                    try {
                        void* a[]{MirrorTexture()};
                        managed::Invoke(UI("RawImage", "set_texture", 1), raw, a);
                    } catch (const std::exception&) {}
                }, &why);
            }
            Outline(in.parent, "Mirror border", kPanelPad, y, mirrorW, mirrorH, Hex("#7b745a99"), 4);
            float by = y + mirrorH + 10, bx = kPanelPad;
            Button left = AddButton(in, bx, by, Tr("Turn left"), Look::Quiet, [sid] { TurnMirror(sid, -45); }, false, "mirror-left");
            bx += left.w + 6;
            Button front = AddButton(in, bx, by, Tr("Front"), Look::Quiet, [sid] { TurnMirror(sid, 0); }, false, "mirror-front");
            bx += front.w + 6;
            Button right = AddButton(in, bx, by, Tr("Turn right"), Look::Quiet, [sid] { TurnMirror(sid, 45); }, false, "mirror-right");
            const float mirrorBottom = by + std::max({left.h, front.h, right.h});
            // the slots (right)
            const float sx = kPanelPad + mirrorW + gap, sw = kPanelInner - mirrorW - gap;
            float sy = top;
            if (!st) {
                sy += EmptyText(in.parent, sx, sy, sw, Tr("Read the looks of the loaded character.")) + 10;
                return std::max(sy, mirrorBottom);
            }
            int shown = 0;
            bool anyLook = false;
            for (const auto& slot : (*st)["slots"].items) {
                if (slot["look"].IsObject()) anyLook = true;
                if (slot["item"].IsNull()) continue;
                const std::string key = slot["slot"].Str(), own = slot["item"]["key"].Str();
                const std::string chosen = slot["look"].IsObject() ? slot["look"]["key"].Str() : "";
                std::vector<std::string> labels{Fmt("Own look: {0}", {slot["item"]["name"].Str()})}, keys{"none"};
                int current = 0;
                for (const auto& o : slot["options"].items) {
                    if (o["key"].Str() == own) continue;
                    labels.push_back(LookLabel(o));
                    keys.push_back(o["key"].Str());
                    if (o["key"].Str() == chosen) current = static_cast<int>(keys.size()) - 1;
                }
                sy += SelectField(in, sx, sy, sw, LookSlotName(key) + " \xC2\xB7 " + slot["item"]["name"].Str(), labels, current,
                                  [key, keys](int i) { if (i >= 0 && i < static_cast<int>(keys.size())) LookCommand("transmog " + key + " " + keys[static_cast<size_t>(i)]); },
                                  "transmog-" + key) + 12;
                ++shown;
            }
            if (!shown) sy += EmptyText(in.parent, sx, sy, sw, Tr("Equip an item to change its look.")) + 10;
            sy += 6;
            Button reset = AddButton(in, sx, sy, Tr("Reset all looks"), Look::Normal, [] { LookCommand("transmog all none"); }, false, "transmog-reset");
            if (!anyLook || !eligible) SetEnabled(in, reset, false);
            sy += reset.h;
            return std::max(sy, mirrorBottom);
        });
    };
}

// ---------------------------------------------------------------- stash tabs from loot filter
// The last stashrules reply (any stash rules command answers with the whole state), for its player.
struct StashRules { JValue state; bool loaded = false; bool hidden = false, disabled = false; int section = -1, sortSection = -1; } stash;
std::string StashOwner() { return IdOf(stash.state.Path("player.id")); }
// For the loaded character only (the inventory read names the character the page shows).
const JValue* CurrentRules() {
    if (!stash.loaded) return nullptr;
    const JValue* inv = Cached("inventoryread");
    if (inv && !IdOf(inv->Path("player.id")).empty() && IdOf(inv->Path("player.id")) != StashOwner()) return nullptr;
    return &stash.state;
}
void StashChanged() {
    if (stash.section >= 0) Rebuild(stash.section);
    if (stash.sortSection >= 0) RebuildTail(stash.sortSection);
}
void StashAction(const std::string& kind, const std::string& line, const std::string& confirmTitle, const std::string& confirmMessage,
                 std::function<std::string(const JValue&)> success) {
    ActionRun run;
    run.command = line;
    run.confirmTitle = confirmTitle;
    run.confirmMessage = confirmMessage;
    if (kind != "read") run.expectId = StashOwner();
    run.success = std::move(success);
    run.done = [](const JValue& v) {
        if (!v["rules"].IsArray()) return;
        stash.state = v;
        stash.loaded = true;
        StashChanged();
    };
    RunAction(run);
}
void ReadStashRules() { StashAction("read", "stashrules {save_id}", "", "", {}); }
std::string Counted(double n, const char* one, const char* many) { return n == 1 ? Tr(one) : Fmt(many, {Num(n)}); }
void MakeStashRules(Section& s) {
    s.reads.insert("inventoryread");
    s.build = [](Section& sec, const Ctx& c) {
        stash.section = sec.id;
        return InPanel(c, [](const Ctx& in, float y) {
            const JValue* rules = CurrentRules();
            const bool eligible = ViewReason().empty();
            y += HeadWithButton(in, y, Tr("Stash tabs from loot filter"), Tr(rules ? "Refresh" : "Read filter and tabs"), [] { ReadStashRules(); }, "stash_rules_read",
                                eligible) + 16;
            // p.muted with <b>Sort inventory by filter rule</b>: each text node translated on its own
            {
                Style m{Face::Body, 14, C("muted")};
                m.line = 1.65f;
                m.wrap = true;
                void* t = TextBox(in.parent, "Text", kPanelPad, y, kPanelInner, LineHeight(m), m, "");
                bool on = true;
                void* ra[]{&on};
                managed::Invoke(T("set_richText", 1), t, ra);
                SetText(t, Tr("One stash tab per loot filter rule, free of gold, in a folder named after the filter. Each tab gets the rule's rarity, type, class, "
                              "item level, potential, faction, unique and corruption conditions as its stash priority, so the game's own stashing puts matching "
                              "items there. Rules with affix or character-level conditions match more broadly in the tab priority;") +
                           " <b>" + Tr("Sort inventory by filter rule") + "</b> " + Tr("uses the exact filter match. Every change creates a recovery backup."));
                y += TextLines(t) * LineHeight(m) + 10;
            }
            if (!rules) return y + EmptyText(in.parent, kPanelPad, y, kPanelInner, Tr("Select a loot filter in the game, then read it here.")) + 10;
            const JValue& st = *rules;
            if (!st["filter"].IsString()) return y + NoteBox(in.parent, kPanelPad, y, kPanelInner, Tr("No loot filter is active. Select one in the game's loot filter window.")) + 18;
            const auto& tabs = st["tabs"].items;
            const auto& list = st["rules"].items;
            {
                const bool limit = !(st["canAddTab"].IsBool() && st["canAddTab"].boolean);
                // p.note: Filter: <b>name</b> · n rules · n stash tabs[ · the game's tab limit is reached].
                Node note = New("Note", in.parent, kPanelPad, y, kPanelInner, 10);
                void* bg = Fill(note.rt, "Background", 0, 0, kPanelInner, 10, C("soft"));
                void* bar = Fill(note.rt, "Bar", 0, 0, 2, 10, C("accent"));
                Style ns{Face::Body, 12, C("muted")};
                ns.line = 1.7f;
                ns.wrap = true;
                void* t = TextBox(note.rt, "Text", 18, 12, kPanelInner - 34, LineHeight(ns), ns, "");
                bool on = true;
                void* ra[]{&on};
                managed::Invoke(T("set_richText", 1), t, ra);
                const std::string tail = Fmt(" \xC2\xB7 {0} rules \xC2\xB7 {1} stash tabs{2}.",
                                             {std::to_string(list.size()), std::to_string(tabs.size()), limit ? " \xC2\xB7 the game's tab limit is reached" : ""});
                SetText(t, Tr("Filter:") + " <b><noparse>" + st["filter"].Str() + "</noparse></b>" + tail);
                const float h = 12 + TextLines(t) * LineHeight(ns) + 12;
                Resize(TransformOf(bg), kPanelInner, h);
                Resize(TransformOf(bar), 2, h);
                y += h + 18;
            }
            // table.stash-rules-table: Rule | Type | Name | Stash tab (a select, min-width 150) | Matching
            const std::vector<std::string> head{Tr("Rule"), Tr("Type"), Tr("Name"), Tr("Stash tab"), Tr("Matching")};
            std::vector<std::vector<std::string>> cells;
            std::vector<std::string> tabNames{"\xE2\x80\x94"};
            std::vector<int> tabIds{0};
            for (const auto& t : tabs) {
                tabNames.push_back(t["name"].Str());
                tabIds.push_back(static_cast<int>(t["id"].Num(0)));
            }
            float widestTab = 0;
            for (const auto& n : tabNames) widestTab = std::max(widestTab, TextAdvance(Face::Body, 12, n));
            const float selectW = std::max(150.f, std::ceil(widestTab) + 20 + 24);
            for (const auto& r : list) {
                static const char* types[]{"Show", "Hide", "Highlight"};
                const int type = static_cast<int>(r["type"].Num(-1));
                const bool enabled = r["enabled"].IsBool() && r["enabled"].boolean;
                std::string matching = "\xE2\x80\x94";
                if (!r["unmapped"].items.empty()) {
                    std::string names;
                    for (const auto& u : r["unmapped"].items) names += (names.empty() ? "" : ", ") + u.Str();
                    matching = Tr("Sort only:") + " " + names;
                } else if (!r["mapped"].items.empty()) matching = Tr("Tab priority");
                cells.push_back({Num(r["number"].Num(0)), (type >= 0 && type < 3 ? Tr(types[type]) : Num(type)) + (enabled ? "" : " \xC2\xB7 " + Tr("off")),
                                 r["name"].Str().empty() ? std::string("\xE2\x80\x94") : r["name"].Str(), std::string(), matching});
            }
            auto widths = AutoColumns(kPanelInner, head, cells, 12, 12, 0);
            // the select column: at least its select (min-width 150) and the cell's padding, from Name
            const float need = selectW + 24 - widths[3];
            if (need > 0) { widths[3] += need; widths[2] = std::max(60.f, widths[2] - need); }
            const float rowH = SelectHeight(Style{Face::Body, 12}) + 20 + 1, headH = LineHeight(Style{Face::Body, 12}) + 21;
            {
                Style th{Face::Body, 12, C("gold")};
                th.align = kLeft;
                float cx = kPanelPad;
                for (size_t i = 0; i < head.size(); ++i) {
                    TextBox(in.parent, "Head", cx + 12, y, widths[i] - 24, headH - 1, th, head[i]);
                    cx += widths[i];
                }
                Solid(in.parent, "Head border", kPanelPad, y + headH - 1, kPanelInner, 1, Hex("#6c755144"));
                y += headH;
            }
            for (size_t i = 0; i < list.size(); ++i) {
                const JValue& r = list[i];
                const bool enabled = r["enabled"].IsBool() && r["enabled"].boolean;
                const Color ink = enabled ? C("ink") : Alpha(C("ink"), 0.65f);
                Style td{Face::Body, 12, ink};
                td.align = kLeft;
                float cx = kPanelPad;
                for (size_t col = 0; col < head.size(); ++col) {
                    if (col == 3) {
                        // the rule's tab: a select (Tab for rule n)
                        const int number = static_cast<int>(r["number"].Num(0));
                        const int current = r["tab"].IsNumber() ? static_cast<int>(r["tab"].number) : 0;
                        int at = 0;
                        for (size_t k = 0; k < tabIds.size(); ++k) if (tabIds[k] == current) at = static_cast<int>(k);
                        const float sx = cx + 12, sy = y + 10, sh = SelectHeight(Style{Face::Body, 12});
                        Fill(in.parent, "Select", sx, sy, selectW, sh, Hex("#080e0e"), 3);
                        void* frame = Outline(in.parent, "Select border", sx, sy, selectW, sh, Hex("#7b745a99"), 3);
                        Style cs{Face::Body, 12, ink};
                        cs.align = kLeft;
                        TextBox(in.parent, "Choice", sx + 11, sy, selectW - 30, sh, cs, tabNames[at]);
                        Style ar{Face::Body, 11, ink};
                        ar.align = kCenter;
                        TextBox(in.parent, "Arrow", sx + selectW - 24, sy, 16, sh, ar, "\xE2\x8C\x84");
                        if (eligible) {
                            Hit hit;
                            hit.kind = HitKind::Action;
                            hit.name = "stash-rule-" + std::to_string(number);
                            hit.x = sx;
                            hit.y = sy;
                            hit.w = selectW;
                            hit.h = sh;
                            AddHover(hit, frame, Hex("#7b745a99"), Hex("#7b745a99"));
                            const Ctx at2 = in;
                            hit.action = [at2, sx, sy, selectW, sh, tabNames, tabIds, at, number, current] {
                                float wx = 0, wy = 0;
                                if (!WindowPoint(at2, sx, sy, &wx, &wy)) return;
                                OpenList(wx, wy, selectW, sh, tabNames, at, [tabIds, number, current](int k) {
                                    const int tab = tabIds[k];
                                    // a tab: link it; "—": the previous tab loses the link
                                    if (tab) {
                                        StashAction("assign", "stashruleassign {save_id} " + std::to_string(tab) + " " + std::to_string(number), Tr(Ctl("stash_rule_assign").label),
                                                    Fmt("Give this stash tab the conditions of rule {0} as its stash priority. A recovery backup is created first.", {std::to_string(number)}),
                                                    [number](const JValue&) { return Fmt("Tab linked to rule {0}.", {std::to_string(number)}); });
                                    } else if (current) {
                                        StashAction("assign", "stashruleassign {save_id} " + std::to_string(current) + " 0", Tr(Ctl("stash_rule_assign").label),
                                                    Tr("Remove the rule link and stash priority from this tab. A recovery backup is created first."),
                                                    [](const JValue&) { return Tr("Tab link removed."); });
                                    }
                                });
                            };
                            in.Add(std::move(hit));
                        }
                    } else {
                        TextBox(in.parent, "Cell", cx + 12, y, widths[col] - 24, rowH - 1, td, cells[i][col]);
                    }
                    cx += widths[col];
                }
                Solid(in.parent, "Row border", kPanelPad, y + rowH - 1, kPanelInner, 1, Hex("#6c755144"));
                y += rowH;
            }
            // .actions: Include Hide rules, Include disabled rules (label.inline-check: margin-top 14)
            y += 18 + 14;
            float w1 = 0;
            CheckField(in, kPanelPad, y, Tr("Include Hide rules"), stash.hidden, [](bool on) { stash.hidden = on; StashChanged(); }, "stash-rules-hidden", 12, 8, &w1);
            CheckField(in, kPanelPad + w1 + 10, y, Tr("Include disabled rules"), stash.disabled, [](bool on) { stash.disabled = on; StashChanged(); },
                       "stash-rules-disabled");
            y += 18;
            // .actions: the primary (margin-left auto) and Sort, at the right
            y += 18;
            const std::string name = Cached("inventoryread") ? Cached("inventoryread")->Path("player.name") ? Cached("inventoryread")->Path("player.name")->Str() : "" : "";
            Button sort = AddButton(in, kPanelPad + kPanelInner, y, Tr("Sort inventory by filter rule"), Look::Normal, [name] {
                StashAction("sort", "stashrulesort {save_id}", Tr(Ctl("stash_rules_sort").label),
                            name.empty() ? Tr("Move the loaded character's inventory items into the tabs linked to the loot filter rule they match. Items without a "
                                              "linked rule stay in the inventory. A recovery backup is created first.")
                                         : Fmt("Move {0}'s inventory items into the tabs linked to the loot filter rule they match. Items without a linked rule stay in "
                                               "the inventory. A recovery backup is created first.", {name}),
                            [](const JValue& v) {
                                std::vector<std::string> parts{Counted(v["moved"].Num(0), "1 item moved", "{0} items moved")};
                                if (v["full"].Num(0)) parts.push_back(Fmt("{0} did not fit", {Num(v["full"].Num(0))}));
                                if (v["unlinked"].Num(0)) parts.push_back(Fmt("{0} match a rule without a tab", {Num(v["unlinked"].Num(0))}));
                                std::string out;
                                for (const auto& p : parts) out += (out.empty() ? "" : ", ") + p;
                                return out + ".";
                            });
            }, true, "stash_rules_sort");
            const std::string filter = st["filter"].Str();
            Button create = AddButton(in, kPanelPad + kPanelInner - sort.w - 10, y, Tr("Create tabs for rules without a tab"), Look::Primary, [filter] {
                StashAction("create", "stashrulescreate {save_id} " + std::string(stash.hidden ? "1" : "0") + " " + (stash.disabled ? "1" : "0"),
                            Tr(Ctl("stash_rules_create").label),
                            stash.hidden ? Fmt("Create one free stash tab for each rule of \xE2\x80\x9C{0}\xE2\x80\x9D that has no tab yet, in a folder named after the filter. "
                                               "A recovery backup is created first.", {filter})
                                         : Fmt("Create one free stash tab for each Show/Highlight rule of \xE2\x80\x9C{0}\xE2\x80\x9D that has no tab yet, in a folder named "
                                               "after the filter. A recovery backup is created first.", {filter}),
                            [](const JValue& v) {
                                std::vector<std::string> parts{Counted(v["created"].Num(0), "1 tab created", "{0} tabs created")};
                                if (v["existing"].Num(0)) parts.push_back(Fmt("{0} already linked", {Num(v["existing"].Num(0))}));
                                if (v["atLimit"].Num(0)) parts.push_back(Fmt("{0} over the game's tab limit", {Num(v["atLimit"].Num(0))}));
                                std::string out;
                                for (const auto& p : parts) out += (out.empty() ? "" : ", ") + p;
                                return out + ".";
                            });
            }, true, "stash_rules_create");
            if (!eligible) { SetEnabled(in, sort, false); SetEnabled(in, create, false); }
            const float row = std::max(sort.h, create.h);
            Move(sort.node.rt, kPanelPad + kPanelInner - sort.w, y + (row - sort.h) / 2);
            Move(create.node.rt, kPanelPad + kPanelInner - sort.w - 10 - create.w, y + (row - create.h) / 2);
            (*in.hits)[sort.hit].y = in.oy + y + (row - sort.h) / 2;
            (*in.hits)[create.hit].y = in.oy + y + (row - create.h) / 2;
            return y + row;
        });
    };
}

// ------------------------------------------------------- automatic sorting: the tabs to sort
void RegisterSortTabsTail() {
    Tail tail;
    tail.reads = {"invread", "inventoryread"};
    tail.build = [](Section& s, const Ctx& c) {
        stash.sortSection = s.id;
        // h3 (margin 0 0 12px) right after the cards' grid
        float y = H3(c.parent, kPanelPad, 0, kPanelInner, Tr("Stash tabs sorted automatically")) + 12;
        const JValue* rules = CurrentRules();
        if (!rules) {
            y += EmptyText(c.parent, kPanelPad, y, kPanelInner, Tr("Read your stash tabs to choose which ones are sorted.")) + 10;
            y += 8;  // .actions margin-top 18 after the p's 10
            Button read = AddButton(c, kPanelPad, y, Tr("Read stash tabs"), Look::Normal, [] { ReadStashRules(); }, false, "sort-tabs-read");
            if (!ViewReason().empty()) SetEnabled(c, read, false);
            return y + read.h;
        }
        const auto& tabs = (*rules)["tabs"].items;
        if (tabs.empty()) return y + EmptyText(c.parent, kPanelPad, y, kPanelInner, Tr("You have no stash tabs yet.")) + 10;
        const JValue* inv = Cached("invread");
        const bool all = inv && (*inv)["autosortAll"].IsBool() && (*inv)["autosortAll"].boolean;
        std::set<int> chosen;
        if (inv) for (const auto& t : (*inv)["tabs"].items) chosen.insert(static_cast<int>(t.Num(-1)));
        const int id = s.id;
        for (const auto& t : tabs) {
            const int tab = static_cast<int>(t["id"].Num(0));
            const std::string name = t["name"].Str().empty() ? Fmt("Tab {0}", {std::to_string(tab)}) : t["name"].Str();
            y += 14;  // label.inline-check margin-top 14
            if (all) {
                // checked and disabled: every tab is sorted
                Fill(c.parent, "Check", kPanelPad, y, 18, 18, Alpha(C("accent"), 0.45f), 3);
                Style m{Face::Body, 13, C("bg")};
                m.align = kCenter;
                TextBox(c.parent, "Check mark", kPanelPad, y, 18, 18, m, "\xE2\x9C\x93");
                Style ls{Face::Body, 12, Alpha(C("ink"), 0.6f)};
                ls.align = kLeft;
                TextBox(c.parent, "Check label", kPanelPad + 26, y, 600, 18, ls, name);
            } else {
                CheckField(c, kPanelPad, y, name, chosen.count(tab) > 0, [tab, id](bool on) {
                    Send("invsorttab " + std::to_string(tab) + " " + (on ? "1" : "0"), [](const std::string& reply) {
                        std::string why;
                        if (Refused(reply, &why)) Toast(PlayerError(why), true);
                        Read("invread");
                    });
                }, "sort-tab-" + std::to_string(tab));
            }
            y += 18;
        }
        return y;
    };
    RegisterTail("sort_tabs", std::move(tail));
}
}  // namespace

void RegisterInventorySections() {
    RegisterSection("inventory_shortcuts", MakeInventoryShortcuts);
    RegisterSection("stash_rules", MakeStashRules);
    RegisterSection("item_categories", MakeItemCategories);
    RegisterSection("item_looks", MakeItemLooks);
    RegisterSortTabsTail();
}

}  // namespace ep::ingame::view
