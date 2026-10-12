# EpochPact

ForgePact-style mods for Last Epoch: an EpochPact native plugin loaded by the game,
with commands sent from a desktop interface. Standalone installation does not require
MelonLoader; an optional bridge supports existing MelonLoader 0.7.3 installations.
Offline characters only: every feature refuses in online play.

**Download:** [latest release](https://github.com/falorfrozen-cmd/EpochPact/releases/latest) ·
**Support:** [Discord](https://discord.gg/q6aexZZSAf)

Working on this repository with an AI agent? Start with `GEMINI.md` (the agent handoff:
layout, build/test/live workflow, the current state and the traps) and
`research/HANDOFF.md` (the full handoff report).

Building the owner's UI? Start with [the complete UI catalog](docs/ui-catalog.md)
and its machine-readable [ui/catalog.json](ui/catalog.json): all 136 implemented
controls, 134 SP properties, 72 stat aliases and 206 character-sheet mappings,
with commands/APIs, units, bounds, neutral defaults and persistence. Refresh both
from the loaded offline character with `py -3 tools/export_ui_catalog.py --refresh`.

Latest changes: [0.1.2-dev.18 minimap, affix tiers and loot filter files](docs/patch-notes-0.1.2-dev.18.md),
[dev.18 validation](docs/feedback-group3-validation-2026-10-12.md),
[0.1.2-dev.17 loot log, loot window, amber pickup and support report](docs/patch-notes-0.1.2-dev.17.md),
[dev.17 validation](docs/feedback-group2-validation-2026-10-12.md),
[0.1.2-dev.16 pickup, stash, summons and startup profile](docs/patch-notes-0.1.2-dev.16.md),
[dev.16 validation](docs/feedback-group1-validation-2026-10-12.md),
[0.1.2-dev.15 stutter, uninstall and audit fixes](docs/patch-notes-0.1.2-dev.15.md),
[dev.15 validation](docs/audit-fixes-validation-2026-10-12.md),
[dev.14 audit](docs/audit-dev14-claude-2026-10-12.md),
[dev.14 inventory lower row](docs/inventory-lower-row-validation-2026-10-12.md),
[0.1.2-dev.13 T7 picker and forging](docs/patch-notes-0.1.2-dev.13.md),
[T7 affix and final package validation](docs/t7-affixes-validation-2026-10-12.md),
[0.1.2-dev.12 inventory footer fix](docs/patch-notes-0.1.2-dev.12.md),
[inventory footer and final package validation](docs/inventory-footer-validation-2026-10-12.md),
[0.1.2-dev.11 connection fixes](docs/patch-notes-0.1.2-dev.11.md),
[connection and final package validation](docs/connection-validation-2026-10-12.md),
[0.1.2-dev.10 Crafting improvements](docs/patch-notes-0.1.2-dev.10.md),
[inventory, interaction, respec and summon-DPS validation](docs/player-feedback-validation-2026-10-12.md),
[remaining-three validation](docs/remaining-three-validation-2026-10-11.md), and
[player-feedback validation](docs/player-feedback-validation-2026-10-11.md).
These separate measured gameplay, installer checks, and outstanding coverage limits.

Earlier [live verification report](docs/all-mods-current-verification.md): all 247
stat keys on the final player DLL, real Monolith panel checks, a complete Echo
combat/return/reward loop, interface recovery and exact save restoration. It
identifies guarded test-driver evidence, retained earlier checks, observed crashes
and coverage limits; it does not claim exhaustive gameplay or zero crashes.

[October 9 review fixes](docs/code-review-fixes-2026-10-09.md) update backup writing,
frame dispatch, cooldown ownership, game discovery and build selection. See its
scoped evidence for these changed paths.

## Player installation

Download [the current Windows development package](https://github.com/falorfrozen-cmd/EpochPact/releases/latest),
extract the complete ZIP and open `EpochPact/EpochPact.exe`. Keep
`EpochPact.exe.config` and `_internal` beside it; no Python installation is
needed. Choose `Last Epoch.exe` with **Browse** (the picker starts in Steam's folder,
and other installations are supported). With the game closed, press **Install mod**,
then **Launch offline**. The selected location is remembered and can be changed in
the sidebar's **Game setup** entry (also available in Settings). Installation and
launch are explicit actions. An occupied interface port uses another local port
with this executable's own backend; an older server is never reused.

To remove the mod, close Last Epoch and press **Game setup → Uninstall mod**. Only
EpochPact's own loader (or MelonLoader bridge) and core are removed; saves, logs and
recovery backups stay.

When updating, extract the **complete new ZIP**, close Last Epoch,
and use **Game setup → Reinstall / update mod**. The EXE and installed native
core must belong to the same package; replacing only the EXE is insufficient.
Connection checks use protocol 3 and the actual Windows process session rather
than the date or presence of `protocol.json`. A connected mod can still be waiting
for an offline character or game updates; gameplay controls remain disabled then.

Windows 10/11 x64, Microsoft Edge WebView2 Runtime and .NET Framework 4.8 are
required. See [the player quick start](docs/player-quickstart.txt).
The [compiled package verification](docs/player-package-verification.md) records
the artifact hash, completed checks and their scope.
Closing the desktop app resets the temporary controls and character bonuses
changed in that session, while completed quests, rewards and saved progress remain.
The [shutdown verification](docs/shutdown-fix-2026-10-09.md) separates the confirmed
desktop cleanup from a UnityPlayer crash observed on full game exit, including
with EpochPact completely removed. This release remains an alpha.

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
| `autopickup <0/1>` | Every 0.25 s (paused while an item card is pending or open), asks the game to pick up selected items and enabled gold, potion, tome and bone categories. |
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

To package the player UI after building the native player artifact, install
`requirements-build.txt` into an isolated Python environment and run
`python -m tools.build_player`. The output is `dist/EpochPact.exe` with a quick
start, dependency notices and SHA256 build record. Python is bundled; the package
excludes research builds and historical character snapshots. For the standard
folder ZIP used by current releases, build with `--layout onedir`, then use:

```
python -m tools.package_player_folder --bundle dist/EpochPact --output dist/share
```

The packager verifies the complete input inventory, keeps matching DLLs in its
optional **Manual Installation** folder, and records file/ZIP hashes. The recommended
path is **EpochPact/EpochPact.exe → Game setup → Install mod**; manual copying is
an alternative, not an additional step. The packager refuses to overwrite
an existing archive.

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
