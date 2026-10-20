// The Circle of Fortune page (app.js renderCof): membership, rank, Favor and Reputation cards,
// the prophecy slots, their reward / lens / charges forms with the preview, the lens catalog.
// Every action changes the game save: it confirms first and runs for the character it was read for.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
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
std::string SaveReason(const std::string& snapshot) {
    std::string reason;
    if (!Eligible(&reason)) return reason;
    if (!snapshot.empty() && !Cached(snapshot)) return Tr("Refresh live data to enable this action for the loaded character.");
    return "";
}
float InPanel(const Ctx& c, const std::function<float(const Ctx& in, float y)>& draw) {
    Panel p = OpenPanel(c.parent, kPadX, 0, kPanelW);
    const Ctx in = c.At(p.rt.Get(), kPadX, 0);
    const float h = draw(in, kPanelPad) + kPanelPad;
    ClosePanel(p, kPanelW, h);
    return h;
}
std::string SaveMessage(const Control& c) { return Tr(c.description) + " " + Tr("This action changes the game save. The backend creates a backup before applying it."); }
std::string Grouped(double v) {  // toLocaleString('en-US')
    const long long n = std::llround(v);
    const std::string digits = std::to_string(n < 0 ? -n : n);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return (n < 0 ? "-" : "") + out;
}
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
std::string NameOf(const JValue& x) { return NoTags(x["uiLabel"].Str(x["name"].Str(x["label"].Str()))); }
std::string IdOf(const JValue& v) { return v.IsNumber() ? Num(v.number) : v.IsString() ? v.text : ""; }
// A game-save card with its form; the command comes from the form when it has fields.
CardMaker SaveForm(const std::string& id, const FieldChoices& choices, const FieldValue& initial, int section, std::function<std::string(const std::string& line, const JValue&)> success,
                   std::function<std::string()> suffix = {}, Fields extra = {}) {
    return [=](const Ctx& cc, float x, float y, float w) {
        const Control& ctl = Ctl(id);
        const bool hasFields = std::any_of(ctl.params.begin(), ctl.params.end(), [](const Param& p) { return p.name != "save_id"; });
        Fields fields;
        if (hasFields || extra)
            fields = [=](const Ctx& fc, float fx, float fy, float fw) {
                float h = hasFields ? ParameterFields(fc, fx, fy, fw, ctl, choices, initial, section) : 0;
                if (extra) h += extra(fc, fx, fy + h, fw);
                return h;
            };
        return ActionCard(cc, x, y, w, ctl, ActionCaption(ctl), true, [id, success, suffix] {
            const Control& c = Ctl(id);
            std::string line, why;
            if (!FormCommand(c, &line, &why)) { Toast(why, true); return; }
            if (suffix) line += suffix();
            ActionRun run;
            run.command = line;
            run.snapshot = "cofread";
            run.confirmTitle = Tr(c.label);
            run.confirmMessage = SaveMessage(c);
            run.success = [line, success](const JValue& v) { return success(line, v); };
            run.reread = {"cofread"};
            run.done = [id](const JValue&) { ForgetForm(id); };
            RunAction(run);
        }, SaveReason("cofread"), fields);
    };
}
// the value a command line was sent with (the last word)
std::string LastWord(const std::string& line) { return line.substr(line.find_last_of(' ') + 1); }
std::string WordAt(const std::string& line, int index) {
    size_t at = 0;
    for (int i = 0; i < index && at != std::string::npos; ++i) at = line.find(' ', at + 1);
    if (at == std::string::npos) return "";
    const size_t end = line.find(' ', at + 1);
    return line.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
}

// ----------------------------------------------------------------------------- Circle of Fortune
bool switchFromMerchant = false;
void MakeCofMain(Section& s) {
    s.reads.insert("cofread");
    s.build = [](Section& sec, const Ctx& c) {
        const int id = sec.id;
        return InPanel(c, [id](const Ctx& in, float y) {
            y += SectionHead(in, kPanelPad, y, kPanelInner, Tr("Circle of Fortune"), {{Tr("Refresh live"), [] { Read("cofread"); }}}) + 16;
            const JValue* live = Cached("cofread");
            const JValue* cof = live ? live->Find("cof") : nullptr;
            if (cof && cof->IsObject()) {
                y += SummaryTiles(in.parent, kPanelPad, y, kPanelInner,
                                  {{Num((*cof)["rank"].Num(0)), Fmt("Rank / {0}", {Num((*live)["maxRank"].Num(0))})},
                                   {Grouped((*cof)["favor"].Num(0)), Tr("Favor balance")},
                                   {Grouped((*cof)["reputation"].Num(0)), Tr("Reputation within rank")},
                                   {(*cof)["member"].IsBool() && (*cof)["member"].boolean ? Tr("Member") : "\xE2\x80\x94", Tr("Circle of Fortune")}}) + 22;
            }
            const auto liveValue = [](const std::string& key) {
                return [key](const Param& p) -> std::string {
                    const JValue* r = Cached("cofread");
                    const JValue* v = r ? r->Path(key) : nullptr;
                    if (v && v->IsNumber()) return Num(v->number);
                    (void)p;
                    return "";
                };
            };
            const auto defaultValue = [](const std::string& control) {
                return [control](const Param&) -> std::string {
                    const Control& c = Ctl(control);
                    return c.hasDefault ? Num(c.def) : std::string();
                };
            };
            std::vector<CardMaker> cards;
            cards.push_back(SaveForm("cof_join", {}, {}, id, [](const std::string&, const JValue&) { return Tr("Joined Circle of Fortune."); },
                                     [] { return std::string(switchFromMerchant ? " switch" : ""); },
                                     [id](const Ctx& fc, float fx, float fy, float) {
                                         // label.inline-check (margin-top 14) after the empty form grid
                                         CheckField(fc, fx, fy + 14, Tr("Leave Merchant\xE2\x80\x99s Guild and join CoF"), switchFromMerchant, [id](bool on) {
                                             switchFromMerchant = on;
                                             Rebuild(id);
                                         }, "cof-switch");
                                         return 14.f + 18;
                                     }));
            cards.push_back(SaveForm("cof_rank", {}, liveValue("cof.rank"), id,
                                     [](const std::string& line, const JValue&) { return Fmt("Circle of Fortune rank set to {0}.", {LastWord(line)}); }));
            cards.push_back(SaveForm("cof_favor", {}, liveValue("cof.favor"), id,
                                     [](const std::string& line, const JValue&) { return Fmt("Favor set to {0}.", {LastWord(line)}); }));
            cards.push_back(SaveForm("cof_reputation_grant", {}, defaultValue("cof_reputation_grant"), id,
                                     [](const std::string& line, const JValue&) { return Fmt("Added {0} Reputation.", {LastWord(line)}); }));
            cards.push_back(SaveForm("cof_lenses_unlock", {}, {}, id, [](const std::string&, const JValue&) { return Tr("Available lenses unlocked."); }));
            return y + CardGrid(in, kPanelPad, y, kPanelInner, cards);
        });
    };
}

// The prophecy slots: the chosen slot's reward, lens and charges fill the fields when it changes.
std::string ChosenSlot(const std::string& control) {
    const Form& f = FormOf(control);
    if (auto it = f.values.find("slot"); it != f.values.end() && !it->second.empty()) return it->second;
    const JValue* r = Cached("cofread");
    if (r) for (const auto& s : (*r)["slots"].items) if (!(s["locked"].IsBool() && s["locked"].boolean)) return IdOf(s["index"]);
    return "";
}
const JValue* SlotOf(const std::string& index) {
    const JValue* r = Cached("cofread");
    if (r) for (const auto& s : (*r)["slots"].items) if (IdOf(s["index"]) == index) return &s;
    return nullptr;
}
FieldChoices ProphecyChoices() {
    return [](const Param& p) -> std::vector<Choice> {
        const JValue* r = Cached("cofread");
        std::vector<Choice> out;
        if (!r) return out;
        if (p.name == "slot") {
            for (const auto& s : (*r)["slots"].items)
                if (!(s["locked"].IsBool() && s["locked"].boolean))
                    out.push_back({IdOf(s["index"]), Fmt("Slot {0} \xC2\xB7 Rank {1}", {Num(s["index"].Num(0) + 1), Num(s["rankRequired"].Num(0))})});
            return out;
        }
        if (p.name == "reward") {
            out.push_back({"none", Tr("Clear selection (none)")});
            for (const auto& x : (*r)["rewards"].items)
                if (x["available"].IsBool() && x["available"].boolean) out.push_back({IdOf(x["id"]), NameOf(x)});
            return out;
        }
        if (p.name == "lens") {
            out.push_back({"none", Tr("Clear selection (none)")});
            const double rank = r->Path("cof.rank") ? r->Path("cof.rank")->Num(0) : 0;
            for (const auto& x : (*r)["lenses"].items)
                if (x["purchased"].IsBool() && x["purchased"].boolean && x["rankRequired"].Num(99) <= rank) out.push_back({IdOf(x["id"]), NameOf(x)});
            return out;
        }
        return out;
    };
}
FieldValue SlotValues(const std::string& control) {
    return [control](const Param& p) -> std::string {
        if (p.name == "slot") return ChosenSlot(control);
        const JValue* slot = SlotOf(ChosenSlot(control));
        if (!slot) return p.name == "reward" || p.name == "lens" ? "none" : "";
        if (p.name == "reward") return (*slot)["rewardId"].IsNumber() ? IdOf((*slot)["rewardId"]) : "none";
        if (p.name == "lens") return (*slot)["lens"].IsNumber() ? IdOf((*slot)["lens"]) : "none";
        if (p.name == "value") return (*slot)["charges"].IsNumber() ? Num((*slot)["charges"].number) : "";
        return "";
    };
}
void MakeCofProphecies(Section& s) {
    s.reads.insert("cofread");
    s.build = [](Section& sec, const Ctx& c) {
        const int id = sec.id;
        // a new slot: its reward, lens and charges in the fields
        for (const char* control : {"cofprophecy", "cof_charges"}) {
            Form& f = FormOf(control);
            if (f.changed == "slot") {
                f.values.erase("reward");
                f.values.erase("lens");
                f.values.erase("value");
                f.changed.clear();
            }
        }
        return InPanel(c, [id, &sec](const Ctx& in, float y) {
            y += H2(in.parent, kPanelPad, y, kPanelInner, Tr("Prophecies and lenses")) + 14;
            y += Muted(in.parent, kPanelPad, y, kPanelInner,
                       Tr("Slots unlock at ranks 1 / 3 / 6 / 9. Choose \xE2\x80\x9Cnone\xE2\x80\x9D to clear a reward or lens. Changing a reward resets its charges.")) + 10;
            const JValue* live = Cached("cofread");
            if (live) {
                const std::vector<std::string> head{Tr("Slot"), Tr("Reward"), Tr("Lens"), Tr("Charges"), Tr("Status")};
                std::vector<std::vector<std::string>> rows;
                for (const auto& sl : (*live)["slots"].items) {
                    std::string reward = Tr("None"), lens = Tr("None");
                    for (const auto& r : (*live)["rewards"].items) if (IdOf(r["id"]) == IdOf(sl["rewardId"]) && !IdOf(sl["rewardId"]).empty()) reward = NameOf(r);
                    for (const auto& l : (*live)["lenses"].items) if (IdOf(l["id"]) == IdOf(sl["lens"]) && !IdOf(sl["lens"]).empty()) lens = NameOf(l);
                    rows.push_back({Num(sl["index"].Num(0) + 1), reward, lens, Num(sl["charges"].Num(0)),
                                    Tr(sl["locked"].IsBool() && sl["locked"].boolean ? "Locked" : "Unlocked")});
                }
                auto table = MakeLiveTable(in.parent, kPanelPad, y, kPanelInner, head, {});
                SetColumns(*table, AutoColumns(kPanelInner, head, rows, 12, 12, 180));
                y += UpdateLiveTable(*table, rows);
                sec.live = table;
            }
            // the prophecy card: slot, reward, lens; Preview changes in its .actions
            const CardMaker prophecy = SaveForm("cofprophecy", ProphecyChoices(), SlotValues("cofprophecy"), id,
                                                [](const std::string& line, const JValue&) { return Fmt("Prophecy slot {0} updated.", {Num(std::atof(WordAt(line, 2).c_str()) + 1)}); },
                                                {},
                                                [](const Ctx& fc, float fx, float fy, float) {
                                                    // .actions (margin-top 18): Preview changes
                                                    const Button b = AddButton(fc, fx, fy + 18, Tr("Preview changes"), Look::Normal, [] {
                                                        const Control& preview = Ctl("cofpreview");
                                                        Form& from = FormOf("cofprophecy");
                                                        std::string line, why;
                                                        if (!FormCommand(preview, &line, &why, from.values)) { Toast(why, true); return; }
                                                        ActionRun run;
                                                        run.command = line;
                                                        run.snapshot = "cofread";
                                                        const auto asked = from.values;  // the dialog shows what was asked
                                                        run.done = [asked](const JValue& v) {
                                                            // the result dialog: Slot, Reward, Lens[, Charges now → after]
                                                            const JValue* r = Cached("cofread");
                                                            const auto name = [r, &asked](const char* list, const char* key) {
                                                                auto it = asked.find(key);
                                                                if (r && it != asked.end())
                                                                    for (const auto& x : (*r)[list].items) if (IdOf(x["id"]) == it->second) return NameOf(x);
                                                                return Tr("None");
                                                            };
                                                            const auto slot = asked.find("slot");
                                                            std::vector<std::pair<std::string, std::string>> rows{
                                                                {Tr("Slot"), Num(std::atof(slot == asked.end() ? "0" : slot->second.c_str()) + 1)},
                                                                {Tr("Reward"), name("rewards", "reward")}, {Tr("Lens"), name("lenses", "lens")}};
                                                            if (v["chargesAfter"].IsNumber())
                                                                rows.push_back({Tr("Charges"), Num(v["currentCharges"].Num(0)) + " \xE2\x86\x92 " + Num(v["chargesAfter"].number)});
                                                            ShowResult(Tr("Prophecy preview"), Tr("Preview only. No changes have been applied."), rows);
                                                        };
                                                        RunAction(run);
                                                    }, false, "cofpreview");
                                                    return 18 + b.h;
                                                });
            const CardMaker charges = SaveForm("cof_charges", ProphecyChoices(), SlotValues("cof_charges"), id, [](const std::string& line, const JValue&) {
                return Fmt("Prophecy slot {0} charges set to {1}.", {Num(std::atof(WordAt(line, 2).c_str()) + 1), LastWord(line)});
            });
            y += CardGrid(in, kPanelPad, y, kPanelInner, {prophecy, charges});
            // <details>: Lens catalog · 12 lens
            const bool open = sec.state["lenses"] == "open";
            y += 15;
            y += Details(in, kPanelPad, y, Fmt("Lens catalog \xC2\xB7 {0} lens", {Num(D()["counts"]["lenses"].Num(12))}), open, [id] {
                if (Section* s2 = FindSection(id)) { s2->state["lenses"] = s2->state["lenses"] == "open" ? "" : "open"; Rebuild(id); }
            });
            if (open) {
                const JValue& lenses = live ? (*live)["lenses"] : D()["lensCatalog"];
                Style ps{Face::Body, 14, C("ink")};
                ps.line = 1.65f;
                ps.wrap = true;
                for (const auto& l : lenses.items) {
                    void* t = TextBox(in.parent, "Lens", kPanelPad, y + 10, kPanelInner, LineHeight(ps), ps, "");
                    bool on = true;
                    void* ra[]{&on};
                    managed::Invoke(T("set_richText", 1), t, ra);
                    const std::string state = live ? (l["purchased"].IsBool() && l["purchased"].boolean ? Tr("Purchased") : Tr("Not purchased")) : Tr("Static catalog");
                    SetText(t, "<b><noparse>" + NameOf(l) + "</noparse></b> \xC2\xB7 " + Fmt("Rank {0}", {Num(l["rankRequired"].Num(0))}) + " \xC2\xB7 " + state +
                                   "\n<noparse>" + NoTags(l["uiEffect"].Str(l["effect"].Str())) + "</noparse>");
                    y += 10 + TextLines(t) * LineHeight(ps) + 10;
                }
            }
            return y;
        });
    };
}
}  // namespace

void RegisterCofSections() {
    RegisterSection("cof_main", MakeCofMain);
    RegisterSection("cof_prophecies", MakeCofProphecies);
}

}  // namespace ep::ingame::view
