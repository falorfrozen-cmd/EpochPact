# EpochPact (working name)

ForgePact-style mods for Last Epoch: a native plugin loaded by the game, a panel outside it,
every line our own. Offline characters only.

- `native/proxy/` the loader (`version.dll` proxy), `native/core/` the plugin core;
  `native\build.bat` builds both (MSVC x64).
- `tools/le_session.py` installs, launches, waits for the research dump and closes the game.
- `docs/research.md` what the game is made of; `docs/requirements.md` what we build.
