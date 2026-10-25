# Saved profiles: applied reliably, and shared as files (0.1.2-dev.42)

## Reports

- "Saved profiles are not applied to characters when the game restarts or when switching
  characters … even after I manually confirm the changes, all settings stay exactly as they were."
- "Where are the profiles saved? I want to share my config with someone."

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

Stored profiles, the startup choice and last used settings keep their keys and format.

## Check

Test build, EpCraftTest clone, commands only, isolated preferences
(`C:\Users\falor\EpochPactLab\builds\profiles-live5`):

| Step | Result |
|---|---|
| F6 Save draft "ShareQA" (102 settings), Export profile | `profile-exports\ShareQA.json`, format as above, settings identical |
| Import of that file | new profile "ShareQA (2)", settings identical; "ShareQA" unchanged |
| Import of a desktop-made file with xp 5, gold 2, a save action, an unknown ID, density 999 | profile with xp 5, gold 2 only |
| Import of a JSON file that is not a profile | refused, nothing added |
| Restart with the 102-setting startup profile | all four in the game after 10.8 s (was about 20 s); the window shows them; no drafts |
| Restart with last used settings xp 4 + a refused choice (unknown affix ID) + loot mode filter | xp 4 after 1.1 s; the refused choice is logged and loot mode filter is still applied |

The session closed with exit code 0 each time; saves and the real preferences were restored.

Tests: Python 386 (6 new in `test_profile_files.py`), JS 240 (2 new and 1 updated in
`test_startup_profile.cjs`), native 26; translations 1738/1738 (tr, ru). In an earlier run the
`test_le_session` two-process IPC test failed once with a temporary-file access error; it passed on
three reruns and in the final run (unrelated to this change).
