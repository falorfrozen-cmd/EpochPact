# EpochPact {{VERSION}}

Your timeline. Your rules. Last Epoch offline tools for Windows x64.

## Start here — choose one installation method

**Recommended:** open **EpochPact/EpochPact.exe**, select your game in **Game setup**, and press **Install mod**. The app already includes the game file it installs. You can ignore the **Manual Installation** folder entirely.

**Alternative:** **Manual Installation** contains the same single game file (**version.dll**) for people who want to copy it themselves. Follow the manual steps below instead of pressing **Install mod**. It is not an additional installation step.

Keep the **EpochPact** app folder complete, including **EpochPact.exe.config** and **_internal**. Do not copy the whole ZIP into your game folder.

## In-game window (F6)

The game file this app installs also contains EpochPact's window inside the game: with an offline character loaded, press **F6** to open or close it. It has the same pages as this app, so you can also play without keeping the app open.

## Requirements

- Legitimate Steam Last Epoch **1.5.3**, build **26087601** (1.5.2.1, build 25816319, is still accepted). Unknown GameAssembly.dll builds are refused by Game setup.
- Windows 10/11 x64, .NET Framework 4.8 and Microsoft Edge WebView2 Runtime. No separate Python installation is needed.
- The included **version.dll** (EpochPact's loader, with the mod's core built into it) is necessary. Do not remove runtime DLLs from the extracted app folder.
- Offline characters only. Online play is unsupported.

WebView2: https://developer.microsoft.com/microsoft-edge/webview2/

## Install with the app

1. Close Last Epoch. Extract the complete ZIP to a writable folder.
2. Run **EpochPact/EpochPact.exe**. Keep **EpochPact.exe.config** and **_internal** beside it.
3. Open **Game setup**, press **Browse**, select your installed **Last Epoch.exe**, then **Save path**. Another Steam library or custom path is supported.
4. Press **Install mod**. If Windows denies the installation, use the explicit **Install as administrator** action and approve the Windows prompt yourself.
5. Press **Launch offline** and load an offline character. Wait until an area has finished loading.

An existing supported MelonLoader installation is preserved; the app installs its own included EpochPact bridge alongside it. This does not guarantee compatibility with every third-party gameplay hook. An unknown foreign loader is refused, rather than overwritten.

## Manual installation (without another loader)

1. Close the game and app. Back up your existing mod files and offline saves.
2. Confirm the supported game version/hash in **supported-game-builds.json** before copying.
3. Open **Manual Installation** and copy only its contents into the folder containing **Last Epoch.exe**. Do not copy the folder named **Manual Installation** itself:

```
<game folder>/version.dll
```

That one file is the whole mod: since 0.1.2-dev.36 the core is built into it, so there is no separate **EpochPact.Core.dll** to copy. If an earlier release left **EpochPact/EpochPact.Core.dll** in your game folder, **Install mod** removes it (a backup is kept); by hand you may delete it once the new **version.dll** is in place.

Never overwrite another loader's version.dll. Use the app's installation flow for supported MelonLoader coexistence.

4. Keep the **EpochPact** app folder outside the game installation. Run its EXE, select the game path in **Game setup**, and press **Launch offline**. An exactly copied **version.dll** is detected as installed.

The game must be able to write its EpochPact IPC folder. A writable Steam library avoids requiring administrator rights for ordinary play. Do not disable security software to install this mod.

## Update, rollback and removal

From 0.1.2-dev.22 on, **New version → Install update** in the app downloads the next release from EpochPact's GitHub page, checks its SHA-256 and every file, replaces this folder and reopens EpochPact; the mod in the game folder is updated when Last Epoch is closed. To update by hand instead, close the game and app, extract the new complete app folder, then use **Install mod** to update the owned runtime. Do not mix UI and DLL versions. Previous runtime files are backed up under **<game>/EpochPact/install-backups/<id>**; follow that backup's README with the matching older app to roll back.

For manual updates, retain the previous **version.dll** and complete app folder outside the game, then replace them with the matching new versions.

To remove the mod, close Last Epoch and press **Game setup → Uninstall mod**. It removes only EpochPact's loader (or its MelonLoader bridge and core); another loader, its mods, your saves, logs and recovery backups stay. To remove a manual installation by hand, close both programs and remove only the EpochPact-owned **<game>/version.dll** (and **<game>/EpochPact/EpochPact.Core.dll** if an earlier release left one). Compare manifest hashes if ownership is unclear. Never delete a foreign loader, shared mod folder or player saves. Removing the extracted app folder removes the UI. Recovery backups and local preferences may be retained.

## Using the controls

Session settings apply automatically after an edit. Campaign rewards, mastery selection, progression changes, material grants and free tree respec require their explicit action buttons. Opening the interface does not grant rewards or apply saved actions.

Closing EpochPact returns the game to its normal values until it starts again: by default your **last used settings** come back by themselves once each time an offline character is ready — after you start EpochPact, restart the game or install an update. They include multipliers, collection, camera, minimap, graphics and HUD switches, loot modes, collection categories, wanted affixes and the font. Your **Character bonuses** come back with them, separately for each character (**Reset all bonuses** removes them). To start with the game's normal values instead, choose **Off** in **Settings → Draft profiles → Apply at startup**, or choose a saved profile there. The loot and damage windows reopen where you left them, at the same size, and a damage session that was running starts again. Profiles, theme, window places and options are kept in `%LOCALAPPDATA%\EpochPact\preferences.json`, so updates and interface port changes keep them.

Inventory shortcuts show **Stash**, **Stash all** and **Vendor** in their own centered row below the item grid, above the game's Transfer/Sort and currency footer. The grid is scaled slightly to make room; item cells stay square and every currency digit stays visible. Stash and Vendor toggle their windows. If the inventory bounds are unsupported, the shortcuts are hidden rather than covering another control. The Inventory page remains available.

**Settings → Language** switches the EpochPact windows (main window, loot feed and damage window) between English, Türkçe and Русский; the choice is kept with your other preferences. Names that come from the game — items, affixes, stats, quests, zones and your loot filter rule names — stay as the game data writes them, and the in-game overlays keep the game's language.

Settings includes navigation favorites and hidden sections. Character stats and the summon table support hiding unwanted entries/columns. Stash Assistant includes copyable search examples.

Free tree respec refunds allocated points while retaining the selected mastery, skill levels and XP. Check the confirmation before applying a save action. A recovery backup is created first.

Custom mixed crafting recipes require the mod and **<game>/EpochPact/workshop-recipes.txt** on every PC where those items are used. Back up that database with your saves. Steam Cloud alone does not make mixed recipes portable to an unmodded game. Restore original gear before removing this feature; never overwrite another PC's recipe database to merge independently created collections.

On normal EpochPact app close, session modifications return to neutral values. Permanent quest rewards, progression and crafting-material grants remain. App close and full game exit are separate operations.

## Known limits

Not every encounter, summon family, hardware configuration or third-party mod has been certified. Larger pickup radii consider more loaded items and can cause more collection work; they do not load distant areas.

Actual summon DPS requires combat and a sampling window; a percentage damage bonus is not DPS. Compare against the same target under the same conditions. Changing equipment while idle cannot establish its exact combat DPS.

Intermittent full-game exit access violations (**0xC0000005**, UnityPlayer.dll) were observed in testing. The same module and fault offset also occurred in a vanilla game session with the EpochPact loader absent, after an automated window/accessibility query. This establishes an environment baseline, not a general cause or fix. Successful closes and feature tests do not establish a crash-free game-exit guarantee. See **docs/player-feedback-validation-2026-10-12.md** in the source repository for separate feature, installer and shutdown results.

Merchant's Guild and unfinished Weaver faction management remain informational. Stash protection reminders do not lock items in the game. No antivirus or Nexus clearance is implied by this package.

## Support and credits

Discord: https://discord.gg/q6aexZZSAf

**Settings → Support report** creates a ZIP with your description, versions, setup state and recent EpochPact logs, plus an optional screenshot of the game window. Nothing is uploaded: the folder opens so you can attach the ZIP to your Discord message. Your Windows user name is masked; logs can contain character names.

Source, releases and validation: https://github.com/falorfrozen-cmd/EpochPact

Report EpochPact **{{VERSION}}**, game/Steam build, Windows version, installation method, the exact action and error, and whether the issue occurred during gameplay, app close or game exit. Remove private names/paths before sharing log excerpts. Do not post saves publicly.

Created by Falor. Community-made, not affiliated with Eleventh Hour Games. AI-assisted code/UI and AI-generated promotional artwork. Runtime and font credits are included. No game executable, GameAssembly.dll, game metadata, private logs or saves are distributed.

**runtime-manifest.json** and **SHA256SUMS.txt** record every payload file's size and SHA-256. The separate outer ZIP hash verifies the complete download.

## Crafting

- **Forge Settings**: preserve materials, tune Forging Potential costs and choose game-default or custom glyph chances. Use **Apply changes** for these session settings.
- **Materials**: enter a minimum stock, then press **Stock all materials** and confirm. This adds saved materials once; it does not enable infinity.
- **Custom Items**: select equipment in the game forge, load item choices, choose a template and searchable affix checkboxes, then review and confirm. Extra Unique/Set effects are in an optional advanced section.

Opening categories, searching and switching themes do not craft or grant materials. Extra mixed effects require EpochPact and its workshop-recipes.txt file when reloading or moving items.

## Connection and matching versions

Use the EXE and DLLs from this complete package together. After updating, close
Last Epoch and use **Game setup → Reinstall / update mod**. **Mod update required**
means the installed core does not match this package; replacing only the EXE is
not enough. Hash verification is still enforced.

**Mod connected · Waiting for character…** means the DLL answered but the offline
character or area is still loading. **Waiting for game updates…** means Unity has
not processed a read yet. **Game data unavailable** is a data-read failure while
the DLL remains connected. Gameplay controls wait until the character is ready.
Connection recovery only reads game state; it never completes quests or grants
rewards. A timed-out save action is never automatically resubmitted.
