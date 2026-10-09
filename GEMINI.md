# EpochPact — agent handoff (read this first)

Last Epoch modding project (current verified runtime: **1.5.2**, 2026-10-08): a native plugin loaded by the game, commands sent to it
from outside, every line our own (no mod loader, no third-party hook library). Offline
characters only all features refuse in online play.

This file is the operating manual for an AI agent picking the project up (Gemini CLI reads
`GEMINI.md` as context automatically). Deeper material:
- `docs/ui-catalog.md` / `ui/catalog.json` — one UI contract for every implemented control,
  command/API, units, limits, defaults, persistence and runtime option IDs. Regenerate both
  with `py -3 tools/export_ui_catalog.py --refresh` (reads only).
- `research/findings.md` — every finding, round by round, labelled measured/static.
- `research/stat-map.md` — the C-screen stat map: every line to its `SP`/`AT`/`AilmentID`.
- `docs/stat-editor.md` — full-key editor commands, units, scope and verification.
- `docs/monster-density.md` — density commands, exclusions, telemetry and limits.
- `docs/progression.md` — campaign completion + minimum level 55, waypoint buttons, snapshots and undo.
- `docs/monolith.md` — timeline unlock/selection, corruption, stability and the UI backend.
- `docs/cof.md` — CoF management, Favor/Reputation, prophecy charge/reward, enemy/echo double-item chance, Exalted/T7/LP and separate lens controls; UI API and verified scope.
- `docs/loot-crafting.md` — smart pickup modes/filter/LP/T7 affixes, category toggles, normal forge FP/Basic Hope/Despair/shard controls and read-only preview.
- `docs/ce-feature-additions.md` — render-only map reveal, rune/glyph preservation, scoped crafting level bypass, one-shot Forge and separate live verification evidence.
- `docs/collection-navigator.md` — runtime Unique/Set atlas, loaded stash comparison, local reminders and existing Echo reward search/focus. No automatic web generation or item transactions. Player builds use `native\build.bat player` (or `player-core`); default builds remain research builds.
- `research/factions-1.5.md` — current CoF/Merchant's Guild mechanics, native data and APIs.
- `research/weaver-1.5.md` — The Woven/TheWeaver: Amber, Woven Echoes, tree points and runtime API research.
- `research/README.md` — the research-folder index.
- `docs/requirements.md` — what we build and the rules it keeps.

## Repository layout

| Path | What it is |
|---|---|
| `native/proxy/` | the loader: `version.dll` forwarding the 17 real exports, starts the core only in `Last Epoch.exe`. |
| `native/core/` | the plugin: `il2cpp_api` (runtime API by name), `hook`/`x64_decode` (our inline detour), `game` (class/method lookup, offline gate, GC handles), `mainthread` (jobs on `EventSystem.Update`), `commands` (file IPC), features `xp`, `loot`, `density`, `items`, `smart_loot`, `crafting`, `player`, `stat_editor`, `progression`, `monolith`, `cof`, read-only `factions`, `dumper`, `research`. |
| `native/tests/` | `hook_test.exe` (engine; 48/48), `xp_test.exe` (xp feature against a stand-in; 24/24), `stat_key_test.exe` (stat identity/validation; 40/40), `density_test.exe` (exclusions, bounds, restoration/reentrancy; 37/37). |
| `tools/` | `le_session.py` (install/launch/close/cmd/restore-saves), progression backend/panel/recovery tests, `EpochPact-Gorevler.cmd` button launcher, speed/stat live checks. |
| `research/` | findings, stat map, static-analysis scripts, the metadata dump (not in git). |
| `live/` | save backups and core logs (not in git). |

## Build and test

```
native\build.bat player     # verified player artifact, loader and tests
native\build.bat            # research artifact, loader and tests
native\build\hook_test.exe  # must stay 48/48
native\build\xp_test.exe    # must stay 24/24
native\build\stat_key_test.exe # 40/40
native\build\density_test.exe  # 37/37
native\build\monolith_test.exe # 22/22
native\build\cof_test.exe      # 48/48
native\build\loot_crafting_test.exe # 6260/6260
native\build\review_test.exe # worker transaction, retention, IPC claim and cooldown regression
native\build\mainthread_test.exe # actual queue/hook code against synthetic Unity Update
py -3 -m unittest tools.test_progression_backend tools.test_monolith_backend tools.test_cof_backend tools.test_le_session -v # 27 recovery/IPC tests
py -3 -m unittest tools.test_loot_crafting_backend tools.test_ui_bridge -v # IPC validation + catalog/UI integration
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
- Command channel: `<game>\EpochPact\ipc\cmd.txt` in / nonce-matched `reply.json` out.
  Only nonce-less legacy commands append to `out.txt`. The loop attaches to
  IL2CPP only while commands run (an attached thread blocks the game's quit otherwise).
- **The focus trap:** the game stops updating when its window is not focused, so
  `mainthread::Run` jobs (queued on `EventSystem.Update`) do not run. `research::KeepTicking`
  tries `Application.set_runInBackground(true)` and retries from the command loop, but in
  practice the game can still stop. **Stat reads/writes now run on the main thread**:
  mutating List<Stats.Stat> from the IPC thread races equipment/buff updates. SetStat
  refuses on a queue timeout without applying the write. Recalculation and UI updates
  also require the main thread. Use IPC for game interaction; the owner prefers no
  desktop input automation. If the game is not ticking, report the timeout.
- Offline gate: `EHG.Multiplayer.GameplayEnvironment._isOnlinePlay`; every feature refuses in
  online play and says why.

## Current features (all offline-only; `x1`/`0` disarms and removes the hooks)

| Command | What it does | Hook / mechanism |
|---|---|---|
| `xp <1-100>` | kill/mote experience × n | `ExperienceTracker.GainExpFromEnemyOrMote` |
| `gold <1-100>` | gold picked up from the ground × n | `GroundItemManager.pickupGold` → `GoldTracker.modifyGold` (only while picking up) |
| `drops <1-25>` | item count of every loot drop × n | static `ItemDrop.DropItem` (`itemMultiplier`) |
| `density <1-5>` | future regular enemy pack sizes × n; explicit exclusions and bounded pre-rarity roll | temporary Spawner.numberToSpawn; generator queue + SpawnOneImmediately/FinishSpawning telemetry |
| `densityread` | on-demand scene spawners, exclusions, queues and spawned lists | main thread; no per-frame scene scan |
| `rarity <1-10>` | each rarity roll upgrades one tier with chance (n-1)/n | static `GenerateItems.RollRarity` (0..4) |
| `autopickup <0/1>` | every 0.75 s picks selected labels and enabled gold/potion/tome/bone categories | `DistantItemPickupHandler.OnUpdateTick` + the game's own pickup calls; GC-rooted snapshots |
| `lootread` / `lootmode all\|filter\|quality\|materials` | smart pickup state/runtime affix catalog and selection | native ItemFilter.Match; selection does not enable auto pickup |
| `lootlp` / `loott7` / `lootaffixes` / `lootfilter` | quality = Unique LP threshold OR wanted T7; optional native filter | LP 0–4; stored tier 6 = T7; up to 64 runtime affix IDs |
| `lootcategory materials\|gold\|potions\|xp\|favor\|bones 0\|1` / `lootreset` | independent pickup categories / neutral selection defaults | inventory capacity remains authoritative; no item deletion |
| `craftread` | actual local forge eligibility, cost range and glyph chance preview | no RNG draw; exact outcome is unknown |
| `craftfp <0–1>` / `crafthope` / `craftdespair` `<0–100\|reset>` | normal forge FP cost / Basic Hope / eligible T1–T4 seal chance | scoped original forge/cost/RNG/getter calls; native FP=0 and item rules remain |
| `craftshards 0\|1` / `craftreset` | refund one shard actually consumed / neutral settings and remove hooks | combined storage + modifier count; rune/glyph preservation has separate switches |
| `craftrunes 0\|1` / `craftglyphs 0\|1` | preserve consumed runes / glyphs | combined storage + both forge slots; refund only an actual loss of one; normal crafting eligibility |
| `craftlevel 0\|1` | bypass only the local forge affix-upgrade level requirement | scoped CheckForgeCapability / CalculateLevelRequirementAfterShard; equipment level and player level unchanged |
| `craftforge <id>` | one normal selected Forge action | verify loaded offline identity; serialize current items and snapshot before calling Forge; never retry a failed/lost response |
| `mapreveal 0\|1` / `mapread` | reveal minimap/overlay / read render state | substitute fog shader input; original exploration texture unchanged; restore and remove both hooks at zero |
| `speed <1-5>` | separate + (n-1)×100% Movespeed contribution | isolated stat_editor entry + recalculation |
| `cooldown <1-10>` | local player charges/cooldowns tick n× faster | `PlayerChargeManager.OnUpdateTick` deltaTime × n; check Actor.chargeManager and ChargeManager.actor; global getCooldown stays unchanged |
| `stat <name> [value]` | convenient aliases for isolated EpochPact modifiers | full-key editor; constructor + GC root + dontCollapse; virtual UpdateStatsInternal via runtime_invoke |
| `sheetread` | reads the seven resistance TMP_Text labels | main thread, no UI input |
| `sheetstats` / `sheetstat <row> [mode value]` | discovers all C rows and edits full identities, including minions, ailments and secondary modifiers | runtime metadata; inactive labels may be cached; no per-frame scan |
| `statraw <SP> <tags> <special> <extra> [mode value or reset]` | all 134 SP enum properties, added/increased/more | separate owned entries; read applicable engine modifiers too |
| `statreset` | removes all current actor's EpochPact stat entries | never edits equipment entries |
| `playerread`, `characters`, `loadname <name> [level]`, `sheetopen 0/1` | identity check, normal offline tile load, C panel open/close | game APIs over IPC; no desktop automation |
| `progressread` | JSON: loaded identity/scene/rewards, runtime quest and waypoint catalogs | one-shot main-thread game reads/map refresh |
| `questscomplete <offline save id>` | finish pending main/side campaign quests and grant normal rewards, then raise to at least 55; open endgame waypoints | normal Quest.completeQuest / GainExpDirect / AddUnlockedWaypointScene; snapshot before change |
| `waypointsunlock <offline save id>` | open all actual waypoint scenes and refresh their map buttons | game waypoint API; same snapshot/identity checks |
| `sessionread` | settled client state and transition status | prevents actions on Login character previews |
| `monolithread` | JSON timeline catalog, normal/Empowered unlocks, corruption/stability and selection | loaded offline actor; runtime asset limits |
| `monolithunlock <id>` | unlock all normal and Empowered timelines | normal unlock API; snapshot; coalesced notifications |
| `monolithselect <id> <timeline> normal\|empowered` | open and select that timeline in the normal Monolith panel | normal panel API; no echo start/teleport |
| `corruption` / `stability` `<id> <timeline> normal\|empowered <integer>` | edit a specific difficulty's persisted run | hub only; bounded; snapshot before write |
| `stabilitymult <1-100>` | multiply positive stability gains; x1 removes hook | session only; losses and manual absolute edits excluded |
| `factionread` | JSON membership, favor, ranks, rank assets and CoF prophecy/lens data | read only; no faction mutations |
| `cofread` | CoF state, actual bonus fields, runtime reward availability, lenses and charges | see docs/cof.md |
| `cofjoin` / `cofrank` / `coffavor` / `cofreputation` | explicit loaded-id faction actions | normal APIs; snapshot; MG switch requires explicit flag |
| `coflenses` / `cofpreview` / `cofprophecy` / `cofcharges` | rank-gated lens unlock, selection preview/configuration, charge edit | normal slot copies/setters/validation; no manual item writes |
| `coffavormult <1–100>` | positive CoF favor gains; x1 removes hook | session only; normal reputation/prophecy flow remains |
| `cofrepmult <1–100>` | positive CoF Reputation gains from gaining/spending Favor; x1 removes hook | session only; normal rank progression; manual gifts excluded; combines with Favor multiplier on gained Favor |
| `cofchargemult` / `cofrewardmult` | independent prophecy charge speed / normal reward item count | scoped AddFavor / SpawnRewardForPlayer; wallet and charges consumed remain normal |
| `cofdouble enemy\|echo <0–100\|reset>` | separate double-item probabilities | enemy-death dispatcher shared with global drops; normal echo-specific rewards, excludes Tomb source |
| `cofexaltedmult` / `coft7mult` / `coflpmult` `<1–100>` | CoF loot Exalted/T7/eligible Unique LP roll coefficients | scoped normal item generation; preserves level/eligibility; LP original result checked first |
| `coflensmult celerity\|charity\|duplication <1–100>` | lens extra-charge contribution / duplication chance | shared assets temporarily borrowed, restored on return/exception |
| `status` | gate + every feature's state | |

Research-only commands: `raritytest <n>`, `statprobe`, `statscan <sp>`, `speedread`,
`posread`, `xpread`, `xpgain <n>`, `xpkill`, `enemies`, `charsel`, `load <i>`, `tab`,
`offline`, `playoffline`, `bg <0/1>`, `waypoints`, `travel <unlocked waypoint scene[:gate]>`,
`exits`, `exit <active scene destination>`.
The travel helper verifies loaded offline CharacterData and its UnlockedWaypointScenes;
it calls the selected active UIWaypointStandard.LoadWaypointScene, using the game's
normal transition flow. It does not unlock destinations. LE.Data.CharacterData exposes
the list through <UnlockedWaypointScenes>k__BackingField, not a global save-JSON field.
An unlocked save entry can still be inactive in the current map (Falor's Z22 is).
`exits` lists active LoadSceneInteraction destinations and checks their ConditionHandler;
`exit` triggers the existing TryTriggerInteraction only for one ready active destination.
It does not reset conditions, toggle triggers or unlock quest gates.

## Measured results (live, offline character)

- 2026-10-08, runtime **1.5.2**: separate solo clone `EpCraftTest` passed real native
  SHOW/HIDE filter queries, materials-only pickup preserving ground equipment,
  generated Unique LP0 rejection / LP2 acceptance, generated Exalted wanted-T7
  selection and normal inventory-capacity behavior. Actual normal forge operations
  passed Basic Hope 0/100%, Despair 0/100%, FP cost 0/0.5/1 and shard preservation
  on/off; all crafting hooks removed on reset; crafting/loot fault counters zero.
  Evidence: `research/live/loot-crafting-live-check.json`. All **54 original save
  files** restored byte-for-byte from `research/live/saves-backups/20261008-053550`;
  clone files archived. New native DLL is installed; game closed normally.
  UI contract now has **68 controls**; new controls are in General settings.

- xp ×10: 27 gains boosted (3→30, 15→150). gold ×10: 11 pickups (2→20). drops ×10: 1→10
  items. density ×3: 14 packs (2.6→7.8).
- rarity ×10, 300 rolls: `0:212 1:53 2:32 3:0 4:0` → `0:17 1:207 2:43 3:32 4:0`.
- autopickup: 452 scans, 15 pickup calls, no errors.
- speed +100%: Movespeed `increased 0.1 → 1.1`, NavMeshAgent 5.49 → 10.53, owner confirms
  it feels natural (the animation follows the same stat).
- cooldown ×5: `cooldown: first boosted tick dt 0.0549 -> 0.2745`; 23k ticks.
- hook engine 48/48, xp feature 24/24 without the game.
- 2026-10-05: owner confirmed resistance labels and the first attack-speed/parry edits.
  After the reflect build restart, level 8 / XP 869 player was back in a zone, and IPC
  verified allres added 0.65, bow/melee AttackSpeed increased 5, Parry added 0.5,
  PercentReflect added 10. `sheetstats` read 187 displays and confirmed PercentReflect
  uses Display_100x_as_percentage (type 3); inactive defense labels were null/cached.
  Do not report reflected combat damage as measured: only the modifier was verified.

- Full editor live check: all 134 SP properties passed real constructor/list/getter/
  neutral removal and baseline preservation; 201 sheet rows / 165 distinct base keys
  passed. The final build added five basic-sheet LineItems without display components:
  endurance threshold +10 showed 56, glancing +0.1 showed 10%, parry +0.5 showed 50%,
  maximum companions +1 showed 3, PotionHealth +10 showed 92. Each temporary setting
  was reset. Final catalog: 206 rows. Requested five bonuses were restored on Falor
  level 8 offline. Native stat tests 33/33, xp 24/24; clean exits 0. The save's 37 items
  matched the pre-test snapshot exactly. Evidence: research/live/full-stat-editor-201-check.json,
  full-stat-editor-final-check.json and full-stat-editor-verification.json. Current
  catalog is research/live/character-sheet-catalog.txt. Do not equate neutral access
  tests with measuring every conditional stat in combat.

## Critical lessons / gotchas

- Steam replaced GameAssembly on 2026-10-08. Current Application.version is 1.5.2;
  the fresh metadata dump is authoritative. Do not reuse earlier 1.5.0.1 RVAs.
- ItemData raw rarity is a different domain from ItemRarity/RarityGroup enums:
  native `Item.rarityIsUnique` accepts **7**. Use `ItemData.isUnique()` in production.
  The normal item factory's `rarity` parameter is raw rarity, not either enum.
- Crafting RNG lives in **LE.Core.dll / LE.Core.RngElement**. Forge itself trusts
  normal UI capability checks; research probes must call CheckForgeCapability first.
  Raising a clone's level uses QuestListHolder.experienceTracker.GainExpDirect;
  Actor.experienceTracker.SetLevel alone did not update CharacterData.
- GenerateItems ChanceForExaltedAffixesMultiplier and ChanceForT7Affixes are
  **direct roll coefficients**, not extra +fraction / bare probability fields.
  Current CoF rank 12 baselines are 1.5 and 2 respectively; getters return the
  stored float unchanged. Scale them directly. RareItemsToBecomeExalted and
  Duplication are actual 0–1 chances. Celerity/Charity are extra-charge fractions.
- Double echo chance is read in MonolithItemManager.SpawnEchoSpecificRewards;
  hook that actual consumer, not only DropChestRewardItemsAtLocation. Scope to
  EchoRewardSource.NormalEcho (1); TombBossReward is 2. Normal game gold/XP
  exclusions remain. Enemy and global drops must share their dispatcher hook.

- GC handles are pointer-sized in IL2CPP (32-bit handles crash the game).
- `Stats.Stat.increasedValue` is a **fraction** (0.1 = +10%), not a percentage. Writing 100
  (+10,000%) sent the agent to 509 u/s.
- `addedValue` also uses fractions for resistance/chance/reflection percentages:
  `stat allres 0.65` adds 65 percentage points to all seven resistances;
  `stat parry 0.5` gives 50%; `stat reflect 10` means 1000% reflected damage.
  Attributes/health/armor use raw counts. Attack speed `increasedValue = 5` means
  +500% increased, not an absolute rate of 5 attacks/sec.
- **Always run Stats.Stat..ctor after object_new.** The constructor creates `moreValues`
  (+0x28). Leaving it null throws in `Stats.GetProtectionValue` and interrupts
  CharacterSheet.UpdateSheet AND equipment changes. It is not a missing refresh delegate.
- Resolve nested `Stats.Stat` through `class_get_nested_types`; runtime layouts and
  methods are resolved by name. The old 2026-10-02 dump's RVAs are stale after the game
  update: the current dump is `<game>\EpochPact\dump\`, not the old local snapshot.
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

## Current state / open threads (2026-10-08)

**Unified UI catalog (2026-10-08).** The owner's next step is wiring an existing UI.
Use `ui/catalog.json` as the central metadata and `docs/ui-catalog.md` as its generated
guide. `tools/export_ui_catalog.py` collects all 54 normal-build controls, 134 SP values,
72 aliases, 206 sheet mappings / 169 distinct base keys, 30 AT tags and 151 ailment IDs;
runtime options include 85 eligible campaign quests, 109 actual waypoints, 10 timelines,
12 lenses and 123 prophecy reward assets. Read-only refresh passed native command coverage,
runtime SP/alias matching, backend signatures and identity checks. The sheet was unavailable
during capture: mapped keys use the prior verified catalog; row IDs and cached labels are
not current-player values. Stat access uses full keys. No gameplay/save settings were changed.
MG and Weaver writes remain explicitly unavailable; permanent profile storage belongs to
the owner's UI work. Regeneration keeps the guide/JSON in sync; edit exporter metadata,
not the generated files. Historical actor bonuses below must not be treated as current values.

**Latest: all six CoF loot/lens controls implemented and installed.** Native
`cof_tuning.cpp` and `tools/cof_backend.py` expose independent prophecy charge,
reward quantity, enemy/normal-echo double-item chance, Exalted/T7/eligible LP roll
coefficients and Celerity/Charity/Duplication potency. See docs/cof.md for limits
and API names. Settings are session only; x1/reset removes dependencies. Shared
source fields are restored after each synchronous normal call. Enemy double-drop
shares the global drops dispatcher; both disabling orders passed live checks.

42 distinct final feature checks passed on isolated EpCoFTest; native CoF 48/48,
Python 27/27, tuning faults 0. Actual prophecy ground counts: 20 normal, 60 at
reward x3, 40 at guaranteed Duplication, 120 combined, one charge used each.
Normal echo-specific item reward: 1 at 0% vs 2 at 100%. LP coefficient 2→6 at
x3; rank without the native bonus stayed 1. Actual level-100 test loot included
T7 affixes. Exalted/T7 were also tested individually; sources remained 1.5/2.
Research echo probe uses guaranteed catalog rolls, zero extra count modifier and
the normal ResetPlayerRewards boundary between simulated echoes. Production
hooks preserve the game's one-claim guard. Earlier failed harness assertions are
retained with resolution in checks.json; finalChecks contains all 42 passing.

Latest recovery used only the fresh 20261007-234638 backup, restored 54 original
files byte-for-byte and archived 3 clone files. Falor reopened and 11 recovery
checks passed: id 0, level 8, XP 1016, Z32, CoF rank 12/Favor 100168, Reputation 0,
slots/lenses/effects/reward catalog and savedItems preserved. Favor x5 restored;
Reputation x1 and all new tuning controls neutral. Game running PID 40168;
installed DLL SHA-256 matches the final build. Latest evidence:
research/live/cof-tuning-20261007/owner-final.json and checks.json, manifest
research/live/cof-tuning-test-manifest.json (restored=true). Older state notes
below are historical; never restore their pre-CoF owner snapshots.

**Separate CoF Reputation gain control added (2026-10-07).** `cofrepmult` and
`cof_backend.reputation_multiplier` are independent of the existing Favor control.
Normal GainReputation is scoped to the loaded offline CoF member; its ordinary
rank progression/cap remains. Both gained-Favor and spent-Favor Reputation paths
are covered; only gained Favor compounds both multipliers. Manual Reputation
gifts bypass scaling. Read JSON includes both multipliers, rank-cap state and
the asset's gained/spent Favor conversion coefficients. 28 isolated live checks
passed: normal 21/42 Favor/Rep vs Favor x5 105/210, Rep x3 21/126, combined
105/630; spending 10 Favor gave 20 vs 60 Rep with unchanged cost. Rounding,
rank-up bonuses, max rank, other-faction exclusion and disarm passed; faults 0.
Native CoF 25/25; Python API/recovery 22/22 plus 3 IPC lock tests. `le_session.send`
retries temporarily locked out.txt reads without resending the command.
All 54 current owner files restored byte-for-byte, 3 clone files archived before
reopening Falor. Fresh backup/manifest is cof-repmult-test-manifest.json; do not
restore the earlier pre-CoF snapshot over current owner progress. New Reputation
multiplier starts x1. Falor was reopened and verified id 0, level 8, XP 1016,
scene Z32; CoF rank 12/Favor 100168, effects, lenses and slots matched the current
pre-test state; savedItems matched the fresh backup. Game running PID 25060,
Favor x5 rearmed, Reputation x1; installed core hash matches the build.
Evidence: research/live/cof-repmult-20261007/ (owner-final.json is the latest state).

**Owner gameplay test now active (2026-10-07).** Owner entered Falor's offline
character and authorised the previously described CoF test. Verified id 0, level 8,
scene Z32; no faction membership before the test. Normal APIs joined CoF, set rank
12/Favor 100000, unlocked all 12 lenses, configured slots (reward/lens) 100/0,
101/1, 110/2, 104/4, and gave only slot 0 two completed charges. CoF Favor gain
multiplier x5 is active for this session. Gameplay baseline: gains/boosted/faults 0.
Owner supplied a screenshot confirming rank 12/Favor 100000, four configured
prophecies and charges 2/0/0/0. Subsequent live play read Favor 100120, slot charge
fractions .12297/.08196/.07452/.08196; hook gains 43, boosted 15, refused/faults 0,
last 1 -> 5. UI and real game GainFavor/charge advancement confirmed; actual
prophecy reward production remains unverified (completed charges still 2/0/0/0).
Evidence: research/live/cof-owner-20261007/owner-ui-confirmed.json and owner-cof-screen.png.
Original membership/rank/favor/lens/slot state has a validated live/disk rollback
snapshot recorded in research/live/cof-owner-20261007/actions.json, plus full save
backup. Saved items matched that live snapshot after the settings. Leave the game
running for the owner; do not automatically undo or restore old 2026-10-05 saves.
Weaver remains read-only. Historical isolated-test results below still apply to
the earlier validation, not the current owner session.

**Latest module: CoF management and Weaver research.** Owner's existing UI will use
tools/cof_backend.py; no new UI or permanent profile storage was added. Read docs/cof.md
and research/weaver-1.5.md. Use normal Faction/ProphecySlot methods, not backing-field
writes. Available reward variants come from GetAvailableRewardsForRank: high rank can
replace the Rare asset with its Exalted variant. Rank lowering requires the normal
ToggleRanks call in the decreasing direction after SetRank, because this build sorts
the SetRank toggle endpoints. Favor balance edits do not charge prophecies; Favor gains
do. Reputation is progress within the current rank, not a cumulative account total.
The Woven (TheWeaver, id 3) is an endgame faction compatible with CoF/MG. Read-only live
data exposes 10 ranks, 13 rank points + 40 echo points = 53 total tree points. Weaver
rank/Amber/tree mutations remain future work. Continue only through background IPC.
CoF verification: 59 live checks, normal restart persistence and pre-join snapshot
undo/reopen passed; native rules 16/16, Python recovery/IPC 21/21. All 54 original
save files were restored byte-for-byte; three isolated test files archived. Falor was
never loaded/advanced. Game is closed, installed core matches the final build; session
stat/density/Favor multipliers are not rearmed. Evidence: research/live/cof-live-checks.json,
cof-reopen-check.json, cof-undo-reopen.json and cof-verification-summary.json.

**Campaign/endgame skip and all waypoints.** Owner explicitly chose
quest rewards + minimum level 55. Two buttons live in tools/progression_panel.py,
launched with tools/EpochPact-Gorevler.cmd. Native commands are progressread,
questscomplete <id> and waypointsunlock <id>. See docs/progression.md. A separate
offline clone verified 85 campaign records completed, 15 quest passives, 8 idol
unlocks, level 8 -> 55 and all 109 waypoint scene keys active; repeat actions gave
no extra rewards, and restart preserved progress. The clone really reached EoT
then MonolithHub. Normal game level/reward APIs are used, no direct stat/save edits
in a running game. Quest/map catalogs are runtime-derived, not hard-coded lists.
Endgame/repeatable/test quests are excluded; mastery/faction are not chosen.
Every action snapshots disk AND live character/stash/global data. Undo closes the
game first and restores affected files only, with another recovery backup.
The clone is reverted and original saves restored before leaving Falor running;
never apply progression to Falor merely to prove the button. Stat/density settings
must be reapplied after test restarts. The character-sheet editor milestone is
complete for runtime access (134 SP values, 206 rows, 169 distinct base keys); keep its
implementation and validation intact. Current Falor bonuses are allres added 0.65,
bow/melee AttackSpeed increased 5, Parry added 0.5 and PercentReflect added 10. These
are actor-memory settings and must be reapplied after a test restart. Density has a
dedicated module with explicit exclusions and spawn telemetry. The owner confirmed it
working in combat. The 03:19 session logged mean 2.7 -> 8.1 at x3, a generator queue of
9, and completed packs of 8/8, 9/9 and 9/9 observed/planned actors. These are real spawn
observations, not a controlled x1/x3 ratio experiment. Continue through background
IPC/game APIs; the owner does not want desktop control.

1. **Resistance display fixed and owner verified.** The root cause was unconstructed
   Stats.Stat objects (null moreValues). Properly initialise before insertion; do not
   hook UpdateStatsInternal merely to force refresh. Live `stat allres 0.65` produced
   physical 79% total / 75% capped, lightning 71%, fire/cold/void 65%, necrotic/poison 66%.
2. **Full-key stat access implemented.** The editor reads all 134 enum SP values by
   runtime name; arbitrary int32 tags/extra and byte special are supported, including
   ailments, PlayerProperty, AbilityProperty, minions, more/less, and secondary row keys.
   Each owned Stats.Stat is constructed and marked dontCollapse. Equipment/buff sources
   remain separate. Reset removes only owned references; a managed calculation failure
   rolls the change back. GameplayEnvironment and loaded CharacterData.IsOffline must
   both confirm offline play. Use runtime_invoke for reflected/generic operations rather
   than guessed float/enum ABI calls.
   Settings are actor memory only. Config and automatic application to replacement
   actors/zone loads remain future work. Derived displays/caps follow game calculations;
   level/XP/name are outside this combat-stat editor. Neutral access tests do not prove
   every stat's combat behavior. See docs/stat-editor.md and the live check script.
   Background loading uses loadname, not the old load <index> backing-field setter.
   Open C over IPC with sheetopen 1 before cataloging its lazily created components.
   If ordinary Steam startup waits on network services, the official --offline launch
   argument reaches the local offline path; launch backs up saves first. No desktop input.
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

## 2026-10-08 responsiveness repair

See docs/responsiveness-fix.md and docs/responsiveness-verification.json before
making release claims. The installed core is a player build. Bulk progression
yields one rewarded quest/level per frame, batches its nested quest display
refreshes, and scopes out analytics emitted by its own synthetic progression
calls. CPU samples identified Gpp analytics queue serialization, not save writes,
as the residual 25-second slowdown. Normal rewards, events, saving and natural
gameplay analytics remain intact. Five core attribute percentages are translated
into the game's actually consumed flat attribute contribution and read back from
CharacterStats; Intelligence 4 -> 8 was verified in the visible C sheet.
IPC v2 uses atomically claimed nonce packets; UI stat reads use identityread and
statraw without expensive progression catalogs. Never replay a timed-out mutation.
All 54 owner/shared save files were restored and hash-verified after isolated
tests. The owner's character was already level 55 before this repair; do not
restore a historical level-8 fixture over it. The fixture helpers are local tests,
not player watchers. No save action may run automatically on UI open.
