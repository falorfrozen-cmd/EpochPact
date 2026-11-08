# Editor-only Q/E rotation — 0.1.2-dev.61

Q rotates the selected green copy left by 15 degrees; E rotates it right.
Holding either key repeats every 110 ms. Left/Right remain aliases. Pressing
both directions cancels rotation, and an alias plus its letter produces only
one step. R has not been reassigned.

The existing editor input lock (`EpochInputManager.forceDisableInput`) keeps
gameplay skills from consuming the editor's input. Its previous value is
restored on Done/Cancel. No key-binding file or operating-system-wide input
setting is changed. F6, blocked pointer areas and an unfocused game pause the
editor's input. Rotation samples are drained even without a selection, so
an old Q/E tap cannot be replayed on the next selected object.

The page instructions and the in-game toolbar now describe Q/E and Up/Down.
The existing dynamic text measurement determines the hint's height.

## Verification

Last Epoch 1.5.4, Windows x64; only the isolated offline EpFeedbackTest clone.

- All 27 native test programs passed, including 84 Hideout rule assertions.
  Rotation tests cover direction, hold timing, simultaneous opposite keys,
  arrow aliases, short taps and a fresh repeat state.
- Nine Python design/package tests passed.
- The private run `rotation-29` fed Q/E and arrow samples into the actual
  selection rotation action. The green copy rotated, opposite inputs were
  neutral, an empty selection did nothing, and F6 blocked rotation.
- The actual game input flag was false before editing, true while editing
  (including while F6 was open), and restored to false afterwards.
- A local inspection of the installed game's native `EpochInputManager.Update`
  verified that its `forceDisableInput` branch skips `HandleAbilityInput` in
  this game build. No new skill hook was necessary.
- Actual toolbar captures and bounds checks at 1280×720 and 2560×1440 showed
  zero overlapping or out-of-bounds buttons with an object selected.
- Run 29 closed normally with exit code 0. All 68 original save files were
  restored and byte-verified; real preferences were unchanged.

Evidence: `research/hideout-native-dev61.json`,
`research/hideout-python-dev61.log`, and
`research/hideout-editor-2026-11-07/rotation-29/rotation-checks.json`.

The key-sample fixture replaces desktop key polling only for this private
check; it is excluded from the player DLL. Physical keyboard delivery was
not automated. The existing focus/rearming rule tests cover input gating;
these results do not claim a physical keyboard test on every key layout.

## Final player package

The final normal player DLL passed `player-31`: all five templates (60 objects
each, zero failed models), actual thumbnails, add/rotate/cancel, the editor
toolbar and normal layout persistence. The private rotation fixture, renderer
audit and UI QA commands were absent from that DLL. The last input-polling
ordering adjustment was rebuilt before this final run.

The game closed normally with exit code 0, all 68 original save files were
restored and byte-verified, and real preferences were unchanged. The exact
tested DLL is now installed, with a dev.60 backup. The game is closed.

- Package: `dist/hideout-dev61/EpochPact-0.1.2-dev.61-InGame.zip`
- Eight allowlisted entries, verified against the payload and manifest.
- ZIP SHA-256: `e1d484a37a46ad0a41c62f06874ac430bee24b280034c3d27a3e244151408067`
- DLL SHA-256: `9e1f4ed6871c2b4341e2711e4b0976d09a8590d97f1f3717f9a31c8afdc9a2a5`
- Install receipt: `research/hideout-dev61-install.json`
- Final smoke: `research/hideout-editor-2026-11-07/player-31/player-smoke.json`
- Shutdown/restore: `research/hideout-player-finish-31.log`

No GitHub or Nexus upload was performed in this task.
