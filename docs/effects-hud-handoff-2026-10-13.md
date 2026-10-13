# Effects HUD handoff — 2026-10-13

> **Taken over by Claude Code the same day and released in 0.1.2-dev.23**
> (commit `e0560ca`, 2026-10-13). All five remaining steps below are done:
> full suites rebuilt and green; the final test core ran in three live sessions
> on a fresh isolated character; the player DLL, EXE and ZIP were built from
> the final sources; the compiled package passed 24/24 player checks (including
> Install update) and 4/4 connection checks, and Windows Defender found no
> threats. Two defects were found and fixed: at HUD size 0.7 the title, names,
> timers and stack counts were not drawn (text boxes scaled below the readable
> text size), and descriptions were cut to one line. The rules test used
> `assert`, which the native build's `/DNDEBUG` removes, so it checked nothing;
> it now uses explicit checks (750 layout cases). Not verified in the game:
> startup failure reporting (it cannot be triggered without breaking game
> data), hiding while a menu or the world map is open, and switching off on a
> character change; the inventory does not hide the HUD. Details in
> [effects-hud.md](effects-hud.md) and
> [hud-amber-validation-2026-10-13.md](hud-amber-validation-2026-10-13.md).
> The sections below are the original handoff, kept as written.

## Status

Implementation candidate; **not a completed player release**. Work stopped at
the user's request to hand over to Claude Code. Repository branch: `feature/dev16`.
Base commit: `88462ef` (0.1.2-dev.21). Version metadata remains dev.21. No new EXE
or ZIP was produced for this HUD. Do not distribute the intermediate player DLL.

## Implemented

- Native Unity HUD visible while playing, controlled through Display & Camera
  → Buffs & debuffs in game. Off by default; offline and character scoped.
- Live player buff/debuff names, descriptions and sprites from the game's own
  BuffsUIManager/BuffUIEntry objects. No fixed six-icon or 24-icon catalog.
- Target effects from the game's displayed target, with ActorSync fallback for
  separate visual/gameplay objects. Target loss clears previous rows.
- Stack counts, known remaining durations, timer bars, untimed `—`, row limits
  and overflow counts. Timers use live receivers rather than definition defaults.
- Scale, row count, player/target visibility and descriptions controls, using
  existing serial IPC and nonce + PID:FILETIME verification.
- Metadata caching, reused rows, 250 ms sampling, bounded row creation and
  scene discovery only when the HUD owner changes. Graphics do not intercept clicks.
- Same-character area transitions retain settings; character changes disable
  the HUD. Closing the desktop app disables a HUD it enabled.
- Window screenshots temporarily use thread DPI awareness to avoid cropping
  physical game pixels on a scaled Windows desktop; the prior context is restored.

Main files: `native/core/effects.cpp`, `effects.hpp`, `effect_rules.hpp`,
`native/tests/effect_rules_test.cpp`, module wiring in `core.cpp`, `commands.cpp`,
`convenience.cpp` and `native/build.bat`; controls in `tools/ui_bridge.py`,
`ui/app.js`, `ui/catalog.json`, `ui/locale-en.json`, and the catalog exporter.
Screenshot fix: `tools/support_report.py` and its tests.

Existing inventory lower-row placement, performance fixes, loader changes and
dev.21 affix display improvements were preserved. Do not replace current files
with older copies.

## Verification already recorded

- An intermediate revision passed **269 Python tests, 156 JavaScript tests and
  19 native test programs**. Logs: `build/effects-20261013/`. This is not a full
  suite result for the final source revision.
- Later focused support-report tests passed (8 tests), including DPI restoration
  on failure and compatibility when the Windows API is absent.
- Live game: Last Epoch **1.5.2.1**, isolated `EpEffectsTest` character, save ID 6.
  Actual Chill/Slow/Haste/Frenzy effects and natural Preparation/Grace Period
  entries displayed. Additional mixed-effect rows used native UI fixtures with
  real game sprites/definitions; their natural combat triggers were not tested.
- Target fixture on Pack Grole verified real receiver countdowns for Chill/Slow,
  an unknown timer for a UI-only Ignite entry, ActorSync lookup and target clearing.
  This was programmatic targeting, not exhaustive mouse-hover/boss encounter QA.
- Normal travel to F10 retained the HUD and performed one new owner discovery.
- Observed steady sample maxima were roughly 0.3–0.7 ms on this PC in these scenes;
  initial binding/creation was about 28–34 ms. Broader combat performance is untested.
- Observed normal game closes returned **0x00000000**. This does not establish
  that every historical shutdown issue is resolved.
- `research/live/effects-20261013/full-native-hud.png` is the full-size native HUD
  evidence image. It includes fixture entries and is not an AI mockup.

## Final changes still needing verification

The last live-tested core SHA-256 was
`8cc4b3184b596a9456b9dcd34d14552bfdb5fc07a091f142964af30d165b10cb`.

After that pass, target stack-label fallback and synchronous startup validation
were adjusted. The latest compiled **test** core is
`build/effects-20261013/test/test/EpochPact.Core.dll`, SHA-256
`7f94aea89b302769b3426f92640cb476eddcb156517de92f77c1bbcba87f0c5d`.
It has not been reloaded in the real game. Some long descriptions still ellipsize
instead of showing the entire sentence; improve/verify wrapping if required.

## Remaining steps

1. Inspect the committed candidate, then rebuild and rerun the full automated
   suites, including native layout/rules tests after the latest edits.
2. Run the final native test core on a newly isolated character. Verify startup
   failure reporting, capped stack labels, description layout, scaling, normal
   targeting, area changes, UI visibility and normal close. Respect any current
   user restriction on launching the game; do not change the main character.
3. Rebuild the **player** native DLL from final sources. The earlier player build
   under `build/effects-20261013/player/player/` predates important live fixes.
4. Use the next release version, build EXE and native runtime together, preserve
   adjacent `EpochPact.exe.config`, and create the normal player ZIP. Check that
   manual DLLs and EXE-bundled DLLs match and exclude test hosts, logs, saves,
   SDKs, game assets and private verification files.
5. Verify compiled startup, install/update and normal desktop shutdown separately
   from live gameplay. No new antivirus scan or Nexus clearance was obtained.

## Test cleanup completed

The game is closed. The driver restored **54 original save files**, verified
their original bytes, and archived only three owned test-character files. It
also restored and hash-verified the original installed `version.dll`, core DLL
and installed-build metadata. Local reports:
`research/live/effects-20261013/save-restore.json` and `runtime-restored.json`.

Local helper scripts are `research/live/effects-20261013/driver.py` and
`quick-build.py`. They and private evidence remain local, outside this commit.
Do not rerun `prepare` using the restored old manifest; create a fresh isolated
test setup before resuming. The quick builder recompiles only effects.cpp and
must not replace a full build when other native sources have changed.
