# EpochPact 0.1.2-dev.15

## Fewer stutters when changing settings

Turning a feature on or off briefly paused the game while EpochPact installed or
removed its hook. Each pause took about a quarter of a second, because the
engine listed every thread on the whole PC (over 10,000 here) twice. It now
lists only the game's own threads (about 0.3 ms instead of about 105 ms per list).
This affects every switch that installs a hook: XP, gold, drops, crafting,
nearby chests/shrines/portals, graphics, summons and more.

Campaign completion now installs its two batching hooks while the game starts,
instead of during the operation.

## Graphics switches no longer search the scene every second

With bloom, fog, ambient occlusion, motion blur or reflections disabled,
EpochPact searched the whole scene once per second for effects to switch off.
It now searches when you change a switch and three times after a new area loads,
then stops. Effects the game re-enables are still caught by the existing guard.

## Uninstall from the app

**Game setup → Uninstall mod** removes EpochPact's own loader (or MelonLoader
bridge) and core from the game folder. Another loader and its mods, your saves,
logs and recovery backups are kept. The game must be closed.

## Smaller fixes

- **Stash all** now says when items stayed in the inventory because the stash had no room.
- A **Support · Discord** link is in the sidebar and in Game setup.
- README and documentation now describe the current inventory button row (below
  the item grid, above Transfer/Sort and currency). Older text still described
  the previous layouts.
- Auto Pickup scan interval is documented correctly (0.25 s, paused while an item card is open).

## Updating

Close Last Epoch and EpochPact. Extract the **complete new ZIP**, open
`EpochPact/EpochPact.exe`, and select **Game setup → Reinstall / update mod**.
Use the EXE and DLLs from the same package.

[Measured tests and limitations](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.15/docs/audit-fixes-validation-2026-10-12.md).
Earlier intermittent full-game shutdown crashes remain under investigation.

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
