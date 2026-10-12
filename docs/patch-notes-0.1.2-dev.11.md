# EpochPact 0.1.2-dev.11

Connection checks now identify the actual running game session. Missing, stale
or read-only `protocol.json` files no longer prevent a valid connection.

- Protocol 3 checks each request and response against its unique nonce, Windows
  process ID and process creation time. Requests for another session are rejected
  before their command runs.
- Ping, settings, session, identity and optional stat reads each receive their own
  eight-second timeout. A slow read no longer uses up a shared seven-second budget.
- Character identity no longer depends on quest-list readiness. Offline, actor,
  area-loading and save-identity checks remain in place.
- The interface distinguishes a disconnected mod from a connected mod waiting
  for a character, game updates or readable game data. Gameplay stays disabled
  until the offline character is ready.
- Mismatched EXE/core packages show **Mod update required** and explain how to
  reinstall. Restarting the game clears the previous session's connection proof
  and cached values.

The three Crafting categories and the latest inventory/convenience improvements
are preserved. Save-changing actions still require their own explicit buttons;
opening the interface or checking the connection does not grant rewards or edit
saves. Submitted save commands are not automatically retried after a timeout.

**Update:** extract the complete ZIP, keeping `EpochPact.exe.config` and
`_internal` beside `EpochPact.exe`. With Last Epoch closed, select **Game setup →
Reinstall / update mod**, then launch offline. Update the full package, including
its native DLLs; do not replace only the EXE.

Validation passed 235 Python tests, 118 JavaScript tests, 17 native test programs,
21 compiled package/installer checks and the separate frozen connection simulator
checks. The final core also connected to two real Last Epoch sessions, and the
same EXE backend reconnected after a game restart. Normal desktop-app closure
returned exit code 0, reset its temporary XP setting and left the game running.

**Known issue:** both final real-game sessions ended with `0xC0000005` when the
game itself was closed. This connection release does not resolve the full-game
exit issue. Build success and passing fixture tests do not establish that every
feature works on every computer. See the
[validation report](connection-validation-2026-10-12.md) for the measured scope.

Offline only. Tested Steam version: 1.5.2.1, build 25816319.

Source and releases: https://github.com/falorfrozen-cmd/EpochPact

Support: https://discord.gg/q6aexZZSAf
