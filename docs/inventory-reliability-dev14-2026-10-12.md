# Inventory shortcut reliability — 0.1.2-dev.14 candidate

Status: built and verified offline; final live-game validation is pending.
The public dev.13 release has not been replaced by this candidate.

## Changes

Stash, Stash all and Vendor now use the existing EventSystem main-thread
dispatcher instead of depending on a separate CameraManager.Update hook. The
original UI update still runs once per invocation. The owned callback runs on
idle frames as well as command frames, polls inventory placement every 250 ms,
and stops before runtime cleanup. No gameplay or save action is queued by opening
the inventory. Existing offline, identity and serial-action checks remain.

The placement uses measured inventory, grid, native controls and currency bounds.
Ordinary values keep a single footer row beside Transfer/Sort. Wide values expand
the native currency group to fit its full text while retaining its right edge.
If the controls and balances cannot share a row, the measured bottom margin holds
two rows: controls and shortcuts above, balances below. Fonts and full balance
values are retained. Owned native size/position changes are restored on cleanup.
Layouts that cannot fit safely remain guarded rather than spilling into another
game panel.

The loader change independently committed as `2ba920e` is included in the latest
candidate bundle. Its native game-start behavior still requires separate live
verification; the tests below do not claim to establish that behavior.

## Evidence and limits

| Check | Result and scope |
|---|---|
| Python regression suite | 237 passed. |
| JavaScript regression suite | 126 passed. |
| Latest core native tests | All 17 programs exited 0; dispatcher 24/24, lifecycle 8/8, hook engine 50/50 including concurrent install/remove. |
| Footer geometry | 120 scale/window/currency/translation combinations passed, including measured large-font control bounds and maximum-balance rows. This is mathematical layout validation. |
| Frozen candidate installer and interface | 21 checks passed: startup/config, fresh install, read-only payload, update/recovery backups, custom Unicode path, ownership/build refusals and HTTP authorization. Fixture game executable was never run. |
| Extracted ZIP connection flow | 4 checks passed with the production native IPC parser and simulated game data: stale read-only announcement, restart/slow reads, core mismatch and unchanged current UI resources. |
| ZIP contents | Every entry matched its manifest. Manual DLLs match the EXE bundle. Adjacent CLR config retained. No test core, testhost, SDK, private logs, saved characters or game binaries included. |
| Test-state restoration | 54 original save files and 3 original installation files restored and hash-verified before pausing. Three isolated character files archived. |

Earlier isolated real-game sessions demonstrated automatic creation of all three
shortcuts before any forced layout helper, inventory reopen/hide behavior, and
Stash/Vendor toggle callbacks. Maximum Gold and Ancient Bones were read back as
2,147,483,647 and Favor as 999,999. Scales 75/100/125/150 were exercised. However,
visual inspection found native Gold clipping, and then a native Favor/Sort
overlap at 150% after the width-only fix. Those earlier screenshots are **not**
passing evidence for the final two-row layout. The extended live harness now
checks native currency children against each other, the grid, controls, shortcuts
and inventory bounds; a saved failing layout is correctly rejected offline.

The original reported missing-button failure was not deterministically reproduced
in the separate baseline character. Removing the extra camera-hook dependency is
an architectural reliability improvement, not proof of every player's root cause.

Still pending: final two-row rendering and click targets in Last Epoch, large and
ordinary balance transitions, non-empty Stash all transfer, production-core
desktop close and full game exit, and the new loader's actual startup path.
Earlier successful exit-code-0 sessions do not clear the previously recorded
intermittent UnityPlayer `0xC0000005` limitation. No antivirus, browser or Nexus
clearance is claimed for this candidate.

The user requested a pause before further game testing. A previously queued
launch ran before cancellation; it failed to enter a character. No new live
verification is counted from that attempt. Subsequent checks used only isolated
fixtures and did not launch Last Epoch.

[Candidate hashes and machine-readable results](inventory-reliability-dev14-2026-10-12.json).
