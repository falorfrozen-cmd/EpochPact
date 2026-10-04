# EpochPact — agent handoff (read this first)

Last Epoch 1.5.0.1 modding project: a native plugin loaded by the game, commands sent to it
from outside, every line our own (no mod loader, no third-party hook library). Offline
characters only all features refuse in online play.

This file is the operating manual for an AI agent picking the project up (Gemini CLI reads
`GEMINI.md` as context automatically). Deeper material:
- `research/findings.md` — every finding, round by round, labelled measured/static.
- `research/stat-map.md` — the C-screen stat map: every line to its `SP`/`AT`/`AilmentID`.
- `research/README.md` — the research-folder index.
- `docs/requirements.md` — what we build and the rules it keeps.

## Repository layout

| Path | What it is |
|---|---|
| `native/proxy/` | the loader: `version.dll` forwarding the 17 real exports, starts the core only in `Last Epoch.exe`. |
| `native/core/` | the plugin: `il2cpp_api` (runtime API by name), `hook`/`x64_decode` (our inline detour), `game` (class/method lookup, offline gate, GC handles), `mainthread` (jobs on `EventSystem.Update`), `commands` (file IPC), features `xp`, `loot`, `items`, `player`, `dumper`, `research`. |
| `native/tests/` | `hook_test.exe` (engine; 48/48), `xp_test.exe` (xp feature against a stand-in; 24/24). |
| `tools/` | `le_session.py` (install/launch/close/cmd/restore-saves), `speed_check.py`, `speed_live_check.py`. |
| `research/` | findings, stat map, static-analysis scripts, the metadata dump (not in git). |
| `live/` | save backups and core logs (not in git). |

## Build and test

```
native\build.bat            # MSVC x64, static CRT; builds version.dll + EpochPact.Core.dll + tests
native\build\hook_test.exe  # must stay 48/48
native\build\xp_test.exe    # must stay 24/24
```

Live loop (backs the saves up first; the game is at `C:\Program Files (x86)\Steam\steamapps\common\Last Epoch`):

```
py -3 tools/le_session.py status
py -3 tools/le_session.py launch          # Steam; poll `cmd posread` until it answers (player in a zone)
py -3 tools/le_session.py cmd "<command>" # through <game>\EpochPact\ipc\cmd.txt
py -3 tools/le_session.py close           # WM_CLOSE, the game saves and exits
py -3 tools/le_session.py restore-saves <backup folder>
```

## How the plugin works

- `GameAssembly.dll` exports 241 `il2cpp_*` functions; classes/methods/fields are found **by
  name** at runtime (`game::FindMethod/FieldOffset/FindClass/FindStaticField/StaticObject`).
  Never hard-code offsets: verify them in the dump first.
- The hook engine patches the first instructions with a 5-byte jump to a relay (within 2 GB),
  relocating RIP-relative operands and short jumps; anything it cannot decode is refused.
- Command channel: `<game>\EpochPact\ipc\cmd.txt` in / `out.txt` out; the loop attaches to
  IL2CPP only while commands run (an attached thread blocks the game's quit otherwise).
- **The focus trap:** the game stops updating when its window is not focused, so
  `mainthread::Run` jobs (queued on `EventSystem.Update`) do not run. `research::KeepTicking`
  tries `Application.set_runInBackground(true)` and retries from the command loop, but in
  practice the game still stops. **Stat reads/writes were therefore moved to the command
  thread** (`player::ReadStat/SetStat/StatScan` call pure managed memory only, wrapped in
  `game::Guarded`). Anything that touches Unity APIs (transforms, NavMeshAgent) still needs
  `mainthread::Run` and a focused game; diagnostics commands (`posread`, `speedread`) are
  like that.
- Offline gate: `EHG.Multiplayer.GameplayEnvironment._isOnlinePlay`; every feature refuses in
  online play and says why.

## Current features (all offline-only; `x1`/`0` disarms and removes the hooks)

| Command | What it does | Hook / mechanism |
|---|---|---|
| `xp <1-100>` | kill/mote experience × n | `ExperienceTracker.GainExpFromEnemyOrMote` |
| `gold <1-100>` | gold picked up from the ground × n | `GroundItemManager.pickupGold` → `GoldTracker.modifyGold` (only while picking up) |
| `drops <1-25>` | item count of every loot drop × n | static `ItemDrop.DropItem` (`itemMultiplier`) |
| `density <1-5>` | monsters per pack × n (packs only) | `Spawner.numberToSpawn` (+0x44) inside `GenerateEntitiesInternal` |
| `rarity <1-10>` | each rarity roll upgrades one tier with chance (n-1)/n | static `GenerateItems.RollRarity` (0..4) |
| `autopickup <0/1>` | every 0.75 s vacuums item labels, gold, potions, tomes, bones | `DistantItemPickupHandler.OnUpdateTick` + the game's own pickup calls |
| `speed <1-5>` | writes + (n-1)×100% into the player's Movespeed `Stats.Stat` `increasedValue` | direct field write + `statsNeedToBeUpdatedNextFrame` |
| `cooldown <1-10>` | charges/cooldowns tick n× faster | `PlayerChargeManager.OnUpdateTick` deltaTime × n; `ChargeManager.getCooldown` ÷ n |
| `stat <name> [value]` | generic stat editor from `research/stat-map.md`; creates the entry with `il2cpp_object_new` + the list's `Add` if missing | pure memory write on the command thread |
| `status` | gate + every feature's state | |

Research-only commands: `raritytest <n>`, `statprobe`, `statscan <sp>`, `speedread`,
`posread`, `xpread`, `xpgain <n>`, `xpkill`, `enemies`, `charsel`, `load <i>`, `tab`,
`offline`, `playoffline`, `bg <0/1>`.

## Measured results (live, offline character)

- xp ×10: 27 gains boosted (3→30, 15→150). gold ×10: 11 pickups (2→20). drops ×10: 1→10
  items. density ×3: 14 packs (2.6→7.8).
- rarity ×10, 300 rolls: `0:212 1:53 2:32 3:0 4:0` → `0:17 1:207 2:43 3:32 4:0`.
- autopickup: 452 scans, 15 pickup calls, no errors.
- speed +100%: Movespeed `increased 0.1 → 1.1`, NavMeshAgent 5.49 → 10.53, owner confirms
  it feels natural (the animation follows the same stat).
- cooldown ×5: `cooldown: first boosted tick dt 0.0549 -> 0.2745`; 23k ticks.
- hook engine 48/48, xp feature 24/24 without the game.

## Critical lessons / gotchas

- GC handles are pointer-sized in IL2CPP (32-bit handles crash the game).
- `Stats.Stat.increasedValue` is a **fraction** (0.1 = +10%), not a percentage. Writing 100
  (+10,000%) sent the agent to 509 u/s.
- Calling `BaseStats.ChangeStatModifier` (or the class+0x2F8 virtual the game's Swiftness
  uses) crashed the game even when mirroring the register/stack layout read from
  disassembly. Do not call it; write the `Stats.Stat` fields instead.
- Stat layouts: player `BaseStats` = `[PlayerFinder.getPlayerActor()+0x108]` →
  `CharacterMutator` → `myStats` +0x98; `Stats.stats` (List<Stats.Stat>) +0x88;
  `Stats.Stat.property` +0x10, `specialTag` +0x11, `tags` +0x14, `extraTag` +0x18,
  `addedValue` +0x1C, `increasedValue` +0x20; `statsNeedToBeUpdatedNextFrame` +0xD8.
- IL2CPP ABI notes: some methods take their first float in xmm2 and a byte enum in dl;
  when calling through a MethodInfo code pointer, copy a working call site's layout
  (`research/tools/disasm.py`, capstone).
- The dump is not committed: `research/game-data/dump-1.5.0.1/` is regenerated by a research
  build the first time the game starts (delete `<game>\EpochPact\dump\done.txt`).

## Current state / open threads (2026-10-04)

1. **Stat persistence.** `stat fire 75`, `stat block 35`, `stat bowattackspeed 3` write
   successfully (fire/bow created new entries) but the C screen showed old values (0 / 18 /
   1). Suspected: `BaseStats` recomputes its stats list and drops/overwrites foreign
   entries. Next: run `statscan 13|29|2` before/after a write (the new build does stat work
   on the command thread, so this works while unfocused), then if entries are dropped, hook
   `BaseStats.UpdateStats`/`UpdateStatsInternal` and re-apply a small override table after
   every recalculation.
2. Creating entries for stats the character has no source for (works via
   `il2cpp_object_new` + `List.Add`; keep an eye on GC safety).
3. Frame cost: add a per-frame timer and hold every feature to a budget.
4. Legendary potential / affix tiers: `GenerateItems.RollTier`, `initialiseRandomItemData`.
5. Loot filter (`ItemFiltering.ItemFilterManager`) and crafting (`CraftingManager`).

## Rules that must not be broken

- Offline only; never touch online characters. Every feature refuses online.
- Saves are backed up before every session (`le_session.py launch`); restore with
  `restore-saves`.
- Never overwrite a `version.dll` that is not ours; the installer refuses.
- `<game>\EpochPact\disabled` is the kill switch.
- Game metadata dumps stay out of git.
