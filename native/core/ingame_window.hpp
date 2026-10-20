// The in-game window's page model and building kit, shared by ingame_panel.cpp (the window),
// ingame_kit.cpp (widgets) and ingame_sections*.cpp (the pages' native sections).
//
// A page is a column of sections below the hero. Each section has a root node in the page, its
// own height and its own clickable places (hits) in section coordinates, so a section can be built
// again on its own (new data) and the page moves the sections below it (Relayout). Widgets draw like
// ui/style.css and report what can be clicked through a Ctx. Main thread only.
#pragma once
#include "ingame_ui.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ep::ingame::view {
using namespace ui;
using Action = std::function<void()>;
using Reply = std::function<void(const std::string&)>;

// ---------------------------------------------------------------------------------- hits
enum class HitKind { Nav, Link, Minus, Plus, Track, Default, Toggle, Choice, Refresh, Apply, Discard, Support, Action, Input };
enum class Area { Fixed, Sidebar, Main, Popup, Dialog };
struct HoverPart { void* graphic; Color normal, hover; float seconds; };
struct Hit {
    HitKind kind = HitKind::Action;
    int a = -1, b = -1;                  // a card index (cards), a page (Nav, Link)
    Area area = Area::Fixed;
    float x = 0, y = 0, w = 0, h = 0;    // window coords (Fixed, Popup, Dialog), sidebar or section coords
    std::vector<HoverPart> hover;
    bool enabled = true, visible = true;
    Action action;                       // HitKind::Action, Input
    std::function<void(float x)> drag;   // HitKind::Track of a native section: pressed and dragged, x from the hit's left edge
    Action release;                      // ... and let go (input type=range: its change event)
    std::string name;                    // for tests ("panel read"): a control id or a short label
};
void AddHover(Hit& h, void* graphic, Color normal, Color hover, float seconds = 0.15f);

// ------------------------------------------------------------------------------- sections
struct Section;
struct Panel;
struct PanelState;  // a setting panel kept for changes in place: its facts note and tail (ingame_panel.cpp)
// Where a widget is drawn: a parent node and that node's place in its hit list's coordinates.
struct Ctx {
    std::vector<Hit>* hits = nullptr;
    Area area = Area::Main;
    void* parent = nullptr;
    float ox = 0, oy = 0;
    int section = -1;                    // the section the hits belong to (-1: the window's own)
    Ctx At(void* child, float x, float y) const { return {hits, area, child, ox + x, oy + y, section}; }
    // Adds a hit given in this ctx's coordinates; returns its index in the list.
    int Add(Hit h) const;
};
struct Section {
    int id = -1, page = -1;
    std::string kind;                    // "settings", "summary", "links", "pending" or a native kind
    const json::Value* spec = nullptr;   // its entry in ui.json
    Pin root, body;                      // root: placed by Relayout; body: what Build made
    float y = 0, h = 0, after = 20;      // after: the CSS margin below (a panel 20, a summary 22, a note 18)
    std::vector<Hit> hits;
    std::set<std::string> reads;         // reads to send when its page is shown (and on Refresh)
    std::set<std::string> polls;         // reads repeated every second while its page is shown
    // Native sections: build the content into ctx (x 0 = the content's left edge, kPadX the
    // panel's) and return its height. Called again by Rebuild.
    std::function<float(Section&, const Ctx&)> build;
    // A read this section listed arrived (it is in Cached). Default: Rebuild.
    std::function<void(Section&, const std::string& read)> onData;
    std::map<std::string, std::string> state;  // small per-section UI state (selected tab, page...)
    std::map<std::string, uint64_t> every;     // a poll's own interval in ms (default 1000)
    std::shared_ptr<void> live;                // widgets the section updates in place (tables, lists)
    std::function<void(Section&)> tick;        // every frame while its page is shown
    // Places where the wheel scrolls a list instead of the page (section coords).
    struct Wheel { float x = 0, y = 0, w = 0, h = 0; std::function<void(float notches)> scroll; };
    std::vector<Wheel> wheels;
    std::shared_ptr<PanelState> panelState;    // settings: the panel, its facts note and its tail
    // Settings a native section draws itself (the glyph chances): onSetting runs when one of them
    // changes (a value read, a draft, Applying…, Applied).
    std::set<std::string> settings;
    std::function<void(Section&, const std::string& id)> onSetting;
    std::function<void(Section&)> onTab;       // its page shows another tab (ShowTab)
    bool hidden = false;                       // on another tab of its page (ui.json "tab")
    // A panel group (ui.json "group"): consecutive sections drawn in one panel, so each part is
    // built again on its own; the group's first section makes the panel, Relayout sizes it.
    std::shared_ptr<Panel> frame;
    bool built = false;
    Ctx Body(void* parent) { return {&hits, Area::Main, parent, 0, 0, id}; }
};
// The section's height changed in place (a live table grew): move the sections below it.
void Resized(Section& s, float h);
// Makes a native section from its ui.json entry: sets build, reads, onData, after.
using SectionMaker = std::function<void(Section&)>;
void RegisterSection(const std::string& kind, SectionMaker make);
void RegisterNativeSections();           // ingame_sections.cpp
void RegisterSummonSections();           // ingame_summons.cpp
void RegisterDisplaySections();          // ingame_display.cpp
void RegisterInventorySections();        // ingame_inventory.cpp
void RegisterCofSections();              // ingame_cof.cpp
void RegisterMonolithSections();         // ingame_monolith.cpp
void RegisterCraftSections();            // ingame_craft.cpp
void RegisterWorkshopSections();         // ingame_workshop.cpp
void RegisterStatSections();             // ingame_stats.cpp
void ReleaseStatRoots();                 // before IL2CPP shutdown, not static DLL destruction
std::string WorkshopState();             // for "panel read": the Custom Items draft
std::string SectionsState();             // for "panel read": the pickers' choices, the pickups kept
Section* FindSection(int id);
void Rebuild(int sectionId);             // build its body again and move the sections below
void Relayout(int page);
// A setting panel's tail: a native part inside the panel, after its cards (ui.json "tail"). build
// draws at (0, 0) of ctx (x 0 = the panel's left edge) and returns its height; onData gets the
// tail's reads (default: RebuildTail). RebuildTail builds it again and resizes the panel.
struct HeadButton { std::string label; Action action; std::string name; bool primary = false; };
struct Tail {
    std::function<float(Section&, const Ctx&)> build;
    std::set<std::string> reads;
    std::function<void(Section&, const std::string& read)> onData;
    // ui.json head "tail": the panel's heading has these buttons (the Death recap's Refresh, Clear)
    // in place of the badge
    std::function<std::vector<HeadButton>(Section&)> head;
};
void RegisterTail(const std::string& kind, Tail tail);
void RebuildTail(int sectionId);

// ------------------------------------------------------------------------------- the data
// The catalog's controls, as the desktop app shows them.
struct Param {
    std::string name, type;
    double min = 0, max = 0;
    bool hasMin = false, hasMax = false;
    std::vector<std::string> choices, choiceLabels;
};
struct Control {
    std::string id, widget, label, description, lifetime, command, unit, icon, read, path, defaultText;
    double min = 0, max = 1, def = 0;
    bool integer = false, hasDefault = false;
    std::vector<double> choices;
    std::vector<std::string> choiceLabels;
    std::vector<Param> params;
};
const Control* ControlById(const std::string& id);
// A page with tabs (Crafting): the shown tab, kept while the game runs; ShowTab shows the sections
// of another (ui.json "tab") and moves the page.
std::string PageTab(int page);
void ShowTab(int page, const std::string& tab);
// The setting values the window keeps (app.js values) for a native section's own cards: false when
// not known yet; NaN is null (the game's default; Apply sends "reset").
bool SettingValue(const std::string& id, double* v);
bool Typing(int sectionId);                      // one of the section's text fields has the keyboard
bool& Disclosure(const std::string& key);        // a <details> open (app.js craftDisclosure), while the game runs
void SetDraft(const std::string& id, double v);  // a draft for Apply changes
std::string SettingFeedback(const std::string& id, Color* color, float* size);  // settingFeedback: Draft, Applying…, Applied
int DraftCount(const std::string& prefix);       // drafts whose id starts so
void ClearDrafts(const std::string& prefix);     // back to the applied values (Restore normal crafting)
// The desktop app's stored choice for a key (preferences.json, read when the window opens); null if none.
json::Value Preference(const std::string& key);
// Stores a choice there as the app does (the JSON text; nullptr removes it): kept for the app and the next game.
bool StorePreference(const std::string& key, const std::string* value);
std::string LifetimeLabel(const std::string& lifetime);

// Commands: one at a time through commands::SubmitUi, replies handled on the main thread.
void Send(std::string line, Reply done);
bool Refused(const std::string& reply, std::string* why);  // the app's text_ok, or ok:false
// Reads: a read command whose JSON reply is kept; sections listing it get onData.
void Read(const std::string& read);
const json::Value* Cached(const std::string& read);
// A reply of a read sent by hand (Send), kept as if Read had asked for it.
void Keep(const std::string& read, const std::string& reply);
// Work for later (a wait between two reads); runs on a later step, also while the window is closed.
void After(uint64_t ms, Action work);
void Forget(const std::string& read);
// The offline character is loaded and its area is ready (eligible in app.js).
bool Eligible(std::string* reason = nullptr);
std::string PlayerError(const std::string& error);  // playerError: technical failures in plain words

// A game-save or view action as app.js formAction runs it: optional confirm, the loaded character
// must still be the one `snapshot` was read for, {save_id} filled in, the reply checked, a
// notification, then the snapshot read again.
struct ActionRun {
    std::string command;                 // template; {save_id} is replaced by the live id
    std::string snapshot;                // read whose player.id must match ("" = no check)
    std::string confirmTitle, confirmMessage;
    std::function<std::string(const json::Value& reply)> success;  // the notification text
    std::vector<std::string> reread;     // reads to send afterwards
    std::function<void(const json::Value& reply)> done;
    std::string expectId;                // else: the player id the section's data was read for
};
void RunAction(ActionRun run);

// An action card's form (app.js parameterFields / formAction): the fields of a control's command
// parameters, their values kept per control while the window runs.
struct Choice { std::string value, label; };
struct Form { std::map<std::string, std::string> values; std::string changed; };
Form& FormOf(const std::string& control);
void ForgetForm(const std::string& control);  // after an apply: the fields show the live values again
using FieldChoices = std::function<std::vector<Choice>(const Param&)>;  // a select's options; none: an input
using FieldValue = std::function<std::string(const Param&)>;            // a field's value before it is changed
// Draws the fields in a 2-column form grid; a changed select builds rebuildSection again (its
// dependent fields follow). Returns the grid's height.
float ParameterFields(const Ctx& c, float x, float y, float w, const Control& ctl, const FieldChoices& choices, const FieldValue& initial,
                      int rebuildSection = -1, const std::vector<std::string>& skip = {});
// The command with the form's values (extra: values the card sets itself), or the reason it
// cannot be sent; {save_id} is left for RunAction.
bool FormCommand(const Control& ctl, std::string* line, std::string* error, const std::map<std::string, std::string>& extra = {});

// ------------------------------------------------------------------------------ overlays
void Toast(const std::string& text, bool error = false);
void Confirm(const std::string& title, const std::string& message, Action yes);
// Custom modal inputs have their own watched-input owner, released on every close/rebuild.
inline constexpr int kCustomDialogSection = -4;
void CloseDialog();
using DialogDraw = std::function<float(const Ctx&, float width)>;
void ShowCustomDialog(float width, const DialogDraw& draw);
uint64_t DialogToken();                  // invalidates asynchronous replies when closed/replaced
// #result-dialog: a note and a summary (dt/dd rows), closed with ×.
void ShowResult(const std::string& title, const std::string& note, const std::vector<std::pair<std::string, std::string>>& rows);
// A list below (or above) a window-coords box, like the browser's select popup.
void OpenList(float x, float y, float w, float h, const std::vector<std::string>& labels, int current, std::function<void(int)> chosen);
void CloseList();
// Window coords of a point given in a ctx's coords (for popups).
bool WindowPoint(const Ctx& c, float x, float y, float* wx, float* wy);
// A TMP_InputField the window watches while it has the keyboard: onChange gets each new text.
void WatchInput(const Ctx& c, void* field, const std::string& value, std::function<void(const std::string&)> onChange, const std::string& name);

// -------------------------------------------------------------------------------- widgets
struct Button { Node node; float w = 0, h = 0; int hit = -1; Pin group, label; Color labelColor{}; };
enum class Look { Normal, Quiet, Primary, Small, Inline13 };  // Inline13: a button in 13px text (line-height 1.6)
// Builds a button (style.css button / .quiet / .primary); its hit gets hover parts and size.
Button MakeButton(void* parent, const std::string& label, Look look, Hit& hit);
// A positioned, clickable button in ctx; right-aligned when right is true (x is its right edge).
Button AddButton(const Ctx& c, float x, float y, const std::string& label, Look look, Action onClick, bool right = false, const std::string& name = "");
void SetEnabled(const Ctx& c, Button& b, bool on);   // .45 opacity and no clicks when off
void SetSelected(const Ctx& c, Button& b, bool on);  // .tabs button.selected: accent border, soft fill
float Badge(void* parent, float right, float y, const std::string& text, float* height = nullptr);
struct Panel { Pin rt; Pin parts[4]; Pin corner; };  // corner: the bottom-right one, moved when it closes again
Panel OpenPanel(void* parent, float x, float y, float w);
void ClosePanel(Panel& p, float w, float h);
// Texts (y is the CSS line box top; each returns its height)
float H2(void* parent, float x, float y, float w, const std::string& text);
float H3(void* parent, float x, float y, float w, const std::string& text);
float Muted(void* parent, float x, float y, float w, const std::string& text, float size = 14, float line = 1.65f);
// p.note; parts (optional) keeps it for SetNote, which writes a new text and returns the new height
struct NoteParts { Pin root, bg, bar, text; float w = 0, h = 0; };
float NoteBox(void* parent, float x, float y, float w, const std::string& text, NoteParts* parts = nullptr);
float SetNote(NoteParts& n, const std::string& text);
float EmptyText(void* parent, float x, float y, float w, const std::string& text);   // p.empty
// .section-head: h2 left, head buttons right (centred on the h2 line); returns its height
float SectionHead(const Ctx& c, float x, float y, float w, const std::string& title, const std::vector<std::pair<std::string, Action>>& buttons);
// The summary tiles (.summary-grid): values and labels, n columns
float SummaryTiles(void* parent, float x, float y, float w, const std::vector<std::pair<std::string, std::string>>& tiles, int columns = 4);
// form-grid label + select (a closed select; OpenList on click)
float SelectField(const Ctx& c, float x, float y, float w, const std::string& label, const std::vector<std::string>& options, int current,
                  std::function<void(int)> chosen, const std::string& name = "");
// form-grid label + a number/text input (TMP_InputField); onChange gets the text as typed
float InputField(const Ctx& c, float x, float y, float w, const std::string& label, const std::string& value, const std::string& placeholder,
                 bool number, std::function<void(const std::string&)> onChange, const std::string& name = "");
// input[type=checkbox] (Chrome's dark control): returns its border
void* CheckBox(void* parent, float x, float y, float size, bool checked);
// .inline-check: an 18px checkbox and its label
float CheckField(const Ctx& c, float x, float y, const std::string& label, bool checked, std::function<void(bool)> changed, const std::string& name = "",
                 float size = 12, float gap = 8, float* width = nullptr);
// A plain table: header row (gold) and rows; column widths as fractions of w
float Table(void* parent, float x, float y, float w, const std::vector<std::string>& head, const std::vector<std::vector<std::string>>& rows,
            const std::vector<float>& widths, float size = 12);
// <details><summary>: a gold line (12px, or size) with the disclosure triangle; returns its height.
// marker: the triangle's text, for a summary that turns in place.
float Details(const Ctx& c, float x, float y, const std::string& summary, bool open, Action toggle, float size = 12, void** marker = nullptr);
// A bare input (TMP_InputField) in a box of (w, h): text style, horizontal padding, optional
// background and border; onChange gets the text as typed.
struct InputLook { Color background{}, border{}; bool filled = true, framed = true; float padX = 11, padRight = -1; };  // padRight < 0: padX
void* MakeInput(const Ctx& c, float x, float y, float w, float h, const Style& s, const InputLook& look, const std::string& value, const std::string& placeholder,
               bool number, std::function<void(const std::string&)> onChange, const std::string& name);

// A card's box (.control-card): border, gradient; rows of a CSS grid stretch it to the tallest.
struct CardBox { float h = 0; Pin node, frame, grad; };
CardBox OpenCard(void* parent, float x, float y, float w);
void CloseCard(CardBox& b, float w, float h);
// actionCard (app.js): title (Georgia 15/600), lifetime badge, description, fields, the reason line
// and the submit button. fields draws into the form grid at (x, y, w) and returns its height.
using Fields = std::function<float(const Ctx& c, float x, float y, float w)>;
// minDescription: the description's min-height (0 in the Crafting page's content).
CardBox ActionCard(const Ctx& c, float x, float y, float w, const Control& control, const std::string& caption, bool primary, Action submit,
                   const std::string& reason, Fields fields = {}, const std::string& title = "", const std::string& description = "",
                   float minDescription = 40);
std::string ActionCaption(const Control& control);
// Cards in a 2-column grid (gap 15); rows share the tallest card. Returns the grid height.
using CardMaker = std::function<CardBox(const Ctx&, float x, float y, float w)>;
float CardGrid(const Ctx& c, float x, float y, float w, const std::vector<CardMaker>& makers);

// A table updated in place (loot log, damage meter): rows are made as needed and reused, cells
// are single lines; Update returns the new height.
struct LiveTable {
    Pin root;
    float w = 0, rowH = 0, headH = 0, size = 12, pad = 12;
    std::vector<float> cols;
    std::vector<Pin> head;
    struct Row { Pin node, topLine; std::vector<Pin> cells; std::vector<std::string> shown; std::vector<uint32_t> colors; bool strong = false; };
    std::vector<Row> rows;
    int visible = 0;
    bool rich = false;     // cells take TMP tags (the caller wraps its own words in <noparse>)
    bool pending = false;  // more rows than were made this frame: update again next frame
};
std::shared_ptr<LiveTable> MakeLiveTable(void* parent, float x, float y, float w, const std::vector<std::string>& head, const std::vector<float>& widths,
                                         float size = 12, float pad = 12, float padY = 10);
// makes at most maxNew new rows per call (-1: all); the rows not made yet wait (pending)
// strong: rows drawn bold with a line above (tr.damage-total)
float UpdateLiveTable(LiveTable& t, const std::vector<std::vector<std::string>>& rows, const std::vector<std::vector<Color>>& colors = {}, int maxNew = -1,
                      const std::vector<bool>& strong = {});
float LiveTableHeight(const LiveTable& t);
// The browser's automatic table layout (width 100%, cells on one line): each column as wide as its
// widest cell, the width left over shared in proportion; first: the first column's min-width.
std::vector<float> AutoColumns(float w, const std::vector<std::string>& head, const std::vector<std::vector<std::string>>& rows, float size, float pad,
                               float first = 0);
void SetColumns(LiveTable& t, const std::vector<float>& widths);  // moves the cells when they differ

// <select size> with a CSS height: a list box with a highlighted row, its own wheel, click to
// select, double-click to activate.
struct ListBox {
    Pin root, thumb;
    float x = 0, y = 0, w = 0, h = 0, rowH = 26;
    int rows = 10, full = 10, offset = 0, selected = -1, lastClick = -1, section = -1;  // rows drawn (the last may be cut), rows whole
    uint64_t lastClickAt = 0;
    std::vector<Pin> texts, fills;
    std::vector<std::string> items, shown;
    std::function<void(int)> onSelect, onActivate;
};
std::shared_ptr<ListBox> MakeListBox(Section& s, const Ctx& c, float x, float y, float w, float h, const std::vector<std::string>& items, int selected,
                                     std::function<void(int)> onSelect, std::function<void(int)> onActivate, const std::string& name);
void SetListItems(ListBox& b, std::vector<std::string> items, int selected);
void PaintListBox(ListBox& b);

// A colour chooser (input type=color): swatches and a hex field, in a popup like OpenList.
void OpenColorPicker(float x, float y, const std::string& hex, std::function<void(const std::string& hex)> chosen);
// Text to the Windows clipboard (Copy buttons).
bool CopyText(const std::string& text);

}  // namespace ep::ingame::view
