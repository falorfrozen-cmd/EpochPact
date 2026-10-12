# Player suggestions, group 1 — 0.1.2-dev.16 validation (2026-10-12)

Steam Last Epoch 1.5.2.1 (internal 1.5.2.2), offline. Suggestions 7, 11, 12, 13,
14 and 19 from the second player feedback list.

## Changes

| # | Suggestion | Implementation |
|---|---|---|
| 7 | Auto-loot interval | `pickupinterval 0.25..10` (seconds); UI control "Auto pickup interval", applied automatically. Default unchanged (0.25 s). |
| 11 | Pause when the inventory is full | Detour on `GroundItemManager.showInventoryFullNofication` (installed at startup; the game's message still shows). While set, equipment labels are skipped; materials (`IsAffixShard/IsRune/IsGlyph`), gold, potions, tomes and bones continue. `UIBase.IsInventoryOpen` open→closed clears it. |
| 12 | Group summons by type | UI only: rows grouped by cleaned object name; totals for DPS/last hit/health, averages for armour/damage increase/resistances; DPS change from members with a baseline. "Show each ally separately" stored in display preferences. |
| 13 | Stash last tab | Hooks on `StashTabbedUIControls.OnEnable/OnDisable` (the 1.5 stash) and `StashPanelV2.OnOpen/OnClose` (newer panel). Remembered tab stored in `<game>/EpochPact/stash-tab.txt`; restored with the game's `SwitchToStashTab` after the tab bar finishes rebuilding. |
| 14 | Loot the whole map | `pickupnow`: radius ignored, up to 32 pickups per frame, stops after two idle frames or 240 frames. Filter, categories, capacity and tooltip pause apply. |
| 19 | Settings persistence | Settings → "Apply at startup": off / last used settings / saved profile. Applied once per game session and character after the offline character is ready. Only session/actor number and toggle settings are stored. |

## Automated

| Suite | Result |
|---|---|
| Native | 17/17 programs; items fixture 32/32 (new: full inventory skips equipment, materials continue, interval bounds and rate limiting, pick-up-now clears the pause and collects). |
| Python | 240/240. |
| JavaScript | 136/136 (new: summon grouping 5, startup profile 5). |
| Compiled package | 21/21 `verify_player_package`; 4/4 extracted-ZIP connection checks. |

## Real game (isolated `EpFeedbackTest` clone, test core)

Four sessions; all 54 original save files restored and byte-verified after
each. The player core was reinstalled afterwards and the test `stash-tab.txt`
removed.

- core.log: `items: inventory-full pause ready`, `inventory shortcut hooks
  installed at startup`.
- Interval: 5 scans in 10 s at 2 s; 40 scans in 10 s at 0.25 s; 0.1 s refused.
- Pick up everything now: gold dropped 40 units away stayed on the ground with
  radius 5 (gold 0 → 0); `pickupnow` picked 1 drop in 3 frames (gold 0 → 25).
- Full inventory: after the game's notification `inventoryFull` was true; gold
  dropped beside the player was still collected (0 → 25); still paused while
  the inventory was open; cleared after closing it.
- Stash (legacy UI is live in 1.5.2): tab 0 → 1, closed and reopened: 1 (the game
  keeps it). After a zone change: 1 (game). After a full game restart: restored
  to 1 by EpochPact (`stashTabRestores` 1). Reset to 0 afterwards.
- Game closes: 0 in sessions 2–4 (two closes in session 4).

**One crash in session 1:** the game crashed (UnityPlayer.dll + 0xd17ab0, no
managed stack) about one second after a rapid sequence of test-only commands:
pick-up-now, the test command that calls the inventory-full notification
directly, and dropping gold next to the player within 80 ms. The same
sequence with the same timing (session 3) and spaced out (session 2) did not
crash. The cause is not established. It is a different location from the
documented intermittent exit crash (UnityPlayer.dll + 0x109ad1). The test
commands that drop gold and call the notification are not in the player build.

## Package

| File | SHA-256 |
|---|---|
| EpochPact-0.1.2-dev.16.zip (26,811,496 bytes) | `ae23491c689421f3e4ec649a3550c3097656b595efc634ffe958ad0af39a87a0` |
| EpochPact.exe | `a0f747b1be524e0440fffa1467e8a61322581ba21d9b0aa99895ae4047fd50ef` |
| EpochPact.Core.dll | `f9d5001ac9e7c43569f2dd351653254f6493ca1ddf13e5532c07b47b89b38a6b` |
| version.dll | `cb135799542f2d06529bde9172b729c38c2abf88d3cbcd29f6c47868dd0dc05b` |

Local Microsoft Defender (1.459.682.0): no threats in the ZIP or app folder.
VirusTotal was not run for these bytes: the browser extension was unavailable and
the web upload endpoint requires a reCAPTCHA, which was not bypassed.

## Not covered

Filling a real inventory to capacity (the notification was invoked directly);
equipment labels while full (fixture-tested only); summon grouping with real
mixed minion families (fixture-tested; names come from the live table); the
startup profile in the real WebView window (fixture-tested functions).
