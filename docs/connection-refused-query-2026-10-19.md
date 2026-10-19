# Connecting when Windows refuses the game process query (2026-10-19)

A player's EpochPact showed **Reconnecting to game…** forever. Their support report showed the mod
loading normally in every game session (MelonLoader, 0.1.2-dev.34 core `7d56b7fc…`, "ready", F8
registered), while 798 connection checks in the report's app log failed before any message was sent:
`Permission denied (WinError 31)` at the handshake, with no game process identified. Windows answered the app's query about
the running `Last Epoch.exe` (its executable path and start time) with error 31, "a device attached to the
system is not functioning", from at least 18 October 20:22 on; once, 8 s after a launch, the query
worked. Security software that guards the game process is the likely cause.

## Why the query exists, and what changed

The app binds every request to one game session (process ID and start time): after a restart the old
session's queued settings, confirmed save actions and clean-ups are not sent to the new game, and a second
copy or a game from another folder is not talked to. The query was the only source of that session.

The mod announces the same session itself: it writes its process ID and start time to
`EpochPact/ipc/protocol.json` in the game folder it runs from, and accepts only requests carrying that key,
answering with it. Now:

- When Windows describes the game, nothing changes.
- When Windows refuses, the session comes from that announcement, only if it names a running
  `Last Epoch.exe` and its key belongs to that process ID. Every request and reply is still bound to the
  key, so an old announcement cannot be used (the mod ignores requests for another session).
- Until the mod has announced itself, the status says so instead of a bare error, naming the refused call:
  "Windows did not let EpochPact read the running game's details (QueryFullProcessImageNameW, WinError 31).
  EpochPact connects as soon as the mod in the game announces itself; if this stays, security software may
  be blocking EpochPact." (English only, like the other connection messages.)
- A second process Windows will not describe is accepted only when the mod in the selected folder
  announced exactly the process that Windows did describe; otherwise the app waits, as two copies of one
  game folder must never both receive commands.

## Measured

Live on the isolated test character with the test core: the app's process query was made to fail with
WinError 31 for every process (as on the player's PC) while the real game ran.

| Check | Result |
|---|---|
| Session from the announcement | `5940:134368556066735546`, the same key Windows gives when it is asked |
| Connection | ready, offline, EpochPact's test character |
| Reads (Rage of Morditas, fonts, sorting settings) | all answered |
| A setting written (special affix squares) | applied |
| A request without a bound session (command line path) | answered |
| Game closed | "stopped" |
| Game started again | 0 s: the message above (mod not yet announced); 12.7 s: mod connected, waiting for game updates; 32.6 s: loading; 46.1 s: waiting for the character; 48.4 s: ready, new session `16576:…`, the old one not reused |
| Query allowed again | normal connection, same key |

Saves and the installed mod were restored and verified after the session (54 files).

Tests: connection lifecycle tests for the announcement (accepted, old or foreign announcement refused,
second process refused unless announced, request path without a bound session); Python 336, interface 235,
native 22, translations 1727 (Turkish and Russian complete). A candidate package with the change passed the
compiled package checks (24 of 24) and the extracted-ZIP connection checks (4 of 4).
