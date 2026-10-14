# EpochPact 0.1.2-dev.27

Compact, see-through loot and damage windows; custom items with T1–T8 affixes
and your own rolls.

## Loot feed and damage windows

- **No title bar.** Point at a window to see its thin bar: drag the bar to move
  the window, drag the corner grip to resize it.
- **− hides the window**, and the buttons in EpochPact now read **Open** or
  **Hide loot window** / **damage window** as the windows really are.
- **⚙ opens the window options:** background colour and **background opacity**
  (0–100% — only the background becomes see-through, the text keeps its full
  strength and gets a dark outline when the background is light), text colour,
  text size, row spacing, and whether the bar shows always or on hover. The
  loot feed keeps its Gold/Materials filters there; the damage window keeps
  Compact, with Start/Stop/Reset in its bar.
- **Place, size and options are remembered.** A window left open when EpochPact
  closes opens again when it starts, however you close EpochPact (its X, the
  taskbar, Alt+F4); only − or Hide forgets it. Closing EpochPact now closes its
  small windows too.

## Custom items: tier and roll per affix

- **Crafting → Custom Items:** every chosen affix has its own **Tier** (T1–T8)
  and **Roll** (1–100% of that tier's range) or **Random**. New affixes start at
  T7 with the item's roll quality, as before.
- **T8 is the game's primordial tier:** one per item, offered only for affixes
  the game has a T8 for, and made with the game's own primordial affix (sealed).
  The game limits how many primordial items you can wear.

## Translations

- The new texts are translated into Türkçe and Русский.

## Updating

With 0.1.2-dev.22 or later: **New version · What's new → Install update** in
the app. From an older version: close Last Epoch and EpochPact, extract the
**complete new ZIP**, open `EpochPact/EpochPact.exe`, and select **Game setup →
Reinstall / update mod**. The new crafting needs the updated mod: Install update
reinstalls it once the game is closed.

Tests: [windows and crafting](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.27/docs/windows-crafting-validation-2026-10-14.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
