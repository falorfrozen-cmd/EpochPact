# EpochPact requirements (draft 1, 2026-10-02)

ForgePact-style mods for Last Epoch: a native plugin in the game, a panel outside it, every
line our own. Research behind each point: `docs/research.md`.

## 1. Ground rules (owner, 2026-10-02)

- **Entirely ours.** No MelonLoader, BepInEx or other loader; no code from other mods
  (RCInet/LastEpoch_Mods has no licence, so nothing of it may be copied); no third-party hook
  library. The loader, the IL2CPP resolver, the hook engine, the command channel and the
  panel are written here. ForgePact's own code may be reused.
- **ForgePact usage:** a plugin DLL in the game, a panel that sends it commands, features
  switched on and off live.
- **Faster than the mods it replaces:** a feature that is off costs nothing (its hook is
  removed, not just skipped), and nothing scans the scene every frame.

## 2. Safety

1. **Offline only.** No feature changes anything unless
   `GameplayEnvironment.IsOnlinePlay` is false **and** the loaded character's
   `CharacterData.IsOffline` is true. In online play every feature refuses and says why; the
   plugin stays loaded but does nothing to the game.
2. **Saves backed up** before every test session (`tools/le_session.py launch` does it).
   A feature that writes a save keeps its own backup and undo, like ForgePact's.
3. **Kill switch:** a file `<game>\EpochPact\disabled` stops the core from loading at all.
4. **Never overwrite** a `version.dll` that is not ours (the installer refuses).
5. **Refuse rather than guess:** a hook whose first bytes the decoder does not understand is
   not installed, and the panel shows that feature as unavailable, with the reason in the
   log (ForgePact's TABLE-ONLY rule).
6. **Game metadata stays private:** dumps and decompiler output are never committed.

## 3. Architecture

| Part | What it is |
|---|---|
| Loader | `version.dll`, proxy of System32's. Starts `EpochPact\EpochPact.Core.dll` in `Last Epoch.exe` only. **Done (round 0).** |
| IL2CPP resolver | The runtime API by name (`il2cpp_api.hpp`); classes, methods and fields found at startup, never by offset. **Done for reading.** |
| Hook engine | Our own x64 inline detour: a length decoder for the instructions IL2CPP prologues use (RIP-relative operands, short and near jumps and calls relocated), trampolines placed within 2 GB, install and remove while game threads are held. Unknown bytes: refuse. |
| Calling game code | Typed calls through `MethodInfo` code pointers (static methods take a trailing `MethodInfo*`), on threads attached to the IL2CPP domain; main-thread work queued to a per-frame point. |
| Command channel | Panel to plugin: commands and status, as ForgePact's `bp_ipc` (files) or a named pipe; decided in round 2. |
| Panel | Python backend plus a browser UI, as ForgePact's. UI design is ChatGPT's: the brief gives data, actions and safety rules only. |
| Config | Saved settings, applied on game launch when the owner turns auto-apply on. |
| Diagnostics | `logs/core.log`; a research build (dump, traces); a frame timer for budgets. |

## 4. Performance

- An idle plugin (all features off) adds no per-frame work.
- Each feature is measured on and off with our own frame timer before it ships, and its cost
  is written in its PR.
- No `FindObjectsOfType` or scene scans in anything that runs per frame; object lists come
  from hooks on the objects' own creation and destruction.

## 5. First features (candidates; the owner picks the order)

| Feature | Game code found (static) |
|---|---|
| Experience multiplier | `ExperienceTracker.GainExp*`, `ExperienceGainedOnKill.GiveExp` |
| Gold multiplier (gains only) | `GoldTracker.modifyGold`, `ItemDrop.SpawnGoldForActor` |
| Item drop rate | `ItemDrop.getItemDropChance`, `getEnemyBaseItemDropRate` |
| Monster density | `Spawner.numberToSpawn`, `SpawnerPlacementManager.RollSpawners` |
| Loot quality (rarity, legendary potential, affix tiers) | `ItemDrop.DropItem` and item creation: not established yet |
| Pickup and auto-loot | not established yet |

Each feature ships with: a slider or switch in the panel, a live check in the game that
proves the effect with the game's own numbers, and its frame cost.

## 6. Testing

- **Hook engine:** decoder tests on recorded prologue bytes from this game build, plus a test
  program that hooks its own functions (install, call, remove, call again).
- **Features:** a behaviour harness for the plugin logic (ForgePact's `stat_add_harness`
  pattern), and a live check per feature on an offline character, driven by code (no
  clicking): read the value, act, read it again.
- **Panel:** the same browser-suite approach as ForgePact's panel.

## 7. Milestones

| Round | Content |
|---|---|
| 0 | Research, loader, resolver, dump. **Done 2026-10-02.** |
| 1 | Hook engine with tests; the offline gate; the first feature end to end, live-verified. |
| 2 | Command channel and panel; config and auto-apply. |
| 3 | The rest of the first feature list, each measured. |
| 4 | Installer, update path, release. |

## 8. Decisions for the owner

1. The feature order for round 1 and after.
2. The name (EpochPact is a working name).
3. Where the code lives: a private GitHub repository, and whether it joins the toolkit hub.
