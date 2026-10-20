# EpochPact — In-game UI {{VERSION}}

Private review build for Windows x64 / **Last Epoch 1.5.2.1, offline only**.
The tested GameAssembly.dll build is listed in supported-game-builds.json.

This package contains one runtime file: **version.dll**. The EpochPact core,
interface, images and fonts are embedded in it. No Python, desktop EXE,
WebView2, MelonLoader or separate EpochPact.Core.dll is required for this route.

## Install or update

1. Close Last Epoch and the EpochPact desktop app normally.
2. Back up your offline saves and the current mod files before updating.
3. Open the folder containing **Last Epoch.exe** (Steam: Manage → Browse local files).
4. If that folder already has a version.dll, keep a copy outside the game folder.
   Do not replace an unrelated loader without checking which mod owns it.
5. Copy this package's **version.dll** beside **Last Epoch.exe**.
   An older EpochPact/EpochPact.Core.dll is not used by this merged loader.
6. Launch Last Epoch in its offline mode and enter an offline character's area.
7. Press **F6** to open or close EpochPact.

The in-game route does not install the optional MelonLoader bridge. Compatibility
with arbitrary other loaders/mods has not been certified for this review build.
Keep the desktop app closed while testing this route so that two interfaces do
not both apply a startup profile.

## Controls

- F6: open/close the main interface. Search finds settings across pages.
- Scroll inside the interface, long dialogs or independent HUD windows.
- Changes marked automatic apply after editing; draft panels use Apply changes.
- Save-changing actions have their own confirmation. Read the description first.
- Open Damage / Recent pickups from the corresponding page. These HUDs remain
  visible while the main interface is closed. Drag their title area, resize the
  lower-right grip, use the gear for display options, and − to hide them.
- Settings contains language, local profiles, startup behaviour, text size,
  navigation favorites, hidden parts and the support report.
- Gamepad focus navigation is implemented but has not been hardware validated.

The Game setup page describes the running installation; selecting or installing
an executable is a desktop operation. Restore a save backup only with the game
closed. No save restore runs when opening this interface.

Loot filter import accepts a full XML/ZIP path. Imports create new files and do
not overwrite existing filters. Backups and exports are under the local
EpochPact folder; ZIP imports accept only top-level filter XML files.

## Uninstall / roll back

Close the game normally. Remove **only the EpochPact version.dll you installed**,
or restore the previous version.dll you backed up. Keep unrelated mods, saves
and preferences. Removing the DLL does not undo permanent save changes already
confirmed in the UI; use your backup if you need to undo those changes.

## Verification and support

runtime-manifest.json records every packaged file's size and SHA-256.
SHA256SUMS.txt also covers that manifest. This is a local review package,
not a public release or an antivirus clearance claim.

The test report distinguishes game reads, UI renderer fixtures, and save actions
which were not executed. New read-only screens being tested successfully does
not prove every possible build/crafting combination works.

Support: https://discord.gg/q6aexZZSAf

Public project: https://github.com/falorfrozen-cmd/EpochPact

Report the EpochPact version, Last Epoch version, install method, offline area,
the exact action, error text and a screenshot. A support report may contain
local paths and character names; inspect it before sharing.

Unofficial community mod by Falor. No Last Epoch game files, saves, SDKs, tests
or private logs are included. See the included distribution and third-party notices.
