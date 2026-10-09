# Windows player package verification — 2026-10-09

The final `dist/EpochPact.exe` passed the installer and packaged HTTP checks with
external Python removed from the child process's PATH. Python is included in the
EXE. Windows WebView2 Runtime and .NET Framework 4.8 remain prerequisites.

## Artifact

- EXE: 26,954,044 bytes.
- EXE SHA256: `ba416ea88f27db008b66ec9acb46f447519477bf9db3eaf50ce73c12aac67c4d`.
- Native player SHA256: `6265d3e4f15a609f174a3b577dd81e6df2e59dea2cfbbecef66f43d8d52f7a9f`.
- ZIP SHA256: `59d15c15de277d58d2afabbf717bf8bc812e5b61b154cb08d06feb8d3d5f9417`.
- Release: `v0.1.0-alpha.20261009`, with EXE, ZIP, build metadata and SHA256SUMS.
- Target: Windows x64, Last Epoch 1.5.2.
- Research DLLs and historical player snapshots are excluded from the archive.
- `dist/START-HERE.txt`, `THIRD-PARTY-NOTICES.txt` and `build-info.json` accompany it.

## Completed checks

137 Python tests and 74 JavaScript tests passed. The checks cover first-run
setup, custom paths, UTF-8 persistence, missing/wrong executables, game-running
guards, a foreign loader, tampered or disabled mod files, queue ordering and
installation failure recovery. They also verify that a typed path survives a UI
refresh and that startup never installs, launches or submits gameplay settings.
Close regressions cover queue draining, rejection of new jobs, retained cleanup
after failure, identity-scoped stat resets, the Movespeed meter, and accurate
reporting of failed or unreadable game exit codes. All ten native test programs
passed (6,571 assertions), including the actual lifecycle detour and frame queue.
The dedicated Game setup page exposes the executable picker without requiring
the player to find it in Settings.

The compiled EXE verifier passed 14 checks. It used non-runnable game fixtures in
an isolated temporary folder and its own user-data directory. The executable's
installer helper copied the exact bundled DLLs into a custom Unicode path, refused
a foreign loader and completed explicit installation through the real HTTP job
queue. A fresh EXE process remembered the selected path and issued a new session
token. The scripts, font and both theme assets were served from the bundle, and
the startup logs contained no exception. No owner installation or saves were
changed, and no fixture game was executed.

The final compiled EXE was also started while a legacy UI server occupied its
requested port. It bound a different port and served its own setup-enabled
backend. The server now uses exclusive binding on Windows: it cannot share a
legacy server's socket or accidentally display that server's outdated interface.

Installer files are staged before publication. A failed copy leaves the previous
installation unchanged; a failed publication restores previously committed files.
If restoration itself fails, its backups remain available and an error is shown.
Regular game controls verify the installation without spawning an extra tasklist
process; existing offline/actor/native gates remain authoritative.

## Scope

The user previously confirmed that Game setup works in the desktop EXE after
performing the interactive selection test. The native picker defaulting to Steam was observed in the earlier
desktop smoke check. The final automated checks use the compiled helper and HTTP
interface, rather than driving the picker. Windows UAC approval was not exercised;
the elevated-helper contract, cancellation and installation backend were tested.

The final EXE embeds the newly built player DLL, with matching local and embedded
hashes. The ZIP contains that exact EXE, matching build metadata, the quick start
and third-party notices; all entries pass CRC and byte-for-byte comparison.
Earlier live-game evidence remains in
[the complete mod verification report](all-mods-current-verification.md). This
packaging check is not a new live test of every gameplay mutation.

Desktop cleanup was verified in an earlier build; the final meter correction and
worker logging were checked with automated regressions. A full-game UnityPlayer
exit crash also occurred with EpochPact completely removed. The release is an
alpha; see [shutdown evidence and limits](shutdown-fix-2026-10-09.md).

## Reproduce

```powershell
# Run inside the developer packaging environment, from the repository root.
native\build.bat player
python -m unittest tools.test_progression_backend tools.test_monolith_backend tools.test_cof_backend tools.test_le_session tools.test_loot_crafting_backend tools.test_ui_bridge tools.test_collection_backend tools.test_player_setup tools.test_shutdown -v
node --test tools/test_stat_model.cjs tools/test_ui_session.cjs tools/test_ui_readiness.cjs tools/test_launcher_ui.cjs
python -m tools.build_player
python -m tools.verify_player_package
```

Machine-readable local evidence: `research/live/player-package-final-20261009/verification.json`.
Full test/build output: `build/release-python-tests.log`, `build/release-javascript-tests.log`,
`build/release-native-build.log`, `build/release-*_test.log`,
`build/release-package-build.log` and `build/release-package-verification.log`.
