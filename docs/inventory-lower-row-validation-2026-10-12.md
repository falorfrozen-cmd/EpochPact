# Inventory lower action row — 0.1.2-dev.14

Stash, Stash all and Vendor now have their own centered horizontal row below the
item grid, above the game's Transfer/Sort and currency footer. This is the layout
approved by the owner in the real-game screenshot. It replaces the dev.12/dev.13
gap beside Sort and the previous floating strip. The native footer stays in its
original position.

Buttons are 32 canvas units high, with widths 100 / 118 / 100 and 15-point labels.
The grid scales uniformly to make space, keeping square cells, its top edge and
horizontal center. Unsupported bounds refuse placement. Maximum currency values
keep every digit; the currency group scales only when needed to clear the native
left controls. Cleanup restores the original grid and currency transforms.

The clean merge of `perf/stutter-fix` is commit `6150c16` (tip `2c8aa9d`). Its closed
inventory fast path, startup button hooks, cached InventoryPanelUI.OnEnable panel
and slower visible-page reads are preserved. Loader startup commit `2ba920e` was
not reverted.

## Verification

| Check | Result and scope |
|---|---|
| Python / JavaScript | 237 Python and 126 JavaScript tests passed. |
| Native regression programs | All 17 passed. The hook stress program passed 50/50, including 300 concurrent install/remove cycles with zero wrong results or failed installs/removals. |
| Placement rules | 120 window/scale/currency/translated-label combinations passed, including invalid bounds. This is part of the 17 native programs. |
| Compiled player package | 21 startup, isolated install/update, path, ownership and HTTP checks passed. They include read-only DLLs, custom Unicode paths, foreign loaders, unsupported builds, adjacent CLR configuration and no startup mutation. |
| Extracted ZIP connection | Four compound checks passed with the production IPC parser and simulated game data, covering stale read-only protocol announcement, separate reads exceeding seven seconds, restart, core mismatch and current UI resources. These are not live gameplay checks. |
| Actual Last Epoch | Steam 1.5.2.1, offline EpFeedbackTest copy. Gold and Ancient Bones were 2,147,483,647 each; Favor was 999,999. The initialized native UI scaling methods exercised 75%, 100%, 125% and 150%; all three buttons remained visible, inside Inventory and clear of grid, native controls and every currency child. No shortcut faults were recorded. |
| Actual rendering | Fresh native captures at 100% and 150% had different hashes and were inspected. Skills beside Inventory, hiding and reopening Inventory also passed. Scaling was invoked through native methods, not manual settings-menu clicks. |
| Button callbacks | Stash and Vendor each opened and closed their panels through the actual button callback. Stash all completed against an empty inventory. Non-empty transfers were not re-tested in this placement pass. |
| Closed inventory performance | Five seconds: inventory searches stayed 0 while frame updates advanced from 10,171 to 10,702. This verifies the merged idle fast path, not every possible game scene. |
| Final production core + frozen EXE | Matching player DLL connected to the loaded offline test character, with external Python removed from PATH. Compiled API Stash/Vendor toggles passed. XP x2 applied; normal desktop close returned 0 and restored `xp: x1, hook out` while the game remained running. |
| Full game close | Both this pass's test-core session and final player-core session closed normally with code 0. Earlier intermittent 0xC0000005 exits documented in dev.12 remain a known issue; two successful closes do not establish a universal fix. |
| Save cleanup | All 54 original save files restored and verified byte-for-byte; three disposable test files archived. The matching final production runtime remains installed locally, with the pre-test runtime backup retained. |

The first aggregate native run used a 120-second timeout that was too short for
the hook stress program. Its separate completed run passed; the timeout is not
counted as a pass. The first installer verification was blocked because the game
was open. Its repeat after normal game close passed all 21 checks. An initial
JavaScript glob found zero tests; only the corrected 126-test run is counted.

## Player package

| File | SHA-256 |
|---|---|
| EpochPact-0.1.2-dev.14-package.2.zip | `ad2b6316180c6808c4781c00dc765c6105277b181da099d5580ce8360c348ec0` |
| EpochPact.exe | `08f8246ef11983bd9c3f4b42c9094f995c8e2b98382ba1299404a741b12693a9` |
| EpochPact.Core.dll | `807c096b43dc6c2aee3d5244efb9a61a88f20ff006891a35530e45f84ebe2bda` |
| version.dll | `d7ca8d9cf96c3a0d17ecb7c4b297aa5d5579870be35ff3950e5b0928274352cf` |

ZIP size: 31,227,522 bytes; 963 entries, 886 runtime entries. The ZIP manifest and
every recorded runtime hash match. Manual Installation DLLs match the application's
bundled DLLs. EpochPact.exe.config stays beside the EXE. Test cores, testhost, SDK,
private logs, saves and game binaries are excluded. No new antivirus, browser or
Nexus clearance is claimed by this validation.

[Machine-readable results](inventory-lower-row-validation-2026-10-12.json).
Detailed local evidence is retained under research/live/inventory-lower-row-20261012.
