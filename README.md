# EpochPact

ForgePact-style mods for Last Epoch: a native plugin loaded by the game, commands sent to it
from outside, every line our own (no mod loader, no hook library, no other mod's code).
Offline characters only: every feature refuses in online play.

## Features

| Command | What it does |
|---|---|
| `xp <1-100>` | Experience from kills and experience motes times the number, before the game's own level rules. `xp 1` removes the hook. |
| `xp` | The multiplier, whether the hook is in, and how many gains it boosted or refused. |
| `status` | Version, the online/offline gate, every feature's state. |

## Layout

- `native/proxy/` the loader: a `version.dll` that forwards the 17 real exports and starts
  the core in `Last Epoch.exe` only. A file `<game>\EpochPact\disabled` keeps it out.
- `native/core/` the plugin: the IL2CPP resolver (`il2cpp_api`), the hook engine
  (`x64_decode`, `hook`), the game layer and offline gate (`game`), main-thread jobs
  (`mainthread`), the command channel (`commands`), features (`xp`), and in research builds
  the metadata dump and capture hooks (`dumper`, `research`).
- `native/tests/` `hook_test.exe` (decoder and engine) and `xp_test.exe` (the experience
  multiplier against a stand-in function, no game needed).
- `tools/le_session.py` install, uninstall, launch (backs the saves up first), close,
  `cmd <command>`, `restore-saves`; `tools/live_xp_check.py` the in-world check.
- `docs/research.md` what the game is made of and what was measured;
  `docs/requirements.md` what we build and the rules it keeps.

## Build and test

```
native\build.bat
native\build\hook_test.exe
native\build\xp_test.exe
```

Live: `py -3 tools/le_session.py install`, `launch`, load an offline character, then
`py -3 tools/live_xp_check.py`; afterwards `close` and `restore-saves <backup>`.
