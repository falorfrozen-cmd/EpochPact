# EpochPact research (round 0, 2026-10-02)

What the game is made of, how our code gets in, and where the first features live. Every
claim is labelled **measured** (seen on this PC), **static** (read from the game's own
metadata dump) or **not established**.

## The game build

| Fact | Value | Label |
|---|---|---|
| Game version | 1.5.0.1 (the game's own `GameVersionManager` line in Player.log; `version.txt` in LocalLow still says 1.4.1 and is stale) | measured |
| Steam build | 25663618 (app 899770), updated 2026-10-02 10:26 | measured |
| Engine | Unity 6000.4.8f1, IL2CPP, x64 | measured (Player.log, MelonLoader's old log) |
| Graphics | Direct3D 11.0, feature level 11.1 (RTX 4070 Laptop) | measured (Player.log) |
| IL2CPP metadata | `global-metadata.dat` magic `AF 1B B1 FA`, version 39; not encrypted | measured |
| Client anti-cheat | none in the game folder (no EasyAntiCheat, BattlEye or similar) | measured |
| Game code | `LE.dll`: 13,517 classes of 40,722 in 188 assemblies | measured (our dump) |

## How our code gets in

- `Last Epoch.exe` imports only `UnityPlayer.dll` (and kernel32). `UnityPlayer.dll` imports
  `version.dll`, `winhttp.dll`, `winmm.dll`, `dxgi.dll`, `d3d11.dll` and others that are
  **not** KnownDLLs, so Windows looks for them in the game folder first. **measured**
  (PE import tables against the KnownDLLs registry list)
- EpochPact uses `version.dll`: 17 exports, two of them forwarders to KERNEL32. Our loader
  exports the same 17 names and ordinals (checked against System32's copy), forwards each
  one with a one-instruction jump, and starts the core only inside `Last Epoch.exe`
  (`UnityCrashHandler64.exe` shares the folder). **measured**
- `GameAssembly.dll` exports 241 `il2cpp_*` runtime functions, so classes, methods and
  fields are found by name at runtime: no offsets are hard-coded, and a game update moves
  nothing we depend on unless a name or a signature changes. **measured**
- `MethodInfo` keeps the code pointer at +0 and the name pointer at +24 (the 2021+ layout).
  **measured** (the core checks it at startup and logs it)

## Round 0 result (measured, 2026-10-02 14:23)

Our loader and core, installed by `tools/le_session.py install`, started with the game
through Steam. The core found the window 0.5 s after loading, resolved all 39 IL2CPP
functions it uses, attached to the domain (188 assemblies) and wrote the dump in 3.45 s:
40,722 classes, 271,838 methods, 221,788 fields; 39 classes faulted while being read and
were skipped (11 of them in `LE.dll`). The game was closed through its window
(`WM_CLOSE`): exit code 0. Saves were backed up first and not touched.

The dump (`dump.cs`, `methods.tsv`, `fields.tsv`) is the game's own metadata. It stays in
`research/game-data/` and is never committed.

## Where the first features live (static)

| Feature | Game code | Notes |
|---|---|---|
| Experience | `ExperienceTracker.GainExp(long,long,long)`, `GainExpFromEnemyOrMote(long)`, `GainExpDirect(long,bool)`; `ExperienceGainedOnKill.GiveExp(ExperienceTracker)` with `experienceGained` (int, +0x20) | Which path a kill takes is not established |
| Gold | `GoldTracker.modifyGold(int)` (bool), `loadGold`, `CanAfford`; `ItemDrop.SpawnGoldForActor`, `DropGoldInPilesForActor`, `getGoldDropChance()` | `modifyGold` also spends gold: only gains may scale |
| Item drops | `ItemDrop.getItemDropChance(int)`, `getEnemyBaseItemDropRate(int)`, static `DropItem(...)` (18 parameters, `BaseDropRates`, `DropFlags`), `ForceItemDrop(float)`, `DropChampionLoot` | Rarity and affix rolls are not established yet |
| Monster numbers | `Spawner.numberToSpawn` (float, +0x44), `percentVariance`, `common/magic/rareWeighting`; `SpawnerPlacementManager.RollSpawners`, `SpawnerPlacementRoom.SpawnerRuntimeConfig` | Where `numberToSpawn` is read is not established |
| Loot filter | `ItemFiltering.ItemFilterManager` (45 methods), `ItemFilter` | |
| Offline gate | static `EHG.Multiplayer.GameplayEnvironment.IsOnlinePlay` (`_isOnlinePlay`), `IsOnlineClient`, `IsServer`; `LE.Data.CharacterData.IsOffline`; `LE.Services.OfflineCharacterService`, `OfflineTransitionService` | The gate reads these before any feature acts |

Offline play still runs the game's network layer as a local server
(`LE.Networking.Generated.PlayerActorSync.MessageSyncGold`, `MessageSyncExperience`,
`GameplayEnvironment.IsServer`), so a feature must change the authoritative (server-side)
value, not only what the client shows. **static**, to be confirmed live.

## Saves

`%USERPROFILE%\AppData\LocalLow\Eleventh Hour Games\Last Epoch\Saves`: offline characters
`1CHARACTERSLOT_BETA_<n>`, stashes `STASH_*`, `Epoch_Local_Global_Data_Beta`; each file is
`EPOCH` followed by JSON. **measured**. `tools/le_session.py launch` copies the folder to
`research/live/saves-backups/<time>/` before every session.

## Round 1 (2026-10-02): the hook engine and the experience multiplier

**Experience path (static).** A static call map (the `E8`/`E9` targets inside each
method's code that land on another method's start) shows a kill's experience going
`ExperienceGainedOnKill.GiveExp(ExperienceTracker)` → `ExperienceTracker.GainExpFromEnemyOrMote(long)`
→ (echo-scene check, under-level penalty, `ActorScaler.GetXPMultiplierFromLevelChange`) →
`GainExp(long,long,long)` → `LevelUp`, `PlayerActorSync.SendSyncExperience`,
`LocalTreeData.ApplyAbilityXp`, `CharacterDataTracker.MarkCharacterDirty`. Quests and other
direct sources come in through `GainExpDirect(long,bool)` → `GainExp`. The multiplier sits
on `GainExpFromEnemyOrMote`, so kills and experience motes scale before the game's own
level rules; its first instruction is `mov [rsp+20h], rbx`, exactly the 5 bytes a jump
needs.

**Measured in the game (1.5.0.1):**
- Our hook engine installed on real game code without a crash: `CharacterSelect.OnEnable`,
  `ExperienceTracker.Awake`, `ExperienceGainedOnKill.Start`,
  `CommandLineManager.IsOnlyOfflineMode` (static) and, on demand,
  `UnityEngine.EventSystems.EventSystem.Update` (the main-thread frame hook).
- A command ran on the main thread through the frame hook (`charsel: selected index 0`),
  and the hook was removed again after two idle seconds.
- The gate reads **ONLINE** at the login and character screens (`_isOnlinePlay` is true
  until offline play starts), and `xp 3` was refused there: "xp: refused: ONLINE play".
- **GC handles are pointer-sized** in this IL2CPP: the first run kept them as `uint32`,
  and freeing a truncated handle crashed the game inside a capture hook (stack:
  GameAssembly ← EpochPact.Core ← il2cpp invoke). Fixed; every capture now runs inside
  `Guarded()`.
- **An attached thread blocks the quit:** with the command thread attached to IL2CPP the
  game logged `Application.quitting...` and never exited. The thread now attaches only
  while commands run; the game then closed through `WM_CLOSE` with exit code 0, with the
  hooks in.
- Returning true from `CommandLineManager.IsOnlyOfflineMode` did **not** skip the online
  login (the game still authenticated and matched a region), so that test hook was removed.
- **Blocked:** 1.5.0 asks for its new Terms of Service ("Version 1.5.0 is not accepted
  locally"). Accepting them is the owner's decision, so no character was loaded and the
  in-world check waits for that.

**Measured without the game:** `hook_test.exe` 48/48 (decoder table; RIP-relative,
short-jcc and call relocation; refusals; 300 installs and removals under a caller running
billions of calls, no wrong result). `xp_test.exe` 24/24: the real `xp.cpp` and hook
engine against a stand-in with `GainExpFromEnemyOrMote`'s convention and first
instruction (x3 turns 1000 into 3000, tracker and MethodInfo untouched, online and
unreadable gates refuse, x1 restores the original bytes).

**Next live step:** with the terms accepted and an offline character in the world,
`tools/live_xp_check.py` runs baseline x1, x3 and x1 again through `xpgain`, then the real
kill path (`xpkill`, a spawned enemy's own `GiveExp`) when an enemy is around.

## Round 2 (2026-10-02 evening): gold, item drops and density, played live

**Where they hook (static, `research/tools/callers.py` and `callmap.py`):**
- **Gold.** `GoldTracker.modifyGold(int)` has 15 callers. Only one of them is a pickup:
  `GroundItemManager.pickupGold(GroundItemList, Actor, uint)`, reached from
  `GoldPickupInteraction.PickUp()`. The rest are shops, stash tabs, respecs, monolith
  rerolls and quest rewards. `gold` hooks both and scales `modifyGold` only while
  `pickupGold` runs (a thread-local depth counter).
- **Item drops.** The enemy-death routine (`ItemDrop.<DropItem>d__102.MoveNext`) calls
  `getItemDropChance` and `getGoldDropChance`, waits (`UniTask.Delay`), then calls the static
  `ItemDrop.DropItem(int level, Vector3 position, float itemDropChance, bool, float
  itemMultiplier, BaseDropRates, bool, float goldMultiplier, float goldChance, float
  craftingOnlyDropChance, DropFlags, Scene, bool causedByEnemyDeath, bool, bool
  limitToOneGoldPile, GoldDropType, CorruptionOutcome forceCorrupt, float dropRadius)`.
  Monolith objectives, arenas, nemesis and Woven echoes call it too. `drops` scales
  `itemMultiplier` (the item count), not `itemDropChance` (a chance, capped at 100%). All
  enums are int32, `Vector3` goes by address and `Scene` as an int.
- **Density.** `Spawner.get_MinimumSpawnCount`/`get_MaximumSpawnCount` have no call sites
  (inlined). The pack is rolled inside `Spawner.GenerateEntitiesInternal()` (`RngElement.Roll`,
  `NextFloat`, then `new MonsterGenerator(...)`), called once per spawner from
  `GenerateEntitiesAsync`. `density` scales `numberToSpawn` (offset found by name, +0x44)
  for that call only and puts it back afterwards. Spawners of one (bosses, unique enemies)
  are left alone.

**Measured (offline character, zone level 1, the owner playing, 19:02-19:05):**
- The gate turned to `offline play` when the owner entered offline play, and all four
  features armed at once.
- **Experience x10:** 27 gains boosted; the first was 3 → 30, the latest 15 → 150.
- **Gold x10:** 10 pickups boosted; the first was 4 → 40.
- **Item drops x10:** the first drop went `itemMultiplier 1 -> 10`. Only kills whose
  chance roll succeeds reach `DropItem`, so this counts fewer than kills.
- **Density x3:** 4 packs boosted; the first went `numberToSpawn 2 -> 6`.
- Also seen: `GameplayEnvironment.IsServer` reads false in offline play (`[client only]`).
  `ExperienceGainedOnKill.Start` never fired, so kills reach `GainExpFromEnemyOrMote`
  without that component in 1.5.
- One game error in the log, "EncounterPlacementData is null", came right after the game's
  own warning that shrines need zone level 2 (the zone was level 1), during scene load. It
  is the encounter system's own message, not one of these hooks.

## Round 3 (2026-10-02 late): item quality, auto-pickup, move speed and cooldowns

**Built (all four offline-only like the rest; x1 removes every hook):**

| Command | Hook | Static notes |
|---|---|---|
| `rarity <1-10>` | static `GenerateItems.RollRarity(int ilvl, float uniqueAndSetDropRateMultiplier) -> byte` | The one rarity roll behind `RollBaseItem`; every item source (drops, shops, nemesis, gambling) goes through it. `GetRarity0To4FromRoll` shows the result is 0..4 (normal, magic, rare, exalted, unique/set). The multiplier upgrades one tier with chance (mult-1)/mult, capped at 4. |
| `autopickup <0/1>` | `DistantItemPickupHandler.OnUpdateTick(float)` | Every 0.75 s it reads the static `ItemTooltipOrganizer.pickableGroundLabelList` (a `DList<PickupableGroundLabel>` → `List<T>` at +0x10, `T[]` at +0x10, count at +0x18, elements at +0x20) and calls `GroundItemLabel.requestPickup()` on each item label, and the same over `GroundItemManager.activeGoldPiles`/`activePotions`/`activeXPTomes`/`activeFavorTomes`/`activeAncientBones` with their own `PickUp()` methods. Class checks keep it from calling anything but the right label types; elements are snapshotted before the calls because pickup mutates the lists. |
| `speed <1-5>` | `RPGCharacterController.UpdateMovement()` and `UnityEngine.AI.NavMeshAgent.set_speed(float)` | Disassembling `SpeedManager.updateSpeed` showed what it really does: it reads stat 9 through `Stats.GetStatValue` and writes `NavMeshAgent.set_speed`; `baseMovementSpeed` (+0x20) is just the agent's speed captured in `Init` for a one-time +% application. Scaling `baseMovementSpeed` therefore did nothing to the player (and could have touched monsters). The real movement reads `RPGCharacterController.walkSpeed` (0xBC), `moveSpeed` (0xC0) and `runSpeed` (0xC4) in `UpdateMovement`; the detour scales those three for the call only, so nothing compounds. Click-to-move uses the agent, so `set_speed` is scaled too — only when the agent belongs to the player (`PlayerFinder.getPlayerActor` → `Actor.navMeshAgent` +0xA8). |
| `cooldown <1-10>` | `PlayerChargeManager.OnUpdateTick(float)`; `ChargeManager.getCooldown(int)` | `getCooldown` has only two call sites (`useCharges`, an AI fallback) and never fired for the player's live abilities, so the player's per-frame countdown is the real target: `PlayerChargeManager.OnUpdateTick` → `ChargeManager.OnUpdateTick(deltaTime)`. Scaling that deltaTime accelerates charges and cooldowns; `getCooldown` stays hooked to divide new cooldown lengths for the abilities that ask for it. |

**Live measurements (offline character, the owner playing):**
- **Rarity x10, 300 rolls at ilvl 1:** `0:212 1:53 2:32 3:0 4:0` before, `0:17 1:207 2:43 3:32 4:0` after. Magic-or-better went 28% → 94%, rare-or-better 10.6% → 25%.
- **Auto-pickup:** 452 scans, 15 pickup calls while the owner killed monsters; the first vacuum scan saw empty lists (zone cleared), later scans collected the new drops. No game errors.
- **The old features on the same session:** xp 25 gains boosted (last 3 → 30), gold 11 pickups (first 2 → 20), drops 2 (1 → 10 items), density 14 packs (first 2.6 → 7.8).
- The wrong speed hook (see above) boosted 96 monster SpeedManagers without changing the player's speed; disassembly found why, and it was replaced before the click-to-move test.

**New research commands:** `raritytest <n>` (calls `RollRarity` n times and prints the distribution), `speedread`, `statprobe` (dumps the player's Movespeed stat entries), `posread` (player world position through `Component.get_transform` → `Transform.get_position`, hidden struct return in rcx), `tab`, `offline`, `playoffline` (`LandingZonePanel.OnPlayOfflineClicked`, with the panel captured through `OnOnEnable`).
**Research tool:** `research/tools/disasm.py` (capstone) disassembles a method by RVA — this is what found the real speed and cooldown paths.

**Addendum (2026-10-03): the move-speed feature now writes the game's own Movespeed stat.**
- The outputs-first implementation (agent speed + controller fields) worked but measured only 2.19x at x5: `WalkAnimationScaler.updateAnimationScale` computes the animation playback from `Stats.GetStatValue(SP=9)/baseMoveSpeed`, and the animation side did not follow. The owner could feel it ("not natural").
- The stat itself is a plain object field: the player's `BaseStats` (path: `PlayerFinder.getPlayerActor` → `Actor.characterMutator` +0x108 → `CharacterMutator.myStats` +0x98) holds `Stats.stats`, a `List<Stats.Stat>` at +0x88 (inherited from `Stats`). A `Stats.Stat` keeps `property` (SP) at +0x10, `specialTag` +0x11, `tags` +0x14, `extraTag` +0x18, `addedValue` +0x1C and `increasedValue` +0x20. Writing `increasedValue` and setting `BaseStats.statsNeedToBeUpdatedNextFrame` (+0xD8) makes the whole pipeline (character sheet, animation, `SpeedManager` → `NavMeshAgent`, click-to-move) use the boosted stat. **Measured live: 0.1 → 1.1 (+100%), NavMeshAgent speed 5.49 → 10.53, owner confirms it feels natural.**
- `increasedValue` is a **fraction** (0.1 = +10%), not a percentage; the first run wrote 100 (+10,000%) and the agent hit 509 u/s before it was corrected.
- Dead end worth keeping: calling `BaseStats.ChangeStatModifier` (or the virtual the game's own Swiftness uses at class+0x2F8) crashed the game even when mirroring the register/stack layout read out of the disassembly. The thunk is kept in `native/core/stat_thunk.asm`; the field write needs no calls at all.
- A fresh character has no Movespeed entry until something grants movement speed; the feature refuses with that message (it could be created with `il2cpp_object_new` later).

**Open questions for the next round**

1. Frame cost: a per-frame timer of our own, to hold every feature to a budget (nothing yet).
2. Creating the Movespeed stat entry from nothing (`il2cpp_object_new` + the list's `Add`), for characters with no movement speed source yet.
3. Legendary potential, affix tiers and Weaver's Will rolls (`GenerateItems.RollTier`, `initialiseRandomItemData`).
4. Loot filter (`ItemFiltering.ItemFilterManager`) and crafting (`CraftingManager`).

## Round 4 (2026-10-05): character-sheet editor milestone and density handoff

**Completed and measured:** the editor discovers all 134 runtime SP values, maps 206
character-sheet rows / 169 distinct base keys, and supports full tags/special/extra
identities, added/increased/more modifiers, minions, ailments and secondary row keys.
Every SP passed neutral construction/read/removal and baseline checks. The original
201 rows plus the final five directly populated LineItems passed their access checks;
the final five also passed nonzero display/reset checks. Native stat validation passed
33/33; XP passed 24/24. The save's 37 items matched the pre-test snapshot exactly.
This establishes editor access, not every conditional effect's combat behavior.

**Root cause of frozen resistances and equipment errors:** Stats.Stat objects had been
allocated without invoking .ctor, leaving moreValues null. Proper construction, GC
rooting, isolated dontCollapse entries and the game's virtual recalculation fixed the
problem. No refresh hook or equipment aggregate overwrite is needed. Details, commands,
units and limits are in docs/stat-editor.md and GEMINI.md. Evidence is retained under
research/live/full-stat-editor-201-check.json, full-stat-editor-final-check.json and
full-stat-editor-verification.json.

**Current Falor level 8 bonuses:** allres added 0.65; bow/melee AttackSpeed increased 5
(+500%, not an absolute 5 attacks/sec); Parry added 0.5 (50%); PercentReflect added 10
(1000%). These apply to the current actor in memory and need reapplication after
restart. Current resistance labels: physical 75% capped / 79% total, lightning 71%,
fire/cold/void 65%, necrotic/poison 66%. Reflected combat damage has not been measured.

**New owner priority:** monster density. Earlier density results (2.6 -> 7.8, 14 calls)
measured the input field and detour counter, not successful spawn counts. Investigate
actual generation/spawn coverage and exclusions before reporting a measured density
ratio. Use IPC/game APIs in the background; no desktop input automation.

## Round 5 (2026-10-05): density strengthened and owner confirmed it live

**Implemented:** density now has a dedicated native module. The existing temporary
pack-mean mechanism is preserved, with reentrancy protection and explicit runtime
ActorData/type/special/friendly/context exclusions. Finite value checks and a bound
on the pre-rarity roll prevent an unbounded multiplier. The original field is restored
on return/unwind. Three hooks cover generation, SpawnOneImmediately and FinishSpawning;
x1 removed all three successfully in the live session.

**Telemetry:** new-generator totalSpawnCount records planned actors; increases of
spawnedActors record actual spawning, with a bounded GC-rooted tracker. densityread
is an on-demand scene snapshot, with no per-frame scene scan. Spawned lists can contain
dead actors; counters are cumulative, and tracking eviction can cause undercounting.
See docs/monster-density.md for exact scope, commands and limits.

**Verified:** density rules 37/37; hook engine 48/48; stat validation 33/33; XP 24/24.
Ten invalid IPC inputs were refused. In Falor's town all 10 scene spawners were
classified outside normal enemy packs, and x3 boosted none of their generation calls.
The 37 saved items stayed identical to the pre-density snapshot. Test exits were clean
(code 0), and installed/core build bytes matched. Evidence is under research/live:
density-before.json, density-invalid-check.json, density-town-check.txt.
Final session/evidence is in density-final-verification.json: installed core SHA256
2351eda631de6e54bda93778377019ed2a3618ed7325c8af05a50c912590c179; verified offline
Falor level 8 loaded again, all five requested stat bonuses reapplied/read back, density
x3 armed, seven resistance labels updated, and the 37 saved items still unchanged.

**Owner's live confirmation:** "Ben test ettim az önce monster density oldu."
This confirms the visible gameplay result qualitatively; do not invent an exact live
spawn ratio from it. The current target setting is x3. Game restarts reset density to
x1 and the actor-memory stat bonuses must be reapplied to verified offline Falor.

**Combat log evidence (03:19-03:20, process 45744):** the first normal pack logged mean
2.7 -> 8.1 at x3, a new generator queue of 9, and restoration of the original mean 2.7.
FinishSpawning subsequently recorded completed packs of 8/8, 9/9 and 9/9 observed /
planned actors. The owner was testing combat while these hooks recorded actual spawns.
No paired x1 baseline was collected, so these observations establish functioning
generation/spawning, not an exact measured population ratio.

**Background transitions:** waypoint travel refuses an inactive destination even if
it appears in saved unlocks (Z22 in this save). A separate research exit helper uses
active LoadSceneInteraction components and the game's ConditionHandler readiness /
TryTriggerInteraction, without resetting conditions or unlocking quest gates. The
exit helper has not yet been exercised live; it is not needed for the owner's completed
density confirmation.

## Round 6 (2026-10-05): campaign completion, minimum level 55 and all waypoints

**Owner request:** main/side campaign completion with rewards and a second button
for all waypoints. Owner explicitly selected quest rewards **plus minimum level
55**. Native progression.cpp and the Python backend/panel implement these; usage
and limits are in docs/progression.md. No desktop input is needed.

**Quest scope:** runtime QuestList has 148 assets, with 85 campaign records (41
main, 44 side, one hidden). Filter Normal, non-test and non-Minilith: Normal alone
would incorrectly include 18 endgame records. Validate each pending step/objective
chain first, then use normal Quest.completeQuest(actor, true). Normal reward caps
are 15 passives / 8 idol unlocks. Current Majasa asset has no attribute reward;
Knowledge of Orobyss's +2 attributes is Minilith and excluded. Mastery/faction
choices remain the owner's; repeatable/echo/test quest progress is untouched.

**Level:** normal quest XP alone raised the clone from 8 to 13. Sum the actual
PlayerUtility.NextLevelExpFromLevel thresholds for levels 1..54 and subtract
current cumulative XP. GainExpDirect(amount, noFavour=true) supplies the shortfall
through normal LevelUp events, with no raw level/stat write. Existing 55+ level
and XP stay unchanged. Kill/mote multipliers and favor do not inflate this top-up.

**Waypoints:** 160 represented scene keys include 109 actual waypoint targets.
Validate scenes through SceneList, add unlocks through AddUnlockedWaypointScene,
and refresh UIWaypoint.CheckWaypoint with the loaded character's list and normal
monolith manager. Inactive era panels and an already-open map otherwise retain
cached flags. MonolithHub has multiple gate icons; active flags aggregate by
scene for the catalog, but travel must select a gate explicitly (research
helper now supports scene:gate). The campaign button also opens EoT, MonolithHub
and M_Rest independently of the all-waypoints button. Timeline conditions remain
under the game's normal control.

**Recovery:** native snapshots contain disk copies plus serialized live character,
stash and global data. Manifest is written last, before any mutation. Python
validates all inputs/identity, closes the game normally, makes another recovery
backup and restores affected character/wallet/global files with their fallback
copies. Other characters, stashes and tabs are not reverted. Panel retains action
result/backup even if the following read fails. Tkinter is the two-button utility;
final general mod UI remains separate work.

**Live clone:** EpProgressTest, level 8 / XP 921 (cumulative 3,842), one completed
campaign quest and isolated gold 0. Normal completion added 84 quests, XP 8,045,
gold 102,001, passives 15, idols 8. Level top-up XP was 1,123,820; final level 55,
cumulative XP 1,135,707, within-level XP 0. All 85 campaign states completed; all
63 excluded states unchanged. All 109 waypoint keys unlocked and at least one
map icon active per key. Repeat actions gave no extra rewards. Restart retained
the result. SceneList.GetCurrentSceneDetails verified real Z32 -> EoT ->
MonolithHub travel (gate 11), not just a transition request reply.

Undo restored level 8 / XP 921, gold 0, passives/idols 0 and the initial quest /
waypoint state. Reopening verified the engine data. With the game closed all 54
original pre-clone files were restored byte-for-byte; three new clone/solo-stash
files were archived. Falor's campaign was not advanced for tests. Final original
identity, equipment and reapplied stat/density evidence are recorded separately.

**Checks:** native build passed without compiler warnings, recovery validation
13/13, XP 24/24, stat keys 33/33, density 37/37. Tkinter widget/action smoke check
passed, including refresh failure after a successful action. Evidence under
research/live (ignored): progression-complete-55.json, progression-reopened-55.json,
progression-after-55-waypoints.json, progression-endgame-read.json,
progression-undo-reopened.json, progression-original-restored.json,
progression-final-verification.json. Progression is one-shot main-thread work;
no persistent progression hooks or per-frame scene searches are installed.

## Round 7 (2026-10-05): Monolith backend and current Item Factions

**Owner scope:** implement the recommended Monolith/endgame manager; permanent
profiles and UI belong in the owner's separate design. Research CoF and Merchant's
Guild using current internet sources and game code. No new faction rank/favor
mutations were requested or applied. Details: docs/monolith.md and
research/factions-1.5.md.

**Native implementation:** monolith.cpp resolves methods/fields by name. The
runtime catalog has ten real timelines with normal and Empowered difficulties.
Unlock uses the normal progress API; selection opens the normal panel. Corruption
updates the owning EchoWeb, then normal corruption-mod/shared-progress methods.
Stability uses normal AddStability with a manual-edit bypass for the gain hook.
Positive gains scale x1–100; losses do not scale; x1 removes the hook. Actor/run
ownership, offline identity, settled InGame and hub scene checks precede changes.
Persistent mutations use the existing live/disk snapshot and normal save API.

**Bounds:** normal corruption 0–50, Empowered at least 100; supported ceiling
65535 follows UInt16 progress serialization. Actual difficulty assets supply
stability caps (normal 500–1000, Empowered 1600–1800). Saturation prevents Int32
addition overflow. TryGetRun precedes getNewRun because the latter resets existing
run progress. Editing an active echo is refused.

**Found/fixed:** normal + Empowered unlocks for one timeline in one frame collided
in Notifications.timelineIDs, a Dictionary<TimelineID,byte>. The second normal
Dictionary.Add threw ArgumentException after progression had already changed.
The backend now coalesces only that pending timeline's notification before the
normal unlock call. Full clean-snapshot retry unlocked all 13 pending difficulties
and all 20 were readable; repeat unlock returned zero. Exceptions are still
reported with their recovery snapshot.

**Loading:** SystemLoading must finish before OnPlayOfflineClicked; CharacterSelect
must settle before loading a tile. A preview actor in Login had previously looked
like a loaded character. New sessionread and RequireSession guards reject that
state; live actions require an actual loaded zone.

**Live engine checks:** EpMonolithTest (independent character-found stash) verified
normal corruption 42 vs Empowered 300, normal stability 120 vs Empowered 1250.
At x5, normal AddStability(+25) produced +125; -10 remained -10; manual absolute
edits bypassed scaling. Near the cap, 1690 + scaled 100 clamped to 1700. Invalid
normal 51, Empowered 99/65536, stability above cap, wrong loaded id and malformed
integers were refused. Timeline 7 Empowered and timeline 3 normal selections
worked; editing an already selected run refreshed it without an exception.
This verifies actual AddStability calls through the installed hook; no natural
echo was played for this test.

**Reopen/recovery/cleanup:** normal close/reopen retained all 20 unlocks, normal
42 corruption / 120 stability and Empowered 300 corruption / 1250 stability.
Multiplier restarted at x1. Undo of a corruption snapshot restored both run
values (Empowered 320 / 1260), with another reopen confirming the result; normal
42 / 120 and unlocks remained intact. Then the game closed normally. All 54
original pre-test files were restored byte-for-byte, and three clone/stash files
were archived. Falor is still id 0, level 8. The game started closed and is left
closed; no actor stat bonuses or density hooks were rearmed. Installed core hash
matches the verified build. Evidence records beforeReopen, afterReopen,
undoReopened and cleanup separately.

**Faction research:** read-only factionread reports four native factions, current
membership/favor/reputation, rank assets, prophecy slots and lens asset parameters.
CoF/MG each have 12 ranks; ForgottenKnights/TheWeaver each have 10. Installed CoF
lens enum has 12 entries. Current 1.5 sources describe four charge-based prophecy
slots and the redesigned MG rank rewards. All equipment types are tradeable from
MG rank 1; later ranks unlock NPC material/key bags. Actual player Bazaar trading
uses online services; offline bag purchase was not tested. Normal Join/Leave,
GainFavor, GainReputation, SetRank, SaveFaction and prophecy APIs are mapped for
future work. No raw rank writes, faction switch or reward generation was done.

**Checks:** build passed; monolith bounds/arithmetic 22/22; Python IPC/recovery
17/17. Existing hook 48/48, XP 24/24, stat 33/33 and density 37/37 passed during
this work. Live evidence: research/live/monolith-live-verification.json and
research/live/factionread-test.json (ignored game/save data).


## Round 8 (2026-10-05): CoF management and The Woven research

**Scope:** owner requested CoF management and investigation of the Season 2 spider
rank system first. Existing UI/permanent profile plans remain in the owner's UI.
Added native cof module + tools/cof_backend.py, no desktop input automation.

**Normal APIs:** offline loaded-id/actor ownership and settled InGame checks;
FactionTracker.TryJoinFaction/TryLeaveFaction, SetRank, ToggleRanks,
SetFavorAndReputation, GainReputation, TryPurchaseLens, slot Copy/SetLens/SetReward,
SlotsToClientConfig/IsConfigValid/AttemptApplyProphecyConfiguration, AddCharges/
ResetCharges. Mutations snapshot disk and live character/stash/global before writes,
then normal SaveAndSync/character save. Guarded errors retain recovery paths.

**Measured traps:** generic boxed ValueTuple field edits failed before any live
mutation; replaced with cloned reference arrays/normal slot setters and the game's
configuration builder. Available rewards are not just unlocksAtRank <= rank:
GetAvailableRewardsForRank replaces eligible Rare assets with their upgrade chains.
Rank 12 showed 92 selectable variants among 123 assets; reward 0 was replaced by
100. Installed SetRank sorts toggle endpoints, enabling the ascending range even
on a decrease. Backend additionally applies normal ToggleRanks(previous,target)
in the decreasing direction; live rank 12 -> 1 removed high-rank effects and
locked slots 1/2/3. Do not write the cached bonus fields manually.

**Semantics:** Reputation is within-rank progress. Manual rank change clears that
progress; an additive 900 grant at rank 1 produced rank 2/reputation 0. Favor balance
edits do not advance Reputation/prophecies. Favor multiplier detours the five-arg
GainFavor ABI (self, amount, ignoreRep, ignoreMultiplier, MethodInfo), changes only
positive eligible CoF gains and honors ignoreMultiplier. Other factions retain
original arguments. Gain input is bounded before game Int32 arithmetic; wallet
saturation still permits normal prophecy charging. x1 removes the session hook.

**Live verification:** isolated EpCoFTest, save id 6 and separate solo-character
stash. Join granted normal initial 500 Favor. Rank 12 toggled actual bonus fields,
all 12 lens unlocks cost no Favor, and all four reward/lens selections worked.
59 checks cover configuration/clearing, charges 0/10/99/7, read-only preview, duplicate
reward/lens rejection, invalid input/stale identity, locked slots and rank bonus
removal. Real GainFavor input 20 yielded 21 at x1 and 105 at x5; raw charge progress
6521 -> 32608 (rounding) after clearing/reselecting. At wallet 999999, another gain
advanced raw progress to 65216 without increasing the wallet. Hook had boosted 2,
refused 0, faults 0, last 20 -> 100. This uses the real game method via a test-only
probe, not a natural combat experiment. Native GetChargePercentage supplies the
fraction; do not assume a unit scale for the raw chargeProgress integer.

**Persistence/recovery:** normal close/reopen retained rank 12, Favor 123456, 12
lenses, selections 100/101/102/103 with lenses 0/1/2/4 and charges 10/7/0/0. Undoing
the pre-join snapshot, then reopening, restored membership false, rank/favor/rep 0,
no purchased lenses and all slots locked. Character savedItems remained identical.
All 54 original files were restored byte-for-byte and three clone files archived.
Falor never loaded/advanced; final game quit exit 0, installed DLL hash matches the
final build. No session stats/density hooks rearmed. Evidence:
research/live/cof-live-checks.json, cof-reopen-check.json, cof-undo-result.json,
cof-undo-reopen.json and cof-verification-summary.json. MG switching and natural
prophecy reward production were not live-tested; explicit switch flag is unit-tested.

**The Woven research:** official support/Season 2/1.5 sources plus runtime assets.
The Woven is class TheWeaver, faction id 3, compatible with CoF/MG as an endgame
faction. Memory Amber, Woven Echoes and Weaver Tree are separate connected systems.
Ten ranks award 13 tree points; Woven Echo completions add up to 40, total 53.
Rank 1-8 give one each, rank 9 two, rank 10 three. Live max/earned getters verified
these bounds; rank, Amber, echoes and tree allocations were not mutated. Rank 10
alone is not full tree completion. See research/weaver-1.5.md for official sources,
rank effects, API map and Purged Horizon's interaction with corruption gain.

**Checks:** full native build and latest core rebuilds passed; cof rules 16/16;
combined Python CoF/Monolith/recovery 21/21. Existing XP 24/24, stat 33/33, density
37/37 and Monolith 22/22 passed after the full build. Hook engine unchanged this
round; previous 48/48 retained as prior evidence, not a new run.


### Owner CoF gameplay test prepared (2026-10-07)

Owner entered offline Falor and authorised the proposed live CoF test. Read id 0,
level 8, Z32, XP 921; all four factions initially unjoined. Full save backup plus
normal mutation snapshots taken. Joined CoF, set rank 12/Favor 100000, unlocked all
12 lenses, configured four reward/lens pairs 100/0 (helmet), 101/1 (body armour),
110/2 (wand), 104/4 (gloves). Slot 0 has only two charges for a small natural reward
test; the other slots start at zero. Favor gain x5 hook armed; initial gains,
boosted, refused and faults all zero. Backend readbacks passed, savedItems matched
the validated live pre-join snapshot. Actual owner UI/combat confirmation pending.
Game left running; no auto-undo or historical full-save restoration. Journal and
baseline: research/live/cof-owner-20261007/. Weaver untouched.


**Owner UI confirmation and live gains (2026-10-07):** screenshot shows rank 12,
Favor 100000, all four reward/lens selections and charges 2/0/0/0. Subsequent live
read had Favor 100120 and charge fractions .12297/.08196/.07452/.08196. GainFavor
hook recorded gains 43, boosted 15, refused 0, faults 0, last input 1 -> 5. Thus
actual game gain calls and partial charging are now observed on Falor, beyond the
prior isolated test probe. No completed-charge consumption/reward production
observed yet. Evidence: cof-owner-20261007/owner-ui-confirmed.json and screenshot.

### Separate CoF Reputation gain multiplier (2026-10-07)

**Static and live:** Faction.GainFavor calls GainReputation for Reputation derived
from actual gained Favor; TrySpendFavor calls it independently for spending.
Their FactionData coefficients are read dynamically in cofread. The native ABI
of GainReputation is (self, Int32 amount, MethodInfo); the detour preserves self
and MethodInfo and calls the original once. It scopes to the loaded settled
offline CoF member, bounds the boosted addition against remaining Int32 room,
and preserves normal rank-up/bonus/cap logic. Manual cofreputation uses a scoped
thread-local bypass so an explicit grant is not multiplied a second time.

**Measured normal-method probes on isolated EpCoFTest:** 28 checks passed. Rank 1
20 Favor input gave normal 21 Favor / 42 Rep; Favor x5 gave 105 / 210; Rep x3
gave 21 / 126; together 105 / 630. The Reputation-only setting did not alter
prophecy progress, while Favor x5 still charged x5. Spending 10 Favor granted
20 vs 60 Rep at x1/x3 without changing cost or charging prophecies. Direct 10 Rep
became 30, manual gift 10 remained 10; fractional 3 x2.5 rounded to 8. 10 x100
advanced to rank 2/progress 100 and normal rune preservation .45. Rank 12 remained
capped with progress 0. Other-faction input passed unchanged, invalid controls
refused and x1 removed hooks; Rep faults 0. This is a normal API/hook integration
test, not a natural combat drop-rate measurement.

**Build/recovery:** full build passed, native CoF 25/25, XP 24/24, stat 33/33,
density 37/37, Monolith 22/22; Python API/recovery 22/22 + 3 IPC read-lock tests.
A transient Windows out.txt append lock exposed an existing IPC read race;
le_session.send now retries only the read within its original deadline, never
resending the mutation. Live continuation passed. Falor quit exit 0 before
creating the separate-stash clone. All 54 current owner files restored exactly
by hash; 3 clone files archived. Clone quit exit 0. Fresh evidence and manifest:
research/live/cof-repmult-20261007/ and cof-repmult-test-manifest.json. Do not use
historical pre-CoF backups for current owner restoration.

**Final owner session verified:** Falor id 0, level 8, XP 1016, Z32; rank 12,
Favor 100168/Reputation 0 matched the pre-test read. Lens purchases, all slot
selections/charges/partial progress and rank effects matched; savedItems matched
the fresh backup. Favor x5 rearmed; new Reputation remains x1. Running PID 25060;
installed core SHA-256 matches the build. Evidence: owner-final.json and
owner-cof-final.json in cof-repmult-20261007/.

### Six CoF loot/lens modifier groups (2026-10-08)

**Scope implemented:** independent prophecy charge speed and reward quantity;
separate enemy/normal-Monolith double-item chances; separate Exalted/T7 roll
coefficients; eligible Unique LP coefficient; Celerity/Charity/Duplication potency.
Native cof_tuning module, ten session controls, UI functions in cof_backend.py,
read telemetry/source values/hook ownership and limits documented in docs/cof.md.

**Native consumers/ABI:** ProphecySlot.AddFavor(2) 0x20B0930; selected
ProphecySlotReward.SpawnRewardForPlayer(2) 0x20ADF30 (Nullable<LensType> is 8-byte
by-value); GenerateItems.GenerateAffixes(9) 0x11BDC60; eligible native
ItemData.GetUniqueLPRollMultiplierFromCoF(2) 0x1200680. Normal echo chance is
consumed inside SpawnEchoSpecificRewards(7), 0x1D7FFF0, field read at 0x1D807C3,
not at the entry of DropChestRewardItemsAtLocation. Scope NormalEcho source 1;
Tomb source 2 stays normal. Enemy chance uses the existing ItemDrop.DropItem(18)
dispatcher; its hook is shared with global drops without duplicate patching.

**Measured native units:** rank-12 generator Exalted raw field +0x84 is 1.5
(getter 0x11CC550 returns it directly); T7 raw +0x88 is coefficient 2 (getter
0x4A2810), not probability. Exalted x3 produces 4.5; T7 x5 produces 10. Rare to
Exalted +0x7C is actual probability .25; scale/clamp 0..1. Faction enemy +0xAC
and echo +0xA8 probabilities are .35, explicit controls 0..100%/reset. Prophecy
data Celerity +0x78=.5, Charity +0x7C=.1, Duplication +0x84=.4. Runtime metadata
resolves every used field/method; numeric offsets here are measured evidence.

Primitive asset/generator/faction fields are borrowed only for synchronous normal
calls, rooted and restored in reverse order on return/unwind. Reward count is
rounded/clamped to 250 base items per normal spawn call. Rank-12 double spawning
and native Duplication combine; normal charge consumption is unchanged. Charge
boost bounds native float→Int32 and progress addition, while preserving upstream
Tyranny Favor cost. Native LP result is obtained first; only eligible bonus >1
is multiplied. Normal item/Weaver eligibility, level rules and LP 0..4 stay in
the game. Shop/gambler affix contexts are excluded.

**42 distinct final live feature checks:** isolated EpCoFTest only. Charge x5,
lens contributions and combined scaling passed without wallet/Rep changes.
Actual ground items from one prophecy charge: normal20, reward x3=60,
guaranteed Duplication=40, combined=120. Enemy field override and both shared
hook disable orders passed. High-level normal loot had actual T7 affixes;
Exalted/T7 independently active and coefficients restored. Eligible LP 2→6;
rank without native bonus remains 1. Normal echo item reward 1→2 at chance100%.
Invalid inputs leave settings unchanged; final disarm removed all new hooks,
restored all source coefficients; tuning faults0.

The first exact echo count assertions exposed flaws in the research setup:
random reward count modifier and the game's alreadyReceivedEchoSpecificReward
one-claim set. Corrected probe selects a guaranteed catalog roll, uses zero extra
count modifier and calls normal ResetPlayerRewards only between isolated
simulated echoes. Production hook never resets the claim guard. Initial failed
assertions remain in checks.json, with resolution and 42 passing finalChecks.

**Build/recovery:** full native build and final core rebuild passed; CoF48/48,
Python27/27. Clone sessions closed normally (exit0). Latest current owner backup
20261007-234638 restored54 files byte-for-byte, archived3 clone files. Falor
reopened with11 passing recovery/installation checks: id0/level8/XP1016/Z32,
rank12/Favor100168/Rep0, slots/lenses/effects/rewards and savedItems preserved.
Favor x5 restored; Rep x1 and new controls neutral. Running PID40168, installed
DLL SHA-256 matches final build. Latest evidence cof-tuning-20261007/ and
cof-tuning-test-manifest.json restored=true; older backups remain historical.
