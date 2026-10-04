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
