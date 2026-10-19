# In-game panel experiment (2026-10-20)

Private experiment (repo EpochPact-ingame-ui, branch ingame-ui), built on the 0.1.2-dev.36
one-file loader. Goal: check whether EpochPact can be used from inside the game, so a player
could install only `version.dll` and need no desktop EXE.

## What it is

- **F6** (`panelkey F5-F12|off`, saved in `<game>\EpochPact\panel-key.txt`) opens and closes a
  small panel drawn with the game's own Unity UI and font, on a canvas of its own
  (screen-space overlay, kept across scene loads). The game's own windows are not changed.
- Rows: XP multiplier (- / +), Monster density (- / +), Auto pickup (On / Off), plus a line with
  the last command's reply.
- Buttons send the ordinary command lines (`xp`, `density`, `autopickup`) through
  `commands::SubmitUi`, the path the inventory buttons already use, so every command's limits
  and the offline-only gate apply unchanged. Values come from the `status` command.
- The hotkey and the mouse are read with GetAsyncKeyState only while the game window is in
  front; nothing draws through DirectX and no input is sent to the game.
- Files: `native/core/ingame_panel.cpp/.hpp`, `native/core/ingame_rules.hpp` (pure rules),
  `native/tests/ingame_rules_test.cpp`; wired in core.cpp, convenience.cpp (frame), commands.cpp
  (`panel read|0|1`, `panelkey`), build.bat.

## Live test (EpFeedbackTest clone, offline, Last Epoch 1.5.2.1, 2560x1440)

Real keyboard and mouse input (SendInput), player build, one-file loader:

| Step | Result |
|---|---|
| F6 (real key) | panel opened, values read: xp x1, density x1, auto pickup off, offline |
| click XP + | xp x2 |
| click density + | density x1.5 |
| click Auto pickup | on |
| `status` | "xp: x2", "density: x1.5", "autopickup: on" |
| click XP -, density -, Auto pickup | x1, x1, off |
| F6 (real key) | panel closed; 2 toggles, 6 clicks, 6 actions, 0 faults |
| game close | exit code 0 |

Saves (54 files) and the game folder's files were restored and checked afterwards.

The first run found three display faults (row names not set, the font has no U+2212 minus
sign, a duplicated hint) and a fault in the test script (a JSON error stopped its cleanup; the
saves and files were restored by hand and checked). Both are fixed; the second run passed.

Checks: Python 346, JavaScript 235, native 23/23 (new ingame_rules_test 21/21), interface texts
and translations unchanged.

## Not covered yet

- Whether a click on the panel also moves the character (the panel has a raycaster like the
  game's windows; not measured).
- Gamepad, other resolutions/UI scales, the panel during loading screens and menus.
- Only three settings; the desktop app has many pages. Moving more would be step by step.
- VirusTotal for this build (expected to stay clean: no new kind of code, same loader shape).
