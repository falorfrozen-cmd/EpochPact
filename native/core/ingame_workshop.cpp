// The Crafting page's Custom Items workshop (app.js customWorkshop, ui/crafting-model.js): the
// equipment item in the game's forge, a Unique / Set template, T7 affixes with their own tier and
// roll, extra Unique / Set effects, the review and Create custom item.
//
// One panel drawn by several sections (a panel group, ui.json "group"), so a change builds again
// only its part: typing a search rebuilds its list, a tick on a row changes the row in place and
// rebuilds the chosen rows and the review. The lists scroll inside their 240px box and make their
// rows as they come into view. The item choices (five reads) are parsed on a worker thread.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace ep::ingame::view {
namespace {
using JValue = json::Value;
using assets::Face;

// ------------------------------------------------------------------------------- the model
std::string IdOf(const JValue* v) { return !v ? "" : v->IsString() ? v->text : v->IsNumber() ? Num(v->number) : ""; }
std::string Clean(const std::string& s) {  // app.js clean(): no <tags>
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
// toLowerCase for the searches: ASCII and the Latin-1 letters item names can have
std::string Lower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = s[i];
        if (c < 0x80) { out += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); continue; }
        if (c == 0xC3 && i + 1 < s.size()) {
            const unsigned char d = s[i + 1];
            out += static_cast<char>(c);
            out += static_cast<char>(d >= 0x80 && d <= 0x9E && d != 0x97 ? d + 0x20 : d);
            ++i;
            continue;
        }
        out += static_cast<char>(c);
    }
    return out;
}
std::vector<std::string> Words(const std::string& query) {  // trim().toLowerCase().split(/\s+/)
    std::vector<std::string> words;
    std::string w;
    for (char ch : Lower(query)) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            if (!w.empty()) words.push_back(w);
            w.clear();
        } else w += ch;
    }
    if (!w.empty()) words.push_back(w);
    return words;
}
bool Matches(const std::string& lower, const std::vector<std::string>& words) {
    for (const auto& w : words) if (lower.find(w) == std::string::npos) return false;
    return true;
}

struct Item { std::string id, name, label, lower; bool set = false; };
struct Affix { std::string id, name, lower; std::vector<std::string> classes; int tiers = 7; };  // classes: of a class-specific affix
struct Modifier { std::string id, names[2], lowers[2]; };  // its name with maximum (0) and midpoint (1) rolls
struct Catalog {
    std::string playerId, playerName;
    std::vector<Item> items;
    std::vector<Affix> affixes;
    std::vector<Modifier> modifiers;
    std::map<std::string, size_t> itemAt, affixAt, modifierAt;
};
constexpr int kPrimordial = 8;
struct Setting { bool tierSet = false; int tier = 7; enum Roll { Unset, Number, Random, Invalid } roll = Unset; int value = 0; };
struct Draft {
    std::string templateId = "-1", perfect = "1";
    std::vector<std::string> affixes, modifiers;
    std::map<std::string, Setting> settings;
};
struct Effective { int tier = 7; bool random = false, valid = true; int roll = 100; };
Effective SettingOf(const Draft& d, const std::string& id) {  // crafting-model setting()
    Effective e;
    e.roll = d.perfect == "0" ? 50 : 100;
    const auto it = d.settings.find(id);
    if (it == d.settings.end()) return e;
    if (it->second.tierSet) e.tier = it->second.tier;
    if (it->second.roll == Setting::Number) e.roll = it->second.value;
    else if (it->second.roll == Setting::Random) e.random = true;
    else if (it->second.roll == Setting::Invalid) e.valid = false;
    return e;
}

// The workshop's state while the game runs (app.js workshopDraft, workshopFilters...).
struct State {
    std::shared_ptr<const Catalog> catalog;
    Draft draft;
    std::string owner;                   // workshopOwner: the character the draft is for
    std::string filters[3];              // template, affixes, modifiers
    std::string kind = "all";            // the template type
    int limits[2] = {40, 40};            // rows shown: affixes, modifiers
    bool advanced = false;               // Add extra Unique / Set effects is open
    bool loading = false;
    std::map<std::string, int> blocks;   // the workshop's sections by kind
    // the catalog parsed on the worker thread, taken on the main thread
    std::mutex lock;
    std::shared_ptr<const Catalog> parsed;
    std::string parseError;
    std::atomic<bool> ready{false};
};
State& W() {
    static State s;
    return s;
}
const Catalog* Data() { return W().catalog.get(); }
bool Loaded() { return Data() && !Data()->items.empty(); }
int TiersOf(const std::string& id) {
    const Catalog* data = Data();
    if (!data) return 7;
    const auto it = data->affixAt.find(id);
    if (it == data->affixAt.end()) return 7;
    return std::min(data->affixes[it->second].tiers, kPrimordial);
}
const JValue* Forge() {
    const JValue* r = Cached("craftread");
    if (!r) return nullptr;
    if (const JValue* f = r->Find("forgePreview")) return f;
    return r->Find("preview");
}
bool True(const JValue* v) { return v && v->IsBool() && v->boolean; }

std::string Problem(const Draft& d, const Catalog* data, const JValue* forge) {  // crafting-model problem()
    if (!data || data->items.empty()) return "Load item choices before creating custom gear.";
    if (d.perfect != "0" && d.perfect != "1") return "Choose maximum or midpoint rolls.";
    if (d.templateId != "-1" && !data->itemAt.count(d.templateId)) return "Choose a template from the current item list.";
    const size_t limit = d.modifiers.empty() ? 7 : 6;
    if (std::set<std::string>(d.affixes.begin(), d.affixes.end()).size() != d.affixes.size() || d.affixes.size() > limit)
        return d.modifiers.empty() ? Fmt("Choose at most {0} different affixes.", {std::to_string(limit)})
                                   : Fmt("Choose at most {0} different affixes when adding extra effects.", {std::to_string(limit)});
    if (std::set<std::string>(d.modifiers.begin(), d.modifiers.end()).size() != d.modifiers.size() || d.modifiers.size() > 64)
        return "Choose at most 64 different extra effects.";
    for (const auto& id : d.affixes) if (!data->affixAt.count(id)) return "An affix is no longer in the current game catalog. Review your selection.";
    for (const auto& id : d.modifiers) if (!data->modifierAt.count(id)) return "An extra effect is no longer in the current game catalog. Review your selection.";
    int primordial = 0;
    for (const auto& id : d.affixes) {
        const Effective s = SettingOf(d, id);
        if (s.tier < 1 || s.tier > kPrimordial) return "Choose a tier from T1 to T8.";
        if (s.tier > TiersOf(id)) return "An affix has no T8 in the game. Choose T1 to T7 for it.";
        if (!s.random && !(s.valid && s.roll >= 1 && s.roll <= 100)) return "Choose a roll from 1 to 100%, or Random.";
        primordial += s.tier == kPrimordial;
    }
    if (primordial > 1) return "An item can have one T8 (primordial) affix.";
    int currentRegular = 0;
    if (forge && (*forge)["affixes"].IsArray())
        for (const auto& a : (*forge)["affixes"].items) currentRegular += data->affixAt.count(IdOf(a.Find("id"))) > 0;
    if (!d.modifiers.empty() && d.affixes.empty() && currentRegular > 6) return "This item has seven affixes. Choose up to six affixes to leave room for extra effects.";
    return "";
}
struct Summary { std::string templ, affixes, effects, rolls; };
Summary SummaryOf(const Draft& d) {  // crafting-model summary()
    Summary s;
    const Catalog* data = Data();
    const auto it = data ? data->itemAt.find(d.templateId) : decltype(data->itemAt.end()){};
    if (data && it != data->itemAt.end()) {
        const Item& t = data->items[it->second];
        s.templ = std::string(t.set ? "Set" : "Unique") + " \xC2\xB7 " + t.name;
    } else s.templ = "Exalted \xC2\xB7 keep this item\xE2\x80\x99s base";
    const size_t n = d.affixes.size();
    if (n) {
        std::string tiers;
        for (const auto& id : d.affixes) tiers += (tiers.empty() ? "T" : ", T") + std::to_string(SettingOf(d, id).tier);
        s.affixes = (n == 1 ? std::string("1 selected affix") : std::to_string(n) + " selected affixes") + " \xC2\xB7 " + tiers;
    } else s.affixes = d.modifiers.empty() ? "Keep the current affixes" : "Current affixes rebuilt at T7";
    s.effects = d.modifiers.empty() ? "No extra effects" : d.modifiers.size() == 1 ? "1 extra effect" : std::to_string(d.modifiers.size()) + " extra effects";
    s.rolls = d.perfect == "0" ? (n || !d.modifiers.empty() ? "Midpoint rolls" : "Midpoint for new effects \xC2\xB7 current affix rolls kept") : "Maximum rolls";
    return s;
}
std::string AffixesArg(const Draft& d) {  // crafting-model args(): id:tier:roll (r: random), or keep
    if (d.affixes.empty()) return "keep";
    std::string out;
    for (const auto& id : d.affixes) {
        const Effective s = SettingOf(d, id);
        out += (out.empty() ? "" : ",") + id + ":" + std::to_string(s.tier) + ":" + (s.random ? std::string("r") : std::to_string(s.roll));
    }
    return out;
}
// The reason Create custom item is off (updateDisabled), "" when it can run.
std::string CreateReason() {
    std::string reason;
    if (!Eligible(&reason)) return reason;
    const JValue* forge = Forge();
    const std::string live = forge ? IdOf(forge->Path("player.id")) : "";
    if (live.empty()) return Tr("Refresh live data to enable this action for the loaded character.");
    if (!True(forge->Find("canCustom"))) return Tr("Put an equipment item in the game's forge, then refresh the selected item.");
    const std::string problem = Problem(W().draft, Data(), forge);
    if (!problem.empty()) return Tr(problem);
    if (Data() && Data()->playerId != live) return Tr("Load item choices for the current character.");
    return "";
}

// -------------------------------------------------------------------- the catalog (worker)
std::string CamelWords(const std::string& s) {  // replace(/([a-z])([A-Z])/g, "$1 $2")
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        out += s[i];
        if (i + 1 < s.size() && s[i] >= 'a' && s[i] <= 'z' && s[i + 1] >= 'A' && s[i + 1] <= 'Z') out += ' ';
    }
    return out;
}
std::string Precise(double v) {  // String(Number(v.toPrecision(6)))
    if (!std::isfinite(v)) return "NaN";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return Num(std::strtod(buf, nullptr));
}
// workshopModifierEntries(): source · stat · tags · ailment · Variant n · amount (the parts not empty)
std::string ModifierName(const JValue& x, const JValue& stats, bool midpoint) {
    const auto nameOf = [](const JValue& list, double id) -> std::string {
        for (const auto& e : list.items) if (e["id"].Num(-1) == id) return e["name"].Str();
        return "";
    };
    std::vector<std::string> parts;
    parts.push_back(x["source"].Str());
    const double property = x["property"].Num(0);
    const std::string stat = nameOf(stats["properties"], property);
    parts.push_back(CamelWords(stat.empty() ? "Modifier " + Num(property) : stat));
    const int tags = static_cast<int>(static_cast<long long>(x["tags"].Num(0)));
    for (const auto& t : stats["tags"].items) {
        const int id = static_cast<int>(t["id"].Num(0));
        if (id > 0 && (tags & id) == id) parts.push_back(CamelWords(t["name"].Str()));
    }
    const double special = x["special"].Num(0);
    if (special != 0) parts.push_back(CamelWords(nameOf(stats["ailments"], special)));
    const double extra = x["extra"].Num(0);
    if (extra != 0) parts.push_back("Variant " + Num(extra));
    const double value = midpoint ? (x["value"].Num(0) + x["maximum"].Num(0)) / 2 : x["maximum"].Num(0);
    const double mode = x["mode"].Num(0);
    if (mode == 0) {
        const std::string amount = Precise(value);
        if (amount != "0" && amount != "NaN") parts.push_back(amount);  // filter(Boolean): 0 and NaN go
    } else {
        parts.push_back(Precise(value * 100) + "% " + (mode == 1 ? "increased" : "more"));
    }
    std::string out;
    for (const auto& p : parts) if (!p.empty()) out += (out.empty() ? "" : " \xC2\xB7 ") + p;
    return out;
}
// The five replies of the item choices (tools/build_backend.py workshop_catalog): checked and turned
// into the workshop's lists, on a worker thread (D() is only read).
void ParseCatalog(std::vector<std::string> replies) {
    State& w = W();
    auto data = std::make_shared<Catalog>();
    std::string error;
    std::vector<JValue> v(replies.size());
    for (size_t i = 0; i < replies.size() && error.empty(); ++i)
        if (!json::Parse(replies[i], v[i]) || !(v[i]["ok"].IsBool() && v[i]["ok"].boolean)) error = v[i]["error"].Str("Could not read workshop templates.");
    if (error.empty()) {
        // atlasread, lootread, identityread, craftmods, identityread: one character throughout
        const std::string id = IdOf(v[0].Path("player.id")), name = v[0]["player"]["name"].Str();
        for (size_t i : {size_t(2), size_t(4)})
            if (IdOf(v[i].Path("player.id")) != id || v[i]["player"]["name"].Str() != name) error = "The loaded character changed. Load workshop templates again.";
        data->playerId = id;
        data->playerName = name;
    }
    if (error.empty()) {
        for (const auto& x : v[0]["items"].items) {
            Item it;
            it.id = IdOf(x.Find("id"));
            it.name = x["name"].Str();
            it.set = x["set"].IsBool() && x["set"].boolean;
            it.label = std::string(it.set ? "Set \xC2\xB7 " : "Unique \xC2\xB7 ") + Clean(it.name);
            it.lower = Lower(it.label);
            data->itemAt[it.id] = data->items.size();
            data->items.push_back(std::move(it));
        }
        for (const auto& x : v[1]["affixes"].items) {
            Affix a;
            a.id = IdOf(x.Find("id"));
            a.name = Clean(x["name"].Str());
            a.lower = Lower(a.name);
            const double n = x["tiers"].Num(0);
            a.tiers = std::floor(n) == n && n > 0 ? static_cast<int>(n) : 7;
            for (const auto& k : x["classes"].items) a.classes.push_back(k.Str());
            data->affixAt[a.id] = data->affixes.size();
            data->affixes.push_back(std::move(a));
        }
        const JValue& stats = D()["characterStats"];
        for (const auto& x : v[3]["modifiers"].items) {
            Modifier m;
            m.id = IdOf(x.Find("id"));
            for (int k = 0; k < 2; ++k) {
                m.names[k] = ModifierName(x, stats, k == 1);
                m.lowers[k] = Lower(m.names[k]);
            }
            data->modifierAt[m.id] = data->modifiers.size();
            data->modifiers.push_back(std::move(m));
        }
    }
    std::lock_guard<std::mutex> guard(w.lock);
    w.parsed = error.empty() ? std::shared_ptr<const Catalog>(data) : nullptr;
    w.parseError = error;
    w.ready = true;
}
void RebuildBlocks(std::initializer_list<const char*> kinds) {
    for (const char* k : kinds) {
        const auto it = W().blocks.find(k);
        if (it != W().blocks.end()) Rebuild(it->second);
    }
}
void RebuildAll() {
    const auto blocks = W().blocks;
    for (const auto& [kind, id] : blocks) Rebuild(id);
}
void LoadCatalog() {
    State& w = W();
    if (w.loading) return;
    w.loading = true;
    RebuildBlocks({"ws_head"});
    auto replies = std::make_shared<std::vector<std::string>>();
    auto step = std::make_shared<std::function<void(size_t)>>();
    std::weak_ptr<std::function<void(size_t)>> weak = step;
    static const char* const reads[] = {"atlasread", "lootread", "identityread", "craftmods", "identityread"};
    *step = [replies, weak](size_t i) {
        if (i == 5) {
            std::thread([r = *replies] { ParseCatalog(r); }).detach();
            return;
        }
        auto self = weak.lock();  // the reply's callback keeps the chain alive
        Send(reads[i], [replies, self, i](const std::string& reply) {
            std::string why;
            if (Refused(reply, &why)) {
                W().loading = false;
                Toast(PlayerError(why), true);
                RebuildBlocks({"ws_head"});
                return;
            }
            replies->push_back(reply);
            if (self) (*self)(i + 1);
        });
    };
    (*step)(0);
}
// The parsed catalog arrives (ws_head's tick): kept; another character's draft starts over.
void TakeCatalog() {
    State& w = W();
    if (!w.ready) return;
    std::shared_ptr<const Catalog> data;
    std::string error;
    {
        std::lock_guard<std::mutex> guard(w.lock);
        data = std::move(w.parsed);
        error = w.parseError;
        w.ready = false;
    }
    w.loading = false;
    if (!data) {
        Toast(PlayerError(error), true);
        RebuildBlocks({"ws_head"});
        return;
    }
    if (!w.owner.empty() && w.owner != data->playerId) w.draft = Draft{};
    w.owner = data->playerId;
    w.catalog = std::move(data);
    RebuildAll();
}

// ------------------------------------------------------------------------- drawing helpers
Style Text(float size, const char* color, float line = 0, bool wrap = true) {
    Style s{Face::Body, size, C(color)};
    s.line = line;
    s.wrap = wrap;
    return s;
}
// A block of the panel: a node at the panel's place; x 0 = the panel's left edge.
Ctx Block(const Ctx& c) {
    Node n = New("Block", c.parent, kPadX, 0, kPanelW, 10);
    return c.At(n.rt, kPadX, 0);
}
void PlaceButton(const Ctx& in, Button& b, float x, float y) {
    Move(b.node.rt, x, y);
    (*in.hits)[b.hit].x = in.ox + x;
    (*in.hits)[b.hit].y = in.oy + y;
}
// .craft-step: margin-top 22, a line on top, padding-top 24; the h3 (a 30px accent circle with the
// number, Georgia 18) and its 14px margin. Returns the y after them.
float StepHead(const Ctx& in, float y, const std::string& number, const std::string& title) {
    y += 22;
    Solid(in.parent, "Step border", kPanelPad, y, kPanelInner, 1, C("line"));
    y += 1 + 24;
    Picture(in.parent, "Step circle", kPanelPad, y, 30, 30, "ring-30.png", false, C("accent"));
    Style n{Face::Body, 13, C("accent")};
    n.align = kCenter;
    TextBox(in.parent, "Step number", kPanelPad, y, 30, 30, n, number);
    Style h3{Face::Serif, 18, C("ink")};
    h3.align = kLeft;
    TextBox(in.parent, "Step title", kPanelPad + 30 + 12, y, kPanelInner - 42, 30, h3, title);
    return y + 30 + 14;
}
float StepText(const Ctx& in, float y, const std::string& text) {  // .craft-step p
    return Paragraph(in.parent, "Text", kPanelPad, y, kPanelInner, Text(13, "muted", 1.7f), text);
}
float FieldLabelText(const Ctx& in, float x, float y, float w, const std::string& text) {  // a label's own line (12px)
    const Style s = Text(12, "muted", 0, false);
    TextBox(in.parent, "Label", x, y, w, LineHeight(s), s, text);
    return LineHeight(s);
}
// a closed select (its own text size); OpenList below it on click
void SelectBox(const Ctx& in, float x, float y, float w, float h, float size, const std::string& text, const std::string& name, Action click) {
    Fill(in.parent, "Select", x, y, w, h, Hex("#080e0e"), 3);
    void* frame = Outline(in.parent, "Select border", x, y, w, h, Hex("#7b745a99"), 3);
    Style cs{Face::Body, size, C("ink")};
    cs.align = kLeft;
    const float pad = size < 12 ? 7.f : 11.f;
    TextBox(in.parent, "Choice", x + pad, y, std::max(10.f, w - pad - 22), h, cs, text);
    Style ar{Face::Body, size < 12 ? 9.f : 11.f, C("ink")};
    ar.align = kCenter;
    TextBox(in.parent, "Arrow", x + w - (size < 12 ? 18 : 26), y, 14, h, ar, "\xE2\x8C\x84");
    Hit hit;
    hit.kind = HitKind::Action;
    hit.name = name;
    hit.x = x;
    hit.y = y;
    hit.w = w;
    hit.h = h;
    AddHover(hit, frame, Hex("#7b745a99"), Hex("#7b745a99"));
    hit.action = std::move(click);
    in.Add(std::move(hit));
}
void OpenBelow(const Ctx& in, float x, float y, float w, float h, const std::vector<std::string>& labels, int current, std::function<void(int)> chosen) {
    float wx = 0, wy = 0;
    if (WindowPoint(in, x, y, &wx, &wy)) OpenList(wx, wy, w, h, labels, current, std::move(chosen));
}
float SearchInput(const Ctx& in, float y, float w, const std::string& value, const std::string& placeholder, std::function<void(const std::string&)> changed,
                  const std::string& name, float x = kPanelPad) {
    Style s = Text(12, "ink", 0, false);
    s.align = kLeft;
    const float h = LineHeight(s) + 22;
    MakeInput(in, x, y, w, h, s, InputLook{Hex("#080e0e"), Hex("#7b745a99")}, value, placeholder, false, std::move(changed), name);
    return h;
}

// ------------------------------------------------------------------------------ the lists
// workshopPicker(kind): "{n} matches · {m} selected", the list (40 rows, Show more adds 40), each
// row a checkbox and the name; a checked row has the soft colour.
struct PickRow { std::string id, name, tag; bool checked = false, made = false; float top = 0, h = 43.8f; Pin node; int hit = -1; };
struct Picker {
    int kind = 0;  // 0 affixes, 1 extra effects
    int section = -1;
    std::vector<PickRow> rows;
    size_t matches = 0, made = 0;
    Pin content, count, thumb;
    float listY = 0, viewH = 0, contentH = 0, offset = 0, rowW = 1079;
    bool scrolls = false;
    Ctx in;
};
constexpr float kRowEstimate = 43.8f;
const char* const kListKinds[2] = {"ws_affix_list", "ws_mod_list"};
const char* const kRowKinds[2] = {"ws_affix_rows", "ws_mod_rows"};
std::vector<std::string>& Chosen(int kind) { return kind ? W().draft.modifiers : W().draft.affixes; }
std::string CountText(const Picker& v) {
    return Fmt("{0} matches \xC2\xB7 {1} selected", {std::to_string(v.matches), std::to_string(Chosen(v.kind).size())});
}
void MakeRow(Picker& v, size_t i);
void PlaceRows(Picker& v) {
    Section* s = FindSection(v.section);
    if (!s) return;
    float top = 0;
    for (auto& r : v.rows) {
        if (r.top != top) {
            r.top = top;
            if (r.made) Move(r.node.Get(), 0, top);
        }
        top += r.h;
    }
    v.contentH = top;
    const float viewTop = v.listY + 1;
    for (auto& r : v.rows) {
        if (r.hit < 0 || r.hit >= static_cast<int>(s->hits.size())) continue;
        Hit& h = s->hits[r.hit];
        const float y0 = std::max(viewTop, viewTop + r.top - v.offset), y1 = std::min(viewTop + v.viewH, viewTop + r.top + r.h - v.offset);
        h.visible = r.made && y1 > y0;
        h.y = v.in.oy + y0;
        h.h = std::max(0.f, y1 - y0);
    }
    if (v.thumb) {
        // the scrollbar between its 15px arrows
        const float track = v.viewH - 30, length = std::max(18.f, track * v.viewH / std::max(v.contentH, v.viewH));
        const float at = v.contentH > v.viewH ? (track - length) * v.offset / (v.contentH - v.viewH) : 0;
        Move(v.thumb.Get(), kPanelPad + 1 + v.rowW + 3, v.listY + 1 + 15 + at);
        Resize(v.thumb.Get(), 9, length);
    }
}
// a few more rows (the section's tick makes the rows below the view)
void MakeRows(Picker& v, size_t budget) {
    for (size_t i = 0; i < v.rows.size() && budget; ++i) {
        if (v.rows[i].made) continue;
        MakeRow(v, i);
        --budget;
    }
    PlaceRows(v);
}
void ScrollTo(Picker& v, float offset) {
    v.offset = std::clamp(offset, 0.f, std::max(0.f, v.contentH - v.viewH));
    Move(v.content.Get(), 0, -v.offset);
    // the rows that come into view are made now
    for (size_t i = 0; i < v.rows.size(); ++i)
        if (!v.rows[i].made && v.rows[i].top < v.offset + v.viewH + kRowEstimate) MakeRow(v, i);
    PlaceRows(v);
}
void Toggle(int sid, size_t index);
void MakeRow(Picker& v, size_t i) {
    PickRow& r = v.rows[i];
    if (r.node) {
        void* go = GameObjectOf(r.node.Get());
        SetActive(go, false);
        void* d[]{go};
        managed::Invoke(U("Object", "Destroy", 1), nullptr, d);
    }
    Node node = New("Row", v.content.Get(), 0, r.top, v.rowW, 10);
    void* fill = Fill(node.rt, "Checked", 0, 0, v.rowW, 10, r.checked ? C("soft") : Alpha(C("soft"), 0));
    // label: padding 11px 14px, gap 12; the 16px checkbox 2px down; 13px text, line-height 1.6
    const float textX = 14 + 16 + 12;
    // a class-specific affix: its classes in a gold tag at the right
    float tagW = 0;
    if (!r.tag.empty()) {
        Style ts{Face::BodySemi, 11, C("gold")};
        ts.align = kCenter;
        tagW = TextAdvance(Face::BodySemi, 11, r.tag) + 14;
        const float tx = v.rowW - 14 - tagW;
        Fill(node.rt, "Class tag", tx, 13, tagW, 18, Alpha(C("gold"), .1f), 3);
        Outline(node.rt, "Class tag border", tx, 13, tagW, 18, Alpha(C("gold"), .55f), 3);
        TextBox(node.rt, "Class tag text", tx, 13, tagW, 18, ts, r.tag);
        tagW += 10;
    }
    const float textH = Paragraph(node.rt, "Name", textX, 11, v.rowW - textX - 14 - tagW, Text(13, "ink", 1.6f), Tr(r.name));
    CheckBox(node.rt, 14, 13, 16, r.checked);
    r.h = 11 + textH + 11 + 1;
    Solid(node.rt, "Row border", 0, r.h - 1, v.rowW, 1, Hex("#92734025"));
    Resize(TransformOf(fill), v.rowW, r.h);
    Resize(node.rt, v.rowW, r.h);
    r.node.Set(node.rt);
    if (!r.made) ++v.made;
    r.made = true;
}
void UpdateCount(Picker& v) { if (v.count) SetText(v.count.Get(), CountText(v)); }
// After a tick the browser built the list again (scrolled to its top) and gave the box the
// keyboard: shown whole, else centred.
void FocusRow(Picker& v, size_t index) {
    const PickRow& r = v.rows[index];
    float offset = 0;
    if (r.top + r.h > v.viewH) offset = r.top + r.h / 2 - v.viewH / 2;
    ScrollTo(v, offset);
}
std::shared_ptr<Picker> PickerOf(int kind) {
    const auto it = W().blocks.find(kListKinds[kind]);
    Section* s = it == W().blocks.end() ? nullptr : FindSection(it->second);
    return s ? std::static_pointer_cast<Picker>(s->live) : nullptr;
}
void Toggle(int sid, size_t index) {
    Section* s = FindSection(sid);
    auto v = s ? std::static_pointer_cast<Picker>(s->live) : nullptr;
    if (!v || index >= v->rows.size()) return;
    PickRow& r = v->rows[index];
    auto& chosen = Chosen(v->kind);
    const auto at = std::find(chosen.begin(), chosen.end(), r.id);
    const size_t before = W().draft.modifiers.size();
    const bool adding = at == chosen.end();
    if (adding) chosen.push_back(r.id);
    else chosen.erase(at);
    r.checked = adding;
    MakeRow(*v, index);
    UpdateCount(*v);
    FocusRow(*v, index);
    // updateWorkshopUi built the other list again too: back at its top
    if (auto other = PickerOf(1 - v->kind)) ScrollTo(*other, 0);
    RebuildBlocks({kRowKinds[v->kind], "ws_review"});
    if ((before == 0) != W().draft.modifiers.empty()) RebuildBlocks({"ws_affix_head"});
}
float BuildPicker(Section& sec, const Ctx& c, int kind) {
    W().blocks[kListKinds[kind]] = sec.id;
    sec.after = 0;
    if (kind == 1 && !W().advanced) return 0;  // inside the closed <details>
    const Ctx in = Block(c);
    auto v = std::make_shared<Picker>();
    sec.live = v;
    v->kind = kind;
    v->section = sec.id;
    v->in = in;
    // the entries matching every word of the search, the first limit of them shown
    const Catalog* data = Data();
    const auto words = Words(W().filters[kind + 1]);
    const auto& chosen = Chosen(kind);
    const int midpoint = W().draft.perfect == "0" ? 1 : 0;
    size_t limit = static_cast<size_t>(W().limits[kind]);
    if (data) {
        const auto add = [&](const std::string& id, const std::string& name, const std::string& lower, const std::string& tag) {
            if (!Matches(lower, words)) return;
            ++v->matches;
            if (v->rows.size() >= limit) return;
            PickRow r;
            r.id = id;
            r.name = name;
            r.tag = tag;
            r.checked = std::find(chosen.begin(), chosen.end(), id) != chosen.end();
            v->rows.push_back(std::move(r));
        };
        const auto tag = [](const std::vector<std::string>& classes) {
            std::string out;
            for (const auto& k : classes) out += (out.empty() ? "" : ", ") + Tr(k);
            return out;
        };
        if (kind == 0) for (const auto& a : data->affixes) add(a.id, a.name, a.lower, tag(a.classes));
        else for (const auto& m : data->modifiers) add(m.id, m.names[midpoint], m.lowers[midpoint], "");
    }
    // .craft-pick-count: margin 14px 0 10px
    float y = 14;
    const Style cs = Text(11, "muted", 0, false);
    v->count.Set(TextBox(in.parent, "Count", kPanelPad, y, kPanelInner, LineHeight(cs), cs, CountText(*v)));
    y += LineHeight(cs) + 10;
    v->listY = y;
    // .craft-pick-list: max-height 240, line border, radius 4, #0002
    Node box = New("List", in.parent, kPanelPad, y, kPanelInner, 10);
    void* bg = Fill(box.rt, "Background", 0, 0, kPanelInner, 10, Hex("#00000022"), 4);
    void* frame = Outline(box.rt, "Border", 0, 0, kPanelInner, 10, C("line"), 4);
    if (v->rows.empty()) {
        // p.muted: padding 8px 14px, its 10px margins inside the border
        const float th = Paragraph(box.rt, "Empty", 1 + 14, 1 + 10 + 8, kPanelInner - 2 - 28, Text(13, "muted", 1.7f),
                                   Tr(Loaded() ? "No matches. Try another search." : "Load item choices above to see the available options."));
        const float h = 1 + 10 + 8 + th + 8 + 10 + 1;
        Resize(TransformOf(bg), kPanelInner, h);
        Resize(TransformOf(frame), kPanelInner, h);
        return y + h;
    }
    v->scrolls = v->rows.size() * kRowEstimate > 238;
    v->rowW = kPanelInner - 2 - (v->scrolls ? 15 : 0);
    Node clip = New("View", box.rt, 1, 1, kPanelInner - 2, 10);
    Clip(clip);
    Node content = New("Rows", clip.rt, 0, 0, v->rowW, 10);
    v->content.Set(content.rt);
    float top = 0;
    for (auto& r : v->rows) { r.top = top; top += r.h; }
    // the rows in view now (a short list: all, so its height is known)
    for (size_t i = 0; i < v->rows.size(); ++i) {
        if (v->rows[i].top > 240 + kRowEstimate) break;
        MakeRow(*v, i);
        float t = 0;
        for (auto& r : v->rows) { r.top = t; t += r.h; }
    }
    if (!v->scrolls) for (size_t i = 0; i < v->rows.size(); ++i) if (!v->rows[i].made) MakeRow(*v, i);
    float total = 0;
    for (const auto& r : v->rows) total += r.h;
    if (!v->scrolls && 2 + total > 240) {
        v->scrolls = true;
        v->rowW = kPanelInner - 2 - 15;
        Resize(content.rt, v->rowW, 10);
        total = 0;
        for (size_t i = 0; i < v->rows.size(); ++i) {
            v->rows[i].top = total;
            MakeRow(*v, i);
            total += v->rows[i].h;
        }
    }
    const float h = std::min(240.f, 2 + total);
    v->viewH = h - 2;
    Resize(clip.rt, kPanelInner - 2, v->viewH);
    Resize(TransformOf(bg), kPanelInner, h);
    Resize(TransformOf(frame), kPanelInner, h);
    if (v->scrolls) {
        // the list's own scrollbar (the page's scrollbar colours): arrows and a gold thumb
        Style ar{Face::Body, 7, C("gold")};
        ar.align = kCenter;
        TextBox(in.parent, "Scroll up", kPanelPad + 1 + v->rowW, y + 1, 15, 15, ar, "\xE2\x96\xB2");
        TextBox(in.parent, "Scroll down", kPanelPad + 1 + v->rowW, y + h - 1 - 15, 15, 15, ar, "\xE2\x96\xBC");
        v->thumb.Set(TransformOf(Fill(in.parent, "Thumb", kPanelPad + 1 + v->rowW + 3, y + 16, 9, 40, C("gold"), 4)));
        Section::Wheel wheel;
        wheel.x = in.ox + kPanelPad + 1;
        wheel.y = in.oy + y + 1;
        wheel.w = kPanelInner - 2;
        wheel.h = v->viewH;
        std::weak_ptr<Picker> weak = v;
        wheel.scroll = [weak](float notches) {
            if (auto p = weak.lock()) ScrollTo(*p, p->offset - notches * 100);
        };
        sec.wheels.push_back(std::move(wheel));
    }
    // a hit for each row; placed (and clipped to the view) by PlaceRows
    for (size_t i = 0; i < v->rows.size(); ++i) {
        Hit hit;
        hit.kind = HitKind::Action;
        hit.name = std::string(kind ? "workshop-modifier#" : "workshop-affix#") + v->rows[i].id;
        hit.x = kPanelPad + 1;
        hit.w = v->rowW;
        hit.visible = false;
        const int sid = sec.id;
        hit.action = [sid, i] { Toggle(sid, i); };
        v->rows[i].hit = in.Add(std::move(hit));
    }
    PlaceRows(*v);
    y += h;
    if (v->matches > v->rows.size()) {
        // button.quiet: Show more ({shown} of {total}), right below the list
        const int sid = sec.id;
        Button more = AddButton(in, kPanelPad, y, Fmt("Show more ({0} of {1})", {std::to_string(v->rows.size()), std::to_string(v->matches)}), Look::Quiet,
                                [sid, kind] {
                                    W().limits[kind] += 40;
                                    Rebuild(sid);
                                }, false, kind ? "workshop-more-modifiers" : "workshop-more-affixes");
        y += more.h;
    }
    return y;
}
void MakePicker(Section& s, int kind) {
    s.build = [kind](Section& sec, const Ctx& c) { return BuildPicker(sec, c, kind); };
    // the rows below the view are made a few a frame
    s.tick = [](Section& sec) {
        auto v = std::static_pointer_cast<Picker>(sec.live);
        if (v && v->made < v->rows.size()) MakeRows(*v, 6);
    };
}

// ----------------------------------------------------------------------------- the blocks
// 1. the panel's top: the heading with Load / Refresh item choices, the muted paragraph
void MakeHead(Section& s) {
    s.build = [](Section& sec, const Ctx& c) {
        W().blocks["ws_head"] = sec.id;
        auto frame = std::make_shared<Panel>(OpenPanel(c.parent, kPadX, 0, kPanelW));
        sec.frame = frame;
        sec.after = 0;
        const Ctx in = c.At(frame->rt.Get(), kPadX, 0);
        Style h2{Face::Serif, 27, C("ink")};
        const float lh = LineHeight(h2);
        Button b = AddButton(in, kPanelPad + kPanelInner, kPanelPad, Tr(Loaded() ? "Refresh item choices" : "Load item choices"), Look::Normal,
                             [] { LoadCatalog(); }, true, "craft_custom_catalog");
        const float row = std::max(lh, b.h);
        PlaceButton(in, b, kPanelPad + kPanelInner - b.w, kPanelPad + (row - b.h) / 2);
        if (W().loading) SetEnabled(in, b, false);
        TextBox(in.parent, "Title", kPanelPad, kPanelPad + (row - lh) / 2, kPanelInner - b.w - 15, lh, h2, Tr("Custom Items"));
        const float y = kPanelPad + row + 16;
        return y + Paragraph(in.parent, "Text", kPanelPad, y, kPanelInner, Text(14, "muted", 1.7f),
                             Tr("Keep your item\xE2\x80\x99s equipment base. Choose its template and affixes, then review the result before creating it."));
    };
    s.tick = [](Section&) { TakeCatalog(); };
}
// 2. Select the item in the game: the forge's equipment item, Refresh selected item
std::string ItemShown() {
    const JValue* f = Forge();
    if (!f || !True(f->Find("canCustom"))) return "Waiting for an equipment item in the game\xE2\x80\x99s forge.";
    const JValue* fp = f->Find("forgingPotential");
    return "Equipment selected \xC2\xB7 " + (fp && fp->IsNumber() ? Num(fp->number) : std::string("\xE2\x80\x94")) + " Forging Potential \xC2\xB7 " +
           std::to_string((*f)["affixes"].items.size()) + " affixes";
}
void MakeItem(Section& s) {
    s.reads.insert("craftread");
    s.build = [](Section& sec, const Ctx& c) {
        W().blocks["ws_item"] = sec.id;
        sec.after = 0;
        sec.state["shown"] = ItemShown();
        const Ctx in = Block(c);
        float y = StepHead(in, 0, "1", Tr("Select the item in the game"));
        y += StepText(in, y, Tr("Open the game\xE2\x80\x99s forge and place the equipment item you want to change.")) + 10;
        // .craft-item-state: padding 16, soft, line border, radius 4; the text and the button centred
        void* bg = Fill(in.parent, "State", kPanelPad, y, kPanelInner, 10, C("soft"), 4);
        void* frame = Outline(in.parent, "State border", kPanelPad, y, kPanelInner, 10, C("line"), 4);
        Button b = AddButton(in, kPanelPad, y, Tr("Refresh selected item"), Look::Inline13, [] { Read("craftread"); }, false, "craft_read");
        const Style ts = Text(13, "ink", 1.6f);
        const float textW = kPanelInner - 34 - 14 - b.w;
        void* text = TextBox(in.parent, "State text", kPanelPad + 17, 0, textW, LineHeight(ts), ts, Tr(ItemShown()));
        const float textH = std::max(1, TextLines(text)) * LineHeight(ts);
        const float inner = std::max(textH, b.h), boxH = 34 + inner;
        Move(TransformOf(text), kPanelPad + 17, y + 17 + (inner - textH) / 2 + TopOffset(ts));
        PlaceButton(in, b, kPanelPad + kPanelInner - 17 - b.w, y + 17 + (inner - b.h) / 2);
        Resize(TransformOf(bg), kPanelInner, boxH);
        Resize(TransformOf(frame), kPanelInner, boxH);
        return y + boxH + 24;
    };
    s.onData = [](Section& sec, const std::string&) {
        if (sec.state["shown"] != ItemShown()) Rebuild(sec.id);
    };
}
// 3. Choose a template: the search, the type, the template select
void MakeTemplate(Section& s) {
    s.build = [](Section& sec, const Ctx& c) {
        W().blocks["ws_template"] = sec.id;
        sec.after = 0;
        const Ctx in = Block(c);
        float y = StepHead(in, 0, "2", Tr("Choose a template"));
        y += StepText(in, y, Tr("Use Exalted for affixes only, or copy one Unique / Set template onto this base.")) + 10;
        // .craft-template-search: minmax(0,2fr) minmax(130px,1fr), gap 14
        const float col1 = (kPanelInner - 14) * 2 / 3, col2 = kPanelInner - 14 - col1, x2 = kPanelPad + col1 + 14;
        const float lh = FieldLabelText(in, kPanelPad, y, col1, Tr("Find a template"));
        SearchInput(in, y + lh, col1, W().filters[0], Tr("Search item names..."), [](const std::string& t) { W().filters[0] = t; }, "workshop-template-search");
        FieldLabelText(in, x2, y, col2, Tr("Type"));
        const std::vector<std::string> kinds{"all", "unique", "set"};
        const std::vector<std::string> kindLabels{Tr("All templates"), Tr("Unique"), Tr("Set")};
        const int kindAt = static_cast<int>(std::find(kinds.begin(), kinds.end(), W().kind) - kinds.begin());
        const Style ss = Text(12, "ink", 0, false);
        const float selH = SelectHeight(ss);
        const int sid = sec.id;
        SelectBox(in, x2, y + lh, col2, selH, 12, kindLabels[kindAt % 3], "workshop-kind", [in, x2, y, lh, col2, selH, kinds, kindLabels, kindAt, sid] {
            OpenBelow(in, x2, y + lh, col2, selH, kindLabels, kindAt, [kinds, sid](int i) {
                W().kind = kinds[i];
                Rebuild(sid);
            });
        });
        y += lh + selH + 14;
        // label: Template, the select (Exalted, then the matching templates; the chosen one stays)
        const float lt = FieldLabelText(in, kPanelPad, y, kPanelInner, Tr("Template"));
        std::string current = Tr("Exalted \xC2\xB7 no Unique / Set template");
        if (const Catalog* data = Data()) {
            const auto it = data->itemAt.find(W().draft.templateId);
            if (it != data->itemAt.end()) current = Tr(data->items[it->second].label);
        }
        const float ty = y + lt;
        SelectBox(in, kPanelPad, ty, kPanelInner, selH, 12, current, "workshop-template", [in, ty, selH] {
            const Catalog* data = Data();
            std::vector<std::string> ids{"-1"}, labels{Tr("Exalted \xC2\xB7 no Unique / Set template")};
            int at = 0;
            if (data) {
                const auto words = Words(W().filters[0]);
                const std::string& kind = W().kind;
                const std::string& chosen = W().draft.templateId;
                bool listed = false;
                std::vector<size_t> matches;
                for (size_t i = 0; i < data->items.size(); ++i) {
                    const Item& x = data->items[i];
                    if ((kind == "all" || (kind == "set" ? x.set : !x.set)) && Matches(x.lower, words)) {
                        matches.push_back(i);
                        listed |= x.id == chosen;
                    }
                }
                const auto chosenAt = data->itemAt.find(chosen);
                if (!listed && chosenAt != data->itemAt.end()) matches.insert(matches.begin(), chosenAt->second);
                for (size_t i : matches) {
                    if (data->items[i].id == chosen) at = static_cast<int>(ids.size());
                    ids.push_back(data->items[i].id);
                    labels.push_back(Tr(data->items[i].label));
                }
            }
            OpenBelow(in, kPanelPad, ty, kPanelInner, selH, labels, at, [ids](int i) {
                W().draft.templateId = ids[i];
                RebuildBlocks({"ws_template", "ws_review"});
            });
        });
        return ty + selH + 14 + 24;
    };
}
// 4. Choose T7 affixes: the text (six with extra effects), the search
void MakeAffixHead(Section& s) {
    s.build = [](Section& sec, const Ctx& c) {
        W().blocks["ws_affix_head"] = sec.id;
        sec.after = 0;
        const Ctx in = Block(c);
        float y = StepHead(in, 0, "3", Tr("Choose T7 affixes"));
        y += StepText(in, y, Fmt("Click to add or remove an affix. Choose up to {0}; leave all unchecked to keep the item\xE2\x80\x99s current affixes.",
                                 {W().draft.modifiers.empty() ? "7" : "6"})) + 14;
        // .craft-step>label (block, margin 14px 0): its line, the search
        const float lh = FieldLabelText(in, kPanelPad, y, kPanelInner, Tr("Find an affix"));
        const float h = SearchInput(in, y + lh, kPanelInner, W().filters[1], Tr("Search health, damage, resistance..."), [](const std::string& t) {
            W().filters[1] = t;
            W().limits[0] = 40;
            RebuildBlocks({"ws_affix_list"});
        }, "workshop-affix-search");
        return y + lh + h;
    };
}
// 5. the chosen affixes: a row each (tier, roll, Random, ×) or the muted line
float ChipButton(const Ctx& in, float x, float y, float w, float padX, float padY, const std::string& text, const std::string& name, Action click,
                 float* height = nullptr) {
    // .craft-chip: 11px, line-height 1.6, soft, accent border, radius 3; the text wraps in max-width
    const Style ts = Text(11, "ink", 1.6f);
    const float lh = LineHeight(ts), room = std::max(10.f, w - 2 * padX - 2);
    void* fill = Fill(in.parent, "Chip", x, y, 10, 10, C("soft"), 3);
    void* frame = Outline(in.parent, "Chip border", x, y, 10, 10, C("accent"), 3);
    void* t = TextBox(in.parent, "Chip text", x + 1 + padX, y + 1 + padY, room, lh, ts, text);
    const int lines = std::max(1, TextLines(t));
    const float textW = lines > 1 ? room : std::min(room, std::ceil(TextWidth(t)));
    const float cw = textW + 2 * padX + 2, ch = lines * lh + 2 * padY + 2;
    Resize(TransformOf(fill), cw, ch);
    Resize(TransformOf(frame), cw, ch);
    Hit hit;
    hit.kind = HitKind::Action;
    hit.name = name;
    hit.x = x;
    hit.y = y;
    hit.w = cw;
    hit.h = ch;
    hit.action = std::move(click);
    in.Add(std::move(hit));
    if (height) *height = ch;
    return cw;
}
void RemoveChosen(int kind, const std::string& id) {
    auto& chosen = Chosen(kind);
    chosen.erase(std::remove(chosen.begin(), chosen.end(), id), chosen.end());
    // updateWorkshopUi: both lists, both chips, the summary
    RebuildBlocks({"ws_affix_list", "ws_affix_rows", "ws_mod_list", "ws_mod_rows", "ws_review"});
    if (kind == 1 && chosen.empty()) RebuildBlocks({"ws_affix_head"});
}
void MakeAffixRows(Section& s) {
    s.build = [](Section& sec, const Ctx& c) {
        W().blocks["ws_affix_rows"] = sec.id;
        sec.after = 0;
        const Ctx in = Block(c);
        float y = 14;  // .craft-chips: margin-top 14
        const Draft& d = W().draft;
        if (d.affixes.empty()) {
            y += Paragraph(in.parent, "Empty", kPanelPad, y, kPanelInner, Text(12, "muted", 1.6f), Tr("No replacements selected. Current affixes are kept."));
            return y + 24;
        }
        const Catalog* data = Data();
        for (size_t i = 0; i < d.affixes.size(); ++i) {
            const std::string id = d.affixes[i];
            std::string name = id;
            if (data) if (const auto it = data->affixAt.find(id); it != data->affixAt.end()) name = data->affixes[it->second].name;
            const Effective st = SettingOf(d, id);
            const int tiers = TiersOf(id);
            // .craft-affix-row: padding 6px 8px, line border, radius 3, #0a121080; gap 12; the name takes the rest
            const float inner = kPanelInner - 2 - 16;
            const Style ls = Text(11, "muted", 0, false);
            const std::string tierText = Tr("Tier"), rollText = Tr("Roll"), randomText = Tr("Random");
            std::vector<std::string> options;
            float widest = 0;
            for (int t = 1; t <= tiers; ++t) {
                options.push_back(t == kPrimordial ? Tr("T8 \xC2\xB7 primordial") : "T" + std::to_string(t));
                widest = std::max(widest, TextAdvance(Face::Body, 11, options.back()));
            }
            const float selW = std::ceil(widest) + 35;
            const float tierW = std::ceil(TextAdvance(Face::Body, 11, tierText)) + 6 + selW;
            const float percentW = TextAdvance(Face::Body, 11, "%");
            const float rollW = std::ceil(TextAdvance(Face::Body, 11, rollText)) + 6 + 64 + 3 + percentW;
            const float randomW = 4 + 18 + 3 + 6 + std::ceil(TextAdvance(Face::Body, 11, randomText));
            const float xW = std::ceil(TextAdvance(Face::Body, 11, "\xC3\x97")) + 18 + 2;
            const float nameW = std::max(180.f, inner - (tierW + rollW + randomW + xW) - 4 * 12);
            const Style ns = Text(12, "ink", 1.6f);
            void* bg = Fill(in.parent, "Affix row", kPanelPad, y, kPanelInner, 10, Hex("#0a121080"), 3);
            void* frame = Outline(in.parent, "Affix row border", kPanelPad, y, kPanelInner, 10, C("line"), 3);
            void* nameText = TextBox(in.parent, "Affix name", kPanelPad + 9, 0, nameW, LineHeight(ns), ns, Tr(name));
            const float nameH = std::max(1, TextLines(nameText)) * LineHeight(ns);
            const float content = std::max(27.6f, nameH), rowH = content + 12 + 2, cy = y + 1 + 6;
            Move(TransformOf(nameText), kPanelPad + 9, cy + (content - nameH) / 2 + TopOffset(ns));
            Resize(TransformOf(bg), kPanelInner, rowH);
            Resize(TransformOf(frame), kPanelInner, rowH);
            const float right = kPanelPad + kPanelInner - 1 - 8;
            float x = right - xW;
            // ×: remove it from the item
            ChipButton(in, x, cy + (content - 27.6f) / 2, xW, 9, 4, "\xC3\x97", "workshop-remove-affix#" + id, [id] { RemoveChosen(0, id); });
            // Random: the checkbox (its 3px/4px margins) and the word
            x -= 12 + randomW;
            const bool random = st.random;
            const float ry = cy + (content - 24) / 2;
            void* box = CheckBox(in.parent, x + 4, ry + 3, 18, random);
            TextBox(in.parent, "Random", x + 4 + 18 + 3 + 6, ry, randomW, 24, Style{Face::Body, 11, C("muted"), 0, 0, kLeft}, randomText);
            {
                Hit hit;
                hit.kind = HitKind::Action;
                hit.name = "workshop-random#" + id;
                hit.x = x;
                hit.y = ry;
                hit.w = randomW;
                hit.h = 24;
                AddHover(hit, box, random ? C("accent") : Hex("#858585"), random ? C("accent") : Hex("#acacac"));
                hit.action = [id, random] {
                    Setting& set = W().draft.settings[id];
                    if (!random) set.roll = Setting::Random;
                    else {
                        set.roll = Setting::Number;
                        set.value = W().draft.perfect == "0" ? 50 : 100;
                    }
                    RebuildBlocks({"ws_affix_rows", "ws_review"});
                };
                in.Add(std::move(hit));
            }
            // Roll: the number (1-100, empty while Random) and %
            x -= 12 + rollW;
            const float rollLabelW = std::ceil(TextAdvance(Face::Body, 11, rollText));
            TextBox(in.parent, "Roll", x, cy, rollLabelW + 4, content, Style{Face::Body, 11, C("muted"), 0, 0, kLeft}, rollText);
            const float ix = x + rollLabelW + 6, iy = cy + (content - 27.6f) / 2;
            Style is{Face::Body, 11, C("ink")};
            is.line = 1.6f;
            is.align = kLeft;
            InputLook look{Hex("#080e0e"), Hex("#7b745a99")};
            look.padX = 7;
            if (random) {
                Fill(in.parent, "Roll", ix, iy, 64, 27.6f, Hex("#080e0e"), 3);
                Outline(in.parent, "Roll border", ix, iy, 64, 27.6f, Hex("#7b745a99"), 3);
            } else {
                MakeInput(in, ix, iy, 64, 27.6f, is, look, st.valid ? std::to_string(st.roll) : "", "", true, [id](const std::string& text) {
                    Setting& set = W().draft.settings[id];
                    char* end = nullptr;
                    const std::string t = Trim(text);
                    const double v = std::strtod(t.c_str(), &end);
                    if (!t.empty() && end && !*end && std::isfinite(v) && std::floor(v) == v) {
                        set.roll = Setting::Number;
                        set.value = static_cast<int>(v);
                    } else set.roll = Setting::Invalid;
                    RebuildBlocks({"ws_review"});
                }, "workshop-roll#" + id);
            }
            TextBox(in.parent, "Percent", ix + 64 + 3, cy, percentW + 4, content, Style{Face::Body, 11, C("muted"), 0, 0, kLeft}, "%");
            // Tier: T1... (T8 · primordial where the affix has it)
            x -= 12 + tierW;
            const float tierLabelW = std::ceil(TextAdvance(Face::Body, 11, tierText));
            TextBox(in.parent, "Tier", x, cy, tierLabelW + 4, content, Style{Face::Body, 11, C("muted"), 0, 0, kLeft}, tierText);
            const float sx = x + tierLabelW + 6, sy = cy + (content - 26) / 2;
            const int current = std::clamp(st.tier, 1, tiers) - 1;
            SelectBox(in, sx, sy, selW, 26, 11, st.tier >= 1 && st.tier <= tiers ? options[current] : "", "workshop-tier#" + id,
                      [in, sx, sy, selW, options, current, id] {
                          OpenBelow(in, sx, sy, selW, 26, options, current, [id](int i) {
                              Setting& set = W().draft.settings[id];
                              set.tierSet = true;
                              set.tier = i + 1;
                              RebuildBlocks({"ws_affix_rows", "ws_review"});
                          });
                      });
            y += rowH + 8;
        }
        return y - 8 + 24;
    };
}
// 6. Add extra Unique / Set effects (a <details> step): the summary; open, the text and the search
void MakeExtraHead(Section& s) {
    s.build = [](Section& sec, const Ctx& c) {
        W().blocks["ws_extra_head"] = sec.id;
        sec.after = 0;
        const Ctx in = Block(c);
        float y = 22;
        Solid(in.parent, "Step border", kPanelPad, y, kPanelInner, 1, C("line"));
        y += 1 + 24;
        // summary: 15px gold, line-height 1.6, the disclosure triangle; its span 11px muted, margin-left 12
        const bool open = W().advanced;
        const Style ss = Text(15, "gold", 1.6f, false);
        const float lh = LineHeight(ss), indent = std::round(15 * 1.15f);
        Style ms{Face::Body, 10, C("gold")};
        ms.align = kLeft;
        TextBox(in.parent, "Marker", kPanelPad, y, indent, lh, ms, open ? "\xE2\x96\xBC" : "\xE2\x96\xB6");
        Style sl = ss;
        sl.align = kLeft;
        void* summary = TextBox(in.parent, "Summary", kPanelPad + indent, y, 700, lh, sl, Tr("Add extra Unique / Set effects"));
        const float spanX = kPanelPad + indent + std::ceil(TextWidth(summary)) + TextAdvance(Face::Body, 15, " ") + 12;
        const Style sp = Text(11, "muted", 1.6f, false);
        // the inline-block span sits on the summary's baseline
        const auto body = assets::FontMetrics(Face::Body);
        const float baseline = y + TopOffset(ss) + body.ascent * 15;
        void* span = TextBox(in.parent, "Optional", spanX, 0, 300, LineHeight(sp), sp, Tr("Advanced \xC2\xB7 optional"));
        Move(TransformOf(span), spanX, baseline - body.ascent * 11);
        Hit hit;
        hit.kind = HitKind::Action;
        hit.name = "workshop-extra";
        hit.x = kPanelPad;
        hit.y = y;
        hit.w = kPanelInner;
        hit.h = lh;
        hit.action = [] {
            W().advanced = !W().advanced;
            RebuildBlocks({"ws_extra_head", "ws_mod_list", "ws_mod_rows"});
        };
        in.Add(std::move(hit));
        (void)span;
        y += lh;
        if (!open) return y + 24;
        y += 10 + StepText(in, y + 10, Tr("Mix individual effects from different items. Choose up to 64 lines. They share one affix slot, so only six regular "
                                          "affixes can be selected.")) + 14;
        const float lt = FieldLabelText(in, kPanelPad, y, kPanelInner, Tr("Find an effect"));
        const float h = SearchInput(in, y + lt, kPanelInner, W().filters[2], Tr("Search a source item or effect..."), [](const std::string& t) {
            W().filters[2] = t;
            W().limits[1] = 40;
            RebuildBlocks({"ws_mod_list"});
        }, "workshop-modifier-search");
        return y + lt + h;
    };
}
// 7. the chosen extra effects: chips (name ×) or the muted line
void MakeModifierRows(Section& s) {
    s.build = [](Section& sec, const Ctx& c) {
        W().blocks["ws_mod_rows"] = sec.id;
        sec.after = 0;
        if (!W().advanced) return 0.f;
        const Ctx in = Block(c);
        float y = 14;
        const Draft& d = W().draft;
        if (d.modifiers.empty()) {
            y += Paragraph(in.parent, "Empty", kPanelPad, y, kPanelInner, Text(12, "muted", 1.6f), Tr("No extra effects selected."));
            return y + 24;
        }
        const Catalog* data = Data();
        const int midpoint = d.perfect == "0" ? 1 : 0;
        float x = kPanelPad, rowH = 0;
        for (const auto& id : d.modifiers) {
            std::string name = id;
            if (data) if (const auto it = data->modifierAt.find(id); it != data->modifierAt.end()) name = data->modifiers[it->second].names[midpoint];
            const std::string text = Tr(Clean(name)) + " \xC3\x97";
            const float want = std::ceil(TextAdvance(Face::Body, 11, text)) + 1 + 22;
            if (x > kPanelPad && x + std::min(want, kPanelInner) > kPanelPad + kPanelInner) {
                y += rowH + 8;
                x = kPanelPad;
                rowH = 0;
            }
            float h = 0;
            const float w = ChipButton(in, x, y, kPanelInner - (x - kPanelPad), 10, 8, text, "workshop-remove-modifier#" + id, [id] { RemoveChosen(1, id); }, &h);
            x += w + 8;
            rowH = std::max(rowH, h);
        }
        return y + rowH + 24;
    };
}
// 8. Review and create: roll quality, the summary, Create custom item
std::string ReviewShown() {
    const JValue* f = Forge();
    std::string affixes;
    if (f) for (const auto& a : (*f)["affixes"].items) affixes += IdOf(a.Find("id")) + ",";
    return CreateReason() + "|" + Problem(W().draft, Data(), f) + "|" + affixes;
}
void Create() {
    const Draft d = W().draft;
    const JValue* forge = Forge();
    const std::string problem = Problem(d, Data(), forge);
    if (!problem.empty()) { Toast(Tr(problem), true); return; }
    const Summary sum = SummaryOf(d);
    std::string modifiers;
    for (const auto& m : d.modifiers) modifiers += (modifiers.empty() ? "" : ",") + m;
    const std::string args = d.templateId + " " + AffixesArg(d) + " " + d.perfect;
    ActionRun run;
    run.command = modifiers.empty() ? "craftcustom {save_id} " + args : "craftmix {save_id} " + args + " " + modifiers;
    run.expectId = forge ? IdOf(forge->Path("player.id")) : "";
    const std::string name = forge ? (*forge)["player"]["name"].Str() : "";
    // one interface text per line, so each line is translated
    run.confirmTitle = Tr("Create this custom item?");
    run.confirmMessage = Fmt("Character: {0}", {name.empty() ? Tr("Loaded offline character") : name}) + "\n" + Tr(Clean(sum.templ)) + "\n" +
                         Tr(sum.affixes + " \xC2\xB7 " + sum.effects + " \xC2\xB7 " + sum.rolls) + "\n\n" +
                         Tr("Replaces the item currently selected in the game\xE2\x80\x99s forge. No materials are consumed. A recovery backup is created.");
    if (!modifiers.empty()) run.confirmMessage += "\n" + Tr("Extra effects require EpochPact and workshop-recipes.txt when reloading or moving this item.");
    const Control* ctl = ControlById("craft_custom");
    const std::string label = ctl ? ctl->label : "Create custom gear";
    run.success = [label](const JValue&) { return Tr(label + " updated."); };
    run.reread = {"craftread"};
    RunAction(run);
}
void MakeReview(Section& s) {
    s.reads.insert("craftread");
    s.build = [](Section& sec, const Ctx& c) {
        W().blocks["ws_review"] = sec.id;
        sec.after = 20;
        sec.state["shown"] = ReviewShown();
        const Ctx in = Block(c);
        float y = StepHead(in, 0, "4", Tr("Review and create"));
        // label: Roll quality, the select
        const float lr = FieldLabelText(in, kPanelPad, y, kPanelInner, Tr("Roll quality"));
        const std::vector<std::string> rolls{Tr("Maximum rolls"), Tr("Midpoint rolls")};
        const int at = W().draft.perfect == "0" ? 1 : 0;
        const float selH = SelectHeight(Text(12, "ink", 0, false)), sy = y + lr;
        SelectBox(in, kPanelPad, sy, kPanelInner, selH, 12, rolls[at], "workshop-perfect", [in, sy, selH, rolls, at] {
            OpenBelow(in, kPanelPad, sy, kPanelInner, selH, rolls, at, [at](int i) {
                if (i == at) return;
                W().draft.perfect = i ? "0" : "1";
                // updateWorkshopUi, with the extra effects' amounts for these rolls
                RebuildBlocks({"ws_affix_list", "ws_affix_rows", "ws_mod_list", "ws_mod_rows", "ws_review"});
            });
        });
        y = sy + selH + 14;
        y += StepText(in, y, Tr("Each chosen affix has its own tier (T1\xE2\x80\x93T8) and roll (1\xE2\x80\x93" "100% of its tier's range, or Random). T8 is the game's primordial "
                                "tier: one per item, shown for affixes that have it; the game limits how many primordial items you can wear. Roll quality applies to the "
                                "template, implicits and extra effects, and is the starting roll for newly chosen affixes. Without extra effects, keeping current affixes "
                                "preserves their tiers and midpoint mode keeps their existing rolls. Adding extra effects rebuilds kept affixes at T7 with the chosen rolls."));
        // #workshop-summary: dl.craft-summary (110px | 1fr, gap 12, padding 18, soft), the problem, the portability note
        y += 18;
        const Draft& d = W().draft;
        const Summary sum = SummaryOf(d);
        const std::array<std::pair<std::string, std::string>, 4> rows{{{"Result", sum.templ}, {"Affixes", sum.affixes}, {"Extra effects", sum.effects},
                                                                       {"Quality", sum.rolls}}};
        void* bg = Fill(in.parent, "Summary", kPanelPad, y, kPanelInner, 10, C("soft"));
        void* frame = Outline(in.parent, "Summary border", kPanelPad, y, kPanelInner, 10, C("line"));
        float ry = y + 1 + 18;
        const float dtX = kPanelPad + 1 + 18, ddX = dtX + 110 + 12, ddW = kPanelInner - 2 - 36 - 122;
        for (size_t i = 0; i < rows.size(); ++i) {
            const float dt = Paragraph(in.parent, "Term", dtX, ry, 110, Text(13, "muted", 1.6f), Tr(rows[i].first));
            const float dd = Paragraph(in.parent, "Value", ddX, ry, ddW, Text(13, "ink", 1.6f), Tr(Clean(rows[i].second)));
            ry += std::max(dt, dd) + (i + 1 < rows.size() ? 12 : 0);
        }
        const float dlH = ry + 18 + 1 - y;
        Resize(TransformOf(bg), kPanelInner, dlH);
        Resize(TransformOf(frame), kPanelInner, dlH);
        y += dlH;
        float margin = 18;  // what the last part keeps below it
        const std::string problem = Problem(d, Data(), Forge());
        if (!problem.empty()) {
            y += 18;
            y += StepText(in, y, Tr(problem));
            margin = 10;
        }
        if (!d.modifiers.empty()) {
            // p.craft-portability: padding 12, a gold line at its left, gold
            y += std::max(margin, 10.f);
            Style gs = Text(13, "gold", 1.7f);
            gs.rich = true;
            const std::string text = "<noparse>" + Tr("Extra effects require EpochPact and") + "</noparse> <b>workshop-recipes.txt</b> <noparse>" +
                                     Tr("when reloading or moving this item to another PC. Keep that file with your saves.") + "</noparse>";
            const float th = Paragraph(in.parent, "Portability", kPanelPad + 2 + 12, y + 12, kPanelInner - 2 - 24, gs, text);
            Solid(in.parent, "Portability bar", kPanelPad, y, 2, th + 24, C("gold"));
            y += th + 24;
            margin = 10;
        }
        y += std::max(margin, 10.f);
        y += StepText(in, y, Tr("Replaces the selected item once and saves a recovery backup. This custom action does not consume crafting materials."));
        margin = 10;
        const std::string reason = CreateReason();
        if (!reason.empty()) {
            y += 12;
            y += StepText(in, y, reason);
            margin = 0;
        }
        y += std::max(margin, 18.f);
        Button b = AddButton(in, kPanelPad + kPanelInner, y, Tr("Create custom item"), Look::Primary, [] { Create(); }, true, "craft_custom");
        if (!reason.empty()) SetEnabled(in, b, false);
        return y + b.h + 24 + kPanelPad;
    };
    s.onData = [](Section& sec, const std::string&) {
        if (sec.state["shown"] != ReviewShown()) Rebuild(sec.id);
    };
}

// ------------------------------------------------------------------------------ Spawn Items
// The Crafting page's fourth tab: one panel in three sections (the group "spawn"), so typing in the
// search builds only the results again and the field keeps the keyboard.
//   spawn_head     title, Load / Refresh item list, the search and the type buttons
//   spawn_list     the matching items, live while typing; a click chooses one
//   spawn_options  the chosen item, only the options that apply to it, Spawn
struct SpawnEntry { std::string id, name, meta, lower, kind; bool weaver = false, lp = true; int maxAffixes = 4; };
struct SpawnState {
    std::string filter, kind = "all", item, level = "100", count = "1";
    int rarity = 2, lp = -1;
    size_t limit = 100;
    std::map<std::string, int> blocks;  // section kind -> id
    std::vector<SpawnEntry> entries;
    std::string entriesOf;              // the spawnlist reply the entries were made from
};
SpawnState& SP() {
    static SpawnState s;
    return s;
}
std::string SpawnShown() {
    const JValue* d = Cached("spawnlist");
    return d ? IdOf(d->Path("player.id")) + "|" + std::to_string((*d)["bases"].items.size()) + "|" + std::to_string((*d)["uniques"].items.size()) : "-";
}
bool SpawnLoaded() {
    const JValue* d = Cached("spawnlist");
    return d && !(*d)["bases"].items.empty();
}
const std::vector<SpawnEntry>& SpawnEntries() {
    SpawnState& st = SP();
    const std::string shown = SpawnShown();
    if (st.entriesOf == shown) return st.entries;
    st.entries.clear();
    st.entriesOf = shown;
    const JValue* d = Cached("spawnlist");
    if (!d) return st.entries;
    for (const auto& u : (*d)["uniques"].items) {
        const bool set = True(u.Find("set"));
        SpawnEntry e{"u" + IdOf(u.Find("id")), u["name"].Str(),
                     Tr(set ? "Set" : "Unique") + " \xC2\xB7 " + u["baseType"].Str() + " \xC2\xB7 " + Fmt("Level {0}", {IdOf(u.Find("level"))}), "",
                     set ? "set" : "unique", True(u.Find("weaver")), !u.Find("lp") || True(u.Find("lp"))};
        e.lower = Lower(e.name + " " + e.meta);
        st.entries.push_back(std::move(e));
    }
    for (const auto& b : (*d)["bases"].items) {
        SpawnEntry e{"b" + IdOf(b.Find("type")) + "." + IdOf(b.Find("sub")), b["name"].Str(),
                     b["baseType"].Str() + " \xC2\xB7 " + Fmt("Level {0}", {IdOf(b.Find("level"))}), "", "base", false, false,
                     static_cast<int>(b["maxAffixes"].Num(4))};
        e.lower = Lower(e.name + " " + e.meta);
        st.entries.push_back(std::move(e));
    }
    return st.entries;
}
const SpawnEntry* SpawnChosen() {
    for (const auto& e : SpawnEntries()) if (e.id == SP().item) return &e;
    return nullptr;
}
Color SpawnColor(const SpawnEntry& e) { return e.kind == "unique" ? Hex("#d99248") : e.kind == "set" ? Hex("#68c47c") : C("ink"); }
void SpawnRebuild(std::initializer_list<const char*> kinds) {
    for (const char* k : kinds) {
        const auto it = SP().blocks.find(k);
        if (it != SP().blocks.end()) Rebuild(it->second);
    }
}
// translated: what applies to the chosen item
std::string SpawnNote(const SpawnEntry& x) {
    if (x.kind == "base") {
        if (x.maxAffixes <= 0) return Tr("This base has no affixes, so rarity does not apply.");
        const int type = std::atoi(x.id.c_str() + 1);  // b<type>.<subtype>; idols are 25-33
        if (type >= 25 && type <= 33) return Tr("Idols roll one prefix and one suffix, or more with Idol prefixes and Idol suffixes in General settings. Rarity does not apply.");
        if (x.maxAffixes < 4) return Fmt("This base always rolls {0} affixes, so rarity does not apply.", {std::to_string(x.maxAffixes)});
        return Tr("Rarity applies. The game rolls the affixes for the item level.");
    }
    if (x.kind == "set") return Tr("Set items have no Legendary Potential.");
    if (x.weaver) return Tr("Weaver\xE2\x80\x99s Will Unique: the game rolls its Weaver\xE2\x80\x99s Will; Legendary Potential does not apply.");
    return x.lp ? Tr("Legendary Potential applies. Rarity does not.") : Tr("This Unique has no Legendary Potential. Rarity does not apply.");
}
std::string SpawnReason() {
    std::string reason;
    if (!Eligible(&reason)) return reason;
    if (!SpawnLoaded()) return Tr("Load the item list first.");
    if (!SpawnChosen()) return Tr("Choose an item from the list above.");
    return "";
}
bool WholeNumber(const std::string& text, int low, int high, int* out) {
    const std::string t = Trim(text);
    if (t.empty() || t.size() > 4) return false;
    int v = 0;
    for (char ch : t) {
        if (ch < '0' || ch > '9') return false;
        v = v * 10 + (ch - '0');
    }
    if (v < low || v > high) return false;
    *out = v;
    return true;
}
void SpawnNow() {
    const std::string reason = SpawnReason();
    if (!reason.empty()) { Toast(reason, true); return; }
    int level = 0, count = 0;
    if (!WholeNumber(SP().level, 1, 100, &level)) { Toast(Tr("Enter an item level from 1 to 100."), true); return; }
    if (!WholeNumber(SP().count, 1, 20, &count)) { Toast(Tr("Enter how many from 1 to 20."), true); return; }
    ActionRun run;
    run.command = "spawnitem {save_id} " + SP().item + " " + std::to_string(SP().rarity) + " " + std::to_string(SP().lp) + " " + std::to_string(level) + " " +
                  std::to_string(count);
    run.expectId = IdOf(Cached("spawnlist")->Path("player.id"));
    run.success = [](const JValue& v) {
        const double n = v["spawned"].Num(0);
        return n == 1 ? Fmt("{0} dropped next to your character.", {v["name"].Str()})
                      : Fmt("{0} \xC3\x97 {1} dropped next to your character.", {Num(n), v["name"].Str()});
    };
    RunAction(run);
}
// A row of buttons, the current one selected; returns its height.
float SpawnButtons(const Ctx& in, float x, float y, const std::vector<std::string>& labels, int current, const std::string& name, std::function<void(int)> chosen) {
    float h = 0;
    for (size_t i = 0; i < labels.size(); ++i) {
        Button b = AddButton(in, x, y, labels[i], Look::Normal, [chosen, i] { chosen(static_cast<int>(i)); }, false, name + "-" + std::to_string(i));
        SetSelected(in, b, static_cast<int>(i) == current);
        x += b.w + 8;
        h = std::max(h, b.h);
    }
    return h;
}

// 1. the head: the panel's frame, the title and the list button, the search and the type buttons
void MakeSpawnHead(Section& s) {
    s.reads.insert("spawnlist");
    s.build = [](Section& sec, const Ctx& c) {
        SP().blocks["spawn_head"] = sec.id;
        sec.state["shown"] = SpawnShown();
        auto frame = std::make_shared<Panel>(OpenPanel(c.parent, kPadX, 0, kPanelW));
        sec.frame = frame;
        sec.after = 0;
        const Ctx in = c.At(frame->rt.Get(), kPadX, 0);
        const bool loaded = SpawnLoaded();
        Style h2{Face::Serif, 27, C("ink")};
        const float lh = LineHeight(h2);
        Button load = AddButton(in, kPanelPad + kPanelInner, kPanelPad, Tr(loaded ? "Refresh item list" : "Load item list"), Look::Normal,
                                [] { Read("spawnlist"); }, true, "spawn_list");
        const float row = std::max(lh, load.h);
        PlaceButton(in, load, kPanelPad + kPanelInner - load.w, kPanelPad + (row - load.h) / 2);
        TextBox(in.parent, "Title", kPanelPad, kPanelPad + (row - lh) / 2, kPanelInner - load.w - 15, lh, h2, Tr("Spawn items"));
        float y = kPanelPad + row + 16;
        y += Muted(in.parent, kPanelPad, y, kPanelInner,
                   Tr("Choose a base item, Unique or Set. The game makes it with its own rules and drops it next to your character, like a monster drop. "
                      "Nothing is saved until you pick it up.")) + 10;
        std::string info = Tr("Load an offline character, then load the item list.");
        if (loaded) {
            const JValue* d = Cached("spawnlist");
            size_t sets = 0;
            for (const auto& u : (*d)["uniques"].items) sets += True(u.Find("set"));
            info = Fmt("{0} base items \xC2\xB7 {1} Uniques \xC2\xB7 {2} Set items",
                       {std::to_string((*d)["bases"].items.size()), std::to_string((*d)["uniques"].items.size() - sets), std::to_string(sets)});
        }
        y += NoteBox(in.parent, kPanelPad, y, kPanelInner, info) + 18;
        // the search: every typed word must be in the name, the type or the base ("calamity", "helmet", "set boots")
        y += FieldLabelText(in, kPanelPad, y, kPanelInner, Tr("Find an item"));
        y += SearchInput(in, y, kPanelInner, SP().filter, Tr("Type a name or a type, for example Calamity, helmet or set boots"),
                         [](const std::string& t) {
                             SP().filter = t;
                             SP().limit = 100;
                             SpawnRebuild({"spawn_list"});
                         }, "spawn-search") + 12;
        const std::vector<std::string> kinds{"all", "base", "unique", "set"};
        const int kindAt = static_cast<int>(std::find(kinds.begin(), kinds.end(), SP().kind) - kinds.begin()) % 4;
        const int sid = sec.id;
        y += SpawnButtons(in, kPanelPad, y, {Tr("All items"), Tr("Base items"), Tr("Uniques"), Tr("Set items")}, kindAt, "spawn-kind", [kinds, sid](int i) {
            SP().kind = kinds[i];
            SP().limit = 100;
            Rebuild(sid);
            SpawnRebuild({"spawn_list"});
        });
        return y;
    };
    s.onData = [](Section& sec, const std::string&) {
        if (sec.state["shown"] == SpawnShown() || Typing(sec.id)) return;
        Rebuild(sec.id);
        SpawnRebuild({"spawn_list", "spawn_options"});
    };
}

// 2. the results: one line each (the name in its rarity's colour, the type and level at the right),
// the chosen one highlighted; a fixed row height, the rows below the view made a few a frame
constexpr float kSpawnRowH = 34, kSpawnViewH = 340;
struct SpawnRow { size_t entry = 0; bool made = false; Pin node, fill; int hit = -1; };
struct SpawnView {
    std::vector<SpawnRow> rows;
    size_t matches = 0, made = 0;
    int chosen = -1;
    Pin content, thumb;
    float listY = 0, viewH = 0, offset = 0, rowW = 0;
    bool scrolls = false;
    Ctx in;
    int section = -1;
};
void MakeSpawnRow(SpawnView& v, size_t i) {
    SpawnRow& r = v.rows[i];
    const SpawnEntry& e = SpawnEntries()[r.entry];
    Node node = New("Row", v.content.Get(), 0, i * kSpawnRowH, v.rowW, kSpawnRowH);
    r.fill.Set(Fill(node.rt, "Chosen", 0, 0, v.rowW, kSpawnRowH, static_cast<int>(i) == v.chosen ? C("soft") : Alpha(C("soft"), 0)));
    Style ns{Face::Body, 13, SpawnColor(e)};
    ns.align = kLeft;
    TextBox(node.rt, "Name", 14, 0, v.rowW * 0.55f, kSpawnRowH, ns, e.name);
    Style ms{Face::Body, 12, C("muted")};
    ms.align = kRight;
    TextBox(node.rt, "Type", v.rowW * 0.55f + 14, 0, v.rowW * 0.45f - 28, kSpawnRowH, ms, e.meta);
    Solid(node.rt, "Row border", 0, kSpawnRowH - 1, v.rowW, 1, Hex("#92734025"));
    r.node.Set(node.rt);
    r.made = true;
    ++v.made;
}
void PlaceSpawnRows(SpawnView& v) {
    Section* s = FindSection(v.section);
    if (!s) return;
    const float viewTop = v.listY + 1;
    for (size_t i = 0; i < v.rows.size(); ++i) {
        const SpawnRow& r = v.rows[i];
        if (r.hit < 0 || r.hit >= static_cast<int>(s->hits.size())) continue;
        Hit& h = s->hits[r.hit];
        const float top = i * kSpawnRowH;
        const float y0 = std::max(viewTop, viewTop + top - v.offset), y1 = std::min(viewTop + v.viewH, viewTop + top + kSpawnRowH - v.offset);
        h.visible = r.made && y1 > y0;
        h.y = v.in.oy + y0;
        h.h = std::max(0.f, y1 - y0);
    }
    if (v.thumb) {
        const float contentH = v.rows.size() * kSpawnRowH, track = v.viewH - 30, length = std::max(18.f, track * v.viewH / std::max(contentH, v.viewH));
        const float at = contentH > v.viewH ? (track - length) * v.offset / (contentH - v.viewH) : 0;
        Move(v.thumb.Get(), kPanelPad + 1 + v.rowW + 3, v.listY + 1 + 15 + at);
        Resize(v.thumb.Get(), 9, length);
    }
}
void ScrollSpawn(SpawnView& v, float offset) {
    v.offset = std::clamp(offset, 0.f, std::max(0.f, v.rows.size() * kSpawnRowH - v.viewH));
    Move(v.content.Get(), 0, -v.offset);
    for (size_t i = 0; i < v.rows.size(); ++i)
        if (!v.rows[i].made && i * kSpawnRowH < v.offset + v.viewH + kSpawnRowH) MakeSpawnRow(v, i);
    PlaceSpawnRows(v);
}
void ChooseSpawn(int sid, size_t index) {
    Section* s = FindSection(sid);
    auto v = s ? std::static_pointer_cast<SpawnView>(s->live) : nullptr;
    if (!v || index >= v->rows.size()) return;
    if (v->chosen >= 0 && v->rows[v->chosen].made) Fade(v->rows[v->chosen].fill.Get(), Alpha(C("soft"), 0), 0);
    v->chosen = static_cast<int>(index);
    Fade(v->rows[index].fill.Get(), C("soft"), 0);
    SP().item = SpawnEntries()[v->rows[index].entry].id;
    SpawnRebuild({"spawn_options"});
}
void MakeSpawnList(Section& s) {
    s.build = [](Section& sec, const Ctx& c) {
        SP().blocks["spawn_list"] = sec.id;
        sec.after = 0;
        const Ctx in = Block(c);
        auto v = std::make_shared<SpawnView>();
        sec.live = v;
        v->section = sec.id;
        v->in = in;
        const auto& entries = SpawnEntries();
        const auto words = Words(SP().filter);
        for (size_t i = 0; i < entries.size(); ++i) {
            if ((SP().kind != "all" && entries[i].kind != SP().kind) || !Matches(entries[i].lower, words)) continue;
            ++v->matches;
            if (v->rows.size() >= SP().limit) continue;
            if (entries[i].id == SP().item) v->chosen = static_cast<int>(v->rows.size());
            SpawnRow r;
            r.entry = i;
            v->rows.push_back(std::move(r));
        }
        float y = 14;
        const Style cs = Text(11, "muted", 0, false);
        const std::string count = v->matches > v->rows.size()
                                      ? Fmt("Showing {0} of {1} matches. Type more to narrow the list.", {std::to_string(v->rows.size()), std::to_string(v->matches)})
                                      : v->matches == 1 ? Tr("1 match") : Fmt("{0} matches", {std::to_string(v->matches)});
        TextBox(in.parent, "Count", kPanelPad, y, kPanelInner, LineHeight(cs), cs, count);
        y += LineHeight(cs) + 8;
        v->listY = y;
        Node box = New("List", in.parent, kPanelPad, y, kPanelInner, 10);
        void* bg = Fill(box.rt, "Background", 0, 0, kPanelInner, 10, Hex("#00000022"), 4);
        void* frame = Outline(box.rt, "Border", 0, 0, kPanelInner, 10, C("line"), 4);
        if (v->rows.empty()) {
            const float th = Paragraph(box.rt, "Empty", 1 + 14, 1 + 10 + 8, kPanelInner - 2 - 28, Text(13, "muted", 1.7f),
                                       Tr(SpawnLoaded() ? "No matches. Try another search." : "Load the item list to choose an item."));
            const float h = 1 + 10 + 8 + th + 8 + 10 + 1;
            Resize(TransformOf(bg), kPanelInner, h);
            Resize(TransformOf(frame), kPanelInner, h);
            return y + h;
        }
        v->scrolls = v->rows.size() * kSpawnRowH > kSpawnViewH;
        v->rowW = kPanelInner - 2 - (v->scrolls ? 15 : 0);
        v->viewH = std::min(kSpawnViewH, v->rows.size() * kSpawnRowH);
        const float h = v->viewH + 2;
        Node clip = New("View", box.rt, 1, 1, kPanelInner - 2, v->viewH);
        Clip(clip);
        Node content = New("Rows", clip.rt, 0, 0, v->rowW, v->rows.size() * kSpawnRowH);
        v->content.Set(content.rt);
        Resize(TransformOf(bg), kPanelInner, h);
        Resize(TransformOf(frame), kPanelInner, h);
        const int sid = sec.id;
        if (v->scrolls) {
            Style ar{Face::Body, 7, C("gold")};
            ar.align = kCenter;
            TextBox(in.parent, "Scroll up", kPanelPad + 1 + v->rowW, y + 1, 15, 15, ar, "\xE2\x96\xB2");
            TextBox(in.parent, "Scroll down", kPanelPad + 1 + v->rowW, y + h - 1 - 15, 15, 15, ar, "\xE2\x96\xBC");
            v->thumb.Set(TransformOf(Fill(in.parent, "Thumb", kPanelPad + 1 + v->rowW + 3, y + 16, 9, 40, C("gold"), 4)));
            Section::Wheel wheel;
            wheel.x = in.ox + kPanelPad + 1;
            wheel.y = in.oy + y + 1;
            wheel.w = kPanelInner - 2;
            wheel.h = v->viewH;
            std::weak_ptr<SpawnView> weak = v;
            wheel.scroll = [weak](float notches) {
                if (auto p = weak.lock()) ScrollSpawn(*p, p->offset - notches * 3 * kSpawnRowH);
            };
            sec.wheels.push_back(std::move(wheel));
        }
        for (size_t i = 0; i < v->rows.size(); ++i) {
            Hit hit;
            hit.kind = HitKind::Action;
            hit.name = "spawn-row#" + SpawnEntries()[v->rows[i].entry].id;
            hit.x = kPanelPad + 1;
            hit.w = v->rowW;
            hit.visible = false;
            hit.action = [sid, i] { ChooseSpawn(sid, i); };
            v->rows[i].hit = in.Add(std::move(hit));
        }
        // the chosen row in view, then the rows in view made now
        if (v->chosen >= 0 && (v->chosen + 1) * kSpawnRowH > v->viewH) v->offset = v->chosen * kSpawnRowH + kSpawnRowH / 2 - v->viewH / 2;
        ScrollSpawn(*v, v->offset);
        y += h;
        if (v->matches > v->rows.size()) {
            Button more = AddButton(in, kPanelPad, y, Fmt("Show more ({0} of {1})", {std::to_string(v->rows.size()), std::to_string(v->matches)}), Look::Quiet,
                                    [sid] {
                                        SP().limit += 100;
                                        Rebuild(sid);
                                    }, false, "spawn-more");
            y += more.h;
        }
        return y;
    };
    s.tick = [](Section& sec) {
        auto v = std::static_pointer_cast<SpawnView>(sec.live);
        if (!v || v->made >= v->rows.size()) return;
        int budget = 8;
        for (size_t i = 0; i < v->rows.size() && budget; ++i)
            if (!v->rows[i].made) { MakeSpawnRow(*v, i); --budget; }
        PlaceSpawnRows(*v);
    };
}

// 3. the chosen item and only the options that apply to it, then Spawn
void MakeSpawnOptions(Section& s) {
    s.build = [](Section& sec, const Ctx& c) {
        SP().blocks["spawn_options"] = sec.id;
        sec.after = 20;
        const Ctx in = Block(c);
        const int sid = sec.id;
        float y = 22;
        Solid(in.parent, "Divider", kPanelPad, y, kPanelInner, 1, C("line"));
        y += 1 + 20;
        const SpawnEntry* item = SpawnChosen();
        if (!item) {
            y += StepText(in, y, Tr(SpawnLoaded() ? "Choose an item from the list above." : "Load the item list to choose an item.")) + 10;
        } else {
            y += FieldLabelText(in, kPanelPad, y, kPanelInner, Tr("Chosen item")) + 4;
            Style name{Face::Serif, 22, SpawnColor(*item)};
            name.wrap = true;
            y += Paragraph(in.parent, "Item", kPanelPad, y, kPanelInner, name, item->name) + 2;
            y += Paragraph(in.parent, "Kind", kPanelPad, y, kPanelInner, Text(12, "muted", 1.6f), item->meta) + 8;
            y += StepText(in, y, SpawnNote(*item)) + 14;
            if (item->kind == "base" && item->maxAffixes >= 4) {
                y += FieldLabelText(in, kPanelPad, y, kPanelInner, Tr("Rarity")) + 6;
                y += SpawnButtons(in, kPanelPad, y, {Tr("Normal"), Tr("Magic"), Tr("Rare"), Tr("Exalted")}, SP().rarity, "spawn-rarity",
                                  [sid](int i) { SP().rarity = i; Rebuild(sid); }) + 16;
            } else if (item->kind == "unique" && item->lp && !item->weaver) {
                y += FieldLabelText(in, kPanelPad, y, kPanelInner, Tr("Legendary Potential")) + 6;
                y += SpawnButtons(in, kPanelPad, y, {Tr("The game\xE2\x80\x99s roll"), "0", "1", "2", "3", "4"}, SP().lp + 1, "spawn-lp",
                                  [sid](int i) { SP().lp = i - 1; Rebuild(sid); }) + 16;
            }
        }
        // Item level (a number) and How many (buttons)
        const float half = (kPanelInner - 14) / 2, hx = kPanelPad + half + 14;
        const float n1 = InputField(in, kPanelPad, y, half, Tr("Item level"), SP().level, "", true, [](const std::string& t) { SP().level = t; }, "spawn-level");
        const float cl = FieldLabelText(in, hx, y, half, Tr("How many"));
        const std::vector<std::string> counts{"1", "5", "10", "20"};
        const int countAt = static_cast<int>(std::find(counts.begin(), counts.end(), SP().count) - counts.begin());
        const float n2 = cl + 6 + SpawnButtons(in, hx, y + cl + 6, counts, countAt < 4 ? countAt : -1, "spawn-count", [sid, counts](int i) {
                             SP().count = counts[i];
                             Rebuild(sid);
                         });
        y += std::max(n1, n2) + 18;
        const std::string reason = SpawnReason();
        if (!reason.empty()) y += StepText(in, y, reason) + 12;
        Button b = AddButton(in, kPanelPad + kPanelInner, y, Tr("Spawn"), Look::Primary, [] { SpawnNow(); }, true, "spawn_item");
        if (!reason.empty()) SetEnabled(in, b, false);
        return y + b.h + kPanelPad;
    };
}
}  // namespace

#if defined(EPOCHPACT_TESTING)
// Private UI tests: choose an item of the loaded list as a click on its row would.
bool SpawnPick(const std::string& id) {
    const auto& entries = SpawnEntries();
    if (std::none_of(entries.begin(), entries.end(), [&](const SpawnEntry& e) { return e.id == id; })) return false;
    SP().item = id;
    return true;
}
#endif

void RegisterWorkshopSections() {
    RegisterSection("ws_head", MakeHead);
    RegisterSection("ws_item", MakeItem);
    RegisterSection("ws_template", MakeTemplate);
    RegisterSection("ws_affix_head", MakeAffixHead);
    RegisterSection("ws_affix_list", [](Section& s) { MakePicker(s, 0); });
    RegisterSection("ws_affix_rows", MakeAffixRows);
    RegisterSection("ws_extra_head", MakeExtraHead);
    RegisterSection("ws_mod_list", [](Section& s) { MakePicker(s, 1); });
    RegisterSection("ws_mod_rows", MakeModifierRows);
    RegisterSection("ws_review", MakeReview);
    RegisterSection("spawn_head", MakeSpawnHead);
    RegisterSection("spawn_list", MakeSpawnList);
    RegisterSection("spawn_options", MakeSpawnOptions);
}
std::string WorkshopState() {
    const State& w = W();
    std::string out = "{\"loaded\":" + std::string(Loaded() ? "true" : "false") + ",\"template\":\"" + w.draft.templateId + "\",\"perfect\":\"" + w.draft.perfect +
                      "\",\"affixes\":" + std::to_string(w.draft.affixes.size()) + ",\"modifiers\":" + std::to_string(w.draft.modifiers.size()) +
                      ",\"advanced\":" + (w.advanced ? "true" : "false") + ",\"command\":\"" + AffixesArg(w.draft) + "\"";
    if (const Catalog* data = Data())
        out += ",\"items\":" + std::to_string(data->items.size()) + ",\"affixList\":" + std::to_string(data->affixes.size()) +
               ",\"modifierList\":" + std::to_string(data->modifiers.size());
    return out + "}";
}

}  // namespace ep::ingame::view
