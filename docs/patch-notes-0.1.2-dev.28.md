# EpochPact 0.1.2-dev.28

Choose what the loot window shows, see what an affix shard does, split the
damage meter by damage type, and read loot filter rules as they are written.

## Loot window filters

- **⚙ → Show** in the loot window: switch each kind of pickup on or off —
  **Items** by rarity (Normal, Magic, Rare, Exalted, Unique, Set, Legendary),
  **Resources** (Gold, Memory Amber, Soul Ember, XP motes) and **Materials**
  (Affix shards, Runes, Glyphs, Other materials), with **All** and **None**.
  Your earlier Gold/Materials choices carry over.

## Affix shard details

- **Point at an affix shard** in the loot window or in **Loot & Pickup →
  Recent pickups** to see what its affix does: prefix or suffix, level, the item
  types it rolls on and the game's own values for each tier (T1–T8). The values
  are those of the first item type it rolls on; some types, such as two-handed
  weapons, have larger values.

## Damage meter by damage type

- **Damage type** on the Summons page and in the damage window's ⚙: Physical,
  Fire, Cold, Lightning, Necrotic, Void or Poison only, for every row and column.
  Each hit is split by the damage per type the game gives it, so the parts add
  up to the hit's damage.
- **Rows:** show or hide each ally type, the allies' total and your character.
- **Copy as CSV** adds one total column per damage type.

## Loot filter rule in tooltips

- **The rule is written exactly as in your filter** — no forced capitals or
  small capitals.
- **Display → Tier colours → Rule line colour:** keep the colour your filter
  gives the item's label (as before), or choose one colour for every rule line.

## Translations

- The new texts are translated into Türkçe and Русский.

## Updating

With 0.1.2-dev.22 or later: **New version · What's new → Install update** in
the app. From an older version: close Last Epoch and EpochPact, extract the
**complete new ZIP**, open `EpochPact/EpochPact.exe`, and select **Game setup →
Reinstall / update mod**. Shard details, damage types and the rule line need the
updated mod: Install update reinstalls it once the game is closed.

Tests: [filters](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.28/docs/filters-validation-2026-10-14.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
