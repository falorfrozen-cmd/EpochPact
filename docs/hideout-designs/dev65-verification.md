# Hideout editor polish — dev.65

Local player build, 8 November 2026. Not uploaded to GitHub or Nexus by this task.

## Changes

- Moving a gate preserves every unselected gate's authored position and native instance. The old path rebuilt all gates and sampled their positions against the previous gate obstacles. Hideout coordinates now stay exact; same-membership edits update only changed transforms and interaction bounds. Deliberately grouped objects still move together.
- **Add objects** opens a dedicated **Hideout Builder**, without the main F6 sidebar/settings. Objects and Placed objects have search, filters, sorting, inspection previews, favorites, pagination, Move/Delete/Restore/Unlock and Undo/Redo. Add & place returns to the world with the existing green ghost.
- The entry button reads **Edit Hideout**.
- Done, unchanged Cancel, object-only Cancel and same-ground history reconcile existing instances instead of recreating the floor. Actual ground/template changes can rebuild; restoring a deleted object necessarily recreates that object.
- A small gold/jade house emblem below the minimap requests Hideout travel without opening F6. It follows native minimap scale, stays left of native map controls and clear of the quest title. Editing, the main window, full overlay map, online/unready/dead/transition states block it. There is no whole-scene scan.
- Travel retains a requested local warp until the NavMesh is ready, with owner/scene/death/timeout guards. Cross-scene requests allow 120 seconds for slow storage; local warmup remains bounded at 10 seconds.
- Async Builder redraws retain the actual search input/caret, invalidate old result hits and stale page responses, cancel immediately when closed, and retain the browser state through text-scale/window rebuilds. A placed-row action explicitly targets its row/group instead of an unrelated previous Ctrl-selection.

## Verification completed

- Full normal player build, then final scoped recompilation of changed translation units. Final normal payload SHA-256: `732a32f1ab3beb9afcfb996830287219366c0a404122d80a0800293065bb041c`.
- **28 freshly built argument-free native programs passed**, including HUD placement **535/535**, hideout/history/tools **114/114** and monolith **95/95**. The parameterized localization fixture is not counted as an argument-free program.
- **35 Python tests passed** for designs/catalog geometry, assets, player packaging, isolated live-session safety and capture. A separate final packaging-only run passed 3 tests.
- Final private-build real-game session **55**, disposable offline **EpFeedbackTest**, passed seven editor/Builder/shortcut checks and five focused Builder checks, plus three immediate-open/close cycles. No physical mouse/keyboard or CUA input was sent.
- Real gate positions, native gate instance IDs, hall/mesh instance IDs and build counters were compared, rather than relying on screenshots alone. One door moved; all other door positions and instances stayed unchanged. Done/Cancel retained the floor. Cancel restored a completed move without replacing surviving decor.
- Actual Builder actions passed: Move with unrelated Ctrl-selection, Delete only target, Undo/Redo with dialog remaining open and floor preserved, cancel/delete unplaced additions without a phantom history step, search caret retention, Back/F6 lifecycle and 100%/125% text-scale rebuild with query preserved. Panel fault/soft-fault counters remained zero in the final run.
- The actual minimap-bound shortcut was visible outside the hideout and returned to the hideout. Final screenshot verified native-sized emblem clear of the quest title.
- Real-game session **54** also applied all five dev.64 designs: each had zero failed/pending models, remaining source scripts, unreachable services/gates and visual bounds outside the room. All five template previews became ready. NavMesh raster clearance is recorded in `redesign-results.json`; its minimum sampled *player-centre* width is 0.44 units, after the native agent footprint and conservative raster allowance. This is not a claim of 0.44-unit physical wall spacing or the earlier planned approach widths.
- Actual captures in these sessions were **640×480**. Native policy tests cover additional screen/HUD/map sizes; higher-resolution gameplay captures were not obtained. Physical gamepad interaction and every possible model from the 6,134-entry catalog were not retested.

## Shutdown failure, kept separate

Sessions 52–55 exited with **0xC0000005**. Sessions 53–55 used the game's actual `Application.Quit(0)`; this was not a forced termination. Therefore normal shutdown is **not a passing result**.

A separate control temporarily removed EpochPact's local version.dll, verified the mod was not loaded, launched offline without selecting a character, and also exited with **0xC0000005**. The control reached only the splash screen; an initial stale-log readiness match was corrected in the evidence. The exception was the same null read in `UnityPlayer.dll+0x109AD1`, in Windows UI Automation provider teardown. This reproduces the failure without EpochPact; it does not prove that every possible mod-related shutdown issue is excluded. No engine patch, global accessibility change, unsafe hook fallback or forced termination was introduced.

See `research/hideout-editor-2026-11-08/vanilla-shutdown-control/INVESTIGATION.md`, its dump mapping and preserved evidence. [Official Unity release notes](https://unity.com/releases/editor/beta/6000.7.0b8) document a similar Windows UI Automation shutdown fix; the exact upstream issue mapping remains an inference.

After every owned private session the original **68 save files were byte-verified restored**, real preferences were unchanged, and the original runtime was restored before any intentional final install. Personal hideout layouts and saved templates were not replaced.

## Package and evidence

- ZIP: `dist/hideout-dev65/EpochPact-0.1.2-dev.65-InGame.zip`
- ZIP SHA-256: `f13a7bf78f51fdfc8e467a0f8d7c3d48c536191b42537ea98d8abb5a9a2d3a12`
- Exactly eight allowlisted entries; only `version.dll` is runtime. Archived bytes match the runtime manifest and SHA256SUMS. No test programs, SDKs, logs, saves, debug fixtures, game models or rendered assets are distributed. Private art/disposable-character fixture markers are absent from the normal DLL.
- Final editor evidence: `research/hideout-editor-2026-11-08/polish-55/` (`polish-checks.json`, `builder-checks.json`, `rapid-close-check.json`, actual screenshots, restoration manifest).
- Design navigation evidence: `research/hideout-editor-2026-11-08/polish-54/redesign-results.json`.
- Native/Python/package records: `research/hideout-editor-2026-11-08/native-tests-dev65.json`, `research/hideout-python-dev65-final.log`, `research/hideout-package-dev65.log`.
- Before-edit source backup: `../backups/hideout-dev64-before-editor-polish`.

## Literal player payload and final install

Session **56** used the literal normal `version.dll` extracted from the ZIP above, with the same SHA-256 as the final installed DLL. Verified connection, disposable offline-character loading, normal F6 window/Hideout page, cross-scene Hideout travel, loaded objects and editor enter/Done passed with zero panel faults/soft faults. No private fixture commands were present or used in this smoke test. Its separate normal-close attempt also failed with **0xC0000005**, and is not counted as a passing shutdown.

The 68 original saves were restored and byte-verified again; real preferences were unchanged. With the game closed, only the installed `version.dll` was intentionally replaced by the normal dev.65 payload. Installed bytes equal the archived runtime bytes. The previous dev.64 DLL is backed up at `C:\Users\falor\EpochPactLab\backups\hideout-dev65-before-install\version.dll` with SHA-256 `4737635210cb2f1aee25a534fa722c9ac3721bd9c92f8abf06c0046147a1202c`.

Literal-payload evidence: `research/hideout-editor-2026-11-08/player-smoke-56/smoke-result.json` and restoration manifest. Final installation, ZIP hash and changed-source hashes: `research/hideout-dev65-install.json`. The game is closed; no source push or release upload was performed.
