# EpochPact 0.1.2-dev.20

Player feedback on the loot feed and loot filter information.

## Loot filter rule in item tooltips

- New **Loot filter rule in tooltips** (Display → Item tiers): every item
  tooltip — inventory, stash or ground — shows which rule of your active loot
  filter matches it, under the item type: rule number, Show / Hide / Highlight
  and the rule's name (or the game's description of it). Useful while building
  a filter.

## Loot feed and loot list

- Identical resources picked up one after another are **one line**: gold piles
  are added up ("+707 Gold (12 pickups)"), the same shard or material is
  counted ("Shard of Health ×3"). Different resources and every piece of
  equipment keep their own line.
- The loot window shows the gold collected in total.
- Rule information is no longer shortened and says whether the rule shows,
  hides or highlights the item; items show their affix tiers (for example
  `T7 T6 T6`), so you can see why a rule matched.

## Loot and damage windows follow the game

- The loot window and the damage window stay above the game only. When you
  switch to EpochPact or another program they go behind it, and while the game
  is minimised they are hidden. Without a running game they are ordinary
  windows.

## Updating

Close Last Epoch and EpochPact. Extract the **complete new ZIP**, open
`EpochPact/EpochPact.exe`, and select **Game setup → Reinstall / update mod**.

[Measured tests](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.20/docs/feedback-group5-validation-2026-10-13.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
