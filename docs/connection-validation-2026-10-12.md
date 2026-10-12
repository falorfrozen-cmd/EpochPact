# Connection integration validation — 2026-10-12

Version: **0.1.2-dev.11**. Connection changes were merged into the latest dev.10
source using the reference copy's common Git ancestor. The older reference files
were not copied wholesale. The dev.10 Crafting organization and dev.9 inventory,
interaction, respec and summon-DPS changes remain present.

## Implementation

The GUI uses a protocol-3 `connectionping` with a unique nonce and actual Windows
process identity (`PID:FILETIME`). Responses must match nonce, PID and session.
The native parser rejects a mismatched bound request before invoking its command.
`protocol.json` is only an optional announcement; its existence, age and write
permissions are not prerequisites for a verified connection.

Each bootstrap read has an independent eight-second timeout. Nested UI backends
inherit the same verified process binding, and commands remain serialized.
Submitted mutations are never automatically replayed after a timeout. A process
restart clears connection proof, cached data, actor ownership and previous
temporary-reset ownership.

Identity uses `Current(false)` without requiring the quest list. Offline status,
live actor, transition and character identity checks remain. Save-changing quest
operations retain the default quest-readiness gate. A real-game observation
during this integration also exposed an empty scene name before area loading had
finished; identity now refuses that state, keeping gameplay controls disabled.

Connected-but-not-ready states are separate from disconnection. Core/EXE hash
mismatches produce `updateRequired`, regardless of loader readiness. Installation
continues to enforce the existing exact hash checks.

## Automated results

| Layer | Final result | Scope |
|---|---|---|
| Python | 235 passed | Connection lifecycle, ownership, nested binding, installer/backend regressions and existing suites |
| JavaScript | 118 passed | UI readiness, labels, update guidance, serial actions and retained UI regressions |
| Native | 17 programs passed | Production IPC parser, identity gates and existing feature/queue/hook/lifecycle fixtures |
| Frozen EXE/package | 21 checks passed | Compiled HTTP interface and isolated install/update fixtures; no game execution |
| Extracted ZIP connection | 4 compound checks passed | Real Windows process/file IPC through the production parser with simulated game data |

Regressions cover absent, stale and read-only announcements; independent reads
whose total exceeds seven seconds; restart and reused-PID identities; wrong
session requests rejected before mutation; wrong nonce/PID or missing ownership
in responses; connected states without character readiness; and owned core hash
mismatches. The extracted frozen EXE's delayed-read case took **8,187.4 ms** and
passed. That timing measures a simulator, not game responsiveness.

The protocol simulator initially encountered a transient Windows reply-file lock
on publication. Its publisher now uses the production publisher's bounded rename
retry; it never re-executes the command. The repaired final 235-test run passed.
This was a test-host change, not a reduction in player-package ownership checks.

Installer fixtures verify exact files on clean install and update, read-only
source/owned files, recovery backups, Unicode/custom paths, unsupported-build and
foreign-loader refusal, token/origin validation and startup without gameplay
writes. Existing unit tests cover transactional rollback and uninstall ownership.
These checks do not claim interactive UAC validation or installation on another
player's computer.

## Real Last Epoch results

Tested on Steam build **25816319**, public version **1.5.2.1**. The internal game
version string is 1.5.2.2. Both final sessions loaded only a separate offline test
character. No mouse/keyboard automation was used to control the game.

- The final player core connected with a protocol-2 announcement dated 1970 and
  marked read-only. It also connected after that announcement was removed.
- A request with the wrong PID and a request using the current PID with an old
  creation time were rejected before the requested XP change. The existing XP
  value was unchanged.
- The compiled EXE connected to the loaded offline character in a completed
  area. Its real interface served the current three-category Crafting layout.
- The same compiled backend remained open across game exit and restart. It
  cleared the old player/cache state, then connected to the new process and
  session key. The new game retained the default XP value rather than inheriting
  the old session's temporary setting.
- The real WebView desktop opened successfully. A measured normal desktop-app
  close returned **exit code 0**, reset a temporary XP multiplier from 2 to 1,
  and left Last Epoch running. This is separate from full game shutdown.
- All **54 original save files** were restored and checked against their original
  SHA-256 values after the test. Separate test-character files were archived out
  of the live save directory. The final matching native package remains installed.

## Full game shutdown — unresolved

Both real sessions using the final core ended with **0xC0000005** on full game
close. The inspected first failure reported `UnityPlayer.dll + 0x109ad1`, the same
location recorded in earlier shutdown investigations. This shared location does
not establish the cause. The second final game's exit code was also measured as
0xC0000005; its module location was not separately established here.

An earlier integration candidate exited with code 0, but it preceded the final
empty-area readiness guard and is not counted as final-core shutdown evidence.
The prior dev.10 Crafting test also had a successful game exit. These mixed
results are retained; this release does **not** claim to fix the full-game-exit
crash or to be free of all crashes.

## Final distribution

The normal player ZIP has 849 entries, including 775 runtime files. It contains
the application-scoped `EpochPact.exe.config` next to the EXE. Manual-install DLLs
are byte-identical to the corresponding DLLs bundled with the EXE. The optional
MelonLoader bridge is the unchanged, verified component from the previous package.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| EpochPact.exe | 372244 | `fa6e87df326ce8f1e22551666f7731f2455a5a5ab4480226eba7b0172337a153` |
| EpochPact.Core.dll | 1535488 | `2837a74618293beb643005b07e83d059df2f91dda42280ab283b5fa88e209f34` |
| version.dll | 112640 | `9f006140935b4cb41d3a68f4b25033bb7bfc5810959265e676bba257013a18b4` |
| EpochPact-0.1.2-dev.11.zip | 26886873 | `12b1a88abb6796259af85ead0ea38d1ccf9cbd732ecdab5594d450257d346d01` |

The ZIP inventory/hash verification excludes test hosts, tests, SDK/build/debug
tools, private logs, saves, game binaries and nested archives. Packaging keeps
the latest release number and normal player naming. The source, native DLLs and
EXE are published together; a full reinstall is necessary when updating.

This pass did not re-test every stat, item recipe, combat feature or third-party
mod combination in-game. It also did not obtain a new VirusTotal or Nexus scan
result. Private raw logs, player paths and saves remain outside the published
source and player ZIP. Public structured results are in
[connection-test-results-2026-10-12.json](connection-test-results-2026-10-12.json).
