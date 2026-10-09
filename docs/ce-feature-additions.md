# Cheat Engine feature comparison and additions

The input was a list of Cheat Engine feature names, not a table with executable
scripts. These additions use EpochPact's existing native IPC, hook engine and
game APIs. No Cheat Engine scripts, guessed addresses or additional loaders
are required. Current verification target: Last Epoch **1.5.2**, offline.

## Selected additions

Map controls are in **General settings → Map visibility**. Crafting controls
have their own **Crafting** navigation page. Smart pickup has a separate
**Loot & Pickup** page. The interface remains English and both themes share the controls.

| Control | Native command | Default / scope |
|---|---|---|
| Reveal zone map | `mapreveal 0\|1` | Off; session, minimap and overlay rendering only |
| Read map visibility | `mapread` | Read only |
| Preserve runes | `craftrunes 0\|1` | Off; session, normal local Forge |
| Preserve glyphs | `craftglyphs 0\|1` | Off; session, normal local Forge |
| Bypass crafting level requirement | `craftlevel 0\|1` | Off; session, selected affix's upgrade eligibility only |
| Forge selected item once | `craftforge <loaded offline id>` | Explicit save action; no startup execution or retry |

Map reveal replaces the minimap's global fog shader input with the game's own
black texture. It never paints the real exploration texture or changes saved
exploration, objectives, enemies or waypoints. Map-load events rebind it. A
cheap offline gate restores the normal texture on an online transition; there
is no per-frame scene scan. Switching off restores the current map's real fog
texture and removes both hooks. Future map loads use the native texture normally.

Rune/glyph preservation requires the normal material to be present. It counts
storage plus both selected forge material slots, calls the original Forge once,
and refunds one only if that material's total actually fell by one. Native free
crafts and automatic slot refill do not generate extra materials. It does not
promise support for separate Weaver/Eternity Cache crafting panels.

The level bypass is scoped to the owned local Forge's `CheckForgeCapability`
call. It leaves equip requirements, character level, affix caps, sealed slots,
FP=0 rules and other normal restrictions intact. This is deliberately narrower
than a universal item requirement bypass.

The Forge button uses a fresh verified forge owner and normal eligibility. The
native command rechecks both. It serializes the live item containers and takes
the existing progression snapshot before calling Forge once. Snapshot strings
are captured on the main thread; file copies run on the IPC worker. Before
crafting, it rechecks the same actor, selected objects, complete live character,
stash and global data and normal eligibility. If these changed during the
backup, it refuses the craft. A post-call error
retains the backup and must never cause an automatic retry. Recovery accepts
the `forge-one-item` snapshot through the existing restore workflow. Changing
session crafting controls triggers one read-only eligibility refresh, not a craft.

## Features already covered, without duplicate buttons

| Requested area | Existing EpochPact coverage |
|---|---|
| Damage / cast speed / area of effect | Full-key character stat editor; applicable keys remain distinct |
| XP / movement / item and gold amounts | Existing XP, speed, drops and gold controls |
| Monolith monster density | Shared density control exposed in Monolith; native exclusions and 1–5 bounds |
| Stability | Absolute stability action and separate stability gain multiplier |
| Shards / Glyph of Despair | Existing shard preservation and eligible T1–T4 seal chance control |
| Item selection / crafting materials | Existing smart pickup modes, filter, LP/T7 conditions and material categories |
| Faction data / rank / Favor / Reputation | Existing read-only faction overview and implemented CoF management |
| Prophecy rewards | Existing CoF charge/reward/drop/quality controls and lens management |

Multiplier controls do **not** mean guaranteed T7 or guaranteed maximum LP.
Mana regeneration does **not** mean infinite mana; faster cooldowns do **not**
mean every skill has no cooldown. Echo reward navigation does not force or cycle
Echo spawns. Those claims are not made by this release.

## Not connected as working features

Item duplication; set/offhand/Primordial restrictions; skill/passive tree bypass;
automatic targeting; universal zero mana; infinite buff duration; extra projectile
rules; perfectly rolled item generation; guaranteed T7; forced Echo generation;
objective markers; universal waypoints; unlimited Eternity Cache or Woven uses;
Merchant's Guild and Weaver management. These require their own implementation
and live verification, and are not enabled merely because their names were listed.

## Verification evidence

Native command transcripts, isolated-character checks and save restoration hashes
are in `research/live/ce-features-20261009/`. The test build uses
`native/build.bat test-core`: only isolated fixtures and native screenshots, no
general research trackers. Test commands require `EpCraftTest` and its matching
save ID. Shipping builds use `native/build.bat player`; test commands are absent.

Automated Python and JavaScript checks cover IPC parameter validation, command
ordering, single-call Forge dispatch, owner/eligibility checks, startup behavior,
UI draft retention and readback synchronization. These are reported separately
from real-game tests. Final results and the installed player DLL hash are recorded
in `verification.json` in that evidence directory.

Final isolated tests: **26 real-game assertions** across material preservation,
low-level crafting and map transitions. Actual Rune of Refinement / Basic Glyph
of Hope consumption was compared with protection off and on; combined shard,
FP and glyph settings did not duplicate glyphs. The native fault counters stayed
zero and reset removed all seven crafting hooks. Native-rendered screenshots
show normal and revealed minimaps, and normal fog returning in a newly loaded zone.

The real local HTTP server also executed a selected Forge operation with rune
preservation and the map toggle through the same backend used by the UI. This
is separate from fixture-only Python/JavaScript integration checks.

The measured maximum main-thread Forge job step fell from **265.955 ms** before
moving snapshot file IO off-thread to **21.985 ms** across the six final native
Forge tests (other samples: 18.085–20.942 ms). These are command job timings,
not a universal FPS or zero-stutter guarantee; vanilla crafting still executes
on the game's main thread.
