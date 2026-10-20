# In-game window (2026-10-20)

Private experiment (repo EpochPact-ingame-ui, branch ingame-ui), built on the 0.1.2-dev.36
one-file loader. Goal: EpochPact from inside the game, so a player could install only
`version.dll` and need no desktop EXE. The first prototype (commit 38b7a55: a small panel with
three rows) proved the idea; this step replaces it with the desktop interface itself.

## What it is

- **F6** (`panelkey F5-F12|off`, saved in `<game>\EpochPact\panel-key.txt`) opens and closes the
  EpochPact window: the desktop interface (ui/index.html, ui/style.css, Chronoforge) drawn with
  the game's own Unity UI on a canvas of its own, at the desktop's 1440x900 layout scaled to 94 %
  of the screen. Sidebar, topbar, hero, pages, cards, sliders, steppers, switches, choice lists,
  badges, notes, footer and scrollbars use the sizes measured from the desktop app's DOM, its
  colours, Inter (embedded, static instances) and Georgia, and its own artwork.
- Pages, settings and words come from the embedded `ui.json`, which
  `tools/build_ingame_assets.py` writes from the same sources as the desktop app: the catalog,
  locale-en, `tools/ingame_pages.py` (the page composition of app.js renderPage) and the
  translations, run through the app's own translator (`ui/i18n.js`, via
  `tools/ingame_translate.js`) so Turkish and Russian read exactly as in the app. The language is
  the app's (preferences.json, read only).
- Settings behave as in app.js: automatic settings are sent 250 ms after the last change, the
  others wait as a draft for Apply changes; feedback Applying… / Draft / Applied / the refusal.
  Every change is an ordinary command line through `commands::SubmitUi`, so each command's
  limits and the offline-only gate apply unchanged. Values come from the same reads as the app
  (status, sessionread, statraw for speed, buildread, qolread, rageread, effectsread, mapsettings,
  tierread, invread, pickupradius), polled once a second while the window is open.
- Sections the window does not have yet show their heading and "This section is not in the
  in-game window yet. Use the EpochPact app for it." (game setup, crafting, character,
  inventory, atlas, stash, campaign, the Monolith/CoF/factions main sections, settings, the
  Weaver's Will affix choice and other action panels). The search box is drawn but not active.
- Files: `native/core/ingame_panel.cpp/.hpp`, `ingame_assets.cpp/.hpp` (embedded data, WIC image
  decoding off the main thread, sprites, TMP font assets), `ingame_json.hpp` (+ test);
  `tools/ingame_pages.py`, `tools/build_ingame_assets.py`, `tools/ingame_translate.js`,
  `tools/test_ingame_assets.py`; build.bat embeds `ingame.rc` (35 files, ~880 KiB) into the core
  and the one-file loader.

## Speed

- Built ahead of time, never on F6: 5 s after a character is in game, in jobs of about 2.5 ms
  per frame (a section, or one row of cards). The whole window is about 230 ms of work spread
  over frames; no frame over 50 ms was seen while it was built.
- Opening and closing switch one Canvas component (about 3 ms); Unity keeps every mesh. Each
  page has its own Canvas, off while hidden; clipping uses a stencil Mask (RectMask2D tests every
  child every frame). TMP texts are marked scale-static (no per-frame scale check).
- Hit testing, hover, sliders, wheel and scrollbars use the window's own layout numbers; a closed
  window does no work per frame.

## Live test (EpFeedbackTest clone, offline, 2560x1440, real keyboard and mouse)

`livetest/live_window.py`, player build (one-file loader):

| Step | Result |
|---|---|
| built in the background | 5.9 s after loading, 83 cards, 0 faults |
| F6 (real key) | open in 3.1 ms; the next frames 19 ms at most |
| click on the window | character moved 0.0 (the same click with the window closed: 9.4) |
| XP + / − | x1.1 then x1, "Applied" shown, `status` agrees |
| slider, Default | x22.8 from the slider, x1 after Default |
| mouse wheel | scrolled 285 px and back |
| Loot & Pickup: auto pickup switch | on, then off again (status agrees) |
| pages overview, display, world, monolith, skills | 1–2.7 ms each to show |
| link card (overview) | opened its page |
| choice list (Interaction area shape) | opened, closed by a click outside, nothing changed |
| 6 × F6 open/close | open 2.5–3.1 ms, close 2.6–4.0 ms, frames after 14.5–18 ms |
| frame time open vs closed | 13.34 ms vs 13.34 ms; 0 frames over 50 ms in 2704 open frames |
| game close | exit code 0; saves (54 files) and game files restored and checked |

Pictures: `livetest/window-out3/compare-<page>.png` (desktop app left, in-game right, both at
1440x900).

The first two runs found: (1) a font file written while the zone loaded could not be read yet
(a virus scanner holds new files for a moment) and the half-made font was kept, so building
faulted; fonts are now written at game start, a font is kept only once its face loaded and its
symbol fallback is attached, and building waits 5 s after loading; (2) a panel filled over
several frames held an unrooted wrapper; panels are now pinned; (3) every text was white: TMP's
colour is TMP_Text.color, not Graphic.color. Diagnostics added: fault address and step in the log,
fonts and frame-time counters in `panel read`.

Checks: Python 353 (new test_ingame_assets 7), JavaScript 235, native 23/23 (new
ingame_json_test 17/17), interface texts 1727 and translations unchanged.

## Not covered yet

- The sections listed above, the search box, the hero's text shadow, Void Atlas theme.
- Gamepad, other resolutions and UI scales, the window over the game's own panels and menus.
- VirusTotal for this build.
