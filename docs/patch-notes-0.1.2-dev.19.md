# EpochPact 0.1.2-dev.19

Player suggestions, last group: stash tabs from the loot filter and the
interface font, plus the open points of dev.18.

## Stash tabs from your loot filter (Inventory page)

- **Create tabs for rules without a tab**: one stash tab per loot filter rule,
  free of gold, in a folder named after the filter. Hide and disabled rules can
  be included.
- Each tab takes the rule's rarity, type, class, item level, potentials,
  faction, unique and corruption conditions as its **stash priority**, so the
  game's own Stash all puts matching items there.
- **Sort inventory by filter rule** moves items into their rule's tab with the
  game's exact filter match, including affix conditions.
- Link any existing tab to a rule, or remove the link, from the same table.
- Every change creates a recovery backup.

## Text and font (Display page)

- **Interface font**: any installed Windows font for the game's interface text;
  missing characters still use the game's font.
- **Interface text size**: 0.7–1.5×.
- "Game font" and size 1 restore everything exactly.

## Item tiers

- New **Colour affix lines by tier**: the whole affix line in its tier colour,
  alone or together with the T1–T8 tags.
- Sealed, primordial and corrupted affix lines get their tier tag too.
- New default colours (a player's palette, T2 and T5 slightly lighter for
  readability): T1 `FFFFFF`, T2 `2E8BFF`, T3 `FFD700`, T4 `FF5500`, T5 `AA55FF`,
  T6 `00FF00`, T7 `FF0044`, T8 `00FFFF`.
- Fixed: after a setting or colour change, an item whose tooltip was already
  shown kept the old look until another item was hovered.

## In-game report

- Press **F8** in the game to save a report (screenshot, state, logs and a
  description file) in `Last Epoch\EpochPact\reports`. The key can be changed
  or turned off in Settings. Nothing is uploaded; zip the folder and share it on
  Discord.

## App

- The version is shown in the sidebar, and a notice appears when a newer
  release is published (can be turned off).
- Sections reopen where you left them (can be turned off).
- The Game setup panel is no longer repeated on Overview and Settings; Overview
  shows one status line.
- Navigation and favorites: section names and their checkboxes sit side by
  side.

## Checked again

- Overlay map zoom: measured in the game at 0.5×, 1× and 4×.
- Imported and duplicated loot filters: the game loads them as the active
  filter.

## Updating

Close Last Epoch and EpochPact. Extract the **complete new ZIP**, open
`EpochPact/EpochPact.exe`, and select **Game setup → Reinstall / update mod**.

[Measured tests and limitations](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.19/docs/feedback-group4-validation-2026-10-12.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
