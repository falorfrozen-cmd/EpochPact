# EpochPact

ForgePact-style mods for Last Epoch: a native plugin loaded by the game, commands sent to it
from outside, every line our own (no mod loader, no hook library, no other mod's code).
Offline characters only: every feature refuses in online play.

Working on this repository with an AI agent? Start with `GEMINI.md` (the agent handoff:
layout, build/test/live workflow, the current state and the traps) and
`research/HANDOFF.md` (the full handoff report).

Building the owner's UI? Start with [the complete UI catalog](docs/ui-catalog.md)
and its machine-readable [ui/catalog.json](ui/catalog.json): all 72 implemented
controls, 134 SP properties, 72 stat aliases and 206 character-sheet mappings,
with commands/APIs, units, bounds, neutral defaults and persistence. Refresh both
from the loaded offline character with `py -3 tools/export_ui_catalog.py --refresh`.

Latest [live verification report](docs/all-mods-current-verification.md): all 247
stat keys on the final player DLL, real Monolith panel checks, a complete Echo
combat/return/reward loop, interface recovery and exact save restoration. It
identifies guarded test-driver evidence, retained earlier checks, observed crashes
and coverage limits; it does not claim exhaustive gameplay or zero crashes.

[October 9 review fixes](docs/code-review-fixes-2026-10-09.md) update backup writing,
frame dispatch, cooldown ownership, game discovery and build selection. See its
scoped evidence for these changed paths.

## Features

| Command | What it does |
|---|---|
| `xp <1-100>` | Experience from kills and experience motes times the number, before the game's own level rules. `xp 1` removes the hook. |
| `gold <1-100>` | Gold picked up from the ground times the number. Shops, respecs and quest rewards are untouched. |
| `drops <1-25>` | The item count of every loot drop (enemy deaths, objectives, arenas, bosses) times the number. |
| `density <1-5>` | Monsters per pack times the number, for packs rolled from then on. Single spawns (bosses, unique enemies) stay single. |
| `rarity <1-10>` | Each numeric rarity roll below grade 4 has a (mult-1)/mult chance to increase by one. Exalted/T7 tuning has separate controls; rarity enum IDs are cataloged by type. |
| `speed <1-5>` | Movement speed: writes the game's own Movespeed stat (like an item or a buff), so the character sheet, the animation and the movement all scale together. |
| `cooldown <1-10>` | Charges and cooldowns count down that many times faster. |
| `autopickup <0/1>` | Every 0.75 s, asks the game to pick up selected items and enabled gold, potion, tome and bone categories. |
| `lootmode all\|filter\|quality\|materials` | Smart pickup using the native loot filter, Unique LP threshold or wanted T7 affixes; normal inventory capacity remains. |
| `lootlp` / `loott7` / `lootaffixes` / `lootfilter` / `lootcategory` | Separate quality, affix and category selections; `lootread` lists actual affix IDs/names. |
| `craftfp <0–1>` / `crafthope` / `craftdespair` `<0–100\|reset>` | Normal forge FP cost, Basic Hope preservation chance and eligible Despair sealing chance. |
| `craftshards <0/1>` / `craftread` / `craftreset` | Preserve the shard actually consumed, read normal forge preview, or reset all crafting controls. Rune/glyph consumption remains normal. |
| `coffavormult <1-100>` | CoF Favor gained times the number; normal prophecy charging and derived Reputation continue. |
| `cofrepmult <1-100>` | CoF Reputation gained from gaining or spending Favor times the number; normal rank progression and cap remain. Manual grants are excluded. |
| `cofchargemult` / `cofrewardmult` | Independent prophecy charge speed and normal reward item count; preserves wallet and charge consumption. |
| `cofdouble enemy\|echo <0-100\|reset>` | Separate enemy/Monolith double-item probabilities; reset uses the game's normal rank bonus. |
| `cofexaltedmult` / `coft7mult` / `coflpmult` | Separate CoF item-generation roll coefficients; normal item/level eligibility remains. |
| `coflensmult celerity\|charity\|duplication <1-100>` | Separate lens extra-charge contributions and duplicate-reward chance. |
| `xp`, `gold`, ... | Without a number: the multiplier, whether its hooks are in, and how many times it boosted or refused. |
| `status` | Version, the online/offline gate, every feature's state. |

Character-sheet editing, campaign/waypoint completion, Monolith management and CoF
management also have native commands and Python APIs for the owner's UI:
[stat editor](docs/stat-editor.md), [progression](docs/progression.md),
[Monolith](docs/monolith.md), [Circle of Fortune](docs/cof.md),
[smart pickup and crafting](docs/loot-crafting.md),
[Unique Atlas, Stash Assistant and Monolith Navigator](docs/collection-navigator.md).
Current faction research: [CoF / MG](research/factions-1.5.md) and
[The Woven / Weaver](research/weaver-1.5.md).

## Layout

- `native/proxy/` the loader: a `version.dll` that forwards the 17 real exports and starts
  the core in `Last Epoch.exe` only. A file `<game>\EpochPact\disabled` keeps it out.
- `native/core/` the plugin: the IL2CPP resolver (`il2cpp_api`), the hook engine
  (`x64_decode`, `hook`), the game layer and offline gate (`game`), main-thread jobs
  (`mainthread`), the command channel (`commands`), features (`xp`, `loot`, `items`,
  `player`), and in research builds the metadata dump and capture hooks (`dumper`, `research`).
- `native/tests/` `hook_test.exe` (decoder and engine) and `xp_test.exe` (the experience
  multiplier against a stand-in function, no game needed).
- `tools/le_session.py` install, uninstall, launch (backs the saves up first), close,
  `cmd <command>`, `restore-saves`; `tools/live_xp_check.py` the in-world check.
- `research/` everything learned about the game: `findings.md`, our research scripts, the
  metadata dump and live logs (game data and saves stay out of git; see `research/README.md`);
  `docs/requirements.md` what we build and the rules it keeps.

## Build and test

```
native\build.bat player     # player core, loader and tests
native\build.bat            # research core, loader and tests
native\build\hook_test.exe
native\build\xp_test.exe
native\build\review_test.exe
native\build\mainthread_test.exe
```

Artifacts are kept separately in `native\build\player`, `research` and `test`.
The installer defaults to the player artifact and verifies its type and SHA256.
Use `install --flavor research` only for intentional development sessions.
Game discovery uses `EPOCHPACT_GAME_DIR`, then Steam's registry/library list,
then the default Steam folder.

Live: `py -3 tools/le_session.py install`, `launch`, load an offline character, then
`py -3 tools/live_xp_check.py`; afterwards `close` and `restore-saves <backup>`.
