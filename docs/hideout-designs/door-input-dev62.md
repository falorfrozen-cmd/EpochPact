# Custom-door input validation — 0.1.2-dev.62

The user subsequently confirmed they were not using the game during run 25's
uncommanded Formosus transition. Its actual door callback was not traced, so
the exact historical caller remains unproven. A separate concrete bug was
reproduced and fixed, rather than declaring that idle observation alone proved
the historical cause.

## Confirmed reproduction

`WorldObjectClickListener.ObjectClick(playerObject, isClick)` supplies a native
click flag. EpochPact's old detour ignored both that flag and `playerObject`,
and also acted with the game in the background. In the isolated run 32, its
existing `doorclick` fixture sent `isClick=false` to the Difficulty door. No
desktop input was synthesized, yet the state changed from Normal to Empowered.
This harmless setting change used the same dispatch path as the boss doors.

Evidence and the historical log excerpt were preserved in
`research/hideout-editor-2026-11-07/door-input-32`.

## Changes

- Owned custom-door callbacks require `isClick=true`, the current local actor's
  GameObject, offline play and the game's process owning the foreground window.
- F6 and Hideout editing retain their existing interaction blocks.
- Rejected callbacks do not consume the valid-click debounce interval.
- Owned callbacks never fall through to the borrowed rock's original native
  interaction, including when a guard throws or actor lookup fails.
- Other game objects keep their original callback unchanged. Boss Rush's
  explicitly requested progression is also unchanged.
- Private fixtures now distinguish a click from a non-click, foreign/null
  sources and actual background input. Their positive call supplies a scoped
  foreground override for that callback only; this code is excluded entirely
  from the player build. It does not move the desktop pointer or change focus.

## Verification

Last Epoch 1.5.4, Windows x64; isolated offline EpFeedbackTest only.

- 27 native test programs passed. Monolith has 86 assertions, including all
  64 combinations of the six input-policy conditions; Hideout retains its
  84 passing assertions from the Q/E implementation.
- Nine Python design/package tests passed.
- Run 33 sent non-click callbacks to all 17 owned doors/stations. None queued
  a boss, started Rush or changed Difficulty.
- Foreign and null sources, real background focus, F6 and editing all rejected
  an otherwise valid callback. The next valid click still worked immediately.
- Valid native callbacks still cycled Difficulty/Harbinger and opened Stash
  and Vendor. A valid timeline-4 callback entered Formosus's `R2Q30` zone.
- A separate 45-second idle observation remained in MonolithHub with no
  pending boss, native menu or door event.
- Run 33 closed normally with exit code 0. All 68 original save files were
  restored and byte-verified; real preferences were unchanged. The temporary
  boss-run ledger was restored too.

The actual native callback and foreground ownership were exercised; physical
mouse/controller key delivery was not automated. These results close the
reproduced unsafe route without establishing the missing historical call stack.

Evidence: `research/hideout-native-dev62.json`,
`research/hideout-python-dev62.log`,
`research/hideout-editor-2026-11-07/door-input-33/door-input-checks.json`, and
`research/hideout-door-finish-33.log`.

## Final player artifact

The exact player DLL passed the normal `player-34` smoke: all five templates
(60 objects each, zero failed models), rendered thumbnails, placement,
rotation/cancellation, HUD and layout persistence. Private door/rotation/UI
fixtures were absent. The normal game session closed with exit code 0; all
68 original save files were restored and byte-verified, and real preferences
were unchanged.

The tested DLL was installed with a separate dev.61 backup. The game is closed.

- Package: `dist/hideout-dev62/EpochPact-0.1.2-dev.62-InGame.zip`
- Eight allowlisted entries; runtime bytes and archive manifest verified.
- ZIP SHA-256: `bf1886305193ccabf9db6113b5b53c7ded17f0d35114888e864804eb1aed7630`
- DLL SHA-256: `fa95ba40bb11a1a9179f5b74d2b8e7be4f42ddcd8db4d108f9cee4d7f164744a`
- Receipt: `research/hideout-dev62-install.json`
- Player smoke: `research/hideout-editor-2026-11-07/player-34/player-smoke.json`
- Shutdown/restore: `research/hideout-player-finish-34.log`

No GitHub or Nexus upload was performed in this task.
