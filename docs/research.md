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
`research-out/` and is never committed.

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
`research-out/saves-backups/<time>/` before every session.

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

## Open questions for the next round

1. The in-world experience check (`tools/live_xp_check.py`), once the 1.5.0 terms are
   accepted: x1/x3/x1 through `xpgain`, then a spawned enemy's own `GiveExp`.
2. When `GameplayEnvironment._isOnlinePlay` turns false in offline play (expected when an
   offline character starts), read live.
3. The gold path a real kill takes (`ItemDrop.SpawnGoldForActor` → pickup → `GoldTracker.modifyGold`).
4. Where `Spawner.numberToSpawn` is consumed, for monster density.
5. Frame cost: a per-frame timer of our own, to hold every feature to a budget.
