# Hideout editor completion — 2026-11-08

Workspace: `C:/Users/falor/EpochPactLab/fix52`. Baseline: dev.62, installed
loader SHA-256 `fa95ba40bb11a1a9179f5b74d2b8e7be4f42ddcd8db4d108f9cee4d7f164744a`.
Backup: `../backups/hideout-dev62-before-editor-completion-2026-11-08`.

User authorized all eight audited issues, with deletion and Undo/Redo first,
then the remaining tools, and real-game verification without regressing prior
features. No publishing was requested. Preserve the existing unrelated dirty
tree; never blanket stage/reset it. No subagents authorized.

## Required order and acceptance

1. Visible Delete, central selection/ghost cleanup, keyboard use above the HUD,
   bounded multi-step Undo/Redo, redo branching, reset recovery. Test in the
   actual game using the private callback fixtures before phase 2.
2. Precision transforms, grid/angle snap, alignment, duplicate, multiple
   selection/groups, per-object locking and optional floor picking.
3. Full-footprint placement validation and route protection for services/doors,
   measured at commit, not expensive scene scans on every mouse frame.
4. Saved-template previews, selected-card management, overwrite confirmation,
   rename/delete; favorites, recent models, detailed library categories.
5. Native/Python checks, real game tests on isolated EpFeedbackTest only, prior
   interaction/input/layout regressions, final normal player DLL + ZIP,
   normal shutdown, byte-verified save/runtime restoration, install exact
   verified player payload with backup and leave the game closed.

Use `tools.ingame_live_session start/finish` and PID/session guards. Never
mutate Falor. Prefer native IPC and Unity captures; avoid desktop UIA inspectors
(previously caused a Unity shutdown crash). Existing input guards, protocol 3,
Q/E controls and dev.62 owned-door/focus checks must remain.

## Status

- Baseline source backup verified; game was closed at task start.
- Phase 1 implementation and actual-game verification passed.
- `history-35` closed with exit code 0; 68 original save files restored and
  byte-verified, actual preferences unchanged. Baseline dev.62 runtime restored.

### Phase 1 implementation notes

- Version is now dev.63 (no distribution yet). New `hideout_history.hpp`
  stores 50 completed layouts with a cursor, no-op suppression and redo
  branching. Automatic model fitting replaces the head without adding steps.
- `Save()` records successful edits, preserves `.before` for restart recovery,
  and rolls failed mutations back to the last completed layout. History restore
  advances the cursor only after an atomic layout write succeeds.
- `RemoveItem` centrally protects the exit and cancels held ghosts before
  deleting; HUD now has Delete and Redo, with history availability states.
- Pointer and keyboard blocking are separate. Keyboard input is still blocked
  for F6/native menus, typing and a background game. Ctrl+Z/Y/Shift+Z added.
- New test-only `hideout testeditkey delete|undo|redo [ui]` feeds key samples
  through actual `EditFrame` above the HUD; never OS-wide input. `testmenus
  click:<hit-name>` invokes a visible enabled HUD callback.
- Initial complete private build: `native/build-hideout-editor63-test`.
  Final phase-1 incremental build running/log:
  `native/build-hideout-editor63-phase1-final.log` (hideout + ingame_hideout).
- Native rules currently 97/97. New live runner `tools/hideout_history_live.py`
  verifies HUD deletion, ghost cleanup, three-step undo/redo, branching, read-only
  failure rollback, reset/template recovery, protected exit and uncommitted
  additions. It chooses a curated model absent from the initial layout.
- Next: finish build, start a fresh owned `history-35` live session, run runner,
  fix any real failures, finish/restoration, then proceed to phase 2.

Phase 1 result: `research/hideout-editor-2026-11-08/history-35/history-checks.json`.
All 12 checks passed (HUD and keyboard deletion, ghost cleanup, three-step
Undo/Redo, branch invalidation, failed undo/deletion persistence rollback,
native UI gating, list removal, reset/template recovery, protected exit,
uncommitted addition cleanup). Actual native `EditFrame` executed injected
editor key samples; no desktop key/focus automation. Screenshot inspected:
`history-35/delete-toolbar.png`. Native 97/97 rules passed.
Final phase-1 loader/core stamp is in `native/build-hideout-editor63-test/test`.
The screenshot was at a 640×480 render size; final phase-2 UI must also be
checked explicitly at 720p/1080p/1440p.

### Planned phase 2 architecture (not implemented yet)

- Keep item lock/group as appended layout columns. Selection is an ID set;
  Ctrl+click toggles, normal group click picks the group. Extend placement to
  secondary green ghosts, commit a group as one history action. Duplicate only
  decorative models (functional stations/doors stay unique). Persistent groups,
  per-object lock, explicit floor-picking toggle; F6 always offers unlock.
- Add grid step and angle step, numeric transform inspector, alignment by
  measured bounds. Keep Q/E and existing arrow aliases, size/height controls.
- Cache geometry at ghost creation rather than scan renderers each frame.
  Validate full footprints with cheap geometry during movement. At commit,
  temporarily apply transforms, rebuild actual NavMesh, compare reachability
  of services/doors against the baseline, and roll back on a newly blocked
  route. Reuse the production equivalent of the existing audit's SamplePosition
  and CalculatePath logic; avoid per-frame scene scans.
- Saved previews should use a key including content revision (e.g.
  `s:<hash>:<name>`), parse layouts without changing live state, and feed the
  existing template renderer. Saved cards offer load/rename/delete directly;
  saving an existing name requires confirmation. Preserve existing names/files.
- Library favorites/recent are separate preferences, not layout edits. Use
  catalog GUIDs; add name-derived wall/column/banner/light/etc. categories.
  Preserve existing search, pagination, priority scheduling and bounded caches.
- `ShowCustomDialog` / `InputField` in ingame_window.hpp support the numeric
  inspector and rename dialogs; `CheckField` supports selection/settings.


### Phase 2 implementation and live evidence

`tools-36`: group ghosts, locks, duplication, actual exact-transform modal,
floor ray picking, HUD and Tools at 720p/1080p/1440p passed. Exit 0; 68
original files byte-verified after restore; real preferences unchanged.

`library-safety-37`: favorites/recent persisted separately from layout history,
name-derived filters, full-bounds rejection from mouse/precision paths, personal
preview with no live-layout mutation, case-insensitive overwrite protection,
rename collision rejection, revision-key refresh all passed on retry2.
First two runner attempts wrongly assumed Garden_Pillar_Big would obstruct the
player at scale 1/3; the real model remained walkable. These are NOT counted as
route-protection passes. A private BoxCollider fixture is being added to test
actual NavMesh rollback deterministically. Exit 0 and all 68 originals restored.

New production files: hideout_tools.hpp/.inl, ingame_hideout_tools.inl,
hideout_catalog.hpp, hideout_library.inl, hideout_templates.inl,
hideout_safety.inl. Personal template keys include name+content revision and
hex-encoded names; temporary parses do not change live objects/IDs. Saved
cards load/rename/delete directly; overwrite requires explicit confirmation.
Floor picker now filters actual floor/path names instead of all walkable props
(flowers must remain selectable). Unsupported static geometry reports a reason.
Full footprint checks run on cached bounds during mouse movement. Only a commit
stages real transforms/colliders, synchronizes physics, rebuilds NavMesh and
compares reachable services/gates, restores staging before durable commit; a
newly blocked path throws and keeps the editable selection. Ground changes now
reject layouts whose full bounds would leave the new ground instead of crowding
objects together through origin-only clamping.

Build currently in progress: native/build-hideout-editor63-final-qa.log,
private out native/build-hideout-editor63-test. Adds private testsafetybox and
rename/confirmation QA hooks. Need final route/UI/history/tool regressions,
normal player build+smoke/package and exact final installation. No game running
after library-safety-37 finish. Do not claim completion before those finish.


Final-private-38: all 12 history checks, 7 tools checks (including alignment and
ungroup), library/full-footprint/actual NavMesh rejection tests, and 4 native
saved-template UI confirmation tests passed. Old door-input suite passed all
17 negative actions, foreign/null/background/F6/edit rejects, Stash/vendor
positive actions, 45 seconds with no unexpected menus/travel, explicit boss
travel. Exit 0, 68 originals restored byte-for-byte, real preferences unchanged.

Final-hints-39: latest hint-resizing/cache change tested at 720p/1080p/1440p;
recent/grid/angle/saved templates loaded in a fresh process; saved preview
rendered. A fixture assumption requiring >1 wrapped line was corrected to >=1
(the actual English hint fits one line). Screenshot invalid-placement-1280
inspected: no overlap. Exit 0, originals and preferences verified.

Player-40: actual ZIP payload passed protocol 3, absence of private fixtures,
all five 60-object templates with no model failures, real previews,
add/rotate/cancel and toolbar. Normal close exit 0, original saves restored.

Final self-review found two pending-placement edges: old `at/move/here`
commands could cancel a new ghost, and grouping/alignment could dereference a
selection after its pending members were cancelled. Fixed by routing held
legacy commands into the preview and requiring committed selections before
Group/Ungroup/Align/Lock. Move keeps an existing preview. Tools/HUD/List disable
premature Lock/Group actions; backend also rejects them without losing the
preview. Added actual-game regression to hideout_tools_live.py. Rebuilding
private and normal outputs (pending-guard logs); final private41 and normal42
verification and new final package are still required. Old dist/hideout-dev63
is a superseded intermediate candidate; only share the final verified package.


Pending-guard-41 final native source: history 12/12 passed. Tools 8/8 passed,
including pending-copy Ctrl+G/Ctrl+Shift+G/Group/Ungroup/Align/Lock rejections,
actual disabled Tools callbacks, legacy at/move/here and unchanged ghost/item
count. The first extra test attempted to assert a ghost before its async visual
was ready; the state still held both pending copies. Fixture now supplies a
world test point and waits for both actual ghosts first. Library and actual
NavMesh rollback passed again. Exit 0; 68 original saves restored byte-verified,
real preferences unchanged. Final source is frozen. Final player package:
dist/hideout-dev63-final/EpochPact-0.1.2-dev.63-InGame.zip (8 allowlisted files,
no private QA/SDK/logs/saves). It supersedes the intermediate dev63 package.
Final native 27 programs passed; hideout rules 103/103. Final package/design/
asset Python suite 20 passed, session/restoration suite 36 passed.
Player-42 is now testing the exact ZIP-extracted DLL, then a normal-runtime
commit/restart/reload round trip; final installation remains pending that result.


Player-42 caught a genuine Cancel regression after the pending legacy-command
fix: `cancel` loaded new IDs before SetEditing(false), which attempted to commit
a still-valid old-ID ghost and returned "Unlock the selected objects first."
No original save was affected. Test session closed normally with exit 0 and
all 68 originals restored. Fixed Cancel to validate its snapshot without
applying, cancel/dispose the old preview FIRST, then restore IDs and finish
editing. Added a full exact-layout Cancel regression to tools runner. Rebuilding
cancel-final outputs. The intermediate dist/hideout-dev63-final package is NOT
the final shareable artifact; it is retained as failure evidence. Final private43
and player44 + restart round trip remain required before install/completion.


COMPLETED: cancel-guard-43 passed final-source tools 9, library/navigation 4,
and history 12 checks. Normal ZIP player-44 passed all five templates, real
preview and add/rotate/Cancel, then a real normal shutdown/restart. Public
add/at/rotate/drop, layout/grid/angle/favorites/recent and saved-template files
survived the new process byte-for-byte; saved preview rendered after reload.
Restart and final shutdown both exited 0. Original 68 saves were restored and
byte-verified, real preferences unchanged. Exact verified ZIP payload installed
with a byte-verified dev62 backup; game left closed. Artifact/installation:
research/hideout-dev63-install.json. Final report:
docs/hideout-designs/dev63-verification.md. No publication performed.
