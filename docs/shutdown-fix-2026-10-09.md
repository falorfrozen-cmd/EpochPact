# Desktop close and runtime shutdown — 2026-10-09

## Change

Closing EpochPact completes accepted serial jobs before resetting this session's
temporary settings and owned character contributions. New jobs are rejected
while closing. Save mutations, rewards and completed quests are retained. Reset
uses the catalog's neutral defaults; it does not snapshot settings previously
applied by another session. No startup cleanup, DLL unload or game quit is added.

The window returns focus to the existing game so Unity can process reset jobs.
A rejected reset keeps the panel open and retains pending cleanup. Character
contributions are reset only for the same loaded identity. The Movespeed control
meter and its shared raw contribution are both reset.

The native command worker now stops before IL2CPP destroys its domain. A shutdown
detour cancels pending frame jobs and waits for the worker to detach and signal
completion before calling the original runtime shutdown. Initialization refuses
if this guard cannot be installed. Exceptions cannot escape the worker entry;
they are logged instead. This is lifetime protection, not a gameplay watcher.

The close helper now treats a nonzero or unreadable process exit code as failure,
and closes the process handle when the window is absent or a timeout occurs.

## Verified

- Final source: 137 Python tests and 74 JavaScript tests passed.
- Final native build: all ten test programs passed, totaling 6,571 assertions.
  The lifecycle harness uses the actual detour and frame queue to check pending
  cancellation, worker completion ordering and refusal after shutdown (6/6).
  The frame queue harness passed 13/13.
- Earlier live desktop build: ten explicit changes were applied on an isolated
  offline test character. Closing the actual desktop EXE reset them, and the
  game remained running. The owner also confirmed closing the panel did not crash.
  Readbacks confirmed neutral session settings and removed owned stat contributions.
- The subsequent meter correction (`speed 1` before raw Movespeed reset) and final
  worker logging changes passed automated regression checks; they were not repeated
  in another live gameplay session.
- All 54 original/shared save files were restored and byte-for-byte verified after
  the isolated live session. No owner character was used for the mutations.

## Remaining limitation

The game later exited with `0xC0000005` in UnityPlayer.dll. A separate launch with
EpochPact disabled also failed on quit. A final comparison with both our loader
and core uninstalled produced a Windows Application Error for Last Epoch PID
33784 at 22:52:11, exception `0xC0000005`, UnityPlayer.dll offset `0x1b3d952`.
The loader-removal check ran before that game was launched.

This shows a full-game exit failure was also present without EpochPact. It does
not prove the cause of every earlier crash, nor that the new native guard has
been verified through a clean live game shutdown. The original command-worker
exception and the later UnityPlayer failure are distinct observations. The
Windows package is therefore published as an alpha, not as a zero-crash release.

Raw dumps, Windows event details and test save identities remain local under
`research/live/shutdown-20261009/`; they are excluded from source and distribution.
Final EXE checks and hashes are recorded in
[the package verification report](player-package-verification.md).
