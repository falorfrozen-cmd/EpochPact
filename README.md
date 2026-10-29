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
and its machine-readable [ui/catalog.json](ui/catalog.json): all 160 implemented
controls, 134 SP properties, 72 stat aliases and 206 character-sheet mappings,
with commands/APIs, units, bounds, neutral defaults and persistence. Refresh both
from the loaded offline character with `py -3 tools/export_ui_catalog.py --refresh`.

Latest changes: [0.1.2-dev.35 connection when Windows refuses the game process query](docs/patch-notes-0.1.2-dev.35.md),
[dev.35 tests](docs/connection-refused-query-2026-10-19.md),
[0.1.2-dev.34 Sell all, automatic sorting, fonts, loot filter colours, death recap, Season 5](docs/patch-notes-0.1.2-dev.34.md),
[dev.34 tests](docs/player-batch-2026-10-17.md),
[0.1.2-dev.33 damage window styles, game icons and names](docs/patch-notes-0.1.2-dev.33.md),
[dev.33 tests](docs/dps-meter-integration-2026-10-15.md),
[0.1.2-dev.32 Weaver's Will](docs/patch-notes-0.1.2-dev.32.md),
[dev.32 tests](docs/weavers-will-validation-2026-10-14.md),
[0.1.2-dev.31 interaction areas, steadier auto pickup, hidden parts](docs/patch-notes-0.1.2-dev.31.md),
[dev.31 tests](docs/conveniences-validation-2026-10-14.md),
[0.1.2-dev.30 minion counts and immortal minions](docs/patch-notes-0.1.2-dev.30.md),
[dev.30 minions](docs/minions-validation-2026-10-14.md),
[0.1.2-dev.29 better Turkish and Russian texts](docs/patch-notes-0.1.2-dev.29.md),
[dev.29 translation review](docs/translation-review-2026-10-14.md),
[0.1.2-dev.28 loot window filters, affix shard details, damage by type](docs/patch-notes-0.1.2-dev.28.md),
[dev.28 filters](docs/filters-validation-2026-10-14.md),
[0.1.2-dev.27 compact see-through loot and damage windows, T1–T8 custom affixes](docs/patch-notes-0.1.2-dev.27.md),
[dev.27 windows and crafting](docs/windows-crafting-validation-2026-10-14.md),
[0.1.2-dev.26 settings that stay and Turkish / Russian interface](docs/patch-notes-0.1.2-dev.26.md),
[dev.26 persistence and language](docs/persistence-language-validation-2026-10-14.md),
[0.1.2-dev.25 five blessing choices, smoother chests, shrines and graphics switches](docs/patch-notes-0.1.2-dev.25.md),
[dev.25 performance](docs/performance-validation-2026-10-13.md), [dev.25 blessings and trees](docs/blessings-trees-validation-2026-10-13.md),
[0.1.2-dev.24 passive tree connections fix](docs/patch-notes-0.1.2-dev.24.md),
[dev.24 validation](docs/passive-tree-validation-2026-10-13.md),
[0.1.2-dev.23 buffs & debuffs in game, Memory Amber multiplier](docs/patch-notes-0.1.2-dev.23.md),
[dev.23 validation](docs/hud-amber-validation-2026-10-13.md),
[0.1.2-dev.22 Install update from inside the app](docs/patch-notes-0.1.2-dev.22.md),
[dev.22 validation](docs/updater-validation-2026-10-13.md),
[0.1.2-dev.21 tier colours on every affix line, coloured and movable rule line](docs/patch-notes-0.1.2-dev.21.md),
[dev.21 validation](docs/feedback-group6-validation-2026-10-13.md),
[0.1.2-dev.20 loot filter rule in tooltips, merged loot feed, windows bound to the game](docs/patch-notes-0.1.2-dev.20.md),
[dev.20 validation](docs/feedback-group5-validation-2026-10-13.md),
[0.1.2-dev.19 stash tabs from the loot filter and interface font](docs/patch-notes-0.1.2-dev.19.md),
[dev.19 validation](docs/feedback-group4-validation-2026-10-12.md),
[0.1.2-dev.18 minimap, affix tiers and loot filter files](docs/patch-notes-0.1.2-dev.18.md),
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
| `bones <1-100>` | Ancient Bones that drop for you times the number; the pile on the ground already shows the larger amount. |
| `density <1-5>` | Monsters per pack times the number, for packs rolled from then on. Single spawns (bosses, unique enemies) stay single. |
| `rarity <1-10>` | Each numeric rarity roll below grade 4 has a (mult-1)/mult chance to increase by one. Exalted/T7 tuning has separate controls; rarity enum IDs are cataloged by type. |
| `weaverswill <1-10>` | The Weaver's Will the game rolls for a Unique (drops, rewards, opened cocoons), times the number, rounded and at most 28 (the game's maximum). |
| `fpdrops <1-10>` | The Forging Potential of new Normal, Magic, Rare and Exalted items (drops, rewards, Nemesis, Spawn Items; not shops or gamblers), times the number, rounded and at most 63 (the most the game lets an item have). |
| `uniqueroll <0-100>` | New Uniques, Set items and Legendaries (drops, rewards, Nemesis, Spawn Items, opened Weaver cocoons; not gamblers) roll every value in the best part of its range: at 80 the best fifth, at 100 the best value itself. The game decides which end is best for each value. |
| `weaversdrop <0-100>` | Percent of the Uniques the game picks at random that come as a Weaver's Will Unique where one fits (about 4 % normally). |
| `weaversupgrades <1-10>` | Every Weaver's Will upgrade a kill grants an equipped item happens that many times (each uses 1 Weaver's Will). |
| `weaveraffixes <ids\|none>` | The affixes Weaver's Will upgrades raise and add when they can; otherwise the game chooses. |
| `idolprefixes <1-3>` / `idolsuffixes <1-3>` | Each new idol (drops, rewards, Nemesis, Spawn Items; not shops or gamblers) rolls 1 to that many prefixes / suffixes and each new idol altar 2 to one more, picked by the game's own affix rules; Weaver idols can get more Weaver affixes too. Heretical idols keep the game's 1 and 1. |
| `enchanteruses <1-40>` | Each Woven Enchanter allows that many times its usual uses ("1/1 uses remain" becomes 20/20 at 20), also the one you are standing at. Each craft still costs its Memory Amber. |
| `catauto <category> 0\|1`, `catshow`, `catopen`, `catsetup`, `catsort` | Item categories: one stash tab per category (Unique & Set, Exalted, Idols, Keys & special, Weapons, Armour, Jewellery, Other) in a folder named EpochPact. Picked-up items of a ticked category go straight to its tab; a column beside the in-game inventory has the ticks, opens a category's tab with one click and can show one category with the others faint. |
| `transmogread` / `transmog <slot\|all> <look\|none>` / `transmogshot <yaw>` | Item looks, with a mirror of the character: an equipped helmet, body armour, gloves, boots, belt, weapon or off-hand can look like another base item or Unique of the same type (the game's own art; shop cosmetics untouched). Kept per character, also after zone changes and new items of that type. |
| `speed <1-5>` | Movement speed: writes the game's own Movespeed stat (like an item or a buff), so the character sheet, the animation and the movement all scale together. |
| `cooldown <1-10>` | Charges and cooldowns count down that many times faster. |
| `autopickup <0/1>` | Every 0.25 s (paused while an item card is pending or open), asks the game to pick up selected items and enabled gold, potion, tome and bone categories. |
| `remotepickup <0/1>` | A click on an item's name on the ground picks it up where it lies; the character does not walk there. With no room in the inventory the game walks there as usual. |
| `lootmode all\|filter\|quality\|materials` | Smart pickup using the native loot filter, Unique LP threshold or wanted T7 affixes; normal inventory capacity remains. |
| `lootlp` / `loott7` / `lootaffixes` / `lootfilter` / `lootcategory` | Separate quality, affix and category selections; `lootread` lists actual affix IDs/names. |
| `craftfp <0–1>` / `crafthope` / `craftdespair` `<0–100\|reset>` | Normal forge FP cost, Basic Hope preservation chance and eligible Despair sealing chance. |
| `craftshards <0/1>` / `craftread` / `craftreset` | Preserve the shard actually consumed, read normal forge preview, or reset all crafting controls. Rune/glyph consumption remains normal. |
| `coffavormult <1-100>` | CoF Favor gained times the number; normal prophecy charging and derived Reputation continue. |
| `cofrepmult <1-100>` | CoF Reputation gained from gaining or spending Favor times the number; normal rank progression and cap remain. Manual grants are excluded. |
| `cofchargemult` / `cofrewardmult` | Prophecy charge speed (0–100; 0 stops prophecies; the experience and Favor multipliers no longer speed prophecies up) and normal reward item count; preserves wallet and charge consumption. |
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

The desktop windows are available in English, Turkish and Russian
(**Settings → Language**). English is the source text: `tools/extract_ui_text.py`
lists every interface text in `ui/i18n-source.json`, `ui/i18n-<language>.json`
holds the translations and `tools/i18n_check.py` reports anything missing.
Settings, profiles, window places and the language are kept in the user's
`preferences.json`, so they survive restarts and updates.

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
