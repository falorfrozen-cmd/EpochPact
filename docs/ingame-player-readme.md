# EpochPact {{VERSION}} — in-game edition

Unofficial offline mod for **Last Epoch 1.5.4** (Windows x64, **offline play only**).
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
- **Window** (top bar): Full screen, Left half, Right half or Default size. Drag the
  top bar to move the window, drag the dots in its bottom-right corner to resize it,
  or double-click the top bar for full screen. The window remembers its place.
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
- Character bonuses are remembered for each character and applied again when it
  loads (unless Apply at startup is Off). Reset all bonuses removes them.
- Loot filters: import an XML or ZIP by its full path; imports create new files
  and never overwrite your filters. Backups and exports are kept in the local
  EpochPact folder.
- Use either the in-game window or the desktop app, not both at once: both
  would apply your startup profile.

## Your hideout

Use **Go to hideout** on the Hideout page; it enables the hub and requests the
normal Monolith Hub transition when needed. Inside, an
**Edit Hideout** button appears above the skill bar. The small house emblem
below the minimap also takes you directly to your hideout; it is disabled when
you are already there and hidden while editing or using the main mod window.

- **Templates**: inspect a thumbnail or hover for a larger actual model preview;
  Apply asks before replacing the layout. Your saved layouts also have previews,
  Rename and Delete controls. Overwriting an existing name asks for confirmation.
- Five built-in architectural designs: **The Moonstone Court**, **The Verdant
  Exedra**, **The Tideglass Terraces**, **The Sunforge Gallery**, and **The
  Celestial Rotunda**. Each has 60 objects, a distinct architectural focus and
  grouped services. Hover to inspect before applying. Updating the DLL does
  not replace your current layout or personal saved templates.
- Template and saved-layout previews render locally at 960x640; individual model
  cards remain 480x320. Old thumbnail caches refresh automatically.
- **Add objects** opens a separate **Hideout Builder** window. Use **Objects**
  to search the game's 6,134 catalog entries, filter by category,
  sort A–Z, Z–A or recently used and browse pages. Favorites and detailed
  model-type filters help narrow the list. Hover to inspect before Add & Place.
  The first preview may take a moment while the game loads that model. Ready
  thumbnails are retained and cached locally for future sessions. **Add & place**
  returns you to the world with a green placement preview. **Placed objects**
  has Move, Delete, Restore and Unlock controls; **Back to editing** closes the
  builder. Undo and Redo are available inside the builder too.
- While editing, click an object to select it. A translucent green copy follows
  your cursor; the original stays still until you click to place the copy.
  Red means the complete selection does not fit or a walking route was blocked. Right-click cancels the
  selected placement. Rotation, size and height buttons are also on the toolbar.
  Q/E rotate left/right (15 degrees by default, configurable in Tools); holding a key repeats. Left/right arrows
  also rotate, up/down arrows change size, Page Up/Page Down adjust
  height, Delete removes the selection, and End finishes editing. Visible Delete,
  Undo and Redo buttons are available. Ctrl+Z undoes; Ctrl+Y / Ctrl+Shift+Z redo
  up to 50 completed edits in this game session. A new edit clears the redo branch.
  The last completed layout is also retained for recovery after a restart.
  Gameplay input is temporarily disabled during editing, so those keys do not
  cast skills. Done/Cancel restores the previous input state; key bindings are
  never rewritten. Q/E does not rotate while the F6 window is open.
- **Tools** provides exact position, height, rotation and scale, grid snap,
  floor-piece selection, duplication, alignment, grouping and locking.
  Ctrl-click selects several objects (up to 32), Ctrl+D duplicates decoration,
  Ctrl+G groups and Ctrl+Shift+G ungroups. Locked objects can be unlocked in
  Placed objects. Explicitly grouped objects move together; Ungroup separates
  them again. Stash, vendor and gates remain unique.
- **Placed objects** gives direct selection when objects overlap. **Ground**
  changes the outline, size and one of the twelve game materials. Every existing
  model must fit a smaller ground; move edge objects inward first. A placement
  that newly blocks a service/gate route is reverted before it is saved.
- **Done** keeps the edit; **Cancel** restores the layout from the start of that
  editing session. Ordinary object edits retain the unchanged floor and models;
  changing the ground or replacing a template can rebuild the room. Save a named
  template to keep several layouts.

Layouts are local EpochPact settings, separate from character saves. The return
gate is protected. Up to 400 objects can be placed. Decorative copies do not run
their original gameplay scripts, animation, sound or particle effects. Some
model-only entries use the game's stone material instead of a placeholder;
their original level appearance is not guaranteed. The catalog is for Last Epoch
1.5.4 and is checked against the installed game's bundle catalog.

## Uninstall

Close the game normally and delete **the EpochPact version.dll** from the game
folder (or put back the version.dll you saved in step 4). Your saves and other
mods stay. Removing the mod does not undo save changes you already confirmed;
use the recovery backup or your own backup for that.

## Good to know

- Offline only. EpochPact does nothing in online play.
- Gamepad navigation is included but has not been tested with a physical
  controller yet.
- Local Windows tests reproduced a Unity shutdown crash (`0xC0000005`), including
  a control with EpochPact removed. This release does not fix that engine-side
  shutdown failure. Include the crash log if it also happens on your computer.
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
