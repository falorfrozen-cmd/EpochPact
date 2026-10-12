# EpochPact 0.1.2-dev.18

Player suggestions, third group: minimap, affix tiers and loot filter files.

## Minimap and overlay map (Display page)

- **Minimap size** 0.5–2.5×, with its frame and buff icons. The quest tracker
  below it moves along, so a larger minimap does not cover it.
- **Minimap zoom** 0.25–4×, **Overlay map zoom** 0.25–4×.
- **Minimap opacity** fades the minimap with its frame and icons; **Overlay map
  opacity** goes beyond the game's slider range. 0 keeps the game's own value.
- Session only: nothing is written to the game's settings, the values follow you
  into new areas, and closing EpochPact or **Reset** returns the game's values.

## Affix tiers

- **Affix tiers on item icons**: each affix tier as coloured digits on inventory,
  stash and equipment icons, highest first (for example `7 6 5 4`).
- **Tier tags in tooltips**: item tooltips show a coloured `T7` in front of each
  affix.
- **Tier colours**: T1 white, T2 blue, T3 yellow, T4 orange, T5 purple, T6 green,
  T7 red, T8 light blue by default; every colour can be changed and is remembered.
- Display only; items are never changed.

## Loot filter files (Loot page)

- List your loot filters with their rule counts, **Export** one to share,
  **Import** a shared filter or an EpochPact backup ZIP, **Duplicate** a filter
  to experiment and **Back up all** to a ZIP.
- Imports and copies are always added as new filters; existing filters are never
  changed or deleted. The game lists an imported filter within seconds, even
  while you play. Last Epoch keeps one filter folder for all characters and
  seasons, so your filters already carry over to a new season.

## Updating

Close Last Epoch and EpochPact. Extract the **complete new ZIP**, open
`EpochPact/EpochPact.exe`, and select **Game setup → Reinstall / update mod**.

[Measured tests and limitations](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.18/docs/feedback-group3-validation-2026-10-12.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
