# EpochPact {{VERSION}} — in-game edition

Unofficial offline mod for **Last Epoch 1.5.2.1** (Windows x64, **offline play only**).
The supported game build is listed in supported-game-builds.json.

The whole mod is one file: **version.dll**. The EpochPact core, its interface,
images and fonts are inside it. You do not need Python, the desktop EXE,
WebView2, MelonLoader or a separate EpochPact.Core.dll. Press **F6** in the game.

## Install or update (manual)

1. Close Last Epoch (and the EpochPact desktop app, if you used it before).
2. Back up your offline saves before updating any mod.
3. Open the folder that contains **Last Epoch.exe**
   (Steam: right-click Last Epoch → Manage → Browse local files).
4. If that folder already has a **version.dll** from another mod, keep a copy of it
   somewhere else first; only one version.dll can be in the folder.
5. Copy this package's **version.dll** next to **Last Epoch.exe**
   (replace an older EpochPact version.dll if asked).
   A leftover EpochPact\EpochPact.Core.dll from an older version is not used.
6. Start Last Epoch, choose offline play and load your offline character.
7. Press **F6** to open or close EpochPact.

## Using it

- F6 opens and closes the window. The search box finds settings on every page.
- Settings that say "Changes apply automatically" apply right away; panels that
  say "Use Apply changes below" wait for the Apply changes button.
- Actions that change your save (stocking materials, crafting, custom items,
  quests, Monolith, Circle of Fortune) ask first and make a recovery backup.
  Read the description before you confirm.
- The damage window and recent pickups can stay on screen while the main window
  is closed: open them from their pages, drag the title, resize from the corner,
  use the gear for options.
- Settings has the language, saved profiles, what to apply at startup, text
  size, navigation, hidden parts and a support report.
- Loot filters: import an XML or ZIP by its full path; imports create new files
  and never overwrite your filters. Backups and exports are kept in the local
  EpochPact folder.
- Use either the in-game window or the desktop app, not both at once: both
  would apply your startup profile.

## Uninstall

Close the game normally and delete **the EpochPact version.dll** from the game
folder (or put back the version.dll you saved in step 4). Your saves and other
mods stay. Removing the mod does not undo save changes you already confirmed;
use the recovery backup or your own backup for that.

## Good to know

- Offline only. EpochPact does nothing in online play.
- Gamepad navigation is included but has not been tested with a physical
  controller yet.
- runtime-manifest.json and SHA256SUMS.txt list the size and SHA-256 of every
  file in this package, so you can check your download.

## Support

Discord: https://discord.gg/q6aexZZSAf
Project: https://github.com/falorfrozen-cmd/EpochPact

When you report a problem, include the EpochPact version, the Last Epoch version,
where you were in the game, what you did, the error text and a screenshot. The
support report (Settings) can contain local paths and character names: look at it
before you share it.

Unofficial community mod by Falor; not made by Eleventh Hour Games. No game
files, saves or SDKs are included. See the distribution and third-party notices.
