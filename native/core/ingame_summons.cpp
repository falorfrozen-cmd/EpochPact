// The Summons page's live sections (app.js renderPage "summons"): the damage meter (damagePanel
// with ui/damage-meter.js), the death recap (deathPanel with ui/death-recap.js; a setting panel
// whose tail draws the deaths) and the live summoned allies (summonTable). Their tables are
// updated in place while the numbers change and built again when their rows or columns change.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
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
float InPanel(const Ctx& c, const std::function<float(const Ctx& in, float y)>& draw, Panel* keep = nullptr) {
    Panel p = OpenPanel(c.parent, kPadX, 0, kPanelW);
    const Ctx in = c.At(p.rt.Get(), kPadX, 0);
    const float h = draw(in, kPanelPad) + kPanelPad;
    ClosePanel(p, kPanelW, h);
    if (keep) *keep = std::move(p);
    return h;
}
const char* const kElements[7] = {"Physical", "Fire", "Cold", "Lightning", "Necrotic", "Void", "Poison"};
std::string Grouped(long long v) {  // toLocaleString('en-US') of a whole number
    const std::string digits = std::to_string(v < 0 ? -v : v);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return (v < 0 ? "-" : "") + out;
}
// toLocaleString('en-US', {maximumFractionDigits: 1})
std::string OneDecimal(double v) {
    if (!std::isfinite(v)) return "\xE2\x80\x94";
    const long long tenths = std::llround(v * 10);
    std::string out = Grouped(tenths / 10);
    if (tenths < 0 && tenths / 10 == 0) out = "-0";
    if (tenths % 10) out += "." + std::to_string(std::llabs(tenths % 10));
    return out;
}
// A row in the panel's heading: h2 left, buttons right (.section-head .actions, gap 10), all
// centred on the tallest; returns the row's height.
struct HeadItem { std::string label; Action action; std::string name; bool primary = false, enabled = true; };
float Head(const Ctx& c, float y, const std::string& title, const std::vector<HeadItem>& items) {
    Style h2{Face::Serif, 27, C("ink")};
    const float lh = LineHeight(h2);
    float right = kPanelPad + kPanelInner, row = lh;
    std::vector<Button> made;
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        Button b = AddButton(c, right, y, it->label, it->primary ? Look::Primary : Look::Normal, it->action, true, it->name);
        if (!it->enabled) SetEnabled(c, b, false);
        right -= b.w + 10;
        row = std::max(row, b.h);
        made.push_back(std::move(b));
    }
    for (auto& b : made) {
        const float by = y + (row - b.h) / 2;
        Move(b.node.rt, (*c.hits)[b.hit].x - c.ox, by);
        (*c.hits)[b.hit].y = c.oy + by;
    }
    TextBox(c.parent, "Title", kPanelPad, y + (row - lh) / 2, right - kPanelPad - 15, lh, h2, title);
    return row;
}
std::string Plain(const std::string& s) { return s.empty() ? s : "<noparse>" + s + "</noparse>"; }

// ------------------------------------------------------------------------------ damage meter
// EpochDamage.fmt: 1.23M, 12.3k, 1,234; — when not a number
std::string DamageNumber(double n) {
    if (!std::isfinite(n)) return "\xE2\x80\x94";
    char b[48];
    if (std::fabs(n) >= 1e6) { std::snprintf(b, sizeof b, "%.2fM", n / 1e6); return b; }
    if (std::fabs(n) >= 1e4) { std::snprintf(b, sizeof b, "%.1fk", n / 1e3); return b; }
    return Grouped(static_cast<long long>(std::floor(n + 0.5)));
}
std::string Duration(double seconds) {
    const long long s = std::max(0LL, static_cast<long long>(std::floor(std::isfinite(seconds) ? seconds : 0)));
    const long long h = s / 3600, m = s % 3600 / 60, r = s % 60;
    char b[48];
    if (h) std::snprintf(b, sizeof b, "%lld:%02lld:%02lld", h, m, r);
    else std::snprintf(b, sizeof b, "%lld:%02lld", m, r);
    return b;
}
struct Column { const char* key; const char* label; };
const Column kColumns[9] = {{"dps10", "DPS 10 s"},          {"dps60", "DPS 60 s"},        {"dps600", "DPS 10 min"},
                            {"dps3600", "DPS 60 min"},      {"combatAverage", "Avg in combat"}, {"average", "Avg session"},
                            {"total", "Total damage"},      {"hits", "Hits"},             {"maxHit", "Biggest hit"}};
const int kCompact[4] = {0, 1, 4, 6};  // the damage taken table: DPS 10 s, DPS 60 s, Avg in combat, Total damage
double NumberOf(const JValue& v) { return v.IsNumber() ? v.number : v.IsBool() ? (v.boolean ? 1 : 0) : NAN; }
// allyName: the game's own name, else the object name made readable
std::string AllyName(const JValue& t) {
    const std::string label = Trim(t["label"].Str());
    if (!label.empty()) return label;
    const std::string n = t["name"].Str();
    if (n.find(' ') != std::string::npos) return n;
    std::string a;
    for (char ch : n) a += ch == '_' ? ' ' : ch;
    std::string b;  // ([a-z0-9])([A-Z]) -> "$1 $2"
    for (size_t i = 0; i < a.size(); ++i) {
        b += a[i];
        if (i + 1 < a.size() && (std::islower(static_cast<unsigned char>(a[i])) || std::isdigit(static_cast<unsigned char>(a[i]))) &&
            std::isupper(static_cast<unsigned char>(a[i + 1])))
            b += ' ';
    }
    std::string c;  // ([A-Z]+)([A-Z][a-z]) -> "$1 $2"
    for (size_t i = 0; i < b.size(); ++i) {
        c += b[i];
        if (i + 2 < b.size() && std::isupper(static_cast<unsigned char>(b[i])) && std::isupper(static_cast<unsigned char>(b[i + 1])) &&
            std::islower(static_cast<unsigned char>(b[i + 2])))
            c += ' ';
    }
    return c;
}
struct DamageRow {
    std::string key, label;
    bool total = false, own = false;  // own: the app's own words (translated); else a game name
    double v[9]{};
    const JValue* source = nullptr;
};
struct DamageState {  // the session or (taken) its damage taken, in the session's shape
    const JValue* reply = nullptr;
    const JValue* types = nullptr;
    const JValue* allies = nullptr;
    double elapsed = 0, combat = 0, hits = 0;
    bool active = false, taken = false;
};
DamageState SessionOf(const JValue* r) {
    DamageState s;
    if (!r) return s;
    s.reply = r;
    s.types = r->Find("types");
    s.allies = r->Find("allies");
    s.elapsed = (*r)["elapsedSeconds"].Num(0);
    s.combat = (*r)["combatSeconds"].Num(0);
    s.hits = (*r)["hits"].Num(0);
    s.active = (*r)["active"].IsBool() && (*r)["active"].boolean;
    return s;
}
// EpochDeathRecap.takenState
DamageState TakenOf(const JValue* r) {
    DamageState s;
    if (!r) return s;
    s.reply = r;
    const JValue& t = (*r)["taken"];
    s.types = t.Find("types");
    s.allies = s.types && !s.types->items.empty() ? &t : nullptr;
    s.elapsed = (*r)["elapsedSeconds"].Num(0);
    s.combat = t["combatSeconds"].Num(0);
    s.hits = t["hits"].Num(0);
    s.active = (*r)["active"].IsBool() && (*r)["active"].boolean;
    s.taken = true;
    return s;
}
DamageRow Pick(const JValue& t, int element, const DamageState& state) {
    DamageRow row;
    row.source = &t;
    if (element < 0) {
        for (int i = 0; i < 9; ++i) row.v[i] = NumberOf(t[kColumns[i].key]);
        return row;
    }
    const auto part = [&](const char* key) {
        const JValue* list = t.Path(std::string("elements.") + key);
        const double n = list && element < static_cast<int>(list->items.size()) ? NumberOf(list->items[element]) : 0;
        return std::isfinite(n) ? n : 0;
    };
    const double total = part("total");
    row.v[0] = part("dps10"); row.v[1] = part("dps60"); row.v[2] = part("dps600"); row.v[3] = part("dps3600");
    row.v[4] = state.combat > 0 ? total / state.combat : 0;
    row.v[5] = state.elapsed > 0 ? total / state.elapsed : 0;
    row.v[6] = total; row.v[7] = part("hits"); row.v[8] = part("maxHit");
    return row;
}
// EpochDamage.rows: the allies (most damage first), the allies' total, then the character
std::vector<DamageRow> DamageRows(const DamageState& s, int element, const std::vector<std::string>& hidden) {
    std::vector<DamageRow> out;
    if (!s.types) return out;
    std::vector<DamageRow> allies;
    const JValue* you = nullptr;
    for (const auto& t : s.types->items) {
        if (s.taken && !t["name"].IsString()) continue;
        if (t["name"].Str() == "You") { you = &t; continue; }
        DamageRow r = Pick(t, element, s);
        r.key = t["name"].Str();
        const std::string name = s.taken ? t["name"].Str() : AllyName(t);
        const double live = s.taken ? 0 : t["live"].Num(0);
        r.label = name + (live > 1 ? " \xC3\x97" + Num(live) : "");
        allies.push_back(std::move(r));
    }
    std::stable_sort(allies.begin(), allies.end(), [](const DamageRow& a, const DamageRow& b) {
        return (std::isfinite(b.v[6]) ? b.v[6] : 0) < (std::isfinite(a.v[6]) ? a.v[6] : 0);
    });
    out = std::move(allies);
    if (out.size() > 1 && s.allies) {
        DamageRow r = Pick(*s.allies, element, s);
        r.key = "__total";
        r.label = s.taken ? "All attackers" : "Allies total";
        r.total = r.own = true;
        out.push_back(std::move(r));
    }
    if (you) {
        DamageRow r = Pick(*you, element, s);
        r.key = "You";
        r.label = s.taken ? "You (self-inflicted)" : "You (character)";
        r.own = true;
        out.push_back(std::move(r));
    }
    out.erase(std::remove_if(out.begin(), out.end(), [&](const DamageRow& r) { return std::find(hidden.begin(), hidden.end(), r.key) != hidden.end(); }), out.end());
    return out;
}
std::string DamageStatus(const JValue* r) {
    if (!r) return "Not started.";
    const DamageState s = SessionOf(r);
    return std::string(s.active ? "Measuring" : "Stopped") + " \xC2\xB7 " + Duration(s.elapsed) + " session \xC2\xB7 " + Duration(s.combat) + " in combat \xC2\xB7 " +
           DamageNumber(s.hits) + " hits";
}
// The Summons page's choice of damage type and rows (preferences "epochpact.damage-panel.live";
// changed here for this game session)
struct DamageFilter { int element = -1; std::vector<std::string> hidden; bool loaded = false; } damageFilter;
DamageFilter& Filter() {
    if (!damageFilter.loaded) {
        damageFilter.loaded = true;
        const JValue f = Preference("epochpact.damage-panel.live");
        const double e = f["element"].Num(-1);
        if (std::floor(e) == e && e >= -1 && e <= 6) damageFilter.element = static_cast<int>(e);
        for (const auto& k : f["hidden"].items)
            if (k.IsString() && damageFilter.hidden.size() < 100) damageFilter.hidden.push_back(k.text);
    }
    return damageFilter;
}
std::string DamageCsv(const JValue* r) {
    const DamageState s = SessionOf(r);
    const auto quote = [](const std::string& v) {
        if (v.find_first_of(",\"\n") == std::string::npos) return v;
        std::string out = "\"";
        for (char ch : v) out += ch == '"' ? std::string("\"\"") : std::string(1, ch);
        return out + "\"";
    };
    const auto number = [](double v) {
        if (!std::isfinite(v)) return std::string();
        char b[48];
        std::snprintf(b, sizeof b, "%.1f", v);
        return std::string(b);
    };
    std::string out = "# EpochPact damage session; " + DamageStatus(r) + "\nSource";
    for (const auto& c : kColumns) out += std::string(",") + c.label;
    for (const char* e : kElements) out += std::string(",") + e + " damage";
    out += "\n";
    for (const auto& row : DamageRows(s, -1, {})) {
        out += quote(row.label);
        for (double v : row.v) out += "," + number(v);
        const JValue* totals = row.source ? row.source->Path("elements.total") : nullptr;
        for (int i = 0; i < 7; ++i) out += "," + number(totals && i < static_cast<int>(totals->items.size()) ? NumberOf(totals->items[i]) : NAN);
        out += "\n";
    }
    return out;
}
struct DamageView {
    std::shared_ptr<LiveTable> dealt, taken;
    std::vector<std::string> dealtHead, takenHead;
    NoteParts status;
    bool active = false;
    std::string keys;          // the rows the filters were made for
    size_t dealtRows = 0, takenRows = 0;
    bool dealtEmpty = true, takenEmpty = true;
};
std::vector<std::vector<std::string>> DamageCells(const std::vector<DamageRow>& rows, bool compact, std::vector<bool>* strong) {
    std::vector<std::vector<std::string>> cells;
    for (const auto& r : rows) {
        std::vector<std::string> line{r.own ? Tr(r.label) : Plain(r.label)};
        if (compact) for (int i : kCompact) line.push_back(DamageNumber(r.v[i]));
        else for (double v : r.v) line.push_back(DamageNumber(v));
        cells.push_back(std::move(line));
        if (strong) strong->push_back(r.total);
    }
    return cells;
}
std::string RowKeys(const JValue* r) {
    std::string keys;
    for (const auto& row : DamageRows(SessionOf(r), -1, {})) keys += row.key + "\n";
    return keys;
}
void MakeDamageMeter(Section& s) {
    s.reads.insert("damagesession");
    s.polls.insert("damagesession");
    s.every["damagesession"] = 3000;
    s.build = [](Section& sec, const Ctx& c) {
        auto v = std::make_shared<DamageView>();
        sec.live = v;
        const JValue* r = Cached("damagesession");
        const DamageState st = SessionOf(r);
        v->active = st.active;
        v->keys = RowKeys(r);
        sec.every["damagesession"] = st.active ? 1000 : 3000;
        const int id = sec.id;
        return InPanel(c, [&](const Ctx& in, float y) {
            const bool eligible = ViewReason().empty();
            const auto act = [id](const std::string& action) {
                return [action] {
                    Send("damagesession " + action, [](const std::string& reply) {
                        std::string why;
                        if (Refused(reply, &why)) Toast(PlayerError(why), true);
                        else Read("damagesession");
                    });
                };
            };
            y += Head(in, y, Tr("Damage meter"),
                      {{Tr(st.active ? "Stop" : "Start"), act(st.active ? "stop" : "start"), "damage_session", !st.active, eligible},
                       {Tr("Reset"), act("reset"), "damage_reset", false, eligible},
                       {Tr("Copy as CSV"), [] {
                            if (CopyText(DamageCsv(Cached("damagesession")))) Toast(Tr("Damage table copied as CSV."));
                        }, "damage_csv", false, st.types && !st.types->items.empty()}}) + 16;
            y += Muted(in.parent, kPanelPad, y, kPanelInner,
                       Tr("Records damage by your allies (grouped by type) and your character only while it runs, so it costs nothing when stopped. Values stay "
                          "after Stop until Reset, also when you switch windows. The damage window stays on top of a windowed or borderless game.")) + 10;
            y += NoteBox(in.parent, kPanelPad, y, kPanelInner, Tr(DamageStatus(r)), &v->status) + 18;
            // .damage-filters (gap 8px 18px, margin-bottom 10): Damage type <select>, then the rows
            DamageFilter& f = Filter();
            {
                Style ls{Face::Body, 14, C("ink")};
                ls.align = kLeft;
                std::vector<std::string> options{Tr("All damage")};
                for (const char* e : kElements) options.push_back(Tr(e));
                const float labelW = std::ceil(TextAdvance(Face::Body, 14, Tr("Damage type")));
                float widest = 0;
                for (const auto& o : options) widest = std::max(widest, TextAdvance(Face::Body, 14, o));
                const float selW = std::ceil(widest) + 20 + 24, selH = SelectHeight(ls);
                TextBox(in.parent, "Label", kPanelPad, y, labelW + 4, selH, ls, Tr("Damage type"));
                const float sx = kPanelPad + labelW + 8;
                Fill(in.parent, "Select", sx, y, selW, selH, Hex("#080e0e"), 3);
                void* frame = Outline(in.parent, "Select border", sx, y, selW, selH, Hex("#7b745a99"), 3);
                TextBox(in.parent, "Choice", sx + 11, y, selW - 30, selH, ls, options[f.element + 1]);
                Style ar{Face::Body, 11, C("ink")};
                ar.align = kCenter;
                TextBox(in.parent, "Arrow", sx + selW - 24, y, 16, selH, ar, "\xE2\x8C\x84");
                Hit hit;
                hit.kind = HitKind::Action;
                hit.name = "damage-element";
                hit.x = sx;
                hit.y = y;
                hit.w = selW;
                hit.h = selH;
                AddHover(hit, frame, Hex("#7b745a99"), Hex("#7b745a99"));
                const Ctx at = in;
                const float top = y;
                hit.action = [at, sx, top, selW, selH, options, id] {
                    float wx = 0, wy = 0;
                    if (!WindowPoint(at, sx, top, &wx, &wy)) return;
                    OpenList(wx, wy, selW, selH, options, Filter().element + 1, [id](int i) {
                        Filter().element = i - 1;
                        Rebuild(id);
                    });
                };
                in.Add(std::move(hit));
                // .damage-rows: "Rows" and a checkbox per row (gap 4px 14px), beside the select when it fits
                const auto sources = DamageRows(st, -1, {});
                float rowsX = sx + selW + 18, rowsY = y + (selH - 18) / 2, lineEnd = kPanelPad + kPanelInner;
                float itemsW = TextAdvance(Face::Body, 14, Tr("Rows"));
                for (const auto& src : sources) itemsW += 14 + 18 + 6 + TextAdvance(Face::Body, 14, src.own ? Tr(src.label) : src.label);
                float bottom = y + selH;
                if (!sources.empty()) {
                    if (rowsX + itemsW > lineEnd) { rowsX = kPanelPad; rowsY = y + selH + 8; }
                    Style ms{Face::Body, 14, C("muted")};
                    ms.align = kLeft;
                    float cx = rowsX, cy = rowsY;
                    const float rowsW = std::ceil(TextAdvance(Face::Body, 14, Tr("Rows")));
                    TextBox(in.parent, "Rows", cx, cy, rowsW + 4, 18, ms, Tr("Rows"));
                    cx += rowsW + 14;
                    for (const auto& src : sources) {
                        const std::string label = src.own ? Tr(src.label) : src.label;
                        // the row's name without its count (sources(): display names)
                        const std::string shown = src.own ? label : AllyName(*src.source);
                        const float w = 18 + 6 + std::ceil(TextAdvance(Face::Body, 14, shown));
                        if (cx + w > lineEnd && cx > rowsX) { cx = rowsX; cy += 18 + 4; }
                        const bool checked = std::find(f.hidden.begin(), f.hidden.end(), src.key) == f.hidden.end();
                        const std::string key = src.key;
                        CheckField(in, cx, cy, shown, checked, [id, key](bool on) {
                            auto& hidden = Filter().hidden;
                            hidden.erase(std::remove(hidden.begin(), hidden.end(), key), hidden.end());
                            if (!on) hidden.push_back(key);
                            Rebuild(id);
                        }, "damage-row-" + key, 14, 6);
                        cx += w + 14;
                    }
                    bottom = std::max(bottom, cy + 18);
                }
                y = bottom + 10;
            }
            // #damage-live: the table, or p.empty
            const auto dealtRows = DamageRows(st, f.element, f.hidden);
            const std::string empty = st.types && !st.types->items.empty() ? "Every row is hidden. Choose the rows to show."
                                                                          : "No damage recorded yet. Start the session and fight.";
            v->dealtHead = {Tr(f.element < 0 ? std::string("Source") : std::string("Source \xC2\xB7 ") + kElements[f.element])};
            for (const auto& col : kColumns) v->dealtHead.push_back(Tr(col.label));
            v->dealtRows = dealtRows.size();
            v->dealtEmpty = dealtRows.empty();
            if (dealtRows.empty()) y += EmptyText(in.parent, kPanelPad, y, kPanelInner, Tr(empty)) + 10;
            else {
                std::vector<bool> strong;
                const auto cells = DamageCells(dealtRows, false, &strong);
                v->dealt = MakeLiveTable(in.parent, kPanelPad, y, kPanelInner, v->dealtHead, {});
                v->dealt->rich = true;
                SetColumns(*v->dealt, AutoColumns(kPanelInner, v->dealtHead, cells, 12, 12, 180));
                y += UpdateLiveTable(*v->dealt, cells, {}, -1, strong);
            }
            // h3 Damage taken (margin 0 0 12px), then its compact table
            y += H3(in.parent, kPanelPad, y, kPanelInner, Tr("Damage taken")) + 12;
            const DamageState taken = TakenOf(r);
            const auto takenRows = DamageRows(taken, -1, {});
            v->takenHead = {Tr("Attacker")};
            for (int i : kCompact) v->takenHead.push_back(Tr(kColumns[i].label));
            v->takenRows = takenRows.size();
            v->takenEmpty = takenRows.empty();
            if (takenRows.empty()) {
                const std::string text = taken.types && !taken.types->items.empty() ? "Every row is hidden. Choose the rows to show."
                                                                                    : "No damage taken yet. Start the session and fight.";
                y += EmptyText(in.parent, kPanelPad, y, kPanelInner, Tr(text)) + 10;
            } else {
                std::vector<bool> strong;
                const auto cells = DamageCells(takenRows, true, &strong);
                v->taken = MakeLiveTable(in.parent, kPanelPad, y, kPanelInner, v->takenHead, {});
                v->taken->rich = true;
                SetColumns(*v->taken, AutoColumns(kPanelInner, v->takenHead, cells, 12, 12, 180));
                y += UpdateLiveTable(*v->taken, cells, {}, -1, strong);
            }
            return y;
        });
    };
    // updateDamageView: the numbers and the status in place; Start/Stop, new rows or an empty table
    // switching builds the panel again
    s.onData = [](Section& sec, const std::string& read) {
        if (read != "damagesession") return;
        static double deaths = -1;
        const JValue* r = Cached("damagesession");
        if (r && (*r)["deaths"].IsNumber() && (*r)["deaths"].number != deaths) {
            deaths = (*r)["deaths"].number;
            Read("deathrecap");  // a new death, or not read yet
        }
        auto v = std::static_pointer_cast<DamageView>(sec.live);
        const DamageState st = SessionOf(r);
        sec.every["damagesession"] = st.active ? 1000 : 3000;
        DamageFilter& f = Filter();
        const auto dealtRows = DamageRows(st, f.element, f.hidden);
        const auto takenRows = DamageRows(TakenOf(r), -1, {});
        if (!v || v->active != st.active || v->keys != RowKeys(r) || dealtRows.size() != v->dealtRows || takenRows.size() != v->takenRows ||
            dealtRows.empty() != v->dealtEmpty || takenRows.empty() != v->takenEmpty) {
            Rebuild(sec.id);
            return;
        }
        const float before = v->status.h;
        if (std::fabs(SetNote(v->status, Tr(DamageStatus(r))) - before) > 0.5f) { Rebuild(sec.id); return; }
        if (v->dealt) {
            std::vector<bool> strong;
            const auto cells = DamageCells(dealtRows, false, &strong);
            SetColumns(*v->dealt, AutoColumns(kPanelInner, v->dealtHead, cells, 12, 12, 180));
            UpdateLiveTable(*v->dealt, cells, {}, -1, strong);
        }
        if (v->taken) {
            std::vector<bool> strong;
            const auto cells = DamageCells(takenRows, true, &strong);
            SetColumns(*v->taken, AutoColumns(kPanelInner, v->takenHead, cells, 12, 12, 180));
            UpdateLiveTable(*v->taken, cells, {}, -1, strong);
        }
    };
}

// ------------------------------------------------------------------------------ death recap
// EpochDeathRecap.html: one death (0 = the newest), drawn after the panel's "Record deaths" card.
struct NumberWords { std::string group, decimal; };
NumberWords Locale() {
    if (language == "tr") return {".", ","};
    if (language == "ru") return {"\xC2\xA0", ","};
    return {",", "."};
}
// Intl.NumberFormat(locale, {maximumFractionDigits: 0})
std::string Whole(double v) {
    if (!std::isfinite(v)) return "\xE2\x80\x94";
    const long long n = std::llround(v);
    const std::string digits = std::to_string(n < 0 ? -n : n);
    const NumberWords w = Locale();
    std::string out;
    // tr/ru do not group four digits (Intl: minimumGroupingDigits 2)
    const bool group = language == "en" || digits.size() > 4;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (group && i && (digits.size() - i) % 3 == 0) out += w.group;
        out += digits[i];
    }
    return (n < 0 ? "-" : "") + out;
}
std::string Tenth(double v) {  // one decimal
    if (!std::isfinite(v)) return "\xE2\x80\x94";
    const long long t = std::llround(v * 10);
    std::string out = Whole(static_cast<double>(t / 10));
    if (t < 0 && t / 10 == 0) out = "-0";
    return out + Locale().decimal + std::to_string(std::llabs(t % 10));
}
struct DeathHit { double t = 0, damage = 0, ward = 0, from = 0; double health = NAN; std::string attacker, what; bool dot = false; int count = 1; };
std::string Short(const std::string& s, size_t most = 96) { return s.size() > most ? s.substr(0, most) : s; }
float DrawDeath(Section& s, const Ctx& c, float x, float y, float w) {
    const JValue* r = Cached("deathrecap");
    Style empty{Face::Body, 14, C("muted")};
    if (!r || ((*r)["ok"].IsBool() && !(*r)["ok"].boolean)) {
        const std::string text = r && (*r)["error"].IsString() ? (*r)["error"].text : Tr("Waiting for an offline character\xE2\x80\xA6");
        return 10 + EmptyText(c.parent, x, y + 10, w, text) + 10;
    }
    const auto& deaths = (*r)["deaths"].items;
    if (deaths.empty()) {
        const bool off = (*r)["enabled"].IsBool() && !(*r)["enabled"].boolean;
        return 10 + EmptyText(c.parent, x, y + 10, w, Tr(off ? "The death recap is off. Turn it on to keep the hits before each death." : "No deaths recorded since the game started.")) + 10;
    }
    const int count = static_cast<int>(std::min<size_t>(deaths.size(), 10));
    int at = std::clamp(std::atoi(s.state["deathIndex"].c_str()), 0, count - 1);
    const JValue& d = deaths[at];
    const float size = 13.3f;  // .dr{font-size:.95em}
    const float gap = 6.3f;    // .dr{gap:.45em}
    const int id = s.id;
    Style base{Face::Body, size, C("ink")};
    base.wrap = true;
    float cy = y;
    // .dr-head: Death <b>n</b>, the time ago (muted), Older / Newer at the right
    {
        Style head = base;
        head.wrap = false;
        void* t = TextBox(c.parent, "Death", x, cy, w, LineHeight(head), head, "");
        bool on = true;
        void* ra[]{&on};
        managed::Invoke(T("set_richText", 1), t, ra);
        const double ago = std::max(0.0, d["secondsAgo"].Num(0));
        const std::string when = ago < 90 ? Fmt("{0} s ago", {Num(std::round(ago))}) : Fmt("{0} min ago", {Num(std::round(ago / 60))});
        SetText(t, "<color=#ac8950>" + Tr("Death") + " <b>" + Num(std::floor(d["number"].Num(0))) + "</b></color>   <color=#b3beb8>" + Plain(when) + "</color>");
        float rowH = LineHeight(head);
        if (count > 1) {
            float right = x + w;
            const auto step = [id, count](int delta) {
                return [id, count, delta] {
                    if (Section* s2 = FindSection(id)) {
                        const int now = std::clamp(std::atoi(s2->state["deathIndex"].c_str()) + delta, 0, count - 1);
                        s2->state["deathIndex"] = std::to_string(now);
                        RebuildTail(id);
                    }
                };
            };
            Button newer = AddButton(c, right, cy, Tr("Newer") + " \xE2\x80\xBA", Look::Small, step(-1), true, "death-newer");
            right -= newer.w + 4;
            Button older = AddButton(c, right, cy, "\xE2\x80\xB9 " + Tr("Older"), Look::Small, step(1), true, "death-older");
            if (at >= count - 1) SetEnabled(c, older, false);
            if (at <= 0) SetEnabled(c, newer, false);
            rowH = std::max(rowH, older.h);
        }
        cy += rowH + gap;
    }
    // p.dr-killer and p.dr-sum (margin 0)
    {
        Style p = base;
        const JValue* k = d.Find("killer");
        std::string text;
        if (!k || !k->IsObject()) text = Tr("The killing blow was not recorded.");
        else {
            const std::string attacker = Short((*k)["attacker"].Str("Unknown")), what = Short((*k)["what"].Str());
            const bool dot = (*k)["dot"].IsBool() && (*k)["dot"].boolean;
            text = Tr("Killed by") + " <b><color=#e6a08f>" + Plain(attacker) + "</color></b>" + (what.empty() ? "" : " \xC2\xB7 " + Plain(what)) + " \xC2\xB7 " +
                   Tr(dot ? "Damage over time" : "Hit") + " <b><color=#e6a08f>" + Whole((*k)["damage"].Num(0)) + "</color></b>";
        }
        void* t = TextBox(c.parent, "Killer", x, cy, w, LineHeight(p), p, "");
        bool on = true;
        void* ra[]{&on};
        managed::Invoke(T("set_richText", 1), t, ra);
        SetText(t, text);
        cy += TextLines(t) * LineHeight(p) + gap;
        std::string sum = Tr("Damage taken in the last 10 s") + " <b>" + Whole(d["total"].Num(0)) + "</b>";
        if (d["dot"].Num(0) > 0) sum += " \xC2\xB7 " + Tr("damage over time") + " <b>" + Whole(d["dot"].Num(0)) + "</b>";
        if (d["maxHealth"].Num(0) > 0) sum += " \xC2\xB7 " + Tr("maximum health") + " <b>" + Whole(d["maxHealth"].Num(0)) + "</b>";
        void* t2 = TextBox(c.parent, "Sum", x, cy, w, LineHeight(p), p, "");
        managed::Invoke(T("set_richText", 1), t2, ra);
        SetText(t2, sum);
        cy += TextLines(t2) * LineHeight(p) + gap;
    }
    const double total = d["total"].Num(0);
    const auto share = [total](double v) { return total > 0 ? Num(std::round(100 * v / total)) + "%" : std::string("\xE2\x80\x94"); };
    // .dr-types: icon, type, <b>value</b> <small>share</small>
    {
        const auto& byType = d["byType"].items;
        float cx = x, rowH = 0;
        bool any = false;
        for (int i = 0; i < 7 && i < static_cast<int>(byType.size()); ++i) {
            const double value = std::max(0.0, byType[i].Num(0));
            if (value <= 0) continue;
            any = true;
            const std::string text = Tr(kElements[i]) + " <b>" + Whole(value) + "</b> <size=83%><alpha=#B3>" + share(value) + "</alpha></size>";
            const float tw = std::ceil(TextAdvance(Face::Body, size, Tr(kElements[i]) + " " + Whole(value) + " " + share(value)) * 1.04f) + 4;
            const float itemW = 17.3f + 4 + tw;
            if (cx > x && cx + itemW > x + w) { cx = x; cy += rowH + 4; }
            Picture(c.parent, "Type", cx, cy + 0.5f, 17.3f, 17.3f, "dmg-type-" + std::to_string(i) + ".png", false, kWhite);
            Style ts = base;
            ts.wrap = false;
            ts.align = kLeft;
            void* t = TextBox(c.parent, "Type text", cx + 17.3f + 4, cy, tw, 18.3f, ts, "");
            bool on = true;
            void* ra[]{&on};
            managed::Invoke(T("set_richText", 1), t, ra);
            SetText(t, text);
            rowH = 18.3f;
            cx += itemW + 10.6f;
        }
        if (any) cy += rowH + gap;
    }
    if (d["unsplit"].Num(0) > 0) {
        Style note{Face::Body, 10, Alpha(C("ink"), 0.7f)};
        note.wrap = true;
        cy += Paragraph(c.parent, "Note", x, cy, w, note, Tr("Some hits are not split by damage type.")) + gap;
    }
    // h4 (margin-top .4em, .9em, 600, gold) and a table.dr-table (12px, padding 2px 6px)
    const auto heading = [&](const std::string& text) {
        Style h4{Face::BodySemi, 12, C("gold")};
        cy += 5.3f;
        TextBox(c.parent, "Heading", x, cy, w, LineHeight(h4), h4, text);
        cy += LineHeight(h4) + gap;
    };
    const auto table = [&](const std::vector<std::string>& head, const std::vector<std::vector<std::string>>& rows, const std::vector<bool>& fatal) {
        auto t = MakeLiveTable(c.parent, x, cy, w, head, {}, 12, 6, 2);
        t->rich = true;
        SetColumns(*t, AutoColumns(w, head, rows, 12, 6, 180));
        std::vector<std::vector<Color>> colors;
        for (size_t i = 0; i < rows.size(); ++i) colors.push_back(std::vector<Color>(head.size(), i < fatal.size() && fatal[i] ? Hex("#e6a08f") : C("ink")));
        cy += UpdateLiveTable(*t, rows, colors) + gap;
        return t;
    };
    std::vector<std::shared_ptr<LiveTable>> keep;
    const auto& attackers = d["attackers"].items;
    if (!attackers.empty()) {
        heading(Tr("By attacker"));
        std::vector<std::vector<std::string>> rows;
        for (size_t i = 0; i < attackers.size() && i < 40; ++i) {
            const JValue& a = attackers[i];
            std::string damage = Whole(a["damage"].Num(0));
            if (a["dot"].Num(0) > 0) damage += " <size=83%><alpha=#B3>(" + Tr("damage over time") + " " + Whole(a["dot"].Num(0)) + ")</alpha></size>";
            rows.push_back({Plain(Short(a["name"].Str("Unknown"))), damage, share(a["damage"].Num(0)), Whole(std::floor(std::max(0.0, a["hits"].Num(0))))});
        }
        keep.push_back(table({Tr("Attacker"), Tr("Damage"), Tr("Share"), Tr("Hits")}, rows, {}));
    }
    // the last hits, newest first; ticks of one ailment from one attacker in a row are one line
    std::vector<DeathHit> hits;
    const auto& list = d["hits"].items;
    const size_t first = list.size() > 600 ? list.size() - 600 : 0;
    for (size_t i = list.size(); i-- > first;) {
        const JValue& h = list[i];
        DeathHit e;
        e.t = std::min(0.0, h["t"].Num(0));
        e.attacker = Short(h["attacker"].Str("Unknown"));
        e.what = Short(h["what"].Str());
        e.dot = h["dot"].IsBool() && h["dot"].boolean;
        e.damage = h["damage"].Num(0);
        e.health = h["health"].IsNumber() ? h["health"].number : NAN;
        e.ward = h["ward"].Num(0);
        e.from = e.t;
        if (!hits.empty() && e.dot && hits.back().dot && hits.back().attacker == e.attacker && hits.back().what == e.what) {
            hits.back().count++;
            hits.back().damage += e.damage;
            hits.back().from = e.t;
            continue;
        }
        if (hits.size() >= 15) break;
        hits.push_back(e);
    }
    if (!hits.empty()) {
        heading(Tr("Last hits"));
        std::vector<std::vector<std::string>> rows;
        std::vector<bool> fatal;
        for (const auto& h : hits) {
            const std::string time = (h.count > 1 ? Tenth(h.from) + " \xE2\x80\xA6 " + Tenth(h.t) : Tenth(h.t)) + " s";
            std::string what = h.what.empty() ? "\xE2\x80\x94" : Plain(h.what);
            if (h.dot) what += " <size=83%><alpha=#B3>DoT</alpha></size>";
            if (h.count > 1) what += " <size=83%><alpha=#B3>\xC3\x97" + std::to_string(h.count) + "</alpha></size>";
            std::string health = std::isfinite(h.health) ? Whole(std::max(0.0, h.health)) : "\xE2\x80\x94";
            if (h.ward > 0) health += " <size=83%><alpha=#B3>+ " + Whole(h.ward) + " " + Tr("ward") + "</alpha></size>";
            rows.push_back({time, Plain(h.attacker), what, Whole(h.damage), health});
            fatal.push_back(std::isfinite(h.health) && h.health <= 0);
        }
        keep.push_back(table({Tr("Time"), Tr("Attacker"), Tr("Skill or ailment"), Tr("Damage"), Tr("Health after")}, rows, fatal));
    }
    heading(Tr("Buffs and debuffs at death"));
    const auto& effects = d["effects"].items;
    std::vector<const JValue*> shown;
    for (const auto& e : effects) if (e["name"].IsString() && !e["name"].text.empty() && shown.size() < 32) shown.push_back(&e);
    if (shown.empty()) {
        Style m{Face::Body, size, C("muted")};
        m.wrap = true;
        m.line = 1.65f;
        cy += Paragraph(c.parent, "None", x, cy, w, m, Tr("None shown by the game."));
    } else {
        // ul.dr-effects: tags (padding 1px 7px, a border by kind), gap 4
        Style ts{Face::Body, size, C("ink")};
        ts.align = kLeft;
        const float tagH = LineHeight(ts) + 2 + 2;
        float cx = x;
        for (const JValue* e : shown) {
            const std::string name = Short((*e)["name"].Str(), 80);
            const double stacks = std::floor(std::max(0.0, (*e)["stacks"].Num(0)));
            const std::string extra = stacks > 1 ? " \xC3\x97" + Num(stacks) : "";
            const float tw = std::ceil(TextAdvance(Face::Body, size, name + extra)) + 2;
            const float tagW = tw + 14 + 2;
            if (cx > x && cx + tagW > x + w) { cx = x; cy += tagH + 4; }
            const bool buff = (*e)["buff"].IsBool() && (*e)["buff"].boolean;
            Outline(c.parent, "Tag", cx, cy, tagW, tagH, buff ? Hex("#5f9e7a80") : Hex("#c0706080"), 3);
            void* t = TextBox(c.parent, "Tag text", cx + 8, cy, tw + 2, tagH, ts, "");
            bool on = true;
            void* ra[]{&on};
            managed::Invoke(T("set_richText", 1), t, ra);
            SetText(t, Plain(name) + (extra.empty() ? "" : " <size=83%><alpha=#B3>" + extra + "</alpha></size>"));
            cx += tagW + 4;
        }
        cy += tagH;
    }
    s.live = std::make_shared<std::vector<std::shared_ptr<LiveTable>>>(std::move(keep));
    return cy - y;
}
void RegisterDeathTail() {
    Tail tail;
    tail.reads = {"deathrecap"};
    tail.head = [](Section& s) {
        const int id = s.id;
        std::vector<HeadButton> buttons;
        buttons.push_back({Tr("Refresh"), [] { Read("deathrecap"); }, "death_refresh", false});
        buttons.push_back({Tr("Clear"), [id] {
                               Send("deathrecap clear", [id](const std::string& reply) {
                                   std::string why;
                                   if (Refused(reply, &why)) { Toast(PlayerError(why), true); return; }
                                   if (Section* s2 = FindSection(id)) s2->state["deathIndex"] = "0";
                                   Read("deathrecap");
                               });
                           }, "death_clear", false});
        return buttons;
    };
    // #death-live after the card grid: no gap between them
    tail.build = [](Section& s, const Ctx& c) { return DrawDeath(s, c, kPanelPad, 0, kPanelInner); };
    tail.onData = [](Section& s, const std::string&) {
        s.state["deathIndex"] = "0";  // refreshDeaths: back to the newest
        RebuildTail(s.id);
    };
    RegisterTail("death_recap", std::move(tail));
}

// -------------------------------------------------------------------- live summoned allies
const char* const kSummonColumns[13] = {"Measured DPS", "Last damage", "DPS change", "Health", "Armour", "Damage increase", "Physical",
                                        "Fire",         "Cold",        "Lightning", "Void",   "Necrotic", "Poison"};
struct SummonChoices { std::set<std::string> hidden; bool individual = false, loaded = false, open = false; } summonChoices;
SummonChoices& Choices() {
    if (!summonChoices.loaded) {
        summonChoices.loaded = true;
        const JValue p = Preference("epochpact.display-preferences.v1");
        for (const auto& c : p["summonColumns"].items) if (c.IsString()) summonChoices.hidden.insert(c.text);
        for (const auto& o : p["summonOptions"].items) if (o.IsString() && o.text == "individual") summonChoices.individual = true;
    }
    return summonChoices;
}
std::map<std::string, double> summonBaseline;  // instance -> measured DPS when saved
std::string SummonName(const JValue& s) {
    std::string n = s["name"].Str();
    for (size_t at; (at = n.find("(Clone)")) != std::string::npos;) n.erase(at, 7);
    std::string plain;
    for (size_t i = 0; i < n.size(); ++i) {  // clean(): game rich-text tags out
        if (n[i] == '<') {
            const size_t end = n.find('>', i);
            if (end != std::string::npos) { i = end; continue; }
        }
        plain += n[i];
    }
    plain = Trim(plain);
    return plain.empty() ? "Ally" : plain;
}
struct SummonRow {
    std::string label;
    int count = 1;
    std::vector<const JValue*> members;
    double measuredDps = 0, lastDamage = 0, health = 0, maxHealth = 0, armour = NAN, damageIncreased = NAN;
    double resist[7]{NAN, NAN, NAN, NAN, NAN, NAN, NAN};
};
const char* const kResist[7] = {"Physical", "Fire", "Cold", "Lightning", "Void", "Necrotic", "Poison"};
std::vector<SummonRow> SummonRows(const JValue& summons, bool individual) {
    std::vector<SummonRow> out;
    const auto num = [](const JValue& v) { return v.IsNumber() ? v.number : NAN; };
    if (individual) {
        for (const auto& s : summons.items) {
            SummonRow r;
            r.label = SummonName(s);
            r.members = {&s};
            r.measuredDps = num(s["measuredDps"]);
            r.lastDamage = num(s["lastDamage"]);
            r.health = num(s["health"]);
            r.maxHealth = num(s["maxHealth"]);
            r.armour = num(s["armour"]);
            r.damageIncreased = num(s["damageIncreased"]);
            for (int i = 0; i < 7; ++i) r.resist[i] = num(s["resistances"][kResist[i]]);
            out.push_back(std::move(r));
        }
        return out;
    }
    std::vector<std::string> order;
    std::map<std::string, std::vector<const JValue*>> groups;
    for (const auto& s : summons.items) {
        const std::string key = SummonName(s);
        if (!groups.count(key)) order.push_back(key);
        groups[key].push_back(&s);
    }
    for (const auto& key : order) {
        const auto& list = groups[key];
        SummonRow r;
        r.label = key;
        r.count = static_cast<int>(list.size());
        r.members = list;
        const auto sum = [&](const char* f) { double t = 0; for (auto* s : list) { const double v = num((*s)[f]); if (std::isfinite(v)) t += v; } return t; };
        const auto avg = [&](auto get) {
            double t = 0;
            int n = 0;
            for (auto* s : list) { const double v = get(*s); if (std::isfinite(v)) { t += v; ++n; } }
            return n ? t / n : NAN;
        };
        r.measuredDps = sum("measuredDps");
        for (auto* s : list) r.lastDamage = std::max(r.lastDamage, std::isfinite(num((*s)["lastDamage"])) ? num((*s)["lastDamage"]) : 0);
        r.health = sum("health");
        r.maxHealth = sum("maxHealth");
        r.armour = avg([&](const JValue& s) { return num(s["armour"]); });
        r.damageIncreased = avg([&](const JValue& s) { return num(s["damageIncreased"]); });
        for (int i = 0; i < 7; ++i) r.resist[i] = avg([&](const JValue& s) { return num(s["resistances"][kResist[i]]); });
        out.push_back(std::move(r));
    }
    return out;
}
std::string SummonCell(const SummonRow& r, const std::string& column) {
    if (column == "Measured DPS") return OneDecimal(r.measuredDps);
    if (column == "Last damage") return OneDecimal(r.lastDamage);
    if (column == "DPS change") {
        double change = 0;
        bool tracked = false;
        for (auto* s : r.members) {
            auto it = summonBaseline.find((*s)["instance"].IsNumber() ? Num((*s)["instance"].number) : (*s)["instance"].Str());
            if (it == summonBaseline.end()) continue;
            tracked = true;
            change += ((*s)["measuredDps"].IsNumber() ? (*s)["measuredDps"].number : 0) - it->second;
        }
        if (!tracked) return "\xE2\x80\x94";
        return (change >= 0 ? "+" : "") + OneDecimal(change);
    }
    if (column == "Health") return OneDecimal(r.health) + " / " + OneDecimal(r.maxHealth);
    if (column == "Armour") return OneDecimal(r.armour);
    if (column == "Damage increase") return OneDecimal(r.damageIncreased) + "%";
    for (int i = 0; i < 7; ++i) if (column == kResist[i]) return OneDecimal(r.resist[i]) + "%";
    return "";
}
struct SummonView {
    std::shared_ptr<LiveTable> table;
    std::vector<std::string> head, columns;
    size_t rows = 0;
    std::string state;  // which text or table is shown
};
std::string SummonState(const JValue* r) {
    if (!r || !r->Find("summons")) return "waiting";
    if ((*r)["readError"].IsBool() && (*r)["readError"].boolean) return "error";
    if ((*r)["summons"].items.empty()) return "empty";
    return std::string("table") + ((*r)["damageSampleClipped"].IsBool() && (*r)["damageSampleClipped"].boolean ? "+clipped" : "");
}
void MakeSummonAllies(Section& s) {
    s.reads.insert("summonread");
    s.polls.insert("summonread");
    s.every["summonread"] = 2000;
    s.build = [](Section& sec, const Ctx& c) {
        auto v = std::make_shared<SummonView>();
        sec.live = v;
        const int id = sec.id;
        SummonChoices& ch = Choices();
        return InPanel(c, [&](const Ctx& in, float y) {
            y += Head(in, y, Tr("Live summoned allies"), {{Tr("Refresh allies"), [] { Read("summonread"); }, "summons_read", false, true}}) + 16;
            // details.display-options (margin 14px 0; the head's 16 before it): Visible columns
            y += Details(in, kPanelPad, y, Tr("Visible columns"), ch.open, [id] {
                Choices().open = !Choices().open;
                Rebuild(id);
            });
            if (ch.open) {
                // .column-options: flex, wrap, gap 16, padding 16px 0; label: checkbox, gap 8, the name
                float cx = kPanelPad, cy = y + 16;
                const auto add = [&](const std::string& label, bool checked, std::function<void(bool)> changed, const std::string& name) {
                    const float w = 18 + 8 + std::ceil(TextAdvance(Face::Body, 14, label));
                    if (cx > kPanelPad && cx + w > kPanelPad + kPanelInner) { cx = kPanelPad; cy += 18 + 16; }
                    CheckField(in, cx, cy, label, checked, std::move(changed), name, 14, 8);
                    cx += w + 16;
                };
                add(Tr("Show each ally separately"), ch.individual, [id](bool on) { Choices().individual = on; Rebuild(id); }, "summon-individual");
                for (const char* col : kSummonColumns) {
                    const std::string name = col;
                    add(Tr(col), !ch.hidden.count(name), [id, name](bool on) {
                        if (on) Choices().hidden.erase(name);
                        else Choices().hidden.insert(name);
                        Rebuild(id);
                    }, "summon-column-" + name);
                }
                y = cy + 18 + 16;
            }
            y += 14;
            // .actions (margin-top 18 collapses with the details' 14)
            y += 4;
            const Button save = AddButton(in, kPanelPad, y, Tr("Save damage baseline"), Look::Normal, [id] {
                const JValue* r = Cached("summonread");
                if (!r || (*r)["summons"].items.empty()) { Toast(Tr("Refresh live summons before saving a damage baseline."), true); return; }
                summonBaseline.clear();
                for (const auto& s2 : (*r)["summons"].items)
                    summonBaseline[s2["instance"].IsNumber() ? Num(s2["instance"].number) : s2["instance"].Str()] = s2["measuredDps"].Num(0);
                Toast(Tr("Damage baseline saved for these active allies. Compare against the same target."));
                Rebuild(id);
            }, false, "summon-baseline");
            const Button clear = AddButton(in, kPanelPad + save.w + 10, y, Tr("Clear baseline"), Look::Normal, [id] {
                summonBaseline.clear();
                Rebuild(id);
            }, false, "summon-baseline-clear");
            y += std::max(save.h, clear.h);
            // #summon-live
            const JValue* r = Cached("summonread");
            v->state = SummonState(r);
            const auto note = [&](const std::string& text) { y += 10 + NoteBox(in.parent, kPanelPad, y + 10, kPanelInner, text) + 18; };
            if (v->state == "waiting") note(Tr("Waiting for live summoned allies\xE2\x80\xA6"));
            else if (v->state == "error") note(Tr("Live summon data is unavailable. Previous values are not shown as current."));
            else if (v->state == "empty") note(Tr("No active summoned allies found."));
            else {
                if (v->state.find("+clipped") != std::string::npos)
                    note(Tr("Damage event capacity was reached. These numbers are a lower bound until a fresh five-second window is collected."));
                for (const char* col : kSummonColumns) if (!ch.hidden.count(col)) v->columns.push_back(col);
                v->head = {Tr(ch.individual ? "Ally" : "Ally type")};
                for (const auto& col : v->columns) v->head.push_back(Tr(col));
                const auto rows = SummonRows((*r)["summons"], ch.individual);
                std::vector<std::vector<std::string>> cells;
                for (const auto& row : rows) {
                    std::vector<std::string> line{Plain(row.label) + (row.count > 1 ? " <color=#b3beb8>\xC3\x97" + std::to_string(row.count) + "</color>" : "")};
                    for (const auto& col : v->columns) line.push_back(SummonCell(row, col));
                    cells.push_back(std::move(line));
                }
                v->rows = rows.size();
                // table.summon-table: 13px, th accent 600, th/td padding 12px 14px, border #665a3b
                v->table = MakeLiveTable(in.parent, kPanelPad, y, kPanelInner, v->head, {}, 13, 14, 12);
                v->table->rich = true;
                SetColumns(*v->table, AutoColumns(kPanelInner, v->head, cells, 13, 14, 180));
                y += UpdateLiveTable(*v->table, cells);
                std::string text = Fmt("Measured DPS uses damage actually applied in a rolling five-second window ({0} seconds collected), including mitigation and "
                                       "critical hits. Attack a target to measure damage; changing gear alone cannot reveal exact combat DPS. Compare on the same "
                                       "target and encounter. Damage increase remains a separate bonus. {1}Updates every two seconds while visible.",
                                       {OneDecimal((*r)["damageWindowSeconds"].Num(NAN)),
                                        ch.individual ? "" : "Grouped rows add damage and health across the group and average armour, damage increase and resistances. "});
                y += 10 + Muted(in.parent, kPanelPad, y + 10, kPanelInner, text) + 10;
            }
            return y;
        });
    };
    // every 2 s: the numbers in place; another state, row count or column set builds the panel again
    s.onData = [](Section& sec, const std::string& read) {
        if (read != "summonread") return;
        auto v = std::static_pointer_cast<SummonView>(sec.live);
        const JValue* r = Cached("summonread");
        if (!v || SummonState(r) != v->state || !v->table) { Rebuild(sec.id); return; }
        const auto rows = SummonRows((*r)["summons"], Choices().individual);
        if (rows.size() != v->rows) { Rebuild(sec.id); return; }
        std::vector<std::vector<std::string>> cells;
        for (const auto& row : rows) {
            std::vector<std::string> line{Plain(row.label) + (row.count > 1 ? " <color=#b3beb8>\xC3\x97" + std::to_string(row.count) + "</color>" : "")};
            for (const auto& col : v->columns) line.push_back(SummonCell(row, col));
            cells.push_back(std::move(line));
        }
        SetColumns(*v->table, AutoColumns(kPanelInner, v->head, cells, 13, 14, 180));
        UpdateLiveTable(*v->table, cells);
    };
}
}  // namespace

void RegisterSummonSections() {
    RegisterSection("damage_meter", MakeDamageMeter);
    RegisterSection("summon_allies", MakeSummonAllies);
    RegisterDeathTail();
}

}  // namespace ep::ingame::view
