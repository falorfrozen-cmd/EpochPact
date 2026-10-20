# Handover: the in-game window (F6)

Continuation: see `ingame-port-completion-2026-10-20.md` for the implementation
and measured verification after this handover. The remaining-work list below
records the original handover state.

Written 2026-10-20 for the next developer. Read this first, then `docs/ingame-panel-2026-10-20.md`
(what the window is) and `docs/ingame-port-map.md` (every desktop page, its HTML, CSS, commands and
texts, which is the specification for the pages still to port).

## Goal and the owner's rules

EpochPact is an offline Last Epoch mod: a native IL2CPP C++ core (`native/core`) plus a
Python/pywebview desktop app (`ui/`, `tools/`). The goal of this branch: the desktop interface drawn
inside the game with Unity UI (F6), so a Nexus player installs only `version.dll` and needs no EXE.
The owner's requirements:
- It must look identical to the desktop app ("mevcut arayüzle aynı olsun"): same layout, sizes,
  colours, texts, behaviour. Compare pictures, not impressions.
- Open and close instantly, no hitches (it is built ahead of time, in small jobs).
- Function first: a page that works matters more than a sub-pixel difference.

Standing rules (keep them):
- Tell the owner before changing files. Report per item (done / partial / missing) and wait for
  approval before any release, merge to main or Discord announcement.
- Never `git add` a folder; add files one by one. Commits end with the co-author line the owner uses.
- Never load or change the owner's main character (Falor). Live tests use the isolated
  EpFeedbackTest clone and restore saves afterwards. Skip anything that could damage saves; game-save
  actions are only opened and cancelled in tests.
- Never let tests write the real `%LOCALAPPDATA%\EpochPact\preferences.json` (tests use the
  `EPOCHPACT_PREFERENCES` copy and check the real file is unchanged).
- Package checks and live tests need the game closed; never close the owner's running game without
  asking. Before a live test with real input, tell the owner not to touch mouse or keyboard.
- MelonLoader must not be required. Screenshots for sharing must not show private data.

## Where things are

- Repo: `C:\Users\falor\EpochPactLab\ingame-ui`, private GitHub repo
  `falorfrozen-cmd/EpochPact-ingame-ui`, branch `ingame-ui` (not merged anywhere, nothing released).
- Builds: `C:\Users\falor\EpochPactLab\builds\ingame-research` (all + tests) and
  `...\builds\ingame-player\player\version.dll` (what the live test installs).
- Live test harness (outside the repo): `C:\Users\falor\EpochPactLab\livetest`
  - `live_window2.py`: the full live test (starts the game with the test clone, real F6, real
    mouse and keyboard, pictures of every page beside the desktop reference, restores everything).
  - `preview_server.py`: the desktop UI in preview mode with its own preferences file.
  - `desktop_ref.py`: 1440x900 desktop references from the preview server with headless Edge
    (steps JSON: navigate, run JS, scroll, picture, element rects). References: `window-ref/`.

## Build and test

```
cd C:\Users\falor\EpochPactLab\ingame-ui
py -3 tools/build_ingame_assets.py                                  # ui.json, fonts, sprites -> native/build/ingame
native\build.bat all C:\Users\falor\EpochPactLab\builds\ingame-research
native\build.bat player C:\Users\falor\EpochPactLab\builds\ingame-player
set EPOCHPACT_NATIVE_TESTS=C:\Users\falor\EpochPactLab\builds\ingame-research
py -3 -m unittest <every tools/test_*.py module>                    # 356 tests
node --test tools/*.cjs                                             # 235 tests
<every *_test.exe in ingame-research>                               # ingame_i18n_test runs inside test_ingame_assets
```
Live test (game closed, owner warned):
```
cd C:\Users\falor\EpochPactLab\livetest
py -3 live_window2.py C:\Users\falor\EpochPactLab\ingame-ui ^
  C:\Users\falor\EpochPactLab\builds\ingame-player\player\version.dll window-outNN window-ref
```
It prints one JSON line per step; pictures `window-<page>.png` and `compare-<page>.png` land in
the output folder. Last full run: `window-out13` (all steps passed except the end of the workshop
step, a harness problem since fixed: its scroll-to-top wheeled over a list box).

State at handover: everything above passes (Python 356, JS 235, native 23, asset/translator 10).

## Architecture (native/core)

- `ingame_panel.cpp`: the window. Pages (built from `ui.json` in jobs), sections, setting panels and
  cards (drafts / automatic settings like app.js), input (hits, hover, drag, wheel), dialogs,
  toasts, lists, reads and commands, eligibility, preferences, `panel read|measure|kern`.
- `ingame_window.hpp`: the API sections use (Section, Hit, Ctx, RegisterSection/RegisterTail,
  Read/Cached/Send/RunAction, forms, tabs, drafts, panel groups, widgets).
- `ingame_kit.cpp`: widgets (buttons, panels, notes, fields, selects, checkboxes, tables, list box,
  live table, action card, card grid, inputs).
- `ingame_ui.cpp/.hpp`: Unity primitives, TMP texts placed like CSS line boxes, the theme, `Tr()`.
- `ingame_i18n.cpp`: the app's translator (ui/i18n.js) in C++; a test checks it against the JS one.
- `ingame_assets.cpp`: fonts (Inter 400/600/700, Georgia), sprites, glyph prewarm.
- Pages' native sections: `ingame_sections.cpp` (loot, skills, campaign, mastery, factions...),
  `ingame_summons.cpp`, `ingame_display.cpp`, `ingame_inventory.cpp`, `ingame_cof.cpp`,
  `ingame_monolith.cpp`, `ingame_forms.cpp`, `ingame_craft.cpp` (Crafting tabs, glyph chances,
  forge, defaults, materials), `ingame_workshop.cpp` (Custom Items).
- `tools/ingame_pages.py`: the page composition (mirrors app.js renderPage). `tools/build_ingame_assets.py`
  builds `ui.json` (catalog controls, pages, translations, dictionaries, charset, advances, stats names).

How a page is made: `ingame_pages.sections()` lists sections: `settings(...)` (setting panels the
shell draws), `native("<kind>", ...)` (a C++ section registered with `RegisterSection`), `pending(title)`
(heading + "not in the window yet"). Options: `tab` (a page tab), `group` (several sections in one
panel), `look="craft"`, `details={summary,key}` (a setting panel inside `<details>`), `tail` (a native
part inside a setting panel), `head`, `facts`, `note`.

## Done (verified live, pictures matched to the desktop within about 1 px)

Overview, General, Loot & Pickup, Crafting (all three tabs incl. the Custom Items workshop),
Skills & Trees, Equipment, Summons (except minion bonuses), Display & Camera, World, Inventory,
Campaign, Monolith (management; not the Navigator), Circle of Fortune, Factions.
Not run live on purpose (they write the save): Stock all materials, Craft once, Create custom item
(their confirm dialogs and Cancel were tested).

## Still to do (each has its section in docs/ingame-port-map.md)

1. Character / stat editor (port map §5): browser (tabs, search, categories, favorites via the
   preference `epochpact.stat-favorites.v1`, hidden stats, pages of 24) and the stat dialog
   (`statraw` reads/sets). Needs a generic custom dialog (Confirm/ShowResult in ingame_panel.cpp
   are the pattern; dialog inputs need their own Watched section id).
2. Settings: language, draft profiles, startup profile and the startup replay of stored choices,
   text size, navigation, hidden parts, recovery backups / undo, in-game report, support report.
3. Loot filter files (Loot page), Monolith Navigator, Minion and companion bonuses (Summons).
4. Unique Atlas, Stash Assistant, Game setup.
5. Global search box (drawn, not active), the hover × "hide part" feature, the HUD overlays (loot
   feed, damage window), gamepad, other resolutions.

## Known open points

- Kerning: Chrome kerns Inter ("AVAVAVAVAV To To Ty Wa Yo LT" at 100px: 1380.6 px kerned, 1527.7
  not); TMP probably does not, so a few paragraphs wrap one word earlier than the desktop.
  `panel kern` (added, not yet run live) reports TMP's width, `enableKerning` and the font assets'
  pair records. If TMP has no pairs, they could be added from Inter's GPOS at build time.
- `panel measure` showed one stale entry (a destroyed text's measure reused by a new object at the
  same address); fixed with `ForgetMeasure` in TextBox, not yet re-run live.

## Lessons (expensive ones)

- IL2CPP `runtime_invoke` does no virtual dispatch: call the concrete override
  (`TextMeshProUGUI.ForceMeshUpdate`, not TMP_Text's empty one).
- TMP measures only texts on active objects: build overlays with their layer on; a section on a
  hidden tab or a closed `<details>` is switched on while it is rebuilt (Rebuild/RebuildTail do it).
- Chrome's "normal" line height = ascent and descent each rounded (LineHeight does it); a select is
  its line + 24; checkboxes are Chrome's dark control (#3b3b3b / #858585, checked accent).
- `AddHover` colours the renderer (CrossFadeColor multiplies): the graphic must stay white (it does).
- This game's TMP shows text alpha as if gamma-corrected: `Ink` gives alpha^2.2; a disabled
  button's label uses `kDisabledTextAlpha` with the CanvasGroup's .45.
- In a setting panel's build jobs the panel is `b->panel` until the last job (`ps.panel` after):
  using `ps.panel` early made every card invisible on every page once.
- Bash heredocs on this PC mangle backslashes: write patch scripts to a file; make patch scripts
  idempotent (skip a pair already applied) so a half-applied patch can be re-run.
- Live tests take 20-25 minutes; batch several pages per run.
