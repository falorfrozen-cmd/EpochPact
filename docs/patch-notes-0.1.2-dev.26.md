# EpochPact 0.1.2-dev.26

Settings that stay after a restart; the app in Turkish and Russian.

## Settings that stay

- **Your last used settings come back by themselves.** Settings → Draft profiles
  → Apply at startup now starts on **My last used settings**: once each time an
  offline character is ready — after you start EpochPact, restart the game or
  install an update — your multipliers, collection, camera, minimap, graphics
  and HUD switches return, followed by the loot mode, collection categories,
  wanted affixes and font. Choose **Off** to start with the game's normal values
  instead.
- **Preferences are kept in a file.** Profiles, the startup choice, theme,
  navigation, hidden stats and columns, window places and the language are saved
  in `preferences.json` in your EpochPact user folder, so they survive restarts,
  another interface port and updates.
- **The loot feed and damage windows remember their place and size.** A window
  left open when EpochPact closes opens again when it starts, and a damage
  session left running starts again once the game is ready. Closing a window
  yourself forgets it.

## Language

- **Settings → Language: English, Türkçe, Русский.** The main window, the loot
  feed and the damage window switch at once, including notifications,
  confirmations and screen-reader labels.
- Names that come from the game — items, affixes, stats, quests, timelines,
  faction and Circle of Fortune perks — stay as the game data writes them, and
  your loot filter rule names are never translated. The in-game overlays keep
  the game's language.

## Smaller changes

- The pickup radius unit reads **game units**; loot mode choices read **All**,
  **Filter**, **Quality** and **Materials**.

## Updating

With 0.1.2-dev.22 or later: **New version · What's new → Install update** in
the app. From an older version: close Last Epoch and EpochPact, extract the
**complete new ZIP**, open `EpochPact/EpochPact.exe`, and select **Game setup →
Reinstall / update mod**.

Tests: [persistence and language](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.26/docs/persistence-language-validation-2026-10-14.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
