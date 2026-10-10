# Read-only installer fix — 10 October 2026

Candidate: `0.1.1-alpha.4`. This replaces the alpha.3 player app for the
installation defect reported as `Permission denied (WinError 5)` on
`EpochPact/.install-<nonce>/new-2`. `new-2` is the staged `version.dll` loader.

## Reproduction and cause

On Windows, the previous installer used `shutil.copy2`. The pinned Python 3.13
implementation uses CopyFile2 and preserves the source DLL's read-only attribute.
A real temporary-file reproduction marked the bundled loader read-only, then
ran fresh installation and update through `PlayerSetup.install`. Fresh install
succeeded but installed a read-only loader. Update failed; unconditional cleanup
then raised WinError 5 on the read-only `new-2`, hiding the original failure.

With the fix, the same reproduction completes both installation and update.
This establishes a defect matching the reported error. The remote player's file
attributes, ACLs and security-software decisions have not been inspected, so this
does not claim that every possible access denial has this cause.

## Changes

- Stage DLL and backup bytes into exclusively created files without inheriting
  source read-only attributes. Check the staged core and loader SHA-256 values
  before changing installed files.
- Clear only the Windows read-only flag on validated owned mod files before
  replacement. Restore their previous bytes and read-only flags on rollback.
  Do not change folder ACLs, ownership or security-software settings.
- Clean only this transaction's flat stage directory, refusing linked or
  unexpected entries. Cleanup failures are logged and cannot replace the primary
  error or reject a successfully committed, independently verified installation.
- Report the failing installation step in both normal and helper error paths.
- Remove owned read-only DLLs during uninstall while retaining unrelated files,
  saves, recovery data and the installation record.

Source payload attributes stay unchanged. Foreign loaders remain protected.
Permission denial from ACLs, a lock or security software is still reported rather
than bypassed. Different-account UAC receipt restrictions remain unchanged.

## Verification

| Check | Result | Scope |
|---|---|---|
| Same-error Windows reproduction | Fresh install and update passed after the fix | Actual temporary read-only DLLs; before-fix update produced WinError 5 on `new-2` |
| Python tests | 182 passed | All `tools/test_*.py` modules; includes actual read-only sources/destinations, rollback attributes, preserved primary errors, foreign files, uninstall and staging-integrity regressions |
| Exact compiled EXE | 20 checks passed | Isolated non-runnable game fixtures, actual read-only bundled/installed DLLs, recovery bytes, HTTP API and bundle inventory; external Python removed from PATH |
| Final ZIP | 836 entries / 769 runtime files verified | Each file's bytes and SHA-256 compared with the verified folder build |
| Local Microsoft Defender | No threats; exit 0 | Final ZIP, signatures `1.459.645.0`, antivirus and real-time protection enabled; ZIP hash unchanged after scan |

The compiled checks include both fresh installation from an extracted read-only
payload and update over an owned read-only installation. Neither leaves a stage
directory, and recovery backups match every previous file byte for byte.

Tests did not approve an interactive UAC prompt, run the actual game, exercise
live hover behavior or measure game exit. The real game installation and saves
were not modified. Shutdown exit codes printed by unit tests are simulated
fixtures, not measurements of this player's game.

## Distribution bytes

| File | Bytes | SHA-256 |
|---|---:|---|
| EpochPact.zip | 25907030 | `e2e78b2338e333cd9709c7d3a7a4b3ca08a8f0b924bf93f36d556e22aa69445e` |
| EpochPact.exe | 371105 | `e49ee41519e6079e9b79244154529ffc6fdef3cc0557e9a9e24a5dc9e58a113e` |
| EpochPact.Core.dll | 1167872 | `7b00bf42ac09ef003eb3a7c67e7b297e39868789b46b2bf7fc77296ab175c13e` |
| version.dll | 111104 | `3e695920df290cb7b6d146db728cdbdb3def7f359ffd7c2032e0f219b109e181` |

Output: `dist/share/EpochPact-0.1.1-alpha.4/EpochPact.zip`, with the extracted
`EpochPact` folder beside it. Share the whole ZIP; keep `_internal` beside the EXE.
The EXE has changed. Native DLLs remain byte-identical to review.3. Older EXE/ZIP
scan results are not evidence for these new hashes. Antivirus detections and
Nexus quarantine remain unresolved; no new multi-engine or publication clearance
is claimed. Previous builds are retained unchanged.

The existing pinned packaging environment was used:

```powershell
build/security-venv/Scripts/python.exe -m tools.build_player --layout onedir --version 0.1.1-alpha.4 --native-build build/security-review/hardening-r3-final/native --output dist/player/readonly-fix-20261010 --work build/player-readonly-fix-20261010 --stage build/player-resources-readonly-fix-20261010
build/security-venv/Scripts/python.exe -m tools.verify_player_package --exe dist/player/readonly-fix-20261010/EpochPact/EpochPact.exe --game-assembly '<local verified GameAssembly.dll>' --report build/security-review/readonly-compiled-check.json
```

The game DLL is only used in local installer fixtures and is not distributed.
