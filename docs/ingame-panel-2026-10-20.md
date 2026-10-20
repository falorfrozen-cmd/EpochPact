# In-game window (2026-10-20)

Private experiment (repo EpochPact-ingame-ui, branch ingame-ui), built on the 0.1.2-dev.36
one-file loader. Goal: EpochPact from inside the game, so a player installs only `version.dll`
and needs no desktop EXE. The window is the desktop interface itself, drawn with the game's own
Unity UI.

## What it is

- **F6** (`panelkey F5-F12|off`) opens and closes the window: the desktop interface
  (ui/index.html, ui/style.css, Chronoforge) at its 1440x900 layout, scaled to 94 % of the
  screen, with the desktop's measured sizes, colours, artwork, Inter (embedded: 400, 600, 700)
  and Georgia. `<b>` and weight 600 use Inter Bold / SemiBold (TMP's weight table), as the
  browser does.
- Pages, settings and words come from the embedded `ui.json` (`tools/build_ingame_assets.py`,
  `tools/ingame_pages.py` = app.js renderPage). Static texts are translated at build time by the
  app's own translator; texts the window writes while it runs (statuses, tables, notifications)
  by the same translator ported to C++ (`native/core/ingame_i18n.cpp`, the app's dictionaries
  embedded). A native test checks the port against ui/i18n.js on every dictionary text, each
  template filled, and the window's own texts (Turkish and Russian, all identical).
- Settings behave as in app.js (automatic after 250 ms, or a draft for Apply changes). Action
  cards follow formAction: game-save actions confirm first and run only for the character their
  data was read for; `{save_id}` comes from `identityread`.
- Native sections (each built from live reads, updated in place while numbers change):
  - Loot & Pickup: Recent pickups (loot log every 2 s, newest 100, grouped, rarity colours),
    Collect now, Pickup selections with the Wanted T7 affix picker (search, sort, two lists,
    double-click, 64 at most, Apply selection), Pickup rules.
  - General: Weaver's Will affixes inside the Weaver's Will panel (the same picker).
  - Skills & Trees: budgets, free respec. Campaign: cards, quest table. Mastery. Factions.
  - Summons: Damage meter (Start/Stop/Reset, element filter, row choice, Copy as CSV, damage
    taken), Death recap (the deaths with killer, damage by type, attackers, last hits, effects),
    Live summoned allies (columns, grouping, damage baseline).
  - Display: Tier colours (colour picker; stored in the app's preferences as the app stores
    them), Text and font (Windows fonts on request, a long list that scrolls).
    World: Interaction area colour, Rage of Morditas facts note.
  - Inventory: shortcuts (stash, vendor, Stash all, Sell all), Automatic sorting and Sell all
    with the tabs to sort, Stash tabs from loot filter (rule table, tab per rule, create, sort).
  - Circle of Fortune: membership, rank, Favor, Reputation, lenses; prophecy slots with reward /
    lens / charges forms and the preview dialog; lens catalog; the session multipliers.
  - Monolith: management (target timeline and difficulty, unlock, select with the trip to
    Traveler's Rest and the wait for the timeline panel, Corruption, Stability).
- Not yet in the window (heading and a pointer to the app): game setup, crafting, character,
  Unique Atlas, Stash Assistant, Monolith Navigator, minion bonuses, loot filter files,
  settings; the search box is drawn but not active.
- Keyboard: while a field of the window has the keyboard, the game's own "typing" switch
  (EpochInputManager.isInputFieldActive, what its own text fields use) is on, so typed keys do
  not drink a potion or cast a skill; it is off again when the field loses the keyboard, on
  another page and when the window closes.
- Preferences: read from `%LOCALAPPDATA%\EpochPact\preferences.json` (`EPOCHPACT_PREFERENCES`
  names another file; tests use an isolated copy); a colour chosen in the window is written in
  the app's format (indent 1, a temporary file replaced in one step).

## Speed

- Built ahead of time, never on F6: 5 s after a character is in game, in jobs of about 2.5 ms
  per frame. The fonts' atlases are filled with every character the window's texts use first,
  so no text waits for a new glyph later.
- Closed, the window's canvas stays on but renders to display 8 (unused) with its raycaster
  off: opening and closing take 0.03 ms and the next frame is an ordinary frame (switching a
  canvas off and on made Unity batch every graphic again, a 15-30 ms frame).
- A text's lines are measured with TextMeshProUGUI.ForceMeshUpdate (TMP_Text's own is an empty
  virtual: runtime_invoke does no virtual dispatch); `panel measure` checks every measured text
  against what TMP draws (377 texts, 0 differences).
- Long tables (quests, pickups) fill over a few frames; the closed window does no work per frame
  except a check once a second (a character change rebuilds the sections in the background).

## Live test (EpFeedbackTest clone, offline, 2560x1440, real keyboard and mouse)

`livetest/live_window2.py`, player build; preferences isolated (the player's own file is checked
unchanged afterwards).

| Step | Result |
|---|---|
| built in the background | 5 s after loading, 0 faults; 377 measured texts, 0 differences |
| F6 open / close | 0.03 ms / 0.04 ms; the frames after them 14 ms (ordinary frames) |
| click on the window / the same click with it closed | moved 0.0 / 7.7 |
| XP + −, slider, Default, wheel, toggle, links, choice list | as before (all pass) |
| free respec dialog | Cancel 78 px, Apply action 147 px wide; Cancel changes nothing |
| campaign quest table, factions, pickup mode list and back | pass |
| a text field takes typing | typed 0 and 1; the game's keys off while typing, on after |
| Wanted T7 affixes | double-click to Wanted, search "str" (1156 → 87), Apply, Clear and Apply back |
| Weaver's Will affixes | inside its panel (1156 affixes) |
| Summons | Damage meter Start, Stop, Reset; visible columns open and close |
| Tier colours / area colour | picker colour stored, Reset colours removes it / back to 3FD0FF |
| Text and font | Windows fonts loaded on request; a list of 20 rows scrolls with the wheel |
| Inventory | stash tabs from the loot filter read (9 rules, 1 tab) |
| Circle of Fortune | prophecy preview dialog (read only) |
| Monolith | 10 timelines in the list (read only); unlock, select, corruption, stability cards |
| frame time open vs closed | 14.1 ms vs 13.9 ms |
| game close | exit code 0; saves (54 files) and game files restored and checked |

Found and fixed on the way: text measured before TMP laid it out (every paragraph one line);
overlays (dialog, toast, list) built while switched off, so their texts could not be measured;
the interface font option of EpochPact changed the window's own texts (now excluded); keys
typed into the window reached the game.

## Not covered yet

- The sections listed as not yet in the window, the search box, gamepad, the HUD overlays (loot
  feed, damage window), settings/profiles and the startup replay of stored choices.
- Other resolutions and UI scales; VirusTotal for this build.
