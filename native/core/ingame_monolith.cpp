// Monolith management (app.js renderMonolith with tools/monolith_backend.py): the target timeline
// and difficulty, Unlock, Select (travelling to Traveler's Rest first and waiting for the timeline
// panel, as the app's backend does), Corruption and Stability. Every action changes the game save:
// it confirms first and runs for the character it was read for.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include "ingame_browser_rules.hpp"
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
struct EchoView {
    std::string query, reward = "all", status = "all";
    JValue data;
    int head = -1, results = -1, selected = -1;
    bool busy = false;
    uint64_t revision = 0, request = 0;
} echoes;
void RebuildEchoes() { if (echoes.head >= 0) Rebuild(echoes.head); if (echoes.results >= 0) Rebuild(echoes.results); }
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
                ++echoes.request; echoes.busy = false; RebuildEchoes();
                ForgetForm("corruption");
                ForgetForm("stability");
                Rebuild(id);
            }, "monolith-timeline");
            const float b = SelectField(in, kPanelPad + colW + 14, y, colW, Tr("Difficulty"), {Tr("Normal"), Tr("Empowered")}, target.difficulty == "empowered" ? 1 : 0,
                                        [id](int i) {
                                            target.difficulty = i == 1 ? "empowered" : "normal";
                                            ++echoes.request; echoes.busy = false; RebuildEchoes();
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
bool CurrentEchoes() {
    const JValue* live = Cached("monolithread");
    return live && model::SameOwner(echoes.data, *live) && model::Id(echoes.data["timeline"]) == target.timeline &&
        echoes.data["difficulty"].Num(-1) == (target.difficulty == "empowered" ? 1 : 0);
}
void ReadEchoes() {
    if (echoes.busy || !Eligible()) return;
    Resolve();
    if (target.timeline.empty()) return;
    echoes.busy = true; const uint64_t token = ++echoes.request;
    const auto selected = target;
    RebuildEchoes();
    Send("identityread", [token, selected](const std::string& identity) {
        if (token != echoes.request) return;
        const JValue who = Parse(identity);
        const JValue* snapshot = Cached("monolithread");
        const std::string id = IdOf(who.Path("player.id"));
        if (!model::On(who["offline"]) || id.empty() || !snapshot || !model::SameOwner(who, *snapshot)) {
            echoes.busy = false; echoes.data = JValue{}; RebuildEchoes();
            Toast(Tr("The loaded character changed. Refresh live data."), true); return;
        }
        Send("echoread " + id + " " + selected.timeline + " " + selected.difficulty, [token, id](const std::string& reply) {
            if (token != echoes.request) return;
            echoes.busy = false;
            std::string why;
            const JValue r = Parse(reply);
            if (Refused(reply, &why) || IdOf(r.Path("player.id")) != id) {
                echoes.data = JValue{};
                Toast(why.empty() ? Tr("The loaded character changed. Refresh live data.") : PlayerError(why), true);
            } else { echoes.data = r; echoes.selected = -1; }
            RebuildEchoes();
        });
    });
}
void FocusEcho(int index) {
    if (!CurrentEchoes() || !MonolithReason().empty()) return;
    const Target selected = target;
    const std::string owner = IdOf(echoes.data.Path("player.id"));
    ForCharacter([selected, owner, index](const std::string& id) {
        if (id != owner) { Toast(Tr("The loaded character changed. Refresh live data."), true); return; }
        const std::string where = id + " " + selected.timeline + " " + selected.difficulty;
        const Fail fail = [](const std::string& why) { Toast(why, true); Read("monolithread"); };
        const Action focus = [where, index, fail] {
            Send("echofocus " + where + " " + std::to_string(index), [index, fail](const std::string& reply) {
                const JValue r = Parse(reply); std::string why;
                if (Refused(reply, &why) || !model::On(r["selectionVerified"]) || r["focused"].Num(-1) != index) {
                    fail(why.empty() ? Tr("The game did not confirm the selected Echo.") : PlayerError(why)); return;
                }
                echoes.selected = index; RebuildEchoes();
                Toast(Tr("Echo focused in the game map. It has not been started."));
            });
        };
        EnsureRest(id, [id, where, focus, fail] {
            Send("monolithpanelready " + where, [id, where, focus, fail](const std::string& reply) {
                const JValue r = Parse(reply);
                if (model::On(r["panelReady"])) { focus(); return; }
                Send("monolithselect " + where, [id, where, focus, fail](const std::string& opened) {
                    std::string why; if (Refused(opened, &why)) { fail(PlayerError(why)); return; }
                    WaitPanel(id, where, focus, fail);
                });
            });
        }, fail);
    });
}
void MakeEchoHead(Section& s) {
    echoes.head = s.id; s.reads.insert("monolithread");
    s.build = [](Section& section, const Ctx& outer) {
        auto panel = std::make_shared<Panel>(OpenPanel(outer.parent, kPadX, 0, kPanelW)); section.frame = panel; section.after = 0;
        const Ctx c = outer.At(panel->rt.Get(), kPadX, 0); Resolve();
        float y = kPanelPad;
        Style eyebrow{Face::Body, 10, C("accent")}; eyebrow.spacing = 2;
        TextBox(c.parent, "Eyebrow", kPanelPad, y, kPanelInner, 13, eyebrow, Tr("REWARD FINDER")); y += 20;
        auto read = AddButton(c, kPanelPad + kPanelInner, y, Tr(echoes.busy ? "Reading…" : "↻ Read Echo web"), Look::Normal, ReadEchoes, true, "echo_read");
        SetEnabled(c, read, !echoes.busy && Eligible() && Cached("monolithread"));
        y += H2(c.parent, kPanelPad, y, kPanelInner - read.w - 15, Tr("Monolith Navigator")) + 14;
        y += Muted(c.parent, kPanelPad, y, kPanelInner, Tr("Search the selected timeline’s existing web. “Show in game” focuses the normal map; it never starts an Echo.")) + 20;
        if (!CurrentEchoes() || !model::On(echoes.data["generated"])) return y;
        const int id = section.id;
        const float searchW = kPanelInner - 160 - 180 - 28;
        InputField(c, kPanelPad, y, searchW, Tr("Search rewards or Echo names"), echoes.query, Tr("Example: Unique, Exalted, Arena…"), false, [](const std::string& text) {
            echoes.query = text; const uint64_t rev = ++echoes.revision;
            After(180, [rev] { if (rev == echoes.revision && echoes.results >= 0) Rebuild(echoes.results); });
        }, "echo-search");
        std::set<int> types;
        for (const auto& e : echoes.data["echoes"].items) types.insert(static_cast<int>(e["rewardType"].Num()));
        std::vector<std::string> rewards{"all"}, names{Tr("Any reward")}; int at = 0;
        for (int type : types) { if (std::to_string(type) == echoes.reward) at = static_cast<int>(names.size()); rewards.push_back(std::to_string(type)); names.push_back(Tr(model::Reward(type))); }
        SelectField(c, kPanelPad + searchW + 14, y, 160, Tr("Reward"), names, at, [rewards, id](int i) {
            echoes.reward = rewards[i]; Rebuild(id); Rebuild(echoes.results);
        }, "echo-reward");
        const std::vector<std::string> states{"all", "ready", "pending", "completed"};
        const auto where = std::find(states.begin(), states.end(), echoes.status);
        const float height = SelectField(c, kPanelPad + searchW + 188, y, 180, Tr("State"),
            {Tr("All existing Echoes"), Tr("Runnable"), Tr("Uncompleted"), Tr("Completed")}, static_cast<int>(where - states.begin()), [states, id](int i) {
                echoes.status = states[i]; Rebuild(id); Rebuild(echoes.results);
            }, "echo-status");
        return y + height + 20;
    };
}
void MakeEchoResults(Section& s) {
    echoes.results = s.id; s.reads.insert("monolithread");
    s.build = [](Section& section, const Ctx& outer) {
        const Node body = New("Echo results", outer.parent, kPadX, 0, kPanelW, 2000); const Ctx c = outer.At(body.rt, kPadX, 0);
        if (!CurrentEchoes()) return kPanelPad + EmptyText(c.parent, kPanelPad, 0, kPanelInner, Tr("Read the Echo web to search rewards. Read requests do not generate new nodes."));
        if (!model::On(echoes.data["generated"])) return kPanelPad + EmptyText(c.parent, kPanelPad, 0, kPanelInner, Tr("No web exists for this timeline yet. Open it normally in game, then read again."));
        std::vector<JValue> matches; std::set<int> ids;
        for (const auto& e : echoes.data["echoes"].items) if (model::EchoMatch(e, Trim(echoes.query), echoes.reward, echoes.status)) {
            matches.push_back(e); ids.insert(static_cast<int>(e["index"].Num()));
        }
        float y = Muted(c.parent, kPanelPad, 0, kPanelInner, Fmt("{0} matches / {1} existing Echoes", {std::to_string(matches.size()), std::to_string(echoes.data["echoes"].items.size())}), 11) + 14;
        const auto& all = echoes.data["echoes"].items;
        if (!all.empty()) {
            const Node map = New("Existing Echo web", c.parent, kPanelPad, y, kPanelInner, 340); Clip(map);
            Fill(map.rt, "Map background", 0, 0, kPanelInner, 340, Hex("#00000044"), 5);
            Picture(map.rt, "Map glow", 0, 0, kPanelInner, 340, "grad-stat-value.png", false, kWhite);
            float left = 0, right = 0, bottom = 0, top = 0; bool first = true; std::map<int, const JValue*> nodes;
            for (const auto& e : all) {
                const float x = static_cast<float>(e["x"].Num()), yy = static_cast<float>(-e["y"].Num());
                if (first) { left = right = x; top = bottom = yy; first = false; }
                left = std::min(left, x); right = std::max(right, x); top = std::min(top, yy); bottom = std::max(bottom, yy);
                nodes[static_cast<int>(e["index"].Num())] = &e;
            }
            const float width = std::max(100.f, right - left + 70), height = std::max(100.f, bottom - top + 70);
            const float scale = std::min(kPanelInner / width, 340 / height), dx = (kPanelInner - width * scale) / 2, dy = (340 - height * scale) / 2;
            const auto position = [=](const JValue& e) { return V2{dx + (static_cast<float>(e["x"].Num()) - left + 35) * scale, dy + (static_cast<float>(-e["y"].Num()) - top + 35) * scale}; };
            std::set<std::pair<int, int>> edges;
            for (const auto& e : all) for (const auto& link : e["connections"].items) {
                const int a = static_cast<int>(e["index"].Num()), b = static_cast<int>(link.Num());
                if (!nodes.count(b) || !edges.insert({std::min(a,b), std::max(a,b)}).second) continue;
                const auto p = position(e), q = position(*nodes[b]); const float vx = q.x - p.x, vy = q.y - p.y;
                void* line = Solid(map.rt, "Echo connection", p.x, p.y, std::hypot(vx, vy), 1, C("line")); Turn(TransformOf(line), -std::atan2(vy, vx) * 180.f / 3.14159265f);
            }
            for (const auto& e : all) {
                const int index = static_cast<int>(e["index"].Num()); const V2 p = position(e);
                const float size = (index == echoes.selected ? 22.f : 14.f) * scale;
                Picture(map.rt, "Echo node", p.x-size/2, p.y-size/2, size, size, "circle-17.png", false, ids.count(index) ? C("accent") : Alpha(C("muted"), .25f));
                if (index == echoes.selected) Picture(map.rt, "Selected Echo", p.x-size/2-2, p.y-size/2-2, size+4, size+4, "ring-30.png", false, C("gold"));
            }
            Outline(c.parent, "Map border", kPanelPad, y, kPanelInner, 340, C("line"), 5); y += 362;
        }
        if (matches.empty()) return y + EmptyText(c.parent, kPanelPad, y, kPanelInner, Tr("No reward matches these filters.")) + kPanelPad;
        std::vector<CardMaker> cards; const int sectionId = section.id;
        for (size_t i = 0; i < std::min(size_t(200), matches.size()); ++i) {
            const JValue e = matches[i];
            cards.push_back([e, sectionId](const Ctx& cc, float x, float yy, float w) {
                const int index = static_cast<int>(e["index"].Num());
                CardBox card = OpenBrowserCard(cc.parent, x, yy, w, index == echoes.selected); const Ctx cell = cc.At(card.node.Get(), x, yy);
                float cy = 19, badgeH = 0;
                Badge(cell.parent, w - 19, cy, Tr(model::On(e["completed"]) ? "Completed" : model::On(e["runnable"]) ? "Runnable" : "Not currently reachable"), &badgeH); cy += badgeH + 16;
                Style title{Face::Serif, 20, C("ink")}; title.line = 1.4f; title.wrap = true;
                cy += Paragraph(cell.parent, "Echo reward", 19, cy, w-38, title, e["reward"].Str().empty() ? Tr(model::Reward(static_cast<int>(e["rewardType"].Num()))) : e["reward"].Str()) + 16;
                cy += Muted(cell.parent, 19, cy, w-38, e["name"].Str() + " · " + Fmt("{0} base stability", {Num(e["stability"].Num())})) + 18;
                auto highlight = AddButton(cell, 19, cy, Tr("Highlight"), Look::Small, [index, sectionId] { echoes.selected = index; Rebuild(sectionId); }, false, "echo-highlight-" + std::to_string(index));
                auto focus = AddButton(cell, 19 + highlight.w + 8, cy, Tr("Show in game →"), Look::Small, [index] { FocusEcho(index); }, false, "echo-focus-" + std::to_string(index));
                if (highlight.w + focus.w + 8 > w - 38) {
                    Move(focus.node.rt, 19, cy + highlight.h + 10);
                    (*cell.hits)[focus.hit].x = cell.ox + 19; (*cell.hits)[focus.hit].y = cell.oy + cy + highlight.h + 10;
                    cy += highlight.h + 10 + focus.h;
                } else cy += std::max(highlight.h, focus.h);
                SetEnabled(cell, focus, MonolithReason().empty()); cy += 19;
                CloseCard(card, w, cy); return card;
            });
        }
        const float top = y;
        const float more = matches.size() > 200 ? Muted(c.parent, kPanelPad, y + 674, kPanelInner, Tr("First 200 matches shown. Narrow your search to see a smaller list.")) + 14 : 0;
        const float grid = ScrollCards(section, c, kPanelPad, y, kPanelInner, 660, std::move(cards), 3, 14,
            [sectionId, top, more](float h) { if (auto* s = FindSection(sectionId)) Resized(*s, top + h + more + kPanelPad); });
        return y + grid + more + kPanelPad;
    };
}
}  // namespace

void RegisterMonolithSections() {
    RegisterSection("monolith_management", MakeMonolithManagement);
    RegisterSection("echo_head", MakeEchoHead);
    RegisterSection("echo_results", MakeEchoResults);
}

}  // namespace ep::ingame::view
