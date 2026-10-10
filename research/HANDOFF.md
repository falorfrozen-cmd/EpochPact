# EpochPact — handoff report (2026-10-04)

> Latest CoF tuning update (2026-10-08): all six requested groups implemented,
> compiled and installed. UI calls tools/cof_backend.py: charge_multiplier,
> reward_multiplier, double_drop_chance(enemy/echo), exalted_multiplier,
> t7_multiplier, lp_multiplier and lens_multiplier(celerity/charity/duplication).
> See docs/cof.md. 42 final isolated feature checks, native CoF 48/48 and Python
> 27/27 passed; tuning faults 0. Actual rewards 20→60 at x3, 40 guaranteed
> Duplication, 120 combined with one consumed charge; normal echo 1→2 at 100%;
> eligible LP coefficient 2→6, native ineligible baseline stays 1. Actual T7 loot
> produced; Exalted/T7 independence and both shared-drop disable orders verified.
> All temporary fields and hooks restored on disarm. The research echo probe
> calls the normal next-echo reset; production keeps the one-claim guard intact.
> Fresh backup 20261007-234638 only: all 54 owner files restored byte-for-byte,
> 3 clone files archived. Falor running PID 40168, id 0/level 8/XP 1016/Z32;
> rank 12/Favor 100168, slots/effects/lenses/rewards/items preserved (11 checks).
> Favor x5 rearmed; Reputation x1, all new settings neutral. Final installed core
> hash verified. Latest evidence: research/live/cof-tuning-20261007/owner-final.json
> and checks.json; cof-tuning-test-manifest.json is restored=true. Older state
> blocks below are historical; do not replace current owner progress with them.

> Reputation gain update (2026-10-07): Separate `cofrepmult <1–100>` and
> `cof_backend.reputation_multiplier(value)` implemented and installed. Existing
> Favor API is `favor_multiplier` (`multiplier` remains a compatible alias).
> Gained/spent Favor Reputation passes through the normal scoped CoF hook; rank
> advancement, bonus events and maximum rank use the original method. Manual
> Reputation gifts are excluded. Favor x5 + Rep x3 yielded 105 Favor / 630 Rep
> versus baseline 21 / 42; spend 10 Favor yielded 20 vs 60 Rep without cost change.
> 28 isolated live checks passed; faults 0. CoF native 25/25; Python 25/25 including
> three tests for transient IPC out.txt locks without resending a mutation.
> Full current saves were taken after Falor quit normally; 54 original files
> restored byte-for-byte, three clone files archived. Manifest/evidence:
> research/live/cof-repmult-test-manifest.json and cof-repmult-20261007/.
> Historical pre-CoF snapshots must not replace current owner progress.
> Falor reopened and verified id 0/level 8/XP 1016/Z32; rank 12/Favor 100168,
> effects, slots, lenses and savedItems preserved. Running PID 25060, Favor x5
> rearmed and Reputation x1. Installed core matches the final build by SHA-256.
> Latest owner evidence: cof-repmult-20261007/owner-final.json.

> Owner test update (2026-10-07): Falor id 0, level 8, Z32 is currently running
> offline. The owner entered the character to perform the described CoF test.
> Applied normal CoF join, rank 12, Favor 100000, all 12 lenses, four prophecy
> rewards/lenses 100/0, 101/1, 110/2, 104/4; slot 0 has two charges, others zero.
> Favor multiplier x5 active; initial telemetry zero gains/boosted/faults. Owner
> screenshot confirmed the UI. Subsequent game read: Favor 100120; hook boosted
> 15, last 1 -> 5, refused/faults 0; all four slots' partial charge advanced.
> Actual prophecy reward production remains unverified. Saved items matched the live
> pre-test snapshot. Full disk backup and validated rollback snapshot are in
> research/live/cof-owner-20261007/actions.json. Keep this session running and
> preserve subsequent owner gameplay; do not auto-restore historical test saves.
> Weaver still read-only. This supersedes the closed-game/Falor-unmodified state
> in the historical 2026-10-05 validation notes below.

> Latest update (2026-10-05, round 8): CoF management is installed and tested; the
> owner's UI can use tools/cof_backend.py. See docs/cof.md. Commands: cofread,
> cofjoin, cofrank, coffavor, cofreputation, coflenses, cofpreview, cofprophecy,
> cofcharges and coffavormult. Normal game APIs, loaded offline save identity,
> live/disk snapshots and undo are used. Favor balance edits do not charge
> prophecies; gain scaling does. Reputation is within the current rank. Available
> rewards use GetAvailableRewardsForRank so obsolete lower-rank variants are not
> shown as selectable. Rank lowering reconciles ToggleRanks in the decreasing
> direction: the installed SetRank sorts toggle endpoints and leaves bonuses on.
> 59 live checks passed on isolated EpCoFTest: 12 lenses, all four configurations,
> charge cap/decrease, preview without mutation, invalid inputs, rank bonus/slot
> removal, normal Reputation rank-up, actual GainFavor x1/x5 and wallet saturation.
> Normal restart preserved rank/favor/slots/lenses/charges; pre-join snapshot undo
> also survived reopening. Native CoF tests 16/16, combined Python tests 21/21;
> existing XP/stat/density/Monolith checks passed with the full build. Natural combat
> prophecy reward production and live MG switching were not tested this round.
> The spider faction is The Woven (TheWeaver, id 3); research/weaver-1.5.md combines
> official current sources and live data. Ten ranks provide 13 tree points;
> Woven Echo completions can provide 40 more, total 53. Compatible with CoF/MG.
> Weaver rank/Amber/tree were only read, not changed. All 54 original save files
> were restored byte-for-byte; three test files archived, Falor not loaded/advanced.
> Game closed normally; installed core matches the final build. Session bonuses
> are not rearmed. No desktop automation, new UI or permanent profile storage.

> Superseded update (2026-10-05, round 7): the Monolith backend is installed and tested.
> Read docs/monolith.md, research/factions-1.5.md and GEMINI.md. New commands:
> monolithread, monolithunlock, monolithselect, corruption, stability, stabilitymult,
> sessionread and read-only factionread. The owner already designed the UI and
> deferred permanent profiles there; this round adds the native/Python API only.
> An isolated character verified all 20 normal/Empowered unlocks, separate run
> values, panel selection, x5 positive gains, unchanged losses and game caps.
> Normal close/reopen preserved all run values and unlocks; undo also survived
> reopening. Checks: monolith 22/22, Python recovery/IPC 17/17, existing suites green.
> All 54 original pre-test save files were restored byte-for-byte; three test
> files were archived. Falor remains id 0, level 8. The game was closed at the
> beginning and is closed now; session stat bonuses/density are not rearmed.
> No faction membership, favor, rank or prophecy reward was changed. CoF/MG 1.5
> research uses official current sources plus live rank/lens assets. No natural
> echo was played in this round; AddStability was tested through the actual hook.
> Normal loading helpers now reject ongoing transitions and Login preview actors.
> No desktop input automation was used. This supersedes historical priorities below.

> Superseded update (2026-10-05, round 6): campaign + minimum level 55 and all-waypoint
> buttons are implemented. Read docs/progression.md and GEMINI.md first. The owner
> chose normal quest rewards plus a minimum level of 55. A separate offline clone
> verified 85 campaign records, 15 quest passives, 8 idol unlocks, 109 waypoint
> scene keys, persistence/no duplicate rewards, and actual EoT -> MonolithHub
> travel. Every action takes live + disk snapshots. Undo closes the game first;
> recovery validation tests are 13/13. The final test cleanup restores original
> Falor saves, then reapplies actor stat bonuses and density x3. This supersedes
> the historical priority and incomplete progression state below.

> Superseded state update (2026-10-05): read GEMINI.md and research/findings.md rounds
> 4–5 before using the historical report below. The full-key stat editor now reaches
> all 134 SP values and 206 character-sheet rows; the constructor/GC-root fix resolved
> frozen resistance labels and equipment errors. Stat operations run on the main
> thread. Density now has dedicated exclusions, bounded scaling and actual spawn
> telemetry; the owner confirmed x3 working in combat, with completed 8/8 and 9/9
> packs in the log. Tests: hook 48/48, XP 24/24, stat 33/33, density 37/37. Current
> commands and limits are in docs/stat-editor.md and docs/monster-density.md. Use
> background IPC/game APIs; no desktop input automation. Density resets after game
> restart; stat bonuses apply to the current actor only. Load by verified offline
> name/level using loadname, not the historical load <index> setter.

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
