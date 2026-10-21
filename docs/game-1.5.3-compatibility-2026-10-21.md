# Last Epoch 1.5.3 compatibility (0.1.2-dev.40)

## Report

Players with the desktop app saw "Unsupported Last Epoch build. This EpochPact release supports
Steam 1.5.2.1 only." Last Epoch 1.5.3 (patch notes 21 October 2026: mostly bug fixes, among them
Rage of Morditas spawning, Circle of Fortune prophecy gifting and Monolith fixes) changed
GameAssembly.dll, and Game setup only installs on the builds in ui/supported-game-builds.json.
The in-game edition has no such gate: it loads on 1.5.3 as it is.

| | Steam build | GameAssembly.dll bytes | SHA-256 |
|---|---|---|---|
| 1.5.2.1 | 25816319 | 97,222,656 | `d5f9fb45…` |
| 1.5.3 | 26087601 | 97,256,960 | `323e9183f3bee600b6c9e1c469218b9de926e9044f2ca3d44fae8a0ec9a91464` |

The game data names the version 1.5.3 (globalgamemanagers).

## Check (0.1.2-dev.39 player loader on 1.5.3, EpFeedbackTest clone, commands only)

Evidence: `C:\Users\falor\EpochPactLab\builds\compat-153-2` and `compat-153-3`.

- Startup: every module found its game code (xp, loot, density, items and auto pickup, smart loot,
  crafting, map view, statedit with 134 SP properties, player, progression, monolith, CoF, CoF
  tuning, inventory shortcuts, Rage of Morditas, Weaver, tiers, fonts, F6 window).
- All 34 read commands answered; all 19 pages of the F6 window built and shown with 0 faults; a
  session setting applied and read back.
- Closing the game: exit code 0xC0000005 in both runs, and in two vanilla runs too (EpochPact's
  version.dll removed, launched offline to the menus, closed with WM_CLOSE): a crash of 1.5.3 itself
  on exit. With EpochPact the shutdown also logged "hook restoration incomplete ... (error 5)":
  the game's threads could not be suspended while it was already exiting.

## Change

- `ui/supported-game-builds.json`: 1.5.3 added first; 1.5.2.1 stays accepted.
- The player READMEs name 1.5.3.
