# EpochPact 0.1.2-dev.16

Player suggestions, first group.

## Loot & Pickup

- **Auto pickup interval:** choose how often collection scans, from 0.25 s
  (default) up to 10 s. Longer intervals do less work per second.
- **Full inventory pauses equipment pickup.** When the game says your
  inventory is full, EpochPact stops retrying equipment every scan. Gold,
  potions, tomes, bones and crafting materials keep being collected. Equipment
  pickup resumes after you open and close your inventory.
- **Pick up everything now:** one button collects every drop the game has
  loaded, ignoring the radius. Your pickup rules, loot filter and inventory
  space still apply.

## Stash

- **Stash remembers the last tab** after a game restart. (Within one session
  the game already keeps it.)

## Summons

- **Live allies are grouped by type:** one row for all Skeletons, one for each
  golem type, and so on, with total damage and health. "Show each ally
  separately" brings back one row per ally.

## Settings

- **Apply at startup:** Settings → Draft profiles. Choose **My last used
  settings** or a saved profile; it is applied once each time an offline
  character is ready. Off by default. Quests, rewards and save actions are never
  applied automatically.

## Updating

Close Last Epoch and EpochPact. Extract the **complete new ZIP**, open
`EpochPact/EpochPact.exe`, and select **Game setup → Reinstall / update mod**.

[Measured tests and limitations](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.16/docs/feedback-group1-validation-2026-10-12.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
