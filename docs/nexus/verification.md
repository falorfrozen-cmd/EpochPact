# EpochPact 0.1.1-alpha.1 release verification — 2026-10-10

## Distribution

Player EXE: 26,956,729 bytes; SHA-256 `75ef47123c8d5c94fb299a85d96a1524cab9c0e3a6cd0aea993dfe0b0585e68a`.
Native player core: 1,163,264 bytes; SHA-256 `6265d3e4f15a609f174a3b577dd81e6df2e59dea2cfbbecef66f43d8d52f7a9f`.
The UI/setup code changed; native player feature logic is unchanged from the retained audit. Native status reports component version 0.1.0; package version is 0.1.1-alpha.1.

The explicit allowlist includes the app, required loader/core, README, licenses/permissions and build/hash manifests. The EXE contains the same loader/core as the manual ZIP. Game-owned executables, GameAssembly.dll, metadata, saves, private logs, SDKs, research drivers and tests are excluded. Frozen player builds refuse preview/developer modes. ZIP entries, bytes, sizes, SHA-256, CRC and encryption flags were checked. Nexus download verification is recorded separately in publication.json after upload.

## Current supported game build

Steam build **25816319**, game display version **1.5.2.1**, internal **1.5.2.2**. Local GameAssembly.dll: 97,222,656 bytes; SHA-256 `d5f9fb458f0e9878e697a92b590a586a52fd77e645f8bf87bb4f2b74c6c56567`. This proprietary file is only used locally for compatibility checks and is not distributed.

The app rejects an unknown build before installing or launching. A manual file copy cannot run this check before the game starts; users must check compatibility first.

## Results by scope

| Scope | Result | Limitation |
| --- | --- | --- |
| Build | Final player EXE built and embedded runtime matched | Compilation alone does not certify gameplay |
| Python | 142 tests passed | Unit/adapter and guarded installer fixtures |
| JavaScript | 74 tests passed | UI state/conversion/queue logic |
| Native | 6,571 retained assertions passed on unchanged native source | Not an exhaustive live-game audit |
| Compiled package | 15 checks passed | Python-independent launch, custom Unicode path, unknown build/foreign loader refusal, serial HTTP work, port collision; real UAC not certified |
| Actual game-folder operations | Manual ZIP DLL copy, app update/backup and guarded uninstall passed; original runtime restored | Game was closed; this phase did not launch the copied files |
| Save preservation | All 54 original files restored and byte hashes verified; three known test files archived | Isolated cloned character; owner's original character never loaded or progressed |
| 1.5.2.1 live smoke | Final EXE IPC applied XP x2 and Intelligence +100%; native Intelligence 11 → 22; Monolith/CoF/crafting/map/loot reads succeeded | Not every feature re-tested on this build; no XP gain event was induced |
| Final EXE close | Normal Alt+F4 ran the app close event; XP x2 → x1, Intelligence 22 → 11, game still running, EXE process exited | Distinct from full game shutdown |
| Full game exit, current build | First exit **0x00000000**; second **0xC0000005** | Mixed result; do not claim shutdown fully fixed |

The newer smoke's XP submission took 1,096.9 ms, stat submission 316.8 ms; these are isolated operation timings, not universal latency promises.

## Retained gameplay evidence on 1.5.2

[Full-feature audit](../all-mods-current-verification.md): 134 stat types, 247 distinct keys and 4,449 apply/repeat/reset checks; 206 character-sheet mappings; 27 Monolith flows plus 60 repeated selections after another cold boot; a real completed Echo with Stability 11 → 33 at x3 and three actual XP tomes from its reward chest. CoF, crafting, collections, pickup and map checks have their own scope in that report. These results are retained evidence, not labelled as new 1.5.2.1 full-feature tests.

## Known limitations

Full game-exit access violations are unresolved. Earlier shutdown controls also failed with the mod removed, but that does not establish the cause of the current failure. Keep these records separate from desktop close restoration and installer results. See [shutdown evidence](../shutdown-fix-2026-10-09.md).

Weaver/Merchant's Guild management is informational. Stash reminders do not enforce in-game locks. Other mods/loaders, all hardware configurations and every combat/crafting consumer are not certified. Version checks are intentionally exact; a later game update requires another audit and manifest update.

Raw local evidence is retained under research/live/nexus-20261010 and excluded from distributed packages/public uploads. It includes original save manifests/private paths and is not advertised as downloadable public evidence.
