# EpochPact 0.1.1-alpha.1 — manual player package

Your timeline. Your rules. Last Epoch offline tools for Windows x64.

## Requirements

- A legitimate Steam installation of Last Epoch **1.5.2.1**, Steam build **25816319** (internal game version 1.5.2.2). Other builds are not certified.
- Windows 10/11 x64, Microsoft Edge WebView2 Runtime and .NET Framework 4.8. Python is bundled; no separate Python installation is needed.
- The included EpochPact `version.dll` loader and `EpochPact.Core.dll` are required. Do not remove either DLL. No MelonLoader, BepInEx, development SDK or additional mod loader is required.
- **Offline characters only.** Online gameplay is unsupported and native mutations are gated to offline play.

WebView2, if missing: https://developer.microsoft.com/microsoft-edge/webview2/

## Clean manual installation

1. Close Last Epoch and EpochPact. Back up your offline saves and any existing mod files yourself before installing. Never overwrite another mod loader's `version.dll`.
2. Check the version shown by the game. The supported GameAssembly.dll hash and byte size are in `supported-game-builds.json`. A manual copy cannot enforce compatibility before the next game launch; check before copying.
3. Extract this ZIP to an ordinary folder. Do not copy the entire ZIP contents into the game folder.
4. Copy **only the contents of `CopyToGame`** into the folder containing `Last Epoch.exe`:

```
<Last Epoch game folder>/version.dll
<Last Epoch game folder>/EpochPact/EpochPact.Core.dll
```

For Steam, use Manage > Browse local files. Any Steam library or custom folder is supported. The default is `C:\Program Files (x86)\Steam\steamapps\common\Last Epoch`.

5. Keep `EpochPactUI/EpochPact.exe` in a writable folder outside the game installation. Run it, open **Game setup**, press **Browse**, and select your installed `Last Epoch.exe`.
6. The exact manually copied DLLs are detected as installed. You do not need to press **Install mod**. Press **Launch offline** and load an offline character. Game setup refuses an unknown GameAssembly.dll build.

The game must be able to write its `EpochPact` IPC directory. If a protected game folder prevents this, use a Steam library in a writable folder. Do not run the whole interface as administrator by default. Grant Windows administrator permission only for an explicit installation when necessary.

## Update and rollback

Close the game and app. Keep a copy of the old UI EXE, `version.dll` and `EpochPact/EpochPact.Core.dll` outside the game folder. Replace those three files with the same-version files from the new package. Preserve the existing `EpochPact` backups and other mods. Do not mix native DLLs and UI versions.

To roll back a manual update, close both programs and restore your three previous files. UI installation also retains previous mod files in `<game>/EpochPact/install-backups/<id>`. Follow the README in the selected backup to restore those files; use the matching old UI download. These mod backups contain no player saves.

## Uninstall

Close the game and app. Remove the EpochPact copy of `<game>/version.dll` and `<game>/EpochPact/EpochPact.Core.dll` only. Compare the manifest hashes if unsure which files belong to this release. Never delete another loader or an entire shared mod folder. Delete the extracted UI folder if no longer needed.

Logs, IPC, mod-install backups and progression recovery backups can remain in `<game>/EpochPact`; without the loader and core they do not run. Preserve recovery backups unless you deliberately choose to discard them. App preferences/profiles/backups are under `%LOCALAPPDATA%/EpochPact`; keep them for a future reinstall. Game saves are under `%USERPROFILE%/AppData/LocalLow/Eleventh Hour Games/Last Epoch/Saves` and are not removed by uninstalling.

## Controls and save changes

This is an external desktop interface; there is no required in-game hotkey. Choose a section in the sidebar. General session sliders apply automatically after an edit. Character bonuses have their own Apply/Reset controls. Campaign rewards, unlocks, faction balances and other permanent changes require their explicit action buttons; opening the app does not grant rewards or alter quests.

Keep the game running and unpaused while applying an action. If background execution is unavailable, return to the game; do not repeatedly click a timed-out save action. On app close, controls changed in that session return to their neutral defaults and its own character bonuses are removed. This does not restore another tool's earlier non-default settings. Completed quests, granted rewards and saved progression remain. A cleanup failure keeps the app open and displays an error.

## Known issues / verification limits

This is a public alpha. Last Epoch **1.5.2.1** is the current compatibility target; the extensive retained full-feature gameplay audit was on **1.5.2**. The release report separates the newer build smoke check, previous gameplay checks, installer fixtures and shutdown outcomes. Not every combat consumer, unusual crafting path, mod combination or hardware configuration is certified.

Full game exit access violations (`0xC0000005`, UnityPlayer.dll) occurred in tests, including an earlier control with EpochPact removed. On the current 1.5.2.1 build, one exit returned zero and a second returned `0xC0000005`. The release EXE's normal window close restored XP from 2x to 1x and Intelligence from 22 to 11 while leaving the game running. This does not establish the cause of every game-exit crash. Closing the EpochPact window and fully exiting the game are separate tests; no zero-crash guarantee is made. Weaver and Merchant's Guild management are informational, not implemented controls. Stash reminders do not lock or protect items in the game.

Unsigned EXE/DLL files may require security review. Do not disable your antivirus. A quarantine's cause must be investigated; it is not automatically a false positive. Both packages contain the same native player code.

## Support and credits

Discord: https://discord.gg/q6aexZZSAf
Source and releases: https://github.com/falorfrozen-cmd/EpochPact

Report EpochPact version **0.1.1-alpha.1**, game version/Steam build, Windows version, manual/app installation method, exact action, error text and whether it occurred during gameplay, app close or game exit. Screenshots are helpful. Share log extracts only after removing character names, paths and other personal information. Do not post entire saves or private logs publicly.

Created by Falor. Community-made and not affiliated with Eleventh Hour Games. AI-assisted code/UI and AI-generated promotional art are disclosed on the Nexus page. Third-party runtime/font licenses are in `THIRD-PARTY-NOTICES.txt` and `INTER-OFL.txt`. No game-owned executable, GameAssembly.dll, metadata or save files are included. See `DISTRIBUTION-PERMISSIONS.txt` for project permissions.

`runtime-manifest.json` records every payload file's size and SHA-256. `SHA256SUMS.txt` also covers the manifest. The separately supplied outer ZIP hash covers the entire download.
