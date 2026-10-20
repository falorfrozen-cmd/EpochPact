// Native sections of the in-game window: the desktop app's panels that read live data or run
// actions (app.js renderPage), drawn with the kit in ingame_window.hpp. Each is registered by its
// ui.json "native" kind (tools/ingame_pages.py) and builds again when its reads change.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
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
std::string IdOf(const JValue* v) { return !v ? "" : v->IsString() ? v->text : v->IsNumber() ? Num(v->number) : ""; }
// formAction's generic confirm for a game-save action
std::string SaveMessage(const Control& c) {
    return Tr(c.description) + " " + Tr("This action changes the game save. The backend creates a backup before applying it.");
}
// saveActionReason: offline and ready, and the section's data read for this character
std::string SaveReason(const std::string& snapshot) {
    std::string reason;
    if (!Eligible(&reason)) return reason;
    if (!snapshot.empty() && !Cached(snapshot)) return Tr("Refresh live data to enable this action for the loaded character.");
    return "";
}
std::string ViewReason() {
    std::string reason;
    Eligible(&reason);
    return reason;
}
// A panel filling the content width; draw gets the panel's ctx and the y below its padding and
// returns the y where the content ends.
float InPanel(const Ctx& c, const std::function<float(const Ctx& in, float y)>& draw) {
    Panel p = OpenPanel(c.parent, kPadX, 0, kPanelW);
    const Ctx in = c.At(p.rt.Get(), kPadX, 0);
    const float h = draw(in, kPanelPad) + kPanelPad;
    ClosePanel(p, kPanelW, h);
    return h;
}
// A game-save action card (badge Game save, primary "Apply action", generic confirm).
CardMaker SaveCard(const std::string& id, const std::string& command, const std::string& snapshot, std::function<std::string(const JValue&)> success,
                   std::vector<std::string> reread, const std::string& extraReason = "") {
    return [=](const Ctx& cc, float x, float y, float w) {
        const Control& c = Ctl(id);
        std::string reason = SaveReason(snapshot);
        if (reason.empty()) reason = extraReason;
        ActionRun run;
        run.command = command;
        run.snapshot = snapshot;
        run.confirmTitle = Tr(c.label);
        run.confirmMessage = SaveMessage(c);
        run.success = success;
        run.reread = reread;
        return ActionCard(cc, x, y, w, c, ActionCaption(c), true, [run] { RunAction(run); }, reason);
    };
}
std::string Updated(const Control& c) { return Fmt("{0} updated.", {Tr(c.label)}); }

// ----------------------------------------------------------------------- cachedSummary(read)
// <div class="note">Live reading · {name}[ · Level {n}]</div>, or the "Refresh live data" note.
void MakeCachedSummary(Section& s) {
    const std::string read = (*s.spec)["read"].Str();
    s.reads.insert(read);
    s.after = 18;
    s.build = [read](Section&, const Ctx& c) {
        const JValue* r = Cached(read);
        const JValue* name = r ? r->Path("player.name") : nullptr;
        std::string text;
        if (!name || !name->IsString()) text = Tr("Refresh live data. Load an offline character, then refresh to see current values.");
        else {
            text = Tr("Live reading") + " \xC2\xB7 " + name->text;
            const JValue* level = r->Path("player.level");
            if (level && level->IsNumber()) text += " \xC2\xB7 " + Fmt("Level {0}", {Num(level->number)});
            const JValue* editable = r->Find("editable");
            if (read == "monolithread" && editable && editable->IsBool() && !editable->boolean)
                text += " \xC2\xB7 " + Tr("Monolith editing is unavailable in this area.");
        }
        return NoteBox(c.parent, kPadX, 0, kPanelW, text);
    };
}

// --------------------------------------------------------------------------- Skills & Trees
void MakeSkillBudgets(Section& s) {
    s.reads.insert("buildread");
    s.build = [](Section&, const Ctx& c) {
        return InPanel(c, [](const Ctx& in, float y) {
            y += SectionHead(in, kPanelPad, y, kPanelInner, Tr("Skill and point budgets"), {{Tr("Refresh live"), [] { Read("buildread"); }}}) + 16;
            const JValue* r = Cached("buildread");
            const auto value = [&](const char* key) {
                const JValue* v = r ? r->Find(key) : nullptr;
                return v && v->IsNumber() ? Num(v->number) : std::string("\xE2\x80\x94");
            };
            y += SummaryTiles(in.parent, kPanelPad, y, kPanelInner, {{value("passivePoints"), Tr("Passive budget")}, {value("weaverPoints"), Tr("Weaver budget")}}, 4) + 22;
            y += NoteBox(in.parent, kPanelPad, y, kPanelInner,
                              Tr("Point multipliers are session settings. Individual node caps still apply. Existing allocations stay when you lower a budget. "
                                 "Available points stop at zero until the budget covers those allocations.")) + 18;  // .note margin-bottom
            return y;
        });
    };
}
void MakeFreeRespec(Section& s) {
    s.reads.insert("buildread");
    s.build = [](Section&, const Ctx& c) {
        return InPanel(c, [](const Ctx& in, float y) {
            y += H2(in.parent, kPanelPad, y, kPanelInner, Tr("Free respec")) + 14;
            y += NoteBox(in.parent, kPanelPad, y, kPanelInner,
                              Tr("Refund points from the selected tree group. Levels and earned budgets stay. Each action creates a recovery backup and runs only "
                                 "after confirmation.")) + 18;
            std::vector<CardMaker> cards;
            for (const auto& [id, tree] : std::vector<std::pair<std::string, std::string>>{{"respec_skills", "skills"}, {"respec_passives", "passives"}, {"respec_weaver", "weaver"}}) {
                const Control& control = Ctl(id);
                cards.push_back(SaveCard(id, "treerespec {save_id} " + tree, "buildread", [&control](const JValue&) { return Updated(control); }, {"buildread"}));
            }
            return y + CardGrid(in, kPanelPad, y, kPanelInner, cards);
        });
    };
}

// ---------------------------------------------------------------------------- Recent pickups
// lootPanel (app.js) with EpochLootLog.table (ui/loot-log.js): the newest 100 pickups, newest
// first, gold and repeated resources on one line, read every 2 s while the page is shown. The
// table is updated in place: a read costs only the cells that changed.
struct Pickup {
    double seq = 0, gold = 0;
    uint64_t at = 0;
    std::string kind, name, rarity, rule, tiers, group;
    int ruleNumber = 0, outcome = 0, count = 1;
    bool automatic = false;
};
std::vector<Pickup> pickups;  // oldest first; the app keeps 300
std::string pickupSession;
void MergePickups(const JValue& r) {
    const std::string session = r["session"].Str();
    if (session != pickupSession) { pickups.clear(); pickupSession = session; }
    std::set<double> have;
    for (const auto& p : pickups) have.insert(p.seq);
    const uint64_t now = GetTickCount64();
    for (const auto& e : r["entries"].items) {
        if (!e["seq"].IsNumber() || have.count(e["seq"].number)) continue;
        Pickup p;
        p.seq = e["seq"].number;
        p.at = now - static_cast<uint64_t>(std::max(0.0, e["ageSeconds"].Num(0)) * 1000);
        p.kind = e["kind"].Str();
        p.name = e["name"].Str();
        p.rarity = e["rarity"].Str();
        p.rule = e["rule"].Str();
        p.tiers = e["tiers"].Str();
        p.gold = e["gold"].Num(0);
        p.ruleNumber = static_cast<int>(e["ruleNumber"].Num(0));
        p.outcome = static_cast<int>(e["outcome"].Num(0));
        p.automatic = e["automatic"].IsBool() && e["automatic"].boolean;
        pickups.push_back(std::move(p));
    }
    std::sort(pickups.begin(), pickups.end(), [](const Pickup& a, const Pickup& b) { return a.seq < b.seq; });
    if (pickups.size() > 300) pickups.erase(pickups.begin(), pickups.end() - 300);
}
std::string EnUs(double v) {  // toLocaleString('en-US') of a whole number
    std::string digits = std::to_string(std::llabs(std::llround(v))), out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return (v < 0 ? "-" : "") + out;
}
std::string Plain(const std::string& s) {  // TMP draws these words as written
    return s.empty() ? s : "<noparse>" + s + "</noparse>";
}
std::string PickupLabel(const Pickup& e) {
    if (e.kind == "gold") return "+" + EnUs(e.gold) + " Gold";
    if (e.kind == "amber") return (e.name.empty() ? "Amber" : e.name) + (e.gold ? " \xC3\x97" + EnUs(e.gold) : "");
    if (e.kind == "material" && e.count > 1) return (e.name.empty() ? "Material" : e.name) + " \xC3\x97" + std::to_string(e.count);
    return e.name.empty() ? "Item" : e.name;
}
std::string RuleLabel(const Pickup& e) {
    if (e.kind == "gold" || e.kind == "amber" || e.kind == "material") return "";
    if (!e.ruleNumber) return e.outcome >= 0 ? "No matching rule" : "";  // as the app writes it (not translated)
    std::string name;
    bool space = false;
    for (size_t i = 0; i < e.rule.size(); ++i) {
        if (e.rule[i] == '<') {
            const size_t end = e.rule.find('>', i);
            if (end != std::string::npos) { i = end; continue; }
        }
        if (std::isspace(static_cast<unsigned char>(e.rule[i]))) { space = !name.empty(); continue; }
        if (space) name += ' ';
        space = false;
        name += e.rule[i];
    }
    std::string out = Tr("Rule") + " " + std::to_string(e.ruleNumber);
    if (e.outcome >= 0 && e.outcome < 3) out += " \xC2\xB7 " + (e.outcome == 0 ? Tr("Show") : e.outcome == 1 ? Tr("Hide") : Tr("Highlight"));
    if (!name.empty()) out += " \xC2\xB7 " + name;
    return out;
}
std::string TierLabel(const std::string& tiers) {
    std::string out, part;
    std::istringstream words(tiers);
    while (words >> part) out += (out.empty() ? "T" : " T") + part;
    return out;
}
std::string RarityWord(const std::string& r) {
    if (r == "Normal") return Tr("Normal");
    if (r == "Magic") return Tr("Magic");
    if (r == "Rare") return Tr("Rare");
    if (r == "Exalted") return Tr("Exalted");
    if (r == "Unique") return Tr("Unique");
    if (r == "Set") return Tr("Set");
    if (r == "Legendary") return Tr("Legendary");
    return r;
}
Color RarityColor(const std::string& rarity) {  // .loot-<rarity>
    std::string key;
    for (char ch : rarity) if (std::isalpha(static_cast<unsigned char>(ch))) key += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    static const std::map<std::string, const char*> colors{{"unique", "#e8a35a"}, {"legendary", "#e8a35a"}, {"set", "#7ee08a"},      {"exalted", "#c79bff"},
                                                           {"rare", "#f2dc6b"},   {"magic", "#7fb0ff"},     {"material", "#9fd6d0"}, {"gold", "#e8c690"},
                                                           {"amber", "#f0a64a"}};
    auto it = colors.find(key);
    return it == colors.end() ? C("ink") : Hex(it->second);
}
std::string Age(uint64_t at, uint64_t now) {
    const uint64_t s = (now > at ? now - at : 0) / 1000 + ((now > at ? now - at : 0) % 1000 >= 500 ? 1 : 0);
    return s < 60 ? std::to_string(s) + "s" : s < 3600 ? std::to_string(s / 60) + "m" : std::to_string(s / 3600) + "h";
}
struct PickupsView {
    Panel panel;
    std::shared_ptr<LiveTable> table;
    Pin empty, below;
    float listY = 0, belowY = 0, belowH = 0, emptyH = 0, h = 0;
    size_t firstHit = 0, lastHit = 0;  // the Clear card's hits, moved with it
    std::vector<std::string> head;
    std::vector<std::vector<std::string>> cells;
    std::vector<std::vector<Color>> colors;
};
// Shows the rows; returns the section height (and moves the sections below unless building).
float PaintPickups(Section& sec, bool building) {
    auto v = std::static_pointer_cast<PickupsView>(sec.live);
    if (!v) return sec.h;
    if (!v->table->pending) {
        std::vector<Pickup> rows;
        for (auto it = pickups.rbegin(); it != pickups.rend(); ++it) {
            const std::string group = it->kind == "gold" ? "gold" : it->kind == "amber" || it->kind == "material" ? it->kind + ":" + it->name : "";
            if (!group.empty() && !rows.empty() && rows.back().group == group) { rows.back().gold += it->gold; rows.back().count += 1; continue; }
            if (rows.size() == 100) break;
            rows.push_back(*it);
            rows.back().group = group;
            rows.back().count = 1;
        }
        const uint64_t now = GetTickCount64();
        v->cells.clear();
        v->colors.clear();
        for (const auto& e : rows) {
            std::string item = Plain(PickupLabel(e));
            if (e.count > 1 && (e.kind == "gold" || e.kind == "amber"))
                item += " <size=83%><color=#b3beb8>" + Plain(Fmt("({0} pickups)", {std::to_string(e.count)})) + "</color></size>";
            v->cells.push_back({Age(e.at, now), item, TierLabel(e.tiers), Plain(RarityWord(e.rarity)), Plain(RuleLabel(e)), Tr(e.automatic ? "Auto Pickup" : "You")});
            v->colors.push_back({C("ink"), RarityColor(e.rarity), C("ink"), C("ink"), C("ink"), C("ink")});
        }
    }
    float listH;
    if (v->cells.empty()) {
        SetActive(v->empty.Get(), true);
        SetActive(GameObjectOf(v->table->root.Get()), false);
        listH = v->emptyH;
    } else {
        SetActive(v->empty.Get(), false);
        SetActive(GameObjectOf(v->table->root.Get()), true);
        // .loot-table: automatic column widths; the first column at least 56px
        SetColumns(*v->table, AutoColumns(kPanelInner, v->head, v->cells, 12, 12, 56));
        listH = UpdateLiveTable(*v->table, v->cells, v->colors, building ? 0 : 6);  // a few rows a frame
    }
    const float belowY = v->listY + listH;
    if (belowY != v->belowY) {
        for (size_t i = v->firstHit; i < v->lastHit && i < sec.hits.size(); ++i) sec.hits[i].y += belowY - v->belowY;
        Move(v->below.Get(), 0, belowY);
        v->belowY = belowY;
    }
    const float h = belowY + v->belowH + kPanelPad;
    if (h != v->h) {
        v->h = h;
        ClosePanel(v->panel, kPanelW, h);
        if (!building) Resized(sec, h);
    }
    return h;
}
void MakeRecentPickups(Section& s) {
    s.reads.insert("lootlog");
    s.polls.insert("lootlog");
    s.every["lootlog"] = 2000;
    s.build = [](Section& sec, const Ctx& c) {
        auto v = std::make_shared<PickupsView>();
        sec.live = v;
        v->panel = OpenPanel(c.parent, kPadX, 0, kPanelW);
        void* rt = v->panel.rt.Get();
        const Ctx in = c.At(rt, kPadX, 0);
        float y = kPanelPad;
        y += SectionHead(in, kPanelPad, y, kPanelInner, Tr("Recent pickups"), {{Tr("Refresh"), [] { Read("lootlog"); }}}) + 16;
        // p.muted (margin 10px 0); the list follows its bottom margin
        y += Muted(rt, kPanelPad, y, kPanelInner,
                   Tr("Items and gold you or Auto Pickup collect, with the loot filter rule that matched. \"Rule\" uses the same number as the game's Item rule "
                      "numbering setting. The loot window stays on top of a windowed or borderless game. Point at an affix shard to see what its affix does on "
                      "each tier.")) + 10;
        v->listY = y;
        Node empty = New("Empty", rt, kPanelPad, y, kPanelInner, 10);
        v->empty.Set(empty.go);
        v->emptyH = EmptyText(empty.rt, 0, 0, kPanelInner, Tr("No pickups yet. Items and gold you or Auto Pickup collect appear here.")) + 10;
        v->head = {Tr("When"), Tr("Pickup"), Tr("Tiers"), Tr("Rarity"), Tr("Loot filter"), Tr("By")};
        v->table = MakeLiveTable(rt, kPanelPad, y, kPanelInner, v->head, {0.08f, 0.30f, 0.12f, 0.11f, 0.27f, 0.12f});
        v->table->rich = true;
        // the Clear card (.control-grid) right below the list
        Node below = New("Below", rt, 0, y, kPanelW, 10);
        v->below.Set(below.rt);
        v->belowY = y;
        v->firstHit = sec.hits.size();
        const int id = sec.id;
        const CardMaker clear = [id](const Ctx& cc, float x, float yy, float w) {
            const Control& ctl = Ctl("loot_log_clear");
            ActionRun run;
            run.command = "lootlogclear";
            run.success = [](const JValue&) { return Tr("Recent pickups cleared. Items and gold are unchanged."); };
            run.done = [id](const JValue&) {
                pickups.clear();
                if (Section* s2 = FindSection(id)) PaintPickups(*s2, false);
            };
            return ActionCard(cc, x, yy, w, ctl, ActionCaption(ctl), false, [run] { RunAction(run); }, ViewReason());
        };
        v->belowH = CardGrid(in.At(below.rt, 0, y), kPanelPad, 0, kPanelInner, {clear});
        v->lastHit = sec.hits.size();
        if (const JValue* r = Cached("lootlog")) MergePickups(*r);
        return PaintPickups(sec, true);
    };
    s.onData = [](Section& sec, const std::string& read) {
        if (read != "lootlog") return;
        if (const JValue* r = Cached("lootlog")) MergePickups(*r);
        PaintPickups(sec, false);
    };
    // rows not made yet (a long list arrives at once): a few more each frame
    s.tick = [](Section& sec) {
        auto v = std::static_pointer_cast<PickupsView>(sec.live);
        if (v && v->table && v->table->pending) PaintPickups(sec, false);
    };
}

// ----------------------------------------------------------------------- wanted affix picker
// wantedAffixFields (app.js) with ui/affix-picker.js: search and sort, the available and wanted
// lists (double-click or the buttons move the highlighted affix), at most 64, and the status note.
// Shared by Wanted T7 affixes (Loot) and Weaver's Will affixes (General). The choice lives here,
// not in the widgets, so a section built again keeps it.
struct AffixState {
    std::vector<std::string> selected, applied;
    std::string query, order = "az";
    bool dirty = false;
};
std::map<std::string, AffixState> affixStates;
constexpr size_t kAffixLimit = 64;
std::string AffixId(const JValue& v) {
    if (v.IsNumber()) return v.number >= 0 && v.number <= 65535 && std::floor(v.number) == v.number ? Num(v.number) : "";
    if (!v.IsString() || v.text.empty() || v.text.size() > 5 || !std::all_of(v.text.begin(), v.text.end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
        return "";
    if (v.text.size() > 1 && v.text[0] == '0') return "";
    return std::stoi(v.text) <= 65535 ? v.text : "";
}
std::vector<std::string> NormalizeIds(const JValue* list) {
    std::vector<std::string> out;
    if (!list) return out;
    for (const auto& v : list->items) {
        const std::string id = AffixId(v);
        if (!id.empty() && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
        if (out.size() == kAffixLimit) break;
    }
    return out;
}
bool SameIds(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    return a.size() == b.size() && std::all_of(a.begin(), a.end(), [&](const std::string& x) { return std::find(b.begin(), b.end(), x) != b.end(); });
}
void ReceiveAffixes(const std::string& kind, const JValue* ids, bool force = false) {
    if (!ids || !ids->IsArray()) return;
    AffixState& st = affixStates[kind];
    st.applied = NormalizeIds(ids);
    if (force || !st.dirty) st.selected = st.applied;
    st.dirty = !SameIds(st.selected, st.applied);
}
const JValue* AppliedIds(const std::string& kind) {
    if (kind == "loot_affixes") { const JValue* r = Cached("lootread"); return r ? r->Find("affixIds") : nullptr; }
    const JValue* r = Cached("weaverread");
    return r ? r->Path("affixes.ids") : nullptr;
}
struct AffixEntry { std::string id, name; };
std::string NoTags(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '<') {
            const size_t end = s.find('>', i);
            if (end != std::string::npos) { i = end; continue; }
        }
        out += s[i];
    }
    return out;
}
// localeCompare('en', {numeric: true, sensitivity: 'base'}): no case, digit runs as numbers,
// spaces and punctuation before digits before letters.
int Collate(const std::string& a, const std::string& b) {
    const auto rank = [](unsigned char ch) { return std::isspace(ch) ? 0 : std::ispunct(ch) ? 1 : std::isdigit(ch) ? 2 : 3; };
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const unsigned char x = a[i], y = b[j];
        if (std::isdigit(x) && std::isdigit(y)) {
            size_t ie = i, je = j;
            while (ie < a.size() && std::isdigit(static_cast<unsigned char>(a[ie]))) ++ie;
            while (je < b.size() && std::isdigit(static_cast<unsigned char>(b[je]))) ++je;
            const std::string na = a.substr(i, ie - i), nb = b.substr(j, je - j);
            const size_t za = na.find_first_not_of('0'), zb = nb.find_first_not_of('0');
            const std::string ta = za == std::string::npos ? "0" : na.substr(za), tb = zb == std::string::npos ? "0" : nb.substr(zb);
            if (ta.size() != tb.size()) return ta.size() < tb.size() ? -1 : 1;
            if (ta != tb) return ta < tb ? -1 : 1;
            i = ie;
            j = je;
            continue;
        }
        if (rank(x) != rank(y)) return rank(x) < rank(y) ? -1 : 1;
        const int lx = std::tolower(x), ly = std::tolower(y);
        if (lx != ly) return lx < ly ? -1 : 1;
        ++i;
        ++j;
    }
    return i < a.size() ? 1 : j < b.size() ? -1 : 0;
}
struct AffixLists { std::vector<AffixEntry> available, selected; };
AffixLists ListsOf(const AffixState& st) {
    std::vector<AffixEntry> all;
    if (const JValue* r = Cached("lootread"))
        for (const auto& x : (*r)["affixes"].items) {
            const std::string id = AffixId(x["id"]);
            if (id.empty()) continue;
            all.push_back({id, NoTags(x["uiLabel"].Str(x["name"].Str("Affix " + id)))});
        }
    std::string q = Trim(st.query);
    std::transform(q.begin(), q.end(), q.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    AffixLists out;
    for (const auto& x : all) {
        if (std::find(st.selected.begin(), st.selected.end(), x.id) != st.selected.end()) continue;
        if (!q.empty()) {
            std::string hay = x.name + " " + x.id;
            std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (hay.find(q) == std::string::npos) continue;
        }
        out.available.push_back(x);
    }
    for (const auto& id : st.selected) {
        auto it = std::find_if(all.begin(), all.end(), [&](const AffixEntry& x) { return x.id == id; });
        out.selected.push_back(it != all.end() ? *it : AffixEntry{id, "Affix " + id + " (name unavailable)"});
    }
    const auto compare = [](const AffixEntry& a, const AffixEntry& b) {
        const int c = Collate(a.name, b.name);
        return c ? c < 0 : std::stoi(a.id) < std::stoi(b.id);
    };
    std::sort(out.available.begin(), out.available.end(), compare);
    std::sort(out.selected.begin(), out.selected.end(), compare);
    if (st.order == "za") {
        std::reverse(out.available.begin(), out.available.end());
        std::reverse(out.selected.begin(), out.selected.end());
    }
    return out;
}
// The status note; the first drawing also has the hint (updates leave it out, as the app does).
std::string AffixStatus(const std::string& kind, const AffixState& st, bool first) {
    const bool loot = kind == "loot_affixes";
    if (first) {
        if (st.dirty) return Tr("Selection changed \xC2\xB7 use Apply selection. Double-click an affix or press Enter to move it. Search only filters the available list.");
        if (!st.selected.empty()) return Tr("Selection applied. Double-click an affix or press Enter to move it. Search only filters the available list.");
        return loot ? Tr("No selection accepts any T7 affix. Double-click an affix or press Enter to move it. Search only filters the available list.")
                    : Tr("No selection: the game chooses every Weaver's Will upgrade. Double-click an affix or press Enter to move it. Search only filters the "
                         "available list.");
    }
    if (st.dirty) return Tr("Selection changed \xC2\xB7 use Apply selection.");
    if (!st.selected.empty()) return Tr("Selection applied.");
    return loot ? Tr("No selection accepts any T7 affix.") : Tr("No selection: the game chooses every Weaver's Will upgrade.");
}
struct PickerView {
    std::string kind;
    std::shared_ptr<ListBox> available, wanted;
    std::vector<std::string> availableIds, wantedIds;
    Pin availableCount, wantedCount, sortText;
    NoteParts status;
    int section = -1;
};
std::map<std::string, std::weak_ptr<PickerView>> pickerViews;
// updateWantedAffixLists: both lists, the counts and the status (a status of another height
// builds the section again).
void RefreshPicker(PickerView& v) {
    const AffixState& st = affixStates[v.kind];
    const AffixLists lists = ListsOf(st);
    v.availableIds.clear();
    v.wantedIds.clear();
    std::vector<std::string> a, w;
    for (const auto& x : lists.available) { v.availableIds.push_back(x.id); a.push_back(x.name); }
    for (const auto& x : lists.selected) { v.wantedIds.push_back(x.id); w.push_back(x.name); }
    SetListItems(*v.available, a, -1);
    SetListItems(*v.wanted, w, -1);
    SetText(v.availableCount.Get(), std::to_string(lists.available.size()));
    SetText(v.wantedCount.Get(), std::to_string(lists.selected.size()) + " / 64");
    const float before = v.status.h;
    if (std::fabs(SetNote(v.status, AffixStatus(v.kind, st, false)) - before) > 0.5f) {
        Section* s = FindSection(v.section);
        if (s && s->panelState) RebuildTail(v.section);
        else if (s) Rebuild(v.section);
    }
}
// moveWantedAffixes: the highlighted affix of one list to the other
void MoveAffix(const std::string& kind, bool add, int index) {
    auto v = pickerViews[kind].lock();
    if (!v) return;
    AffixState& st = affixStates[kind];
    const auto& ids = add ? v->availableIds : v->wantedIds;
    if (index < 0 || index >= static_cast<int>(ids.size())) return;
    const std::string id = ids[index];
    if (add) {
        if (std::find(st.selected.begin(), st.selected.end(), id) == st.selected.end()) {
            if (st.selected.size() >= kAffixLimit) { Toast(Tr("Choose at most 64 wanted affixes."), true); return; }
            st.selected.push_back(id);
        }
    } else st.selected.erase(std::remove(st.selected.begin(), st.selected.end(), id), st.selected.end());
    st.dirty = !SameIds(st.selected, st.applied);
    RefreshPicker(*v);
}
// .affix-picker in a form grid of width w; returns its height (with the note's 18px margin)
float DrawPicker(Section& s, const Ctx& c, float x, float y, float w, const std::string& kind) {
    AffixState& st = affixStates[kind];
    auto v = std::make_shared<PickerView>();
    v->kind = kind;
    v->section = s.id;
    pickerViews[kind] = v;
    s.live = v;  // the section keeps the picker's widgets (its only live part)
    // .affix-tools: the search (1fr) and Sort (190px), gap 18, margin-bottom 16
    const float sortW = 190, searchW = w - sortW - 18;
    float toolsH = InputField(c, x, y, searchW, Tr("Find an affix"), st.query, Tr("Search name or ID\xE2\x80\xA6"), false, [kind](const std::string& text) {
        affixStates[kind].query = text;
        if (auto view = pickerViews[kind].lock()) RefreshPicker(*view);
    }, kind + "-search");
    {
        Style ls{Face::Body, 12, C("muted")};
        const float top = LineHeight(ls) + 8;
        TextBox(c.parent, "Label", x + searchW + 18, y, sortW, LineHeight(ls), ls, Tr("Sort"));
        Style cs{Face::Body, 12, C("ink")};
        cs.align = kLeft;
        const float h = SelectHeight(cs);
        const float sx = x + searchW + 18;
        Fill(c.parent, "Select", sx, y + top, sortW, h, Hex("#080e0e"), 3);
        void* frame = Outline(c.parent, "Select border", sx, y + top, sortW, h, Hex("#7b745a99"), 3);
        const std::vector<std::string> labels{Tr("Name \xC2\xB7 A\xE2\x80\x93Z"), Tr("Name \xC2\xB7 Z\xE2\x80\x93" "A")};
        v->sortText.Set(TextBox(c.parent, "Choice", sx + 11, y + top, sortW - 40, h, cs, labels[st.order == "za" ? 1 : 0]));
        Style ar{Face::Body, 11, C("ink")};
        ar.align = kCenter;
        TextBox(c.parent, "Arrow", sx + sortW - 26, y + top, 16, h, ar, "\xE2\x8C\x84");
        Hit hit;
        hit.kind = HitKind::Action;
        hit.name = kind + "-sort";
        hit.x = sx;
        hit.y = y + top;
        hit.w = sortW;
        hit.h = h;
        AddHover(hit, frame, Hex("#7b745a99"), Hex("#7b745a99"));
        const Ctx at = c;
        hit.action = [at, sx, y, top, sortW, h, labels, kind] {
            float wx = 0, wy = 0;
            if (!WindowPoint(at, sx, y + top, &wx, &wy)) return;
            OpenList(wx, wy, sortW, h, labels, affixStates[kind].order == "za" ? 1 : 0, [kind, labels](int i) {
                affixStates[kind].order = i == 1 ? "za" : "az";
                if (auto view = pickerViews[kind].lock()) {
                    SetText(view->sortText.Get(), labels[i]);
                    RefreshPicker(*view);
                }
            });
        };
        c.Add(std::move(hit));
        toolsH = std::max(toolsH, top + h);
    }
    float cy = y + toolsH + 16;
    // .affix-lists: two columns, gap 18; a label line with its count (accent, at the right), the
    // 260px list (margin 8px 0 12px after the label's gap 8), then the buttons
    const AffixLists lists = ListsOf(st);
    const float colW = (w - 18) / 2;
    Style ls{Face::Body, 12, C("muted")}, count{Face::Body, 12, C("accent")};
    count.align = kTopRight;
    const float labelH = LineHeight(ls), listY = cy + labelH + 8 + 8, listH = 260;
    TextBox(c.parent, "Label", x, cy, colW, labelH, ls, Tr("Available affixes"));
    v->availableCount.Set(TextBox(c.parent, "Count", x, cy, colW, labelH, count, std::to_string(lists.available.size())));
    TextBox(c.parent, "Label", x + colW + 18, cy, colW, labelH, ls, Tr("Wanted affixes"));
    v->wantedCount.Set(TextBox(c.parent, "Count", x + colW + 18, cy, colW, labelH, count, std::to_string(lists.selected.size()) + " / 64"));
    std::vector<std::string> a, wn;
    for (const auto& e : lists.available) { v->availableIds.push_back(e.id); a.push_back(e.name); }
    for (const auto& e : lists.selected) { v->wantedIds.push_back(e.id); wn.push_back(e.name); }
    v->available = MakeListBox(s, c, x, listY, colW, listH, a, -1, {}, [kind](int i) { MoveAffix(kind, true, i); }, kind + "-available");
    v->wanted = MakeListBox(s, c, x + colW + 18, listY, colW, listH, wn, -1, {}, [kind](int i) { MoveAffix(kind, false, i); }, kind + "-wanted");
    const float buttonsY = listY + listH + 12;
    const auto add = AddButton(c, x, buttonsY, Tr("Add selected \xE2\x86\x92"), Look::Normal, [kind] {
        if (auto view = pickerViews[kind].lock()) MoveAffix(kind, true, view->available->selected);
    }, false, kind + "-add");
    // .actions (margin-top 18): Remove selected, Clear all
    const auto remove = AddButton(c, x + colW + 18, buttonsY + 18, Tr("\xE2\x86\x90 Remove selected"), Look::Normal, [kind] {
        if (auto view = pickerViews[kind].lock()) MoveAffix(kind, false, view->wanted->selected);
    }, false, kind + "-remove");
    AddButton(c, x + colW + 18 + remove.w + 10, buttonsY + 18, Tr("Clear all"), Look::Normal, [kind] {
        AffixState& state = affixStates[kind];
        state.selected.clear();
        state.dirty = !SameIds(state.selected, state.applied);
        if (auto view = pickerViews[kind].lock()) RefreshPicker(*view);
    }, false, kind + "-clear");
    cy = std::max(buttonsY + add.h, buttonsY + 18 + remove.h);
    // p.note (margin-top 16, margin-bottom 18)
    cy += 16;
    cy += NoteBox(c.parent, x, cy, w, AffixStatus(kind, st, true), &v->status) + 18;
    return cy - y;
}
// Apply selection: lootaffixes / weaveraffixes with the wanted ids (none: an empty list)
Action ApplyAffixes(const std::string& kind) {
    return [kind] {
        const AffixState& st = affixStates[kind];
        std::string ids;
        for (const auto& id : st.selected) ids += (ids.empty() ? "" : ",") + id;
        const bool loot = kind == "loot_affixes";
        ActionRun run;
        run.command = (loot ? "lootaffixes " : "weaveraffixes ") + (ids.empty() ? std::string("none") : ids);
        run.success = [kind](const JValue&) { return Fmt("{0} updated.", {Tr(Ctl(kind).label)}); };
        run.done = [kind, loot](const JValue& reply) {
            ReceiveAffixes(kind, loot ? reply.Find("affixIds") : reply.Path("affixes.ids"), true);
            if (auto view = pickerViews[kind].lock()) RefreshPicker(*view);
        };
        run.reread = {loot ? "lootread" : "weaverread"};
        RunAction(run);
    };
}
// The affix card (.affix-card spans the whole grid row): title, badge, description, the picker in
// its form grid, then Apply selection.
CardBox AffixCard(Section& s, const Ctx& c, float x, float y, float w, const std::string& kind) {
    ReceiveAffixes(kind, AppliedIds(kind));
    const Control& ctl = Ctl(kind);
    Section* self = &s;
    return ActionCard(c, x, y, w, ctl, ActionCaption(ctl), false, ApplyAffixes(kind), ViewReason(),
                      [self, kind](const Ctx& fc, float fx, float fy, float fw) { return DrawPicker(*self, fc, fx, fy, fw, kind); });
}
// General → Weaver's Will: after its cards, "Refresh loads…" while the affix names are not read,
// then the Weaver's Will affixes card.
void RegisterWeaverTail() {
    Tail tail;
    tail.reads = {"lootread", "weaverread"};
    tail.build = [](Section& s, const Ctx& c) {
        float y = 0;
        const JValue* r = Cached("lootread");
        const bool note = !r || (*r)["affixes"].items.empty();
        s.state["weaverNote"] = note ? "1" : "0";
        if (note) y += 10 + NoteBox(c.parent, kPanelPad, 10, kPanelInner, Tr("Refresh loads the affix names and the current selection.")) + 18;
        return y + AffixCard(s, c, kPanelPad, y, kPanelInner, "weaver_affixes").h;
    };
    // the names or the applied list changed: the lists in place, or again when the note comes or goes
    tail.onData = [](Section& s, const std::string&) {
        const JValue* r = Cached("lootread");
        auto v = pickerViews["weaver_affixes"].lock();
        const bool noted = s.state["weaverNote"] == "1", needNote = !r || (*r)["affixes"].items.empty();
        s.state["weaverNote"] = needNote ? "1" : "0";
        ReceiveAffixes("weaver_affixes", AppliedIds("weaver_affixes"));
        if (!v || noted != needNote) RebuildTail(s.id);
        else RefreshPicker(*v);
    };
    RegisterTail("weaver_affixes", std::move(tail));
}

// ----------------------------------------------------------------------------- Loot & Pickup
void MakeCollectNow(Section& s) {
    s.reads.insert("pickupradius");
    s.build = [](Section&, const Ctx& c) {
        return InPanel(c, [](const Ctx& in, float y) {
            y += H2(in.parent, kPanelPad, y, kPanelInner, Tr("Collect now")) + 14;
            const JValue* r = Cached("pickupradius");
            if (r && (*r)["inventoryFull"].IsBool() && (*r)["inventoryFull"].boolean)
                y += NoteBox(in.parent, kPanelPad, y, kPanelInner,
                                  Tr("Your inventory is full. Equipment pickup is paused until you open and close your inventory; gold, potions, tomes, bones and "
                                     "materials are still collected.")) + 18;
            const CardMaker card = [](const Ctx& cc, float x, float yy, float w) {
                const Control& c = Ctl("pickup_now");
                ActionRun run;
                run.command = "pickupnow";
                run.success = [](const JValue& v) {
                    const bool full = v["inventoryFull"].IsBool() && v["inventoryFull"].boolean;
                    return Fmt("Picked up {0} drops.{1}", {Num(v["picked"].Num(0)),
                                                           full ? " " + Tr("Your inventory is full: equipment waits until you open and close your inventory.") : ""});
                };
                return ActionCard(cc, x, yy, w, c, ActionCaption(c), false, [run] { RunAction(run); }, ViewReason());
            };
            return y + CardGrid(in, kPanelPad, y, kPanelInner, {card});
        });
    };
}
std::string ModeLabel(const std::string& mode) {
    if (mode == "all") return Tr("All items");
    if (mode == "filter") return Tr("Game loot filter");
    if (mode == "quality") return Tr("LP and T7 quality rules");
    if (mode == "materials") return Tr("Crafting materials");
    return Tr("Refresh to read your rules");
}
void MakePickupRules(Section& s) {
    s.reads.insert("lootread");
    s.build = [](Section&, const Ctx& c) {
        return InPanel(c, [](const Ctx& in, float y) {
            y += SectionHead(in, kPanelPad, y, kPanelInner, Tr("Pickup rules"), {{Tr("Refresh rules"), [] { Read("lootread"); }}}) + 16;
            const JValue* r = Cached("lootread");
            y += NoteBox(in.parent, kPanelPad, y, kPanelInner, Fmt("Current selection: {0}.", {ModeLabel(r ? (*r)["mode"].Str("none") : "none")})) + 18;
            const Control& c = Ctl("loot_reset");
            ActionRun run;
            run.command = "lootreset";
            run.success = [](const JValue&) { return Tr("Pickup rules reset."); };
            run.reread = {"lootread"};
            y += ActionCard(in, kPanelPad, y, kPanelInner, c, ActionCaption(c), false, [run] { RunAction(run); }, ViewReason()).h;
            return y;
        });
    };
}
// Pickup selections: Pickup mode, Pickup category, Wanted T7 affixes.
std::string ChoiceWord(const std::string& value) {
    static const std::map<std::string, std::string> words{{"all", "All"},         {"filter", "Filter"}, {"quality", "Quality"}, {"materials", "Materials"},
                                                          {"gold", "Gold"},       {"potions", "Potions"}, {"xp", "XP"},         {"favor", "Favor"},
                                                          {"bones", "Bones"},     {"amber", "Amber"},     {"normal", "Normal"}, {"empowered", "Empowered"}};
    auto it = words.find(value);
    return Tr(it == words.end() ? value : it->second);
}
void MakePickupSelections(Section& s) {
    s.reads.insert("lootread");
    s.build = [](Section& sec, const Ctx& c) {
        Section* self = &sec;
        return InPanel(c, [self](const Ctx& in, float y) {
            y += H2(in.parent, kPanelPad, y, kPanelInner, Tr("Pickup selections")) + 14;
            const JValue* r = Cached("lootread");
            std::vector<CardMaker> cards;
            // Pickup mode: <label>Selection<select name="mode"> All/Filter/Quality/Materials
            cards.push_back([self, r](const Ctx& cc, float x, float yy, float w) {
                const Control& c = Ctl("loot_mode");
                const std::vector<std::string> modes{"all", "filter", "quality", "materials"};
                const std::string mode = self->state.count("mode") ? self->state["mode"] : r ? (*r)["mode"].Str("all") : "all";
                int current = 0;
                for (size_t i = 0; i < modes.size(); ++i) if (modes[i] == mode) current = static_cast<int>(i);
                std::vector<std::string> labels;
                for (const auto& m : modes) labels.push_back(ChoiceWord(m));
                const int id = self->id;
                const Fields fields = [labels, current, modes, id](const Ctx& fc, float fx, float fy, float fw) {
                    return SelectField(fc, fx, fy, (fw - 14) / 2, Tr("Selection"), labels, current, [modes, id](int i) {
                        if (Section* s2 = FindSection(id)) { s2->state["mode"] = modes[i]; Rebuild(id); }
                    }, "mode");
                };
                ActionRun run;
                run.command = "lootmode " + modes[current];
                run.success = [](const JValue&) { return Tr("Pickup mode updated."); };
                run.reread = {"lootread"};
                return ActionCard(cc, x, yy, w, c, ActionCaption(c), false, [run] { RunAction(run); }, ViewReason(), fields);
            });
            // Pickup category: <select name="category"> and <input name="value" type=number min 0 max 1>
            cards.push_back([self](const Ctx& cc, float x, float yy, float w) {
                const Control& c = Ctl("loot_category");
                const std::vector<std::string> kinds{"materials", "gold", "potions", "xp", "favor", "bones", "amber"};
                if (!self->state.count("category")) self->state["category"] = "materials";
                if (!self->state.count("value")) self->state["value"] = "1";
                int current = 0;
                for (size_t i = 0; i < kinds.size(); ++i) if (kinds[i] == self->state["category"]) current = static_cast<int>(i);
                std::vector<std::string> labels;
                for (const auto& k : kinds) labels.push_back(ChoiceWord(k));
                const int id = self->id;
                const std::string value = self->state["value"];
                const Fields fields = [labels, current, kinds, id, value](const Ctx& fc, float fx, float fy, float fw) {
                    const float half = (fw - 14) / 2;
                    const float a = SelectField(fc, fx, fy, half, Tr("Pickup category"), labels, current, [kinds, id](int i) {
                        if (Section* s2 = FindSection(id)) { s2->state["category"] = kinds[i]; Rebuild(id); }
                    }, "category");
                    const float b = InputField(fc, fx + half + 14, fy, half, Tr("Value"), value, "", true, [id](const std::string& text) {
                        if (Section* s2 = FindSection(id)) s2->state["value"] = text;
                    }, "category-value");
                    return std::max(a, b);
                };
                return ActionCard(cc, x, yy, w, c, ActionCaption(c), false, [id] {
                    Section* s2 = FindSection(id);
                    if (!s2) return;
                    const std::string v = s2->state["value"];
                    if (v != "0" && v != "1") { Toast(Tr("Value") + ": 0\xE2\x80\x93" "1", true); return; }
                    ActionRun run;
                    run.command = "lootcategory " + s2->state["category"] + " " + v;
                    run.success = [](const JValue&) { return Fmt("{0} updated.", {Tr(Ctl("loot_category").label)}); };
                    run.reread = {"lootread"};
                    RunAction(run);
                }, ViewReason(), fields);
            });
            y += CardGrid(in, kPanelPad, y, kPanelInner, cards) + 15;
            // Wanted T7 affixes: .affix-card takes the whole grid row
            return y + AffixCard(*self, in, kPanelPad, y, kPanelInner, "loot_affixes").h;
        });
    };
}

// --------------------------------------------------------------------------------- Campaign
void MakeCampaign(Section& s) {
    s.reads.insert("progressread");
    // the quest table fills over a few frames (85 rows at once would hold one frame for long)
    struct QuestView { Panel panel; std::shared_ptr<LiveTable> table; std::vector<std::vector<std::string>> cells; float tableY = 0; };
    s.tick = [](Section& sec) {
        auto v = std::static_pointer_cast<QuestView>(sec.live);
        if (!v || !v->table || !v->table->pending) return;
        const float h = v->tableY + UpdateLiveTable(*v->table, v->cells, {}, 10) + kPanelPad;
        ClosePanel(v->panel, kPanelW, h);
        Resized(sec, h);
    };
    s.build = [](Section& sec, const Ctx& c) {
        Section* self = &sec;
        auto view = std::make_shared<QuestView>();
        sec.live = view;
        view->panel = OpenPanel(c.parent, kPadX, 0, kPanelW);
        const Ctx in0 = c.At(view->panel.rt.Get(), kPadX, 0);
        const float total = [&](const Ctx& in, float y) {
            y += SectionHead(in, kPanelPad, y, kPanelInner, Tr("Campaign and waypoints"), {{Tr("Refresh live"), [] { Read("progressread"); }}}) + 16;
            const Control& complete = Ctl("campaign_complete");
            const Control& waypoints = Ctl("waypoints_unlock");
            y += CardGrid(in, kPanelPad, y, kPanelInner,
                          {SaveCard("campaign_complete", "questscomplete {save_id}", "progressread",
                                    [](const JValue& v) {
                                        const JValue* need = v.Path("mastery.needsSelection");
                                        return need && need->IsBool() && need->boolean ? Tr("Campaign completed with normal rewards. Use Choose Mastery to finish your character.")
                                                                                       : Tr("Campaign completed with normal rewards.");
                                    },
                                    {"progressread"}),
                           SaveCard("waypoints_unlock", "waypointsunlock {save_id}", "progressread", [](const JValue&) { return Tr("All waypoints unlocked."); },
                                    {"progressread"})});
            (void)complete;
            (void)waypoints;
            // <details><summary>Quest rewards and coverage (85 quests / 109 waypoints)</summary>
            const bool open = self->state["quests"] == "open";
            const int id = self->id;
            y += 15;
            y += Details(in, kPanelPad, y, Fmt("Quest rewards and coverage ({0} quests / {1} waypoints)", {Num(D()["counts"]["eligibleCampaignQuests"].Num(85)), Num(D()["counts"]["waypoints"].Num(109))}),
                         open, [id] {
                             if (Section* s2 = FindSection(id)) {
                                 s2->state["quests"] = s2->state["quests"] == "open" ? "" : "open";
                                 Rebuild(id);
                             }
                         });
            if (open) {
                y += 10 + Muted(in.parent, kPanelPad, y + 10, kPanelInner,
                                Tr("Completion grants normal rewards and raises the character to at least level 55. Completed quests do not grant rewards again. "
                                   "Test, repeatable and endgame quests are excluded."));
                const JValue* r = Cached("progressread");
                y += 10 + Muted(in.parent, kPanelPad, y + 10, kPanelInner, Tr(r ? "Live quest data" : "Static coverage catalog")) + 10;
                std::vector<std::vector<std::string>> rows;
                const JValue& quests = r ? (*r)["quests"] : D()["campaignQuests"];
                for (const auto& q : quests.items) {
                    if (q.Find("eligible") && q["eligible"].IsBool() && !q["eligible"].boolean) continue;
                    rows.push_back({q["name"].Str(), Tr(q["main"].IsBool() && q["main"].boolean ? "Main" : "Side"), Num(q["xp"].Num(0)), Num(q["gold"].Num(0)),
                                    Num(q["passive"].Num(0)), Num(q["idol"].Num(0)), Num(q["attributes"].Num(0))});
                }
                const std::vector<std::string> head{Tr("Quest"), Tr("Type"), Tr("XP"), Tr("Gold"), Tr("Passive"), Tr("Idol"), Tr("Attribute")};
                view->table = MakeLiveTable(in.parent, kPanelPad, y, kPanelInner, head, {});
                SetColumns(*view->table, AutoColumns(kPanelInner, head, rows, 12, 12, 180));
                view->tableY = y;
                view->cells = std::move(rows);
                y += UpdateLiveTable(*view->table, view->cells, {}, 0);  // the header; the rows on the next frames
            }
            return y;
        }(in0, kPanelPad) + kPanelPad;
        ClosePanel(view->panel, kPanelW, total);
        return total;
    };
}
void MakeMastery(Section& s) {
    s.reads.insert("progressread");
    s.build = [](Section&, const Ctx& c) {
        return InPanel(c, [](const Ctx& in, float y) {
            y += H2(in.parent, kPanelPad, y, kPanelInner, Tr("Mastery")) + 14;
            const JValue* r = Cached("progressread");
            const JValue* mastery = r ? r->Find("mastery") : nullptr;
            const bool available = mastery && (*mastery)["available"].IsBool() && (*mastery)["available"].boolean;
            const std::string status = available ? Tr("Your character has no mastery yet. Open the selection screen, then choose your mastery in the game.")
                                       : mastery && (*mastery)["reason"].IsString() ? (*mastery)["reason"].text
                                                                                       : Tr("Refresh live to check mastery selection for your character.");
            y += NoteBox(in.parent, kPanelPad, y, kPanelInner, status) + 18;
            const Control& c = Ctl("mastery_choose");
            std::string reason = SaveReason("progressread");
            if (reason.empty() && !available) reason = mastery && (*mastery)["reason"].IsString() ? (*mastery)["reason"].text : Tr("Refresh live to check mastery selection for your character.");
            ActionRun run;
            run.command = "masteryopen {save_id}";
            run.snapshot = "progressread";
            run.success = [](const JValue&) { return Tr("Mastery selection opened in the game. Choose and confirm your mastery there."); };
            run.reread = {"progressread"};
            y += ActionCard(in, kPanelPad, y, kPanelInner, c, ActionCaption(c), true, [run] { RunAction(run); }, reason).h;
            return y;
        });
    };
}

// --------------------------------------------------------------------------------- Factions
void MakeFactions(Section& s) {
    s.reads.insert("factionread");
    s.build = [](Section& sec, const Ctx& c) {
        Section* self = &sec;
        return InPanel(c, [self](const Ctx& in, float y) {
            y += SectionHead(in, kPanelPad, y, kPanelInner, Tr("Factions"), {{Tr("Refresh live"), [] { Read("factionread"); }}}) + 16;
            y += NoteBox(in.parent, kPanelPad, y, kPanelInner,
                         Tr("Merchant’s Guild and Weaver management actions are not implemented. This section reads information only.")) + 18;
            const JValue* r = Cached("factionread");
            if (!r || (*r)["factions"].items.empty()) return y + EmptyText(in.parent, kPanelPad, y, kPanelInner, Tr("Live faction information has not been read yet."));
            std::vector<CardMaker> cards;
            const int id = self->id;
            for (size_t i = 0; i < (*r)["factions"].items.size(); ++i) {
                cards.push_back([self, r, i, id](const Ctx& cc, float x, float yy, float w) {
                    const JValue& f = (*r)["factions"].items[i];
                    CardBox b = OpenCard(cc.parent, x, yy, w);
                    void* rt = b.node.Get();
                    const Ctx inner = cc.At(rt, x, yy);
                    const float pad = 20, iw = w - 2 * pad;
                    float cy = pad;
                    cy += H3(rt, pad, cy, iw, f["name"].Str()) + 12;
                    {
                        Style bs{Face::Body, 10, C("accent")};
                        bs.spacing = 0.7f;
                        bs.upper = true;
                        bs.align = kLeft;
                        const float bh = LineHeight(bs) + 12;
                        Outline(rt, "Badge border", pad, cy, iw, bh, C("line"), 2);
                        TextBox(rt, "Badge", pad + 8, cy, iw - 16, bh, bs, Tr("Information only"));
                        cy += bh;
                    }
                    const bool member = f["member"].IsBool() && f["member"].boolean;
                    cy += 10 + Muted(rt, pad, cy + 10, iw,
                                     Tr(member ? "Member" : "Not a member") + " \xC2\xB7 " + Fmt("Rank {0}", {Num(f["rank"].Num(0))}) + " \xC2\xB7 " +
                                         Fmt("Favor {0}", {Num(f["favor"].Num(0))}) + " \xC2\xB7 " + Fmt("Reputation {0}", {Num(f["reputation"].Num(0))})) + 10;
                    if (const JValue* wp = f.Find("weaverPoints")) {
                        cy += 10 + Muted(rt, pad, cy + 10, iw,
                                         Fmt("Weaver points: {0} / {1} ({2} rank + {3} echo)", {Num((*wp)["earned"].Num(0)), Num((*wp)["maxTotal"].Num(0)),
                                                                                                Num((*wp)["maxRank"].Num(0)), Num((*wp)["maxEchoes"].Num(0))}),
                                         14, 1.65f) + 10;
                    }
                    const std::string key = "ranks" + std::to_string(i);
                    const bool open = self->state[key] == "open";
                    cy += 15;
                    cy += Details(inner, pad, cy, Tr("Rank rewards"), open, [id, key] {
                        if (Section* s2 = FindSection(id)) { s2->state[key] = s2->state[key] == "open" ? "" : "open"; Rebuild(id); }
                    });
                    if (open) {
                        for (const auto& rank : f["ranks"].items) {
                            std::string desc = rank["description"].Str();
                            for (size_t a = desc.find('<'); a != std::string::npos; a = desc.find('<')) {
                                const size_t z = desc.find('>', a);
                                if (z == std::string::npos) break;
                                desc.erase(a, z - a + 1);
                            }
                            Style bs{Face::BodySemi, 14, C("ink")};
                            bs.wrap = true;
                            bs.line = 1.65f;
                            cy += 10 + Paragraph(rt, "Rank", pad, cy + 10, iw, bs, Num(rank["index"].Num(0) + 1) + " \xC2\xB7 " + rank["title"].Str());
                            cy += Muted(rt, pad, cy, iw, desc, 14, 1.65f) + 10;
                        }
                    }
                    CloseCard(b, w, cy + pad);
                    return b;
                });
            }
            return y + CardGrid(in, kPanelPad, y, kPanelInner, cards);
        });
    };
}
}  // namespace

// For "panel read": the pickers' choices and the pickups kept (a test checks them).
std::string SectionsState() {
    const auto list = [](const std::vector<std::string>& ids) {
        std::string out = "[";
        for (size_t i = 0; i < ids.size(); ++i) out += (i ? "," : "") + ids[i];
        return out + "]";
    };
    std::string out = "{\"pickups\":" + std::to_string(pickups.size()) + ",\"affixes\":{";
    bool comma = false;
    for (const auto& [kind, st] : affixStates) {
        out += std::string(comma ? "," : "") + managed::Json(kind) + ":{\"selected\":" + list(st.selected) + ",\"applied\":" + list(st.applied) +
               ",\"dirty\":" + (st.dirty ? "true" : "false") + ",\"query\":" + managed::Json(st.query) + ",\"order\":" + managed::Json(st.order) + "}";
        comma = true;
    }
    return out + "},\"workshop\":" + WorkshopState() + "}";
}
void RegisterNativeSections() {
    RegisterWeaverTail();
    RegisterSummonSections();
    RegisterDisplaySections();
    RegisterInventorySections();
    RegisterCofSections();
    RegisterMonolithSections();
    RegisterCraftSections();
    RegisterWorkshopSections();
    RegisterSection("cached_summary", MakeCachedSummary);
    RegisterSection("skill_budgets", MakeSkillBudgets);
    RegisterSection("free_respec", MakeFreeRespec);
    RegisterSection("recent_pickups", MakeRecentPickups);
    RegisterSection("collect_now", MakeCollectNow);
    RegisterSection("pickup_rules", MakePickupRules);
    RegisterSection("pickup_selections", MakePickupSelections);
    RegisterSection("campaign", MakeCampaign);
    RegisterSection("mastery", MakeMastery);
    RegisterSection("factions", MakeFactions);
}

}  // namespace ep::ingame::view
