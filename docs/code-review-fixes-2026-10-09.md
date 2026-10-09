# October 9 code review: assessment and fixes

The findings were checked against the current sources before editing. This
report separates automated regression checks from live game evidence.

| Review item | Assessment and resulting change |
| --- | --- |
| Untracked source | Valid. All existing UI, backend and native sources were captured in baseline commit `5388912` on `fix/review-20261009` before corrections. Build output, dumps and live saves stay ignored. |
| Backup and log growth | Valid. New explicitly managed progression snapshots retain 30 entries; launch backups retain 20. Legacy owner/test backups are preserved. Cleanup rejects linked trees and checks the canonical parent before recursive removal. Nonce replies no longer append to `out.txt`; legacy replies still do. Startup rotates `out.txt` above 1 MiB and `core.log` above 5 MiB, with one previous log. |
| Cooldown ownership / possible N² | Valid risk; the original N² effect was not established in live gameplay. Removed the global `ChargeManager.getCooldown` detour. Only the local player's `OnUpdateTick` delta is scaled, after checking both manager-to-actor and actor-to-manager ownership. |
| Monolith/CoF disk work | Valid. Both use prepare/capture on the game thread → complete backup on the IPC worker → revalidate/apply/save on the game thread. A write failure prevents application; later refusal/timeout retains the committed recovery path. Each phase repeats validation, and identity/managers must still match. CoF compares faction/prophecy state rather than all unrelated serialized character data. |
| Frame hook churn | Valid. Keep the frame hook installed. Idle frames take an atomic pending-count check and call the original update without locking the queue. Timeout cancellation and one step per frame remain. Telemetry reports hook installation count and pending jobs. |
| Fixed game directory | Valid. `EPOCHPACT_GAME_DIR` overrides Steam registry/library discovery; the default path is the fallback. Malformed library files do not prevent default-library detection. Invalid explicit overrides produce a clear error. |
| IPC read/delete race | Already fixed in the original baseline. Preserved atomic claim before read and deletion of only the claimed file. Moved this same production implementation to the filesystem helper for a real file-race regression test. |
| Mixed build artifacts | Valid. Player, research and test artifacts have independent folders and SHA256 manifests. Installation defaults to `player`, verifies its flavor/hash and records what was installed. Research/test require explicit selection. |

Broad dispatcher/render/helper refactors are deferred: they are maintainability
suggestions, not demonstrated defects, and would unnecessarily widen this repair.

## Automated verification

- MSVC x64 `/W4` builds: player and research modes.
- Native: **6,562 checks passed**: original 6,479 plus 73 transaction/filesystem/
  cooldown regressions and 10 tests of the actual frame queue and hook engine
  against a synthetic Unity update.
- Python: **103 tests passed**, including real concurrent IPC producers, Windows
  junction protection, retention, Steam library discovery, artifact validation,
  recovery-history expiry and the existing restore/adapter tests.
- JavaScript: **65 tests passed** across stat units, readiness, automatic apply,
  reconnect and preservation of unapplied drafts.

The new native regressions test phase ordering, no mutation after backup failure,
recovery-path preservation after errors, cancellation without later execution,
one permanent hook installation, native file claim races, legacy backup protection
and player-only single-factor cooldown arithmetic. These are not live combat tests.

## Live verification

[Structured evidence](code-review-verification-2026-10-09.json) records the final
installed player SHA256 and actual IPC replies on isolated `EpAllModsTest` save 6:

- Stability **204 → 205 → 204**, with native readback and committed snapshots.
- Corruption **0 → 0**, verifying the new transaction path, not a range audit.
- CoF Favor **1303 → 1304 → 1303**, preserving Reputation and normal membership.
- Cooldown x5 entered the actual local-owner tick: **0.0133156 → 0.0665778**;
  reset removed the feature hook. Individual ability durations were not retested.
- Frame hook installation count stayed **1** after three idle seconds and a new
  request; pending count was zero. Nonce commands left `out.txt` unchanged.
- The game closed normally; all **54 original save files** were restored and
  independently matched to their fresh pre-test hashes. No owner character was
  loaded. The final player DLL remains installed, with the game closed.
- The restarted player HTTP backend returned its page successfully, kept the
  existing three recovery entries and correctly reported disconnected.

The longest dispatched game-thread step was **49.485 ms**. Disk copies now run
outside that thread, but managed capture/save and full read operations can still
take multiple frame budgets. This is not a promise of zero stutter or a before/
after FPS benchmark; `maxFrameGapMs=0` here is not an FPS measurement.

Initial loading timeouts and test-harness response/transition mistakes are kept
in `research/live/review-20261009` and are not counted as passes. The first CoF
implementation's full-character comparison was too strict and refused the
request before mutation. The final faction/prophecy comparison passed live.

## Operational notes

Only backups carrying the new `.epochpact-retention` marker enter automatic
retention. Old backups are not deleted as an incidental side effect of this fix.
Expired native backups disappear from the interface's recovery list; the existing
restore validation remains in force.

Player output: `native/build/player/EpochPact.Core.dll` and `build-info.json`.
Install while the game is closed: `py -3 tools/le_session.py install`.
The root `native/build/EpochPact.Core.dll` remains a development compatibility
output; the installer does not use it.
