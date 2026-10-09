# Monster density

## Monolith control

Open **Monolith → Echo modifiers → Monolith monster density**. The slider accepts
1–5× (including fractions) and defaults to 1×. Use **Apply changes** before entering
the next Normal or Empowered Echo; already generated packs remain unchanged.

This is the same session setting as **General settings → Monster density**. Both
controls share the draft and applied value, and eligible campaign packs are also
affected. It is not a separate Echo-only multiplier. Restarting the game resets
it to 1×. Opening the page or moving the slider sends no density command.

The UI uses the existing native `density` backend and adds no native hooks. Its
activation/reset was verified through the real UI and IPC on 2026-10-09; that check
did not start a new Echo. Evidence: `research/live/monolith-density-ui-20261009/`.

## Native behavior

`density 3` multiplies the average size of future regular enemy packs by three.
`density 1` switches it off and removes the generation and telemetry hooks.
`density` prints the state; `densityread` takes a one-time snapshot of scene spawners,
their generation queues and spawned-actor lists through background IPC.

```text
py -3 tools/le_session.py cmd "density 3"
py -3 tools/le_session.py cmd "densityread"
py -3 tools/le_session.py cmd "density 1"
```

The accepted multiplier is 1–5, including fractional values. The setting lasts for
the process, across zone transitions, and defaults to off after restarting the game.
Existing generated packs are unchanged. Enter/reload a combat zone after enabling
it for full coverage. This changes pack sizes; the locations and number of pack
spawners are determined by the game.

The game rolls `Spawner.numberToSpawn` with `percentVariance` inside
`GenerateEntitiesInternal`, then constructs a `MonsterGenerator`. Density changes
the mean only for that original call and restores it afterward, including exception
unwind. Reentrant generation of the same spawner does not compound the multiplier.
The original RNG, rarity selection, placement, emergence, level and loot code run.
Rarity can adjust the final count, so x3 does not guarantee precisely three times
the number of actors in every individual randomized pack.

Exclusions are based on runtime metadata and game enum names:

| Spawn | Behavior |
|---|---|
| Normal enemy, average count above 1.5, Default or WaveSpawner context | Eligible |
| Single/empty spawn | Unchanged |
| Boss, miniboss, minion, ally, friendly neutral, container, special actor type | Unchanged |
| Lone boss, champion, omen, nemesis, harbinger flags | Unchanged |
| forceGood spawner | Unchanged |
| Continual summons, twinned or unknown context | Unchanged |
| Missing ActorData, nonfinite values, unexpected variance | Unchanged |
| Online or unreadable environment flag | Refused; no scaling |

The increased pre-rarity count is bounded at 128 including the configured maximum
variance. An existing pack already at/above that bound passes through unchanged.
The game can subsequently adjust counts for rarity; this is not a universal live
enemy cap. The density command refuses missing layout/exclusion metadata rather
than writing with guessed offsets.

`generated packs` and `planned actors` come from actual newly constructed generators,
not merely the input-field change. `observed spawns` counts increases of
`Spawner.spawnedActors` in the game's spawning path; `completed packs` records
`FinishSpawning`. `SpawnOneImmediately` returns queue completion, not a success flag
for one actor, so its boolean is preserved and never used as a monster count.

Telemetry retains up to 256 spawners with GC handles while their packs finish.
Completion/disabling releases handles; destroyed sources are pruned on generation.
If the bound is reached, the oldest record is evicted and the eviction count is
reported. Spawn observations can then undercount; the production multiplier keeps
working. Counters are cumulative for the process. There is no per-frame scene scan.
Snapshot spawned lists can include dead actors; they are not a living-enemy census.

Background live testing can use the research `waypoints` / `travel <scene>` commands
for an active unlocked waypoint, or `exits` / `exit <scene>` for a ready normal zone
exit. Exits run the game's ConditionHandler checks and TryTriggerInteraction; they
do not unlock quests or alter condition flags. A waypoint listed in saved unlocks
can still be inactive in the current map, in which case travel refuses it.

Stat-editor completion, the active character bonuses and its validation remain in
GEMINI.md, research/findings.md and docs/stat-editor.md.

Live verification on 2026-10-05: the owner confirmed density in combat. The core log
recorded a pack mean of 2.7 -> 8.1 at x3 with the original mean restored, a generator
queue of 9, and completed packs of 8/8, 9/9 and 9/9 observed/planned actors. There is
no paired x1 baseline. Native density rules passed 37/37 tests; town special spawns
were excluded and x1 removed all three hooks successfully.
