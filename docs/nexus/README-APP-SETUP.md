# EpochPact 0.1.1-alpha.1 — optional app installation

This is an alternative to manual native-DLL copying, not a required extra download. The EXE contains the same player loader and core as the Main manual ZIP and the Python runtime. No separate Python install is needed.

1. Extract the ZIP to a writable folder and run EpochPact.exe.
2. Game setup > Browse > select Last Epoch.exe. The picker opens in Steam's folder; other drives/libraries/custom folders are supported.
3. Close Last Epoch. Press Install mod. If Windows denies file access, use the explicit Install as administrator action and approve the Windows prompt yourself.
4. Launch offline and load an offline character.

The verified game build is Steam Last Epoch **1.5.2.1**, build **25816319**. Unknown GameAssembly.dll hashes are refused before installation or modded launch. The app refuses a foreign version.dll or installation while the game runs. Files are staged; a failed publication restores the old files where possible, and a failed rollback retains recovery files. Previous files from successful updates remain in <game>/EpochPact/install-backups/<id>; close the game and follow that backup's README to restore them. Keep the matching previous UI EXE yourself.

Requirements: Windows 10/11 x64, Microsoft Edge WebView2 Runtime, .NET Framework 4.8 and legitimate Steam Last Epoch. The bundled EpochPact loader is required; no MelonLoader/BepInEx/SDK is needed. Offline only. Do not mix UI and native versions. Full installer elevation through a real UAC approval is not certified by automated fixtures.

Uninstall, controls, save-change behavior, limitations, credits, support and manual rollback are described in README-MANUAL.md. Read it before using progression actions. No game files or saves are distributed.
