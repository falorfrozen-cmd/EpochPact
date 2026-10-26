# Saved profiles applied reliably and shared as files; character bonuses remembered (0.1.2-dev.42)

## Reports

- "Saved profiles are not applied to characters when the game restarts or when switching
  characters … even after I manually confirm the changes, all settings stay exactly as they were."
- "Where are the profiles saved? I want to share my config with someone."
- "Some weird stuff on area of effect … I need to reapply every time I open Pact. It works when I
  configure it, but after a close and restart it seems not to work anymore."
- "Character modifications like health regen / mana regen / area of effect etc. modifiers don't
  remain after restart of the game." (the same player: "area of effect" was the Character bonus,
  not the interaction area)

## What happened (0.1.2-dev.41 and earlier)

Measured on the disposable EpCraftTest clone (test build, commands only, isolated preferences):
a startup profile with every profile setting (102, as Save draft writes them) and four changed
values (amber, drops, gold x2, xp x3).

- **Slow.** The in-game window sent all 102 settings one by one, each after an identity check, also
  the ones the game already had. The changed values arrived over about 20 seconds (amber after 2 s,
  gold and drops after 10 s, xp after 20 s), in alphabetical order.
- **The window kept old values.** With F6 closed during the run, the window still showed x1 for all
  four after 60 seconds although the game had applied them.
- **One refusal stopped everything.** The in-game startup run and Apply changes (both editions)
  stopped at the first setting the game refused; everything after it was never sent, with only an
  error message.
- None of the 102 settings was refused on the test character (each sent at its default, at a
  changed value and at its catalog limits), so the slowness and the stale window are what most
  players saw.
- Profiles live only in `%LOCALAPPDATA%\EpochPact\preferences.json` (`epochpact.profiles.live`);
  there was no way to share one profile.
- **Colours were never restored by the in-game window.** The interaction area colour, the tier
  colour palette and the rule line colour are stored choices (not profile settings). The desktop app
  sends them when it connects (`applyAreaColor`, `applyTierPalette`); the in-game window only sent
  them when they were changed. After a restart the window showed the chosen colour while the game
  drew its default. Measured with last used settings radius 8, square, areas shown, and stored
  colours area FF0000, T7 112233, rule ABCDEF: after a restart radius, shape and show came back, the
  three colours were still the game's defaults after 45 seconds.

- **Character bonuses were never kept.** The Character bonuses page (stat editor: `stat`,
  `statraw`, `sheetstat`) changes the loaded character's stats in memory only, by design
  (`docs/stat-editor.md`); they are not profile settings. Measured on the clone with health regen
  +50, mana regen +20 and area +50 %: all three stay through a zone change (Z32 → End of Time) and
  are gone after a restart; a character load also starts without them. The page did not say so.

## Change

`native/core/ingame_settings.cpp`, `native/core/ingame_panel.cpp`, `ui/app.js`:

- The in-game startup run first reads the game's current values with the window's own reads (11
  reads) and sends only the settings that differ. A setting without a read is sent; a character's
  own setting (speed) is compared only with a read of this character.
- A refused setting no longer stops the run: it stays a draft with its value (as the desktop app
  already did for startup profiles), the others are still sent, and the notice says
  "X of Y settings … applied; the others were refused and stay in the draft". The refusal is
  written to `core.log`.
- "Applying … (N settings)" when the run starts; "Applied …" when it ends. Nothing is shown when the
  game already had every value.
- The window shows the values the run applied (also when it was closed).
- Apply changes (in-game and desktop) goes on past a refused setting; refused ones stay drafts.
- **Export profile / Import profile…** in Settings → Draft profiles, in both editions. A profile is
  written to `%LOCALAPPDATA%\EpochPact\profile-exports\<name>.json`:
  `{"epochpact":"profile","version":1,"name":"…","settings":{id: value}}`. Import adds it as a new
  profile ("Name (2)" when the name is taken) and never replaces one; only settings a profile may
  hold are kept (session settings and speed within their limits), so a file cannot carry a save
  action, a command or an unknown ID. The desktop app uses a file picker; the in-game window a path
  field (quotes from "Copy as path" are accepted) and Open export folder. Python:
  `tools/profile_files.py`.

- The in-game window sends the stored tier palette, rule line colour and interaction area colour
  when a character is ready, as the desktop app does: only those that differ from the game's own
  colours, also with the startup profile off (`StoredColourCommands`, `ingame_display.cpp`).

- **Character bonuses are remembered per character** (`native/core/stat_memory.hpp`,
  `stat_editor.cpp`; details in `docs/stat-editor.md`). The core writes every bonus change, from
  either edition, to `%LOCALAPPDATA%\EpochPact\character-bonuses.json` under the character's save id
  and name; a reset removes the key, Reset all bonuses the character, reads never write. Movement
  speed stays with the Speed setting. When a character is ready, after the startup profile, both
  editions send `statrestore` (unless Apply at startup is Off): bonuses the game already has are not
  sent again, a refused one is skipped and kept. The notice adds "N character bonuses restored.".
  The desktop app's close now sends `statraw … release` instead of `reset`: the bonus leaves the
  game as before but stays remembered. A note on the Character bonuses page says all this.

Stored profiles, the startup choice, last used settings and stored colours keep their keys and
format.

## Check

Test build, EpCraftTest clone, commands only, isolated preferences
(`C:\Users\falor\EpochPactLab\builds\profiles-live5`, `profiles-live7`, `area-old`, `area-new`):

| Step | Result |
|---|---|
| F6 Save draft "ShareQA" (102 settings), Export profile | `profile-exports\ShareQA.json`, format as above, settings identical |
| Import of that file | new profile "ShareQA (2)", settings identical; "ShareQA" unchanged |
| Import of a desktop-made file with xp 5, gold 2, a save action, an unknown ID, density 999 | profile with xp 5, gold 2 only |
| Import of a JSON file that is not a profile | refused, nothing added |
| Restart with the 102-setting startup profile | all four in the game after 10.8 s and 14.3 s in two runs (dev.41: about 20 s); the window shows them; no drafts |
| Restart with last used settings xp 4 + a refused choice (unknown affix ID) + loot mode filter | xp 4 after 1.1 s; the refused choice is logged and loot mode filter is still applied |
| Restart with area radius 8, square, shown + colours area FF0000, T7 112233, rule ABCDEF (`area-new`) | all six in the game after 1.3 s (dev.41 code, `area-old`: the three colours missing after 45 s) |

Character bonuses (`builds\bonus-live`, `bonus-live2` before the change; `bonus-live5` after it):

| Step | Result |
|---|---|
| Health regen +50, mana regen +20, area +50 %, then a zone change (Z32 → End of Time), then a restart (dev.41 code) | all three kept through the zone change, all gone after the restart |
| Same through `stat healthregen 50`, `statraw` and Strength +100 % (attribute), plus `speed 2` | `character-bonuses.json`: `6\|EpCraftTest` with the four keys, no movement speed; a read wrote nothing |
| Restart with last used settings | all four in the game 0.35 s after the startup run started (`core.log`: "startup run: last used …", "statrestore: restored 4 of 4") |
| `statraw 116 … reset`, `statraw 18 … release`, then `statrestore` | area forgotten; mana regen out of the game but kept, then "restored 1 of 3 bonuses (2 already applied)" |
| Restart with Apply at startup Off | no bonus in the game, all still remembered; `statrestore` by hand: 3 of 3 |
| `statreset` | none in the game, the character removed from the file |
| The player's own `%LOCALAPPDATA%\EpochPact\character-bonuses.json` | not created by the tests (isolated preferences folder) |

Strength shows `attached=0` right after it is set, as before this change: the attribute bonus is
re-added by the attribute hook when the game recalculates. Not tested live: the desktop app (its
restore, notice and close with `release` are covered by `test_startup_profile.cjs` and
`test_ui_bridge.py`), and a second character (the file key is save id and name;
`stat_key_test.cpp`).

The session closed with exit code 0 each time; saves and the real preferences were restored.

Tests: Python 387 (6 new in `test_profile_files.py`, 1 in `test_ui_bridge.py`; 2 in
`test_shutdown.py` now expect `release`), JS 241 (3 new and 1 updated in
`test_startup_profile.cjs`), native 26 (`stat_key_test` 49 checks, 9 new for
`stat_memory.hpp`); translations 1743/1743 (tr, ru). In an earlier run the
`test_le_session` two-process IPC test failed once with a temporary-file access error; it passed on
three reruns and in the final run (unrelated to this change).

## Reset all settings to default (0.1.2-dev.43)

Player question: "anyway to reset the whole thing to default after messed around with some settings?"
Until now this took one Default button per card, or Apply at startup Off and a restart.

Settings → Draft profiles → **Reset all settings to default** (both editions, after a confirmation)
returns every setting a profile can hold to the game's normal value and removes it from the last used
settings, so a restart does not bring it back. Saved profiles, the startup choice, character bonuses
(Character → Reset all bonuses), loot/font choices and changes in the save are kept.

- In-game window (`ResetAllSettings`, ingame_settings.cpp): a startup run with every profile setting at
  its catalogue default. It reads the game's values first and sends only the settings that differ; a
  refused one stays a draft. Notices "Applying the game's normal values (N settings)", then "All settings
  are back to the game's normal values." (no count: the run also counts the settings the game already had),
  or "All settings are already at the game's normal values." when nothing was sent (settings whose
  default is the game's own value, such as rank bonuses, are always sent as `reset`).
- Desktop (`resetAllSettings`, app.js): the startup profile's batch (`applyDraft`) for every profile
  setting the window has not read at its default; each successful reset leaves the last used settings.

Live check 2026-10-26 (EpFeedbackTest clone, `builds\reset-live1`): three settings came back from the last
used settings at a restart (primordials 1, extra companions 3, offhand 1) and speed was set to 2. The reset
(105 profile settings considered) returned all four to the game's values and emptied the last used
settings; a second reset changed nothing; the next restart applied nothing.
