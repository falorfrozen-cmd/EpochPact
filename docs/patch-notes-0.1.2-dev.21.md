# EpochPact 0.1.2-dev.21

Player feedback on tier colours and the loot filter rule line.

## Fixes

- **Tier colours now reach every affix line.** Exalted (T6–T7), experimental,
  personal (including champion), corrupted and similar affixes kept the
  game's own colour because the game writes a colour tag inside those lines,
  which won over ours. With **Colour affix lines by tier** on, the whole line
  now takes its tier colour; icons and the corrupted glow stay. Switching it
  off restores the game's original text exactly.
- The **In-game stash search** examples are shown once, on the Stash
  Assistant page, instead of on both Inventory and Stash Assistant.

## Loot filter rule in tooltips

- The rule line is drawn in the **colour your loot filter gives the item's
  label**: the rule's own colour when it recolours, otherwise the item's
  colour. Hide rules are grey (hidden items have no label on the ground).
- New **Rule line position** (Display → Item tiers): Top (under the item
  type, as before), Middle (before the affixes) or Bottom (at the end of the
  tooltip, below the flavour text).
- New **Rule name only**: hides "Loot filter:", the rule number and
  Show/Hide/Highlight, so only the rule's name remains.

## Updating

Close Last Epoch and EpochPact. Extract the **complete new ZIP**, open
`EpochPact/EpochPact.exe`, and select **Game setup → Reinstall / update mod**.

[Measured tests](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.21/docs/feedback-group6-validation-2026-10-13.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
