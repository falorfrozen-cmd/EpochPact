# EpochPact research

Everything learned about Last Epoch so far, in one place.

| Path | What it is | In git |
|---|---|---|
| `findings.md` | The findings, round by round: the game build, how our code gets in, where each feature lives, what was measured live, what failed and why. Start here. | yes |
| `tools/pe_scan.py` | Imports and exports of `Last Epoch.exe`, `UnityPlayer.dll` and `GameAssembly.dll`, marked KnownDLL or not (how the `version.dll` loader was chosen). | yes |
| `tools/callmap.py` | For named methods: the other methods each one calls directly (`E8`/`E9` targets that land on a method start). | yes |
| `tools/callers.py` | For named methods: every method that calls them, across all code sections. | yes |
| `game-data/dump-1.5.0.1/` | Our runtime dump of the game's metadata: `dump.cs` (C#-like listing), `methods.tsv` (method, parameters, RVA), `fields.tsv` (field, type, offset, enum values), `summary.txt`. 40,722 classes, 271,838 methods, 221,788 fields. | no: game data |
| `game-data/pe_scan.json` | `pe_scan.py`'s output. | no |
| `game-data/callmap-xp-spawner.txt` | Call maps for the experience path and the spawner. | no |
| `game-data/callers-gold-drops-spawner.txt` | Callers of the gold, drop and spawner functions. | no |
| `live/saves-backups/<time>/` | The saves folder as it was before each test session (`le_session.py launch` writes these). | no: the owner's saves |
| `live/` (logs) | Copies of the core log and command replies from live sessions. | no |

The scripts read the dump from the path given on their command line, for example:

```
py -3 research/tools/callers.py research/game-data/dump-1.5.0.1 GoldTracker.modifyGold
py -3 research/tools/callmap.py research/game-data/dump-1.5.0.1 ExperienceTracker.GainExp
```

A new dump is written by the research build into `<game>\EpochPact\dump\` the first time
the game starts with it (delete `dump\done.txt` there to write it again).

## The features, where they live

| Feature | Command | Game code | State |
|---|---|---|---|
| Experience | `xp <1-100>` | `ExperienceTracker.GainExpFromEnemyOrMote(long)`; every kill goes `ExperienceGainedOnKill.GiveExp` → it → `GainExp` | built; 24/24 without the game |
| Gold | `gold <1-100>` | `GroundItemManager.pickupGold` → `GoldTracker.modifyGold(int)`; the other 14 callers (shops, respecs, quests, stash tabs) are left alone | built |
| Item drops | `drops <1-25>` | static `ItemDrop.DropItem(level, position, itemDropChance, ..., itemMultiplier, ...)` (18 parameters), called by enemy deaths, monolith objectives, arenas, nemesis and Woven echoes | built |
| Monster density | `density <1-5>` | `Spawner.GenerateEntitiesInternal()` rolls the pack from `numberToSpawn` (+0x44) and `percentVariance`; only packs (more than one) are scaled | built |
| Offline gate | (all) | `EHG.Multiplayer.GameplayEnvironment._isOnlinePlay`; reads ONLINE at login and character select | built, checked live |
| Loot filter | | `ItemFiltering.ItemFilterManager` | found |
| Rarity, affixes, legendary potential | | item creation behind `ItemDrop.DropItem` | to read |
