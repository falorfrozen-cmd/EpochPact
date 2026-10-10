# Monolith / endgame backend

Offline characters only. UI and permanent character profiles remain in the owner's
separate UI project. `tools/monolith_backend.py` exposes the native actions for that UI.

## Commands

All persistent actions require the loaded save id reported by `monolithread`.

| Command | Effect |
|---|---|
| `monolithread` | JSON: loaded character, scene, editable state, selection, ten timelines, difficulty unlocks, corruption limits, stability limits and existing runs. |
| `monolithunlock <id>` | Unlock normal and Empowered difficulties through `MonolithProgressManager.unlockTimeline`. Already unlocked difficulties are skipped. |
| `monolithselect <id> <timeline> normal\|empowered` | Select the requested difficulty through the game's Monolith panel. Native IPC requires its rest/reward context to be loaded. The Python/UI action travels to Traveler's Rest through the normal unlocked waypoint first; it does not start an Echo. |
| `corruption <id> <timeline> normal\|empowered <integer>` | Set that run's corruption and recalculate its corruption mod/shared progression. Does not regenerate the echo web. |
| `stability <id> <timeline> normal\|empowered <integer>` | Set stability through `MonolithRun.AddStability`, independent of the gain multiplier. |
| `stabilitymult <1–100>` | Scale positive stability gains for runs owned by the loaded offline character. Losses remain unchanged; x1 removes the hook. |
| `factionread` | Read faction ranks, current membership/favor/reputation, prophecy slots and lens data. No faction writes. |
| `sessionread` | Read the client state and whether a transition is in progress. |

For example, after reading the **current** offline id, a UI can call
`monolith_backend.corruption(save_id, 7, True, 300)` to set Reign of Dragons' Empowered
run to 300 corruption. It must first verify that the selected difficulty is unlocked.

## Limits and persistence

Persistent changes are allowed in End of Time (`EoT`), Monolith Hub (`MonolithHub`) or
Traveler's Rest (`M_Rest`), after the client's normal transition reaches InGame.
Changing corruption inside an echo would leave already spawned actors/rewards at the
old values, so it is refused. New values take effect on the next echo.

Current runtime assets specify normal corruption **0–50** and Empowered corruption
**100 and above**. The backend also prevents the UInt16 serialization used by
`MonolithProgressManager.timelineCorruption` from wrapping at 65536: its supported
maximum is 65535. This is a storage boundary, not a recommended farming value.

Normal stability caps range from 500 to 1000; Empowered caps range from 1600 to 1800.
Limits are read from each difficulty's asset. Scaling also prevents Int32 addition
overflow and retains the game's own cap in `AddStability`.

`TryGetRun` is used first. `getNewRun` is only used when the requested run does not
exist, because calling it on an existing run resets its progress. Unlocking timelines
does not grant boss kills, blessings, campaign rewards or faction ranks.

Before each mutation the backend saves current run data into CharacterData and makes
the existing progression snapshot: disk saves plus live character, stash and global
data. Mutation errors include the snapshot path. Use `progression_backend.undo` with
the expected character id to close the game normally and restore that snapshot.
No active character save is edited directly.

Corruption/stability/unlock state uses the game's normal saves. The stability
multiplier is a session setting and resets to x1 when the process restarts.

## Runtime implementation

Methods and fields are resolved by name; these RVAs are evidence for the installed
build, not constants compiled into the feature:

| Method | RVA |
|---|---|
| `MonolithRun.AddStability(int)` | `0x12ED380` |
| `MonolithRun.updateCorruptionMod()` | `0x12F1F30` |
| `MonolithRunsManager.TryGetRun` | `0x12F3840` |
| `MonolithRunsManager.getNewRun` | `0x12F4990` |
| `MonolithRunsManager.saveRuns` | `0x12F63C0` |
| `MonolithProgressManager.UpdateCorruption` | `0x12E7620` |
| `MonolithProgressManager.unlockTimeline` | `0x12E9D60` |
| `MonolithPanelManager.open` | `0x12E3950` |

`EchoWeb` has no runtime corruption setter. The backend writes its validated Int32
field on the main thread, then runs the normal mod, progress and save methods.

Normal and Empowered echo webs keep distinct corruption values. The auxiliary
`MonolithProgressManager.timelineCorruption` entry records the last update for that
timeline through the same API used by Shade and Sanctuary of Eterra. The game's
shared highest-ever corruption remains a historical peak when a run is lowered.

The game queues timeline notifications in `Dictionary<TimelineID,byte>`. Unlocking
normal and Empowered for the same timeline in one frame otherwise throws on its
second `Add`. The backend removes only that timeline's pending notification before
the normal unlock method adds the final difficulty. It does not suppress managed
exceptions or replace unlock progression.

## Verification

`native/build/monolith_test.exe`: 22 arithmetic/bounds checks. Python IPC/recovery:
17/17. Existing hook 48/48, XP 24/24, stat-key 33/33 and density 37/37 passed.
Isolated live evidence is in `research/live/monolith-live-verification.json`:

- All 20 normal/Empowered difficulties unlocked; second unlock returned zero.
- Normal corruption 42/stability 120 and Empowered corruption 300/stability 1250
  persisted through a normal game close/reopen.
- At x5, `AddStability(+25)` became +125 and `-10` remained -10; manual edits
  bypassed the multiplier. 1690 plus a multiplied gain clamped to the 1700 cap.
- Invalid difficulty bounds, UInt16 wrap, wrong loaded id and malformed integers
  were refused. x1 removed the hook (zero refused/faulted hook calls).
- Normal timeline 3 and Empowered timeline 7 selection worked through the game
  panel. Edits to a selected run refreshed its data.

These are actual game-method/hook checks on EpMonolithTest; a natural echo was not
played. Undo restored Empowered 320 corruption/1260 stability, confirmed by another
reopen. All 54 original save files were then restored byte-for-byte and three test
files archived. The game is closed; Falor remains id 0, level 8. The evidence file
records recovery and cleanup, including the installed/built DLL hash match.

`tools/monolith_live_check.py prepare` creates EpMonolithTest with a separate
character-found stash, after a full original byte backup. `restore` only accepts the
recorded original hashes and known test filenames, archives the test saves and
verifies all original files byte-for-byte. Both operations require the game closed.

Client loading helpers now check `ClientStateManager` state/active transition:
`playoffline` waits for settled Login; `loadname` waits for settled CharacterSelect.
An instantiated character preview in Login is not evidence of a loaded game zone.

## 2026-10-09 panel entry correction

`select` and `focus_echo` prepare Traveler's Rest on an explicit button action, wait for its live manager and recheck the loaded save id before opening the panel. Direct native selection/focus refuses when this context is absent. Read/refresh, unlock, corruption and stability edits never travel automatically. This prevents bypassing the rest scene needed by native completion rewards and return placement.
