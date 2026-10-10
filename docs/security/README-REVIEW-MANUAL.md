# EpochPact — transparent folder build (security review candidate)

This is a review candidate, not a Nexus-approved replacement for the quarantined
0.1.1-alpha.1 files. The reason for those antivirus detections has not been
established. Do not disable antivirus, add exclusions or bypass Nexus quarantine.

## Requirements

- Windows 10/11 x64.
- Steam Last Epoch 1.5.2.1, build 25816319. Offline characters only.
- Microsoft Edge WebView2 Runtime and .NET Framework 4.8.
- No separately installed Python, MelonLoader or BepInEx.

Keep the entire `EpochPactUI` folder together, including `_internal`. The EXE uses
the separately visible bundled Python/runtime files; it does not unpack the app
into a temporary `_MEI` folder. `_internal` is required, not development material.
Every distributed file has a size and SHA-256 entry in `runtime-manifest.json`.

## Manual installation

1. Close Last Epoch normally. Extract the whole ZIP into a separate folder.
2. Open `EpochPactUI/EpochPact.exe`, use **Game setup → Browse** to select the
   actual `Last Epoch.exe`, and verify that the selected game build is supported.
3. Check the game folder for an existing `version.dll`. Never overwrite another
   mod loader. If updating EpochPact, first keep a copy of its existing
   `version.dll` and `EpochPact/EpochPact.Core.dll` outside the game folder.
4. Copy the contents of `CopyToGame` into the selected Last Epoch game folder,
   beside `Last Epoch.exe`. Required paths are `version.dll` and
   `EpochPact/EpochPact.Core.dll`. Do not copy `EpochPactUI` into the game.
5. Refresh **Game setup**, choose **Launch offline** and load an offline
   character. The app also offers explicit **Install mod** using these same
   bundled DLLs if manual copying is not preferred.

Opening the app does not install the mod, launch the game, complete quests or
grant rewards. Settings apply when changed; save actions require their buttons.
Closing the app restores session settings/bonuses it changed; saved progression
and rewards are retained. Back up important characters before modded play.

## Updating, rollback and removal

Close the game and app first. Extract an update into a new app folder; do not mix
runtime files from different versions. Replace only EpochPact's two game DLLs,
retaining the previous copies for rollback. To roll back, restore those copies
and use the matching previous app folder.

To remove the mod, close the game/app and remove only EpochPact's `version.dll`
and `EpochPact/EpochPact.Core.dll`, plus this extracted app folder. Leave player
saves, unrelated mods and existing backups/logs alone. App settings are under
`%LOCALAPPDATA%/EpochPact`; keep them if you may reinstall.

## Review scope and known limitations

Candidate `0.1.1-alpha.2-review.3` preserves unreadable item-hover state instead
of interpreting it as no tooltip. Its loader initializes on the first forwarded
version call, outside its DllMain. Hook writing checks thread/cache/protection
failures, rolls failed patches back, preserves foreign patches and restores owned
hooks before IL2CPP shutdown. Installer result files use the OS-known user folder,
reject linked paths and cannot overwrite existing files. Permission errors remain
visible. These are functional/security changes, not antivirus verdicts.

The first review candidate updated Flask for its published session-cache advisory,
removed unused build tooling and restricted DLL search paths. Game hooks remain
required; these candidates do not establish why security products flagged the files
or certifies absence of malware. Do not treat old scan results as scans of the
new EXE/core/loader/ZIP.

The explicit administrator installer still elevates the bundled Python app tree;
the review's tamper-before-elevation concern remains open. Prefer the manual
copying option while a separate trusted helper is being designed. Never elevate
a package from an unverified source.

Do not infer fresh gameplay validation from a successful build or installer
fixture test. See the accompanying verification report in the source repository.
The previously observed full-game exit `0xC0000005` remains unresolved; this build
does not claim to fix it. Weaver/Merchant's Guild management remains informational.

Support: https://discord.gg/q6aexZZSAf

Source / review results: https://github.com/falorfrozen-cmd/EpochPact

Report the candidate/package version, game build, action, exact error and runtime
hashes. Redact private character names and local paths before posting logs.
