# Player setup error reporting — 10 October 2026

Candidate: `0.1.1-alpha.3`. This fixes confirmed setup/reporting defects; it does
not establish why the remote player's installation was blocked. Their screenshot
contains a generic message. The clicked button, timestamp and corresponding
`%LOCALAPPDATA%/EpochPact/live/ui/operations.log` entry are still needed.

## Confirmed defects and changes

- `playerError` previously replaced any message containing a Windows file path
  with a game-connection error. Setup now retains the actionable English error,
  Windows error number and file path. Technical JSON/tracebacks still use a
  setup-specific fallback with the correct log location.
- Waiting for installation now mentions game setup and the Windows administrator
  prompt, rather than waiting for the game.
- The administrator launcher reads the child process exit code, distinguishes a
  running helper from a failed wait, and sets its working directory explicitly.
- A refused result path returns code 20 before copying any files. This includes
  a different administrator account's LocalAppData. Validation remains strict;
  arbitrary result paths, junctions and pre-existing receipts are not authorized.
- A failed receipt write returns code 21. Success is reported only if the parent
  independently verifies the installed loader and core hashes; otherwise setup
  reports that verification failed. Missing, invalid and conflicting receipts
  are not silently accepted or retried. Receipt cleanup cannot hide the primary
  installation error.
- Missing/inconsistent downloaded runtime files explain how to extract the whole
  ZIP, without offering administrator installation as a repair for missing files.

Different-account UAC installation is still unsupported. Running from the
administrator's own Windows account or manually copying the bundled mod DLLs is
the alternative. The plain player README includes the actual manual file paths.
This change does not resolve the separate concern that the administrator path
elevates a user-writable Python application tree; see the earlier security review.

## Verification

| Check | Result | Scope |
|---|---|---|
| Python regression tests | 87 passed | Setup, result validation, UI API and shutdown fixtures |
| JavaScript tests | 68 passed | Shipping handlers, serial transport, launcher UI and setup error regression |
| Final compiled app | 18 passed | Isolated non-runnable game fixtures, installer entry point, API and bundle inventory; external Python removed from PATH |
| Final player ZIP | 836 entries / 769 runtime files | Every byte/hash compared with the verified folder build; no development tools, saves or private logs |
| Local Microsoft Defender | No threats; exit 0 | Final ZIP, signatures 1.459.645.0; a local scan is not multi-engine clearance |

The compiled helper also returned code 20 for an unapproved receipt directory
without installing or overwriting an unrelated existing file. Actual UAC approval,
the friend's security software and real-game behavior were not tested here.
The local game installation and player saves were not modified.

## Exact distribution bytes

| File | Bytes | SHA-256 |
|---|---:|---|
| EpochPact.zip | 25905053 | `5f888b326a09c339336c44316a61a5fa4710e5e0e11af124c107fbd065a9e1d9` |
| EpochPact.exe | 371008 | `f79e9f0884b32b45cddc81f4256374e153056d0d7402d13b5de6a182bcb67e9f` |
| EpochPact.Core.dll | 1167872 | `7b00bf42ac09ef003eb3a7c67e7b297e39868789b46b2bf7fc77296ab175c13e` |
| version.dll | 111104 | `3e695920df290cb7b6d146db728cdbdb3def7f359ffd7c2032e0f219b109e181` |

The EXE changed; both native DLLs are unchanged from review.3. Previous scan
results for older EXE/ZIP hashes must not be reused for this candidate. No fresh
multi-engine result, vendor clearance, code signature or Nexus clearance is
claimed. The existing hover/live shutdown verification gaps also remain.

Local outputs: `dist/share/EpochPact-0.1.1-alpha.3/EpochPact.zip` and the extracted
`EpochPact` folder beside it. `_internal` must stay beside the EXE.

The isolated app build used the existing pinned packaging environment:

```powershell
build/security-venv/Scripts/python.exe -m tools.build_player --layout onedir --version 0.1.1-alpha.3 --native-build build/security-review/hardening-r3-final/native --output dist/player/setup-fix-20261010 --work build/player-setup-fix-20261010 --stage build/player-resources-setup-fix-20261010
build/security-venv/Scripts/python.exe -m tools.verify_player_package --exe dist/player/setup-fix-20261010/EpochPact/EpochPact.exe --game-assembly '<local verified GameAssembly.dll>' --report build/security-review/setup-fix-compiled-check.json
```

The game DLL is used only for local fixture validation and is not distributed.
