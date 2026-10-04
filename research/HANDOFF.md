# EpochPact — handoff report (2026-10-04)

Everything a new researcher (human or AI) needs to continue the Last Epoch modding work.
The short operating manual is `GEMINI.md` in the repository root; the round-by-round detail
is `research/findings.md`; the character-sheet stat map is `research/stat-map.md`. This file
condenses the state, the measured results, the traps, and the next steps.

## 1. What we are building

ForgePact-style mods for Last Epoch 1.5.0.1: a native `version.dll` loader starts our own
`EpochPact.Core.dll` inside the game; commands arrive through files (`EpochPact\ipc\cmd.txt`,
replies in `out.txt`); every feature works on **offline characters only** and refuses online
with a reason. No mod loader, no third-party hook library, no code from other mods.

Rules: saves are backed up before every session; a foreign `version.dll` is never
overwritten; `<game>\EpochPact\disabled` is the kill switch; game metadata stays out of git.

## 2. The environment

| Thing | Value |
|---|---|
| Game | Last Epoch 1.5.0.1 (Steam app 899770), Unity 6000.4.8f1, IL2CPP, x64 |
| Game folder | `C:\Program Files (x86)\Steam\steamapps\common\Last Epoch` |
| Our files in the game | `version.dll`, `EpochPact\EpochPact.Core.dll`, `EpochPact\ipc\`, `EpochPact\logs\core.log`, `EpochPact\dump\` |
| Saves | `%USERPROFILE%\AppData\LocalLow\Eleventh Hour Games\Last Epoch\Saves` (offline `1CHARACTERSLOT_BETA_<n>`, `STASH_*`) |
| Repo | `C:\Users\falor\OneDrive\Belgeler\Last Epoch\EpochPact` |
| Build | `native\build.bat` (MSVC x64, static CRT) |
| Tests | `native\build\hook_test.exe` 48/48; `native\build\xp_test.exe` 24/24 |
| Python | `py -3`, needs `pefile` and `capstone` for the static tools |

The metadata dump (`research/game-data/dump-1.5.0.1`: `dump.cs` 44 MB, `methods.tsv` 35 MB,
`fields.tsv` 24 MB) is produced by a research build on first game start; it is not in git.
Regenerate with `<game>\EpochPact\dump\done.txt` deleted and a research build installed.
The dump of this build: 40,722 classes / 271,838 methods / 221,788 fields; `LE.dll` alone
has 13,517 classes; 39 classes faulted while being read.

## 3. How the code gets in (summary)

- `Last Epoch.exe` imports only `UnityPlayer.dll`; `UnityPlayer.dll` imports `version.dll`
  (not a KnownDLL), so our proxy in the game folder is loaded first. It forwards the 17 real
  exports and starts the core only when the process is `Last Epoch.exe`.
- `GameAssembly.dll` exports 241 `il2cpp_*` runtime functions. We resolve classes, methods,
  fields and static fields **by name** (`game::FindMethod`, `FieldOffset`, `FindClass`,
  `FindStaticField`, `StaticObject`). No offsets are hard-coded; a game update only breaks a
  feature if the name or signature changes.
- The hook engine (`x64_decode`, `hook`) installs a 5-byte jump to a relay in 2 GB range,
  relocating the replaced instructions; it refuses anything it cannot decode. Installs and
  removals suspend the game's other threads and retry if one stands inside the bytes.
- `mainthread::Run` queues jobs on `UnityEngine.EventSystems.EventSystem.Update` (the frame
  hook is installed on demand and removed again after two idle seconds). **The game stops
  updating when its window is not focused**, so main-thread jobs only run while the game is
  focused. This is why all stat reads/writes now run on the **command thread** (they only
  touch managed memory, wrapped in `game::Guarded`). `research::KeepTicking` also tries
  `Application.set_runInBackground(true)` at startup and from the command loop, but the game
  still pauses unfocused.
- The offline gate reads `EHG.Multiplayer.GameplayEnvironment._isOnlinePlay`; it is true at
  the login/character screens and false only once offline play starts. Every feature checks
  it before acting.

## 4. Features and live results

All multipliers accept `1` (= off; the hooks are removed) and refuse online.

| Command | Hook / mechanism | Live result (offline, 1.5.0.1) |
|---|---|---|
| `xp <1-100>` | `ExperienceTracker.GainExpFromEnemyOrMote(long)` per kill/mote | ×10: 27 gains, first 3→30, latest 15→150 |
| `gold <1-100>` | `GroundItemManager.pickupGold` → `GoldTracker.modifyGold(int)` only while picking up | ×10: 11 pickups, first 2→20 |
| `drops <1-25>` | static `ItemDrop.DropItem` (18 params), scales `itemMultiplier` (item count, not chance) | ×10: first drop 1→10 items |
| `density <1-5>` | `Spawner.numberToSpawn` (+0x44) while `GenerateEntitiesInternal` rolls the pack; packs only (>1.5) | ×3: 14 packs, first 2→6 |
| `rarity <1-10>` | static `GenerateItems.RollRarity(int,float)->byte` (0 normal .. 4 unique/set): upgrade one tier with chance (n-1)/n | ×10, 300 rolls: `0:212 1:53 2:32 3:0 4:0` → `0:17 1:207 2:43 3:32 4:0` |
| `autopickup <0/1>` | `DistantItemPickupHandler.OnUpdateTick` every 0.75 s: static `ItemTooltipOrganizer.pickableGroundLabelList` (DList→List→array) + `GroundItemManager.activeGoldPiles/activePotions/activeXPTomes/activeFavorTomes/activeAncientBones`; calls the game's own `requestPickup`/`PickUp` | 452 scans, 15 pickups, no errors |
| `speed <1-5>` | writes `increasedValue` of the player's `Stats.Stat` entry for SP.Movespeed=9 (tags 0), then sets `statsNeedToBeUpdatedNextFrame` | +100%: entry 0.1→1.1, NavMeshAgent 5.49→10.53, feels natural (animation follows the same stat) |
| `cooldown <1-10>` | `PlayerChargeManager.OnUpdateTick(dt)` scales deltaTime; `ChargeManager.getCooldown(int)` divides new cooldown lengths | ×5: `dt 0.0549 → 0.2745`, 23k ticks |
| `stat <name> [value]` | generic stat editor driven by `research/stat-map.md`; writes `addedValue` (flat) or `increasedValue` (fraction); creates the entry with `il2cpp_object_new` + the list's `Add` if missing | fire 75 / block 35 / bowattackspeed 3 wrote; C screen did not show them yet (see §6) |

Research commands: `raritytest <n>`, `statprobe`, `statscan <sp>`, `speedread`, `posread`,
`xpread`, `xpgain <n>`, `xpkill`, `enemies`, `charsel`, `load <i>`, `tab`, `offline`,
`playoffline`, `bg <0/1>`.

## 5. Research tools

| Tool | What it does |
|---|---|
| `tools/le_session.py` | status / install / uninstall / launch (backs up saves) / wait-dump / close (WM_CLOSE) / cmd (IPC) / restore-saves |
| `tools/speed_check.py` | samples `posread` for a few seconds and reports the distance walked |
| `tools/speed_live_check.py` | waits for movement, then measures x1 vs x5 walking speed automatically and restores x1 |
| `research/tools/callers.py` | every `E8` call site of named methods across GameAssembly (who calls it) |
| `research/tools/callmap.py` | the methods a named method calls directly (callees + first bytes) |
| `research/tools/disasm.py` | disassembles a method by RVA (capstone) — this is how the real speed/cooldown paths were found |
| `research/tools/pe_scan.py` | imports/exports of the game's PE files (how the `version.dll` proxy was chosen) |

## 6. Open threads (in priority order)

1. **Stat persistence (the current blocker).** `stat fire 75` / `block 35` /
   `bowattackspeed 3` write successfully and the read-back is correct immediately after, but
   the C screen still shows the old values (fire 0, block 18, bow 1). Most likely
   `BaseStats` recomputes its stat list and drops or overwrites foreign entries.
   Suggested work: with the current build (stat ops run on the command thread, so no focus
   needed) run `statscan 13|29|2` before and after a write with the game running; if the
   entries vanish after a stat update, hook `BaseStats.UpdateStats` (0x1679090) or
   `UpdateStatsInternal` (0x1678E00), keep a small override table, and re-apply the wanted
   `addedValue`/`increasedValue` after the original runs. Trigger only when overrides exist.
2. **Creating entries from nothing safely.** `EnsureStatEntry` uses `il2cpp_object_new` on
   the existing entry's class plus `List<Stats.Stat>.Add` (found through
   `object_get_class(list)` + `class_get_method_from_name(..., "Add", 1)`). It works; keep
   an eye on the GC and on racing the main thread's list iteration.
3. **Frame cost.** No per-frame timer yet; measure each feature on/off before shipping.
4. **Loot quality deeper:** `GenerateItems.RollTier`, `initialiseRandomItemData`,
   legendary potential, Weaver's Will.
5. **Loot filter and crafting:** `ItemFiltering.ItemFilterManager` (45 methods),
   `CraftingManager`.

## 7. Traps we already paid for

- IL2CPP GC handles are pointer-sized (32-bit truncation crashed the game).
- An attached thread blocks the game's quit; attach only while a command runs.
- `Stats.Stat.increasedValue` is a fraction (0.1 = +10%); writing 100 sent the agent to
  509 u/s before it was caught.
- Calling `BaseStats.ChangeStatModifier` (or the class+0x2F8 virtual Swiftness uses) crashed
  the game even with a register/stack layout copied from disassembly. Field writes reach the
  same result with no calls.
- The main-thread `EventSystem.Update` hook only ticks while the game is focused; anything
  that must work unfocused belongs on the command thread and must not touch Unity APIs.
- `gold` must scale only the pickup path (`pickupGold` → `modifyGold`); the other 14 callers
  are shops, respecs, quest rewards and stash tabs.
- `drops` scales the item **count** (`itemMultiplier`), not the chance (capped at 100%).
- Resistance/attribute stats are `addedValue`, damage/speed/crit are `increasedValue`
  (fraction). The full mapping is `research/stat-map.md`.

## 8. How to start fresh

1. `py -3 tools/le_session.py status` — is the game installed/running, is the dump there.
2. `native\build.bat` — build; run both test executables, they must stay green.
3. `py -3 tools/le_session.py install` (game closed) and `launch` — the saves are backed up.
4. Press **PLAY OFFLINE**, load an offline character; poll `cmd posread` until it answers.
5. Arm features with `cmd "xp 10"`, `cmd "rarity 10"`, ... ; read states with `cmd status`.
6. Close with `le_session.py close` (the game saves) and restore saves if a test wrote
   something unwanted.

The latest build also installs the `stat` command and runs its reads/writes on the command
thread; the game window may be unfocused while those run.
