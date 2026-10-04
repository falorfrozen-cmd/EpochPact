# EpochPact

ForgePact-style mods for Last Epoch: a native plugin loaded by the game, commands sent to it
from outside, every line our own (no mod loader, no hook library, no other mod's code).
Offline characters only: every feature refuses in online play.

Working on this repository with an AI agent? Start with `GEMINI.md` (the agent handoff:
layout, build/test/live workflow, the current state and the traps) and
`research/HANDOFF.md` (the full handoff report).

## Features

| Command | What it does |
|---|---|
| `xp <1-100>` | Experience from kills and experience motes times the number, before the game's own level rules. `xp 1` removes the hook. |
| `gold <1-100>` | Gold picked up from the ground times the number. Shops, respecs and quest rewards are untouched. |
| `drops <1-25>` | The item count of every loot drop (enemy deaths, objectives, arenas, bosses) times the number. |
| `density <1-5>` | Monsters per pack times the number, for packs rolled from then on. Single spawns (bosses, unique enemies) stay single. |
| `rarity <1-10>` | Every rarity roll has a (mult-1)/mult chance to come out one tier better: normal → magic → rare → exalted → unique/set. |
| `speed <1-5>` | Movement speed: writes the game's own Movespeed stat (like an item or a buff), so the character sheet, the animation and the movement all scale together. |
| `cooldown <1-10>` | Charges and cooldowns count down that many times faster. |
| `autopickup <0/1>` | Every 0.75 s, asks the game to pick up all ground items, gold, potions, xp/favor tomes and ancient bones in the zone. |
| `xp`, `gold`, ... | Without a number: the multiplier, whether its hooks are in, and how many times it boosted or refused. |
| `status` | Version, the online/offline gate, every feature's state. |

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
native\build.bat
native\build\hook_test.exe
native\build\xp_test.exe
```

Live: `py -3 tools/le_session.py install`, `launch`, load an offline character, then
`py -3 tools/live_xp_check.py`; afterwards `close` and `restore-saves <backup>`.
