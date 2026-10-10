# Responsiveness and attribute regression fix — 2026-10-08

Verified with the player DLL on Last Epoch 1.5.2. Source and runtime evidence are
local; this is not a claim that every combat stat and every unrelated feature has
been exhaustively exercised.

## Corrected behavior

- Campaign completion keeps normal quest/objective events, XP, gold, passive and
  idol rewards, level-up events and the game's save path. It processes one quest
  or level per frame and makes the required backup before any mutation.
- The native quest display was refreshed 520 times during the test campaign.
  Those nested refreshes now run once after the explicit bulk operation.
- CPU sampling found Gpp's analytics queue repeatedly serializing its entire
  contents on the main thread. A temporary, thread-local scope omits analytics
  generated synchronously by our bulk quest/level calls. It does not suppress
  ordinary gameplay events, rewards, saving or all telemetry globally. Both
  temporary hooks are removed when the operation finishes.
- The game ignores Increased/More entries for its five core attributes. Owned
  percentage bonuses are translated into the flat contribution the game actually
  consumes, rebased when native attribute calculation runs. Reset removes only
  the mod's contribution. All Attributes offers Flat bonus only.
- Stat reads use a cheap settled offline identity check rather than fetching all
  quests, scanning/opening maps and reading a full player snapshot.
- Current bonus summarizes all applied contributions, even while editing a
  different bonus type. Reopening a stat selects its applied nonzero mode instead
  of showing a misleading zero flat bonus. The actual game value remains visible.
- Connection refresh invalidates another character's cached action state. Campaign,
  Monolith and CoF panels fetch their needed live data when eligible; disabled
  buttons explain the missing requirement.
- Unity continues processing commands when the UI has focus. No research capture
  watchers are enabled in the installed player build.
- IPC packets use atomic claiming and nonce-matched replies. File-lock retries
  retry publication/reading, never a game mutation. Legacy clients still work.
- Save backup names are unique even for two launches in the same clock tick.

## Actual live evidence

All progression mutations used a newly backed-up isolated character, starting at
level 8. The owner's current level-55 character and shared files were preserved.
After testing, all 54 original save files matched their pre-test SHA-256 hashes.

| Measurement | Before final analytics fix | Verified fixed run |
|---|---:|---:|
| Complete 84 rewarded quests and raise level to 55 | 25.20 seconds | 4.08 seconds |
| Longest measured between-step game frame gap | 379.20 ms | 53.05 ms |
| Mean between-step frame gap | 177.09 ms | 22.25 ms |
| Full stat read, adapter + IPC | — | 121.91 ms mean, 174.34 ms max (20 reads) |
| All-waypoint action | — | 2.28 seconds |

Callback timings alone are insufficient: the test explicitly rejects a campaign
frame gap above 100 ms or total duration above 10 seconds. A 53 ms worst frame is
not a promise of zero stutter on every machine.

Live checks confirmed +100% for all five core attributes, repeated application
without accumulation, flat/percent composition, reset to the original values and
the game's visible Intelligence display changing 4 → 8. Campaign rewards were
15 quest passives and 8 idol unlocks. Repeating the action did not add rewards or
XP. Normal game close/restart preserved level 55, XP, rewards and all actual
waypoints. No save-changing actions run when the UI opens.

Evidence: `docs/responsiveness-verification.json`; screenshot evidence is stored
with the other UI evidence. The JSON is a transcript, not a mocked test result.

## Validation and delivery

- 72 Python adapter/IPC/recovery regressions and 9 stat presentation checks.
- Native hook, XP, stat, density, Monolith, CoF and loot/crafting tests were run
  during this repair. Player core builds successfully without EPOCHPACT_RESEARCH.
- Tests covering other features may use fakes. Only the live checks above prove
  game behavior in this verification session.
- No repository reset, cleanup, stash, push, publication or game-data patching.

The final player DLL was installed and its SHA-256 matched the source build.
The original UI at port 17884 was verified with Falor (save 0, level 55): campaign
actions became enabled after automatic live refresh, and Intelligence read 8 with
the user's restored +100% contribution. Reopening and switching the editor's
bonus type preserved the correct current-bonus summary. No additional campaign
actions were applied to the owner's character during this delivery check.

`tools/responsiveness_live_check.py` is a local verification helper, not a player
background process. Its fixture preparation requires this machine's historical
snapshot and always writes that baseline into a NEW isolated test character.
It never replaces the owner's progress with that historical snapshot.
