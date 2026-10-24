# Craft affixes up to T7: choosing another affix in the forge (0.1.2-dev.41)

## Report

"Clicking the option to upgrade them all 1 at a time only lets me upgrade 1 affix to t6/t7 and
then the game won't let me even select a other affix."

## Cause

The forge window draws each affix row with `AffixSlotForge.SetVisuals`, which hides the row's
upgrade button (the arrow, `CraftingUpgradeButton`) when the affix is at T5 or above. That is a
fixed tier check in the window code, not the max-tier query EpochPact already extends
(`ItemData.AffixIsAtMaxCraftableTierOrAbove`). So with "Craft affixes up to T7" on, the shard that
was in the modifier slot could be forged past T5, but no other T5/T6 affix showed an arrow, and
the arrow is how the forge selects an existing affix. Clicking the row itself
(`AffixSlotForge.SlotClicked`) never selects a shard; it is for adding an affix to an empty row.

Reproduced on the disposable EpCraftTest clone (test build, commands only): two T5 affixes A
(selected) and B; B's row had no arrow before A was forged and none after A reached T7, and B
could not be chosen. Forcing the game's own `CraftingManager.PopShardToModifierSlot(B)` selected B
and forged it T5 → T6, so only the window's button was missing.

## Change

`native/core/crafting.cpp`: with "Craft affixes up to T7" on, a hook after
`AffixSlotForge.SetVisuals` gives a seven-tier affix at T5 or T6 its row's arrow the way the game
gives one below T5: shards of that affix in stock, Forging Potential left, not the shard already
selected, no glyph or Basic Glyph of Hope (the same rule as the T6/T7 forge itself). The arrow is
the game's own button (`CraftingUpgradeButton.UpdateButton`), and clicking it selects the shard
with the game's own `PopShardToModifierSlot`. Switching the setting on or off redraws an open
forge window. Without the setting the hook is not installed.

## Check (test build, EpCraftTest clone, commands only)

Evidence: `C:\Users\falor\EpochPactLab\builds\t7-row-live` and `t7-row-click`.

| Step | Result |
|---|---|
| A (T5) selected | B's row shows the arrow; A's does not (selected) |
| B's arrow | B selected; A's row shows the arrow |
| A's arrow, forge twice | A T5 → T6 → T7; B's row keeps its arrow |
| B's arrow after A reached T7 | B selected; forge twice: B T5 → T6 → T7 |
| Both at T7 | no arrows; the forge refuses with the game's own max-tier message |
| T7 switched off with a new T5 pair | no arrows above T5, the game's normal limit |

The session closed with exit code 0 and the saves and real preferences were restored.
