# Character stat editor

The editor discovers the game's complete `SP` enum at runtime (134 properties in the
tested build). Each modifier has a full key: **SP, tags, specialTag, extraTag**.
`sheetstats` discovers the C panel's actual components, including inactive tabs and
referenced basic-sheet LineItems, and prints the corresponding keys. Resistances,
armor, dodge and block labels are included explicitly.

## IPC commands

Send these with `py -3 tools/le_session.py cmd "<command>"`.

| Command | Action |
|---|---|
| `playerread` | Confirms loaded character name, level and `CharacterData.IsOffline`. |
| `sheetopen 1` / `sheetopen 0` | Opens/closes C via `UIBase`, without desktop input. |
| `sheetstats` | Lists all current sheet rows, full keys, formatting/cap flags and cached labels. |
| `sheetstat <row ID or unique object name>` | Reads a row's exact-key and applicable engine modifiers. |
| `sheetstat <row> added 20` | Replaces this mod's flat contribution to that row's underlying stat. |
| `sheetstat <row> increased 0.25` | Replaces this mod's increased contribution with +25%. |
| `sheetstat <row> more 0.5` | Replaces this mod's multiplicative contribution with +50% more. |
| `sheetstat <row> modifier more 0.1` | Edits the secondary stat when the row declares `useModifierStat`. |
| `sheetstat <row> reset` | Removes this mod's whole contribution for that full key. |
| `statraw` | Lists every supported SP name/id and full-key syntax. |
| `statraw <SP name/id> <tags> <special> <extra> [mode value or reset]` | Reads/edits any defined SP with any int32 tags/extra and byte special. |
| `statreset` | Removes all current actor's EpochPact stat contributions. |
| `stat <name> [value]` | Existing convenient aliases; uses the same isolated editor. |

Examples:

```text
stat allres 0.65
stat bowattackspeed 5
stat meleeattackspeed 5
stat parry 0.5
stat reflect 10
statraw AilmentChance 512 1 0 added 0.2
statraw AilmentChance 512 2 0 added 0.3
statraw Damage 8192 0 0 increased 0.25
statraw Damage 256 0 0 more 0.1
statraw PlayerProperty 636 0 0 added 0.002
statraw AbilityProperty 782 0 0 added 0.01
statraw AilmentChance 512 1 0 reset
```

## Units and scope

- Percentage chances/resistances/reflection use fractions: 0.65 means 65 percentage
  points, 0.5 means 50%, 10 means 1000%. Health/attributes/armor use counts.
- Increased 5 means +500% increased attack speed. Absolute attacks/sec also depend on
  the weapon and other modifiers. A mod contribution is distinct from the final total.
- More 0.5 means ×1.5; more -0.5 means ×0.5. Increased/more below -1 is refused.
- For ordinary stats, tags are AT bits; the minion bit 8192 is included automatically
  when editing a `minionStat` row. PlayerProperty/AbilityProperty tags can encode an
  index instead of AT bits. Copy the key from the live catalog.
- Ailment IDs belong in specialTag. extraTag distinguishes further contexts.
- Exact-key sums describe identical entries only; `applicable*` getters also include
  matching broader tags. Neither includes the character/weapon's base value or all
  display transformations. Cached labels on unopened tabs may be empty or old.
- Rows marked `derived` share their source stat with another row. Armor mitigation,
  dodge probability, caps, attack rate, inverse/quotient rows and conditional bonuses
  follow the game's calculations. Setting a modifier does not independently replace
  these derived outputs or bypass their caps. Level/XP/name are progression/identity
  data and are outside the combat-stat modifier editor. The minion power **from character
  level** caption also describes progression; minion damage/health modifiers have their
  own editable rows.
- Row IDs belong to the current panel instance. Regenerate the catalog after reloading.

## Implementation and lifetime

`Stats.Stat` is allocated, rooted, and constructed before insertion. Its `moreValues`
list is always initialized. Every EpochPact entry is marked `dontCollapse`, which the
game's `GetExactStatMatch` skips. Equipment/buff aggregation therefore uses separate
entries. No equipment value is overwritten; repeat settings replace the mod's own
value, and reset removes only the owned object through `List.Remove`.

Calls use `il2cpp_runtime_invoke`, including generic List operations and the actual
virtual `UpdateStatsInternal` override, on the game's main thread. Both the gameplay
environment and loaded `CharacterData.IsOffline` must confirm offline play. Invalid
keys/numbers/extra arguments are refused. A failed managed calculation restores the
previous contribution and recalculates; a failed rollback explicitly requires restart.

Settings live in the current actor's memory. They are not a saved configuration and
are not automatically copied into a replacement actor after a zone/character load.
Queries report whether an owned entry is still attached. Typed scene lookups and sheet
scans run only on an IPC request; no permanent update hook is installed for the editor.

## Verification

`native/build/stat_key_test.exe` covers parsing, units, overflow/underflow and identity
separation. `tools/stat_editor_live_check.py` verifies all 134 enum properties with
neutral construction/removal and unchanged baseline modifiers, then exercises distinct
ailments, minions, player/ability properties, multiplicative modifiers, invalid input,
sheet-row mapping and repeat-setting behavior. It leaves the five requested bonuses
enabled and writes replies to `research/live/full-stat-editor-check.json`.

Neutral tests establish runtime access and restoration, not combat behavior of every
possible stat/tag combination. Save backups are made before each session/test.

Measured final build: 206 mapped sheet rows (including the five directly populated
basic LineItems). Full live evidence is split between `full-stat-editor-201-check.json`
and `full-stat-editor-final-check.json`; the summary is `full-stat-editor-verification.json`.
All temporary tests were reset and the five requested bonuses restored on Falor level 8.
The save's 37 item records were identical to the pre-test backup; clean game exits returned 0.
