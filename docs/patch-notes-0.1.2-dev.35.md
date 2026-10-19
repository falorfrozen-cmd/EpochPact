# EpochPact 0.1.2-dev.35

A connection fix: EpochPact now connects even when Windows or security software
refuses to describe the running game.

## Fixes

- **"Reconnecting to game…" that never ended.** On some PCs Windows refuses
  EpochPact's question about the running Last Epoch (which file it is and when it
  started), most likely because security software guards the game. EpochPact then
  never connected, although the mod was working in the game. Now EpochPact uses
  the details the mod in the game announces about itself, and every request stays
  tied to that game session, as before: after a game restart nothing from the
  previous session is sent to the new one.
- While the game is still starting, hovering over the connection status now says
  which Windows check was refused and that EpochPact connects as soon as the mod
  in the game is ready.

Measured in the game with that refusal forced: connecting, reading, changing a
setting, and a game restart (the new session was recognised within 48 s, the old
one was not reused).

## Updating

With 0.1.2-dev.22 or later: **New version · What's new → Install update** in
the app. From an older version: close Last Epoch and EpochPact, extract the
**complete new ZIP**, open `EpochPact/EpochPact.exe`, and select **Game setup →
Reinstall / update mod**.

Tests: [connection when Windows refuses the game process query](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.35/docs/connection-refused-query-2026-10-19.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
