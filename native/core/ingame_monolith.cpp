// Monolith management (app.js renderMonolith with tools/monolith_backend.py): the target timeline
// and difficulty, Unlock, Select (travelling to Traveler's Rest first and waiting for the timeline
// panel, as the app's backend does), Corruption and Stability. Every action changes the game save:
// it confirms first and runs for the character it was read for.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <memory>
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
float InPanel(const Ctx& c, const std::function<float(const Ctx& in, float y)>& draw) {
    Panel p = OpenPanel(c.parent, kPadX, 0, kPanelW);
    const Ctx in = c.At(p.rt.Get(), kPadX, 0);
    const float h = draw(in, kPanelPad) + kPanelPad;
    ClosePanel(p, kPanelW, h);
    return h;
}
std::string IdOf(const JValue* v) { return !v ? "" : v->IsString() ? v->text : v->IsNumber() ? Num(v->number) : ""; }
std::string SaveMessage(const Control& c) { return Tr(c.description) + " " + Tr("This action changes the game save. The backend creates a backup before applying it."); }
// saveActionReason for the Monolith: offline and ready, data read, an area where it can be edited
std::string MonolithReason() {
    std::string reason;
    if (!Eligible(&reason)) return reason;
    const JValue* r = Cached("monolithread");
    if (!r) return Tr("Refresh live data to enable this action for the loaded character.");
    if (!((*r)["editable"].IsBool() && (*r)["editable"].boolean))
        return Tr("Return to End of Time, Monolith Hub or Traveler's Rest to edit Monoliths, then refresh live data.");
    return "";
}

// monoTarget: the timeline and difficulty the cards act on (default: the selected one, else the first)
struct Target { std::string timeline, difficulty = "normal"; } target;
const JValue* Timeline(const std::string& id) {
    const JValue* r = Cached("monolithread");
    if (r) for (const auto& t : (*r)["timelines"].items) if (IdOf(&t["id"]) == id) return &t;
    return nullptr;
}
void Resolve() {
    const JValue* r = Cached("monolithread");
    if (!r) return;
    if (!target.timeline.empty() && Timeline(target.timeline)) return;
    const std::string selected = IdOf(r->Path("selected.timeline"));
    target.timeline = !selected.empty() && Timeline(selected) ? selected : !(*r)["timelines"].items.empty() ? IdOf(&(*r)["timelines"].items[0]["id"]) : "";
}
const JValue* Difficulty() {
    const JValue* t = Timeline(target.timeline);
    if (!t) return nullptr;
    const size_t index = target.difficulty == "empowered" ? 1 : 0;
    return index < (*t)["difficulties"].items.size() ? &(*t)["difficulties"].items[index] : nullptr;
}
std::string TargetName() {
    const JValue* t = Timeline(target.timeline);
    return (t ? (*t)["name"].Str() : target.timeline) + " (" + Tr(target.difficulty == "empowered" ? "Empowered" : "Normal") + ")";
}

// ------------------------------------------------------- travel to Traveler's Rest, then wait
using Fail = std::function<void(const std::string&)>;
JValue Parse(const std::string& reply) {
    JValue v;
    if (!json::Parse(reply, v)) v = JValue{};
    return v;
}
// _ensure_rest: in Traveler's Rest with its context ready, travelling there first if needed
void EnsureRest(const std::string& id, Action ready, Fail fail) {
    Send("monolithread", [id, ready, fail](const std::string& reply) {
        const JValue live = Parse(reply);
        if (IdOf(live.Path("player.id")) != id) { fail(Tr("The loaded character changed. Refresh live data.")); return; }
        if (live.Path("player.scene") && live.Path("player.scene")->Str() == "M_Rest" && live["restContextReady"].IsBool() && live["restContextReady"].boolean) {
            ready();
            return;
        }
        if (!(live["editable"].IsBool() && live["editable"].boolean)) { fail(Tr("Open timelines from End of Time, Monolith Hub or Traveler's Rest.")); return; }
        auto deadline = std::make_shared<uint64_t>(GetTickCount64() + 60000);
        auto poll = std::make_shared<std::function<void()>>();
        *poll = [id, ready, fail, deadline, poll] {
            if (GetTickCount64() > *deadline) { fail(Tr("Traveler's Rest has not finished loading. Resume the game and try again.")); return; }
            Send("identityread", [id, ready, fail, poll](const std::string& reply2) {
                const JValue who = Parse(reply2);
                const std::string player = IdOf(who.Path("player.id"));
                if (!player.empty() && player != id) { fail(Tr("The loaded character changed during travel.")); return; }
                if (!(who["ok"].IsBool() && who["ok"].boolean) || !who.Path("player.scene") || who.Path("player.scene")->Str() != "M_Rest") {
                    After(200, *poll);
                    return;
                }
                Send("monolithread", [id, ready, fail, poll](const std::string& reply3) {
                    const JValue now = Parse(reply3);
                    if (IdOf(now.Path("player.id")) != id) { fail(Tr("The loaded character changed during travel.")); return; }
                    if (now["restContextReady"].IsBool() && now["restContextReady"].boolean) ready();
                    else After(200, *poll);
                });
            });
        };
        if (live.Path("player.scene") && live.Path("player.scene")->Str() == "M_Rest") { (*poll)(); return; }
        Send("monolithrest " + id, [fail, poll](const std::string& reply2) {
            if (reply2.find("normal waypoint transition requested for M_Rest") == std::string::npos) {
                fail(reply2.empty() ? Tr("Traveler's Rest did not respond.") : PlayerError(reply2));
                return;
            }
            (*poll)();
        });
    });
}
// _wait_panel: the timeline panel reports ready twice in a row (never repeating the selection)
void WaitPanel(const std::string& id, const std::string& where, Action done, Fail fail) {
    auto deadline = std::make_shared<uint64_t>(GetTickCount64() + 60000);
    auto stable = std::make_shared<int>(0);
    auto poll = std::make_shared<std::function<void()>>();
    *poll = [id, where, done, fail, deadline, stable, poll] {
        if (GetTickCount64() > *deadline) { fail(Tr("The timeline is still loading. The opening request was not repeated.")); return; }
        Send("monolithpanelready " + where, [id, done, fail, stable, poll](const std::string& reply) {
            const JValue r = Parse(reply);
            const std::string player = IdOf(r.Path("player.id"));
            if (!player.empty() && player != id) { fail(Tr("The loaded character changed while opening the timeline.")); return; }
            *stable = r["ok"].IsBool() && r["ok"].boolean && r["panelReady"].IsBool() && r["panelReady"].boolean ? *stable + 1 : 0;
            if (*stable >= 2) done();
            else After(200, *poll);
        });
    };
    (*poll)();
}
// the loaded character's id, when it is the one the Monolith data was read for
void ForCharacter(std::function<void(const std::string& id)> go) {
    Send("identityread", [go](const std::string& reply) {
        const JValue who = Parse(reply);
        if (!(who["offline"].IsBool() && who["offline"].boolean)) { Toast(Tr("Load an offline character and wait for the area to finish loading."), true); return; }
        const std::string live = IdOf(who.Path("player.id"));
        const JValue* snap = Cached("monolithread");
        if (live.empty() || !snap || IdOf(snap->Path("player.id")) != live) {
            Toast(Tr("The loaded character has changed. Refresh this section and try again."), true);
            return;
        }
        go(live);
    });
}

// ------------------------------------------------------------------------- Monolith management
void MakeMonolithManagement(Section& s) {
    s.reads.insert("monolithread");
    s.build = [](Section& sec, const Ctx& c) {
        const int id = sec.id;
        Resolve();
        return InPanel(c, [id](const Ctx& in, float y) {
            y += SectionHead(in, kPanelPad, y, kPanelInner, Tr("Monolith management"), {{Tr("Refresh live"), [] { Read("monolithread"); }}}) + 16;
            y += Muted(in.parent, kPanelPad, y, kPanelInner,
                       Tr("Opening a timeline or showing an Echo travels to Traveler's Rest first. Refreshing data does not travel or start an Echo.")) + 10;
            // .form-grid: Timeline, Difficulty
            const JValue* r = Cached("monolithread");
            std::vector<std::string> ids, names;
            if (r) for (const auto& t : (*r)["timelines"].items) { ids.push_back(IdOf(&t["id"])); names.push_back(t["name"].Str()); }
            int at = 0;
            for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == target.timeline) at = static_cast<int>(i);
            const float colW = (kPanelInner - 14) / 2;
            y += 10;  // p margin-bottom before the grid (the grid has none)
            const float a = SelectField(in, kPanelPad, y, colW, Tr("Timeline"), names, at, [ids, id](int i) {
                target.timeline = ids[i];
                ForgetForm("corruption");
                ForgetForm("stability");
                Rebuild(id);
            }, "monolith-timeline");
            const float b = SelectField(in, kPanelPad + colW + 14, y, colW, Tr("Difficulty"), {Tr("Normal"), Tr("Empowered")}, target.difficulty == "empowered" ? 1 : 0,
                                        [id](int i) {
                                            target.difficulty = i == 1 ? "empowered" : "normal";
                                            ForgetForm("corruption");
                                            ForgetForm("stability");
                                            Rebuild(id);
                                        }, "monolith-difficulty");
            y += std::max(a, b);
            const JValue* d = Difficulty();
            const auto num = [](const JValue* v, const char* key) { return v && (*v)[key].IsNumber() ? Num((*v)[key].number) : std::string("?"); };
            const std::string facts = d ? Fmt("Level {0} \xC2\xB7 {1} \xC2\xB7 Corruption {2}\xE2\x80\x93{3} \xC2\xB7 Stability 0\xE2\x80\x93{4}",
                                              {num(d, "level"), (*d)["unlocked"].IsBool() && (*d)["unlocked"].boolean ? "Unlocked" : "Locked", num(d, "minCorruption"),
                                               num(d, "maxCorruption"), num(d, "maxStability")})
                                        : Tr("Refresh the live timeline catalog.");
            y += 10 + Muted(in.parent, kPanelPad, y + 10, kPanelInner, facts) + 10;
            const std::string reason = MonolithReason();
            std::vector<CardMaker> cards;
            // Unlock Normal + Empowered timelines
            cards.push_back([reason](const Ctx& cc, float x, float yy, float w) {
                const Control& ctl = Ctl("monolith_unlock");
                ActionRun run;
                run.command = "monolithunlock {save_id}";
                run.snapshot = "monolithread";
                run.confirmTitle = Tr(ctl.label);
                run.confirmMessage = SaveMessage(ctl);
                run.success = [](const JValue&) { return Tr("Normal and Empowered timelines unlocked."); };
                run.reread = {"monolithread"};
                return ActionCard(cc, x, yy, w, ctl, ActionCaption(ctl), true, [run] { RunAction(run); }, reason);
            });
            // Select timeline / difficulty: Traveler's Rest first, then the selection, then its panel
            cards.push_back([reason](const Ctx& cc, float x, float yy, float w) {
                const Control& ctl = Ctl("monolith_select");
                return ActionCard(cc, x, yy, w, ctl, ActionCaption(ctl), true, [] {
                    const Control& c = Ctl("monolith_select");
                    const JValue* dd = Difficulty();
                    if (!dd || !((*dd)["unlocked"].IsBool() && (*dd)["unlocked"].boolean)) { Toast(Tr("This timeline / difficulty is not unlocked yet."), true); return; }
                    Confirm(Tr(c.label), SaveMessage(c), [] {
                        ForCharacter([](const std::string& id) {
                            const std::string where = id + " " + target.timeline + " " + target.difficulty;
                            const Fail fail = [](const std::string& why) { Toast(why, true); Read("monolithread"); };
                            EnsureRest(id, [id, where, fail] {
                                Send("monolithselect " + where, [id, where, fail](const std::string& reply) {
                                    std::string why;
                                    if (Refused(reply, &why)) { fail(PlayerError(why)); return; }
                                    WaitPanel(id, where, [] {
                                        Toast(Fmt("Timeline selected. {0}.", {TargetName()}));
                                        Forget("monolithread");
                                        Read("monolithread");
                                    }, fail);
                                });
                            }, fail);
                        });
                    });
                }, reason);
            });
            // Corruption and Stability: a value within the target's own limits
            for (const char* kind : {"corruption", "stability"}) {
                const std::string control = kind;
                cards.push_back([control, reason, d](const Ctx& cc, float x, float yy, float w) {
                    const Control& ctl = Ctl(control);
                    const bool corruption = control == "corruption";
                    const auto num = [](const JValue* v, const char* key) { return v && (*v)[key].IsNumber() ? Num((*v)[key].number) : std::string("?"); };
                    const std::string low = corruption ? num(d, "minCorruption") : "0", high = num(d, corruption ? "maxCorruption" : "maxStability");
                    const Fields fields = [control, corruption, low, high, d](const Ctx& fc, float fx, float fy, float fw) {
                        Form& form = FormOf(control);
                        if (!form.values.count("value")) {
                            const JValue* run = d ? d->Find("run") : nullptr;
                            form.values["value"] = run && run->IsObject() && (*run)[corruption ? "corruption" : "stability"].IsNumber()
                                                       ? Num((*run)[corruption ? "corruption" : "stability"].number) : "";
                        }
                        return InputField(fc, fx, fy, (fw - 14) / 2, Tr(corruption ? "Corruption" : "Stability") + " \xC2\xB7 " + low + "\xE2\x80\x93" + high,
                                          form.values["value"], "", true, [control](const std::string& text) { FormOf(control).values["value"] = text; },
                                          control + "-value");
                    };
                    return ActionCard(cc, x, yy, w, ctl, ActionCaption(ctl), true, [control, corruption, low, high] {
                        const Control& c = Ctl(control);
                        const std::string text = Trim(FormOf(control).values["value"]);
                        char* end = nullptr;
                        const double v = std::strtod(text.c_str(), &end);
                        if (text.empty() || !end || *end || std::floor(v) != v || v < std::atof(low.c_str()) || (high != "?" && v > std::atof(high.c_str()))) {
                            Toast(Fmt("{0}: please enter a value from {1} to {2}.", {Tr(corruption ? "Corruption" : "Stability"), low, high}), true);
                            return;
                        }
                        const JValue* dd = Difficulty();
                        if (!dd || !((*dd)["unlocked"].IsBool() && (*dd)["unlocked"].boolean)) { Toast(Tr("This timeline / difficulty is not unlocked yet."), true); return; }
                        ActionRun run;
                        run.command = control + " {save_id} " + target.timeline + " " + target.difficulty + " " + Num(v);
                        run.snapshot = "monolithread";
                        run.confirmTitle = Tr(c.label);
                        run.confirmMessage = SaveMessage(c);
                        const std::string value = Num(v);
                        run.success = [corruption, value](const JValue&) {
                            return Fmt(corruption ? "Corruption set to {0}. {1}." : "Stability set to {0}. {1}.", {value, TargetName()});
                        };
                        run.reread = {"monolithread"};
                        run.done = [control](const JValue&) { ForgetForm(control); };
                        RunAction(run);
                    }, reason, fields);
                });
            }
            return y + CardGrid(in, kPanelPad, y, kPanelInner, cards);
        });
    };
}
}  // namespace

void RegisterMonolithSections() { RegisterSection("monolith_management", MakeMonolithManagement); }

}  // namespace ep::ingame::view
