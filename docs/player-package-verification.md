# Windows player package verification — 2026-10-09

The final `dist/EpochPact.exe` passed the installer and packaged HTTP checks with
external Python removed from the child process's PATH. Python is included in the
EXE. Windows WebView2 Runtime and .NET Framework 4.8 remain prerequisites.

## Artifact

- EXE: 26,950,160 bytes.
- EXE SHA256: `dfee4fe2af65d83f1c60b054608d1c008b1636a7c4cdbfe57cce6cf159e17262`.
- Native player SHA256: `e514a3f24f151f4ada2434bf2a8e561caec57e14ebf277deae1f157b5db0c3ec`.
- Target: Windows x64, Last Epoch 1.5.2.
- Research DLLs and historical player snapshots are excluded from the archive.
- `dist/START-HERE.txt`, `THIRD-PARTY-NOTICES.txt` and `build-info.json` accompany it.

## Completed checks

125 Python tests and 74 JavaScript tests passed. The new checks cover first-run
setup, custom paths, UTF-8 persistence, missing/wrong executables, game-running
guards, a foreign loader, tampered or disabled mod files, queue ordering and
installation failure recovery. They also verify that a typed path survives a UI
refresh and that startup never installs, launches or submits gameplay settings.
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

The user confirmed that Game setup works in the updated desktop EXE after
performing the interactive selection test. The native picker defaulting to Steam was observed in the earlier
desktop smoke check. The final automated checks use the compiled helper and HTTP
interface, rather than driving the picker. Windows UAC approval was not exercised;
the elevated-helper contract, cancellation and installation backend were tested.

The native player DLL is unchanged. Earlier live-game evidence remains in
[the complete mod verification report](all-mods-current-verification.md). This
packaging check is not a new live test of every gameplay mutation.

## Reproduce

```powershell
# Run inside the developer packaging environment, from the repository root.
python -m unittest tools.test_progression_backend tools.test_monolith_backend tools.test_cof_backend tools.test_le_session tools.test_loot_crafting_backend tools.test_ui_bridge tools.test_collection_backend tools.test_player_setup -v
node --test tools/test_stat_model.cjs tools/test_ui_session.cjs tools/test_ui_readiness.cjs tools/test_launcher_ui.cjs
python -m tools.build_player
python -m tools.verify_player_package
```

Machine-readable local evidence: `research/live/player-package-20261009/verification.json`.
Full test/build output: `build/python-tests.log`, `build/javascript-tests.log`,
`build/package-build.log` and `build/package-verification.log`.
