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
| `noblessing <0|1>` | No blessing choice after a timeline boss, see below |
| `bossdoors <0|1>` | Boss doors and Boss Rush in the Monolith Hub, see below |
| `questskip <0|1>` | Skip quest echoes before the boss: once a Normal run's Stability is full, its quest echoes before the boss count as completed (see below). Session setting; turned on, a full run changes at once in EoT/Hub/Rest with a recovery backup, the others when their Stability fills. |
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

## Skip quest echoes before the boss (0.1.2-dev.55, owner request 2026-11-03)

The owner: when the Stability bar is full, the side quest echoes should already be done and only the boss echo open.
How the game does it (1.5.3): `MonolithTimeline.questZones` lists a timeline's quest echoes in order, the last one is
the boss (`MonolithRun.IsBossQuestEcho`: index == Count - 1). `MonolithRun.questCompletion` counts the finished ones:
quest echo i is completed while questCompletion > i (`IsQuestEchoCompleted`), `nextQuestEcho` returns the zone whose
`requiredQuestCompletion` equals questCompletion (and its branch, for a zone that requires one), and finishing a quest
echo adds 1 (`MonolithZoneManager.OnQuestCompleted`). Empowered runs offer only the boss (`QuestEcho.GetAvailability`
answers Unavailable for the others).

`questskip 1` (control `monolith_questskip`, Monolith → Echo modifiers): after every Stability gain of a run of the
loaded offline character (the AddStability hook, shared with the Stability multiplier and installed while either needs
it), a Normal run whose Stability is at its maximum and whose boss needs more completed quest echoes gets
questCompletion = the boss's requiredQuestCompletion; the change is kept only if `nextQuestEcho` then returns the boss
zone. A route that is not linear (boss requiredQuestCompletion != Count - 1) or a boss that requires another branch is
left to the game. The game saves the run with its other progress; turned on with a full run already, the change is
made at once in EoT, Monolith Hub or Traveler's Rest through the Monolith mutation path (recovery backup, save).
`monolithread` gives `questSkip`, each timeline's `questEchoes` (before the boss) and each run's `questCompletion`.
The skipped quest echoes give none of their own rewards; the boss, its loot and the timeline completion are unchanged.

## The Boss Hall (owner request 2026-11-06)

With `bossdoors 1` the doors stand in a place of our own, in the Hub's scene around 1400, 1, 1400. It is no copied place. The ground is a mesh made at run time in the outline we choose, flat on top, falling into rock at its edge, in a material of the game (`kHallMaterialFrom`; sand now; red sand, snow, grass and plain rock also tried). The parts come one by one from `kHallParts` (a named object of the scene, its place, a turn, a size), made inside an inactive holder so their scripts never start, scripts off. `NavMeshSurface.BuildNavMesh` from the ground's and the parts' colliders says where one can walk; clicks find the ground on the same collider (`MouseManager.WalkablePoint`). A rock by the Hub's waypoint goes in, a rock in the hall comes back, the way out of a door's boss returns to the hall. Three stations open the game's panels through the mod's own commands: Stash, Vendor, Eternity Cache. If it cannot be built the doors stay in the Hub as before. Nothing is saved; a scene change removes it and the next visit rebuilds it.

## The hideout (owner request 2026-11-06)

`native/core/hideout.cpp`, F6 page Hideout. Everything in the place of our own but its ground is a list the player changes, kept in `%LOCALAPPDATA%\EpochPact\hideout.txt`: boss doors, stations and anything of the game. `ui/hideout-catalog.tsv` (made by `tools/build_hideout_catalog.py` from a scan of the game's bundles with test-only `bosstest id assetprefabs`) names about 6100 prefabs: bundle index, asset Guid, category, name. It fits one game build: the bundle count is compared, and a new game build needs a new scan. A thing goes up through the game's own `AssetBundlesManager.CreateBundleIdArrayLoadRef` (the bundle and all it depends on, counted like the game's own loads), then `AssetBundle.LoadAsset`, inside an inactive carrier so no script ever runs. A thing added is scaled down if larger than 14 units and stood on the ground. Stations wear real models: `Stash`, Annya the Blacksmith, `Cache_of_Eternity_01`; the rock under each takes the clicks with its renderers off. The ground (`hideout_rules.hpp`, `HallGround`) is a generated grid: five shapes as signed distances, flat inside, falling into rock outside. Textures mirror on grid lines, so no cut shows; for a natural texture the grid itself leans and bends so the mirror does not read, a laid floor stays straight. A texture is a material of the Hub or the first real material of a floor prefab of another map (many floor prefabs hold only `Default-Material` and cannot lend theirs). The Monolith rock pictures are mesh unwraps streaked towards their edges, so only their middle is used. A point light high above lifts the dark ones. Templates: `hideout template ready|save|load|remove|undo`; ready ones are a ground plus a door layout (`rules::BossDoor`) on the first layout; saved ones are `hideout.txt`-format files in `%LOCALAPPDATA%\EpochPact\hideout-templates`, so a friend's file dropped there shows up; before a template replaces the layout it goes to `hideout.txt.before` for Undo. A mesh is not freed with its object: `DestroyHall` destroys it. The hall's minimap: `MinimapGenerator.GetMinimapForTriangulation` fed with the ground's flat part (cells clipped along the outline), a `MinimapInformation` of our own with a fog `RenderTexture` made as `MinimapManager.LoadFoW` makes its own (random write on: `Minimap.UpdateFoW` stamps into it every frame; a plain black texture there crashes the game) and all of it shown; `Minimap.OnMapLoaded` switches to it inside the hall and back to the Hub's outside. Many floor prefabs hold only `Default-Material`; the Monolith rock pictures are unwraps with shards, so none is on the list. The mouse editor uses `MouseManager.MouseHoverPoint` and holds back `ClientMovingPlayer.MouseClickMoveCommand` and `MoveToPoint` while it is on. `hideout read|search|add|at|move|here|turn|size|lift|remove|show|hold|edit|go|reset`. Test games use `hideout-test.txt`.

## Boss doors and Boss Rush (owner request 2026-11-05)

The setting has a small ? beside its switch (F6 and desktop, owner request 2026-11-06): it tells the player to go to Fall of the Outcasts in the Monolith Hub, what the boss and Empowered doors do, and that Boss Rush is in the same area.

`bossdoors 1` puts twelve copies of the Hub's own Monolith rock (`MonolithObject`, smaller, without its quest pulses and map
icon) around the Monolith Hub's centre: ten start a timeline's boss echo, one switches Normal and Empowered, one runs all ten in
turn. The click is taken at `WorldObjectClickListener.ObjectClick`, so the rock's own event chain and timeline panel do not run.

Start: the run from `GetRun` or `getNewRun`, `questCompletion` at the boss, Stability full, then the game's own
`MonolithGameplayManager.AttemptStartQuestAsync(player, user, timeline, difficulty, last quest echo, false)`. It refuses inside
an echo, and an echo started outside M_Rest's scene (Forgotten Knight) loses its run, so a next boss waits for the player's portal
out and for M_Rest. A locked timeline starts too; `MonolithProgressManager.timelineUnlocked` answers true for the door's own
boss because `MovePlayer` looks the run up through it. `Notifications.TimelineUnlockedNotification` is skipped during a door's boss:
it throws on a repeated name, which ended `TimelineBossDied` before the loot and the completion.

The doors are free of the timeline's own progress. `questCompletion`, `questBranch`, Stability, the previous-echo flags and the Gaze of
Orobyss are kept before the start (also in `bossdoors-borrowed.txt`, so a game closed during a boss gets them back at the next
start) and written back once the player is outside the echo, whatever happened inside. Corruption is not touched: a door's boss
uses the run's own corruption, which is the minimum on a timeline never played.

A kill counts only for what the game counts it for, nothing is added for quests: the bosses drop as usual, repeated kills too.
Bosses wake when someone comes near, and a few have conditions: the player is placed before the strongest fighter of the scene
when it is more than 15 units away, or at a known place (1 arena, 3 before the guards, because its boss sleeps until they fall, 4, 8, 9).

Live (EpFeedbackTest clones, kills by test command with damage off): all ten bosses through the door path in order, from
Rush start to the hall, including locked 8 to 10 and Forgotten Knight arrivals; Empowered 1 and 2 at corruption 100.

## No blessing choice (owner request 2026-11-05)

`noblessing 1` leaves out `UIBase.OpenBlessingOptions`, the panel that opens after a timeline boss's kill, on every boss, through a
door or not. The game still rolls its options into the timeline's option slot; only `selectBlessing` from the panel gives one,
so those kills give no new blessing and the next roll clears the slot. Blessings already held stay.

## Boss doors: the way on, town portals, Harbingers (owner request 2026-11-05)

A door's boss ending in `completeRun` (questCompletion leaves the boss's value) opens a portal of type Echo next to the player
(`IPortalService.CreatePortalAsync`, one portal per player, so the player's own portal key replaces it). Only that type goes on
(`PortalLogicController.OnAttemptUse`): a Rush's next boss, or the hall. Leaving any other way is a town portal: nothing starts, the player stays where the game puts them, and the Rush door continues from the next boss, or the same
one if it lives.

The difficulty door switches Normal, Empowered and Empowered + Harbinger. The game's rule (`SetUpHarbingerBossEncounter`) makes the boss
echo a Harbinger fight with no Forgotten Knights rank, or with the opt-in and enough corruption. For a door the choice decides: Empowered
leaves that setup out, Empowered + Harbinger passes the opt-in and `ShouldBossfightBeGuarenteedHarbinger` answers true.

`noblessing` also closes a blessing window it finds open (`PanelSystem.ClosePanelInternal` on `ChooseBlessingPanel`), for one opened
before the setting or by another way.

A door's boss opens no Quest Completed window (`QuestRewardPanel.OpenImmediate`, `Notifications.ShowQuestRewardNotification`), also for 20 seconds
after leaving, because the window can wait for the scene change. `StatefulQuestList.onQuestCompletion` gives the rewards before it, so they still arrive.
