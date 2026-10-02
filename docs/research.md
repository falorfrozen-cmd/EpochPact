# EpochPact research (round 0, 2026-10-02)

What the game is made of, how our code gets in, and where the first features live. Every
claim is labelled **measured** (seen on this PC), **static** (read from the game's own
metadata dump) or **not established**.

## The game build

| Fact | Value | Label |
|---|---|---|
| Game version | 1.4.1 (`%LOCALAPPDATA%Low\Eleventh Hour Games\Last Epoch\version.txt`) | measured |
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

## Open questions for the next round

1. Our own x64 hook engine: how IL2CPP prologues look in this build (the
   `s_Il2CppMethodInitialized` check puts a RIP-relative compare and a conditional jump in
   the first bytes), so the instruction decoder covers exactly what it meets.
2. Which experience and gold paths a real kill takes (trace the candidates above once).
3. Where `Spawner.numberToSpawn` is consumed, for monster density.
4. Frame cost: a per-frame timer of our own, to hold every feature to a budget.
