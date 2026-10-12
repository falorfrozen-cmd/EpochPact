# EpochPact 0.1.2-dev.13

## Package layout clarification

The current download is **EpochPact-0.1.2-dev.13-package.1.zip**. `CopyToGame`
has been renamed to **Manual Installation** and README now explains both methods.
For the recommended setup, open **EpochPact/EpochPact.exe → Game setup → Install mod**;
ignore the manual folder. Manual copying is an alternative, not an extra step.
This packaging revision contains the identical tested EXE, DLLs and app resources;
no reinstall is needed if dev.13 is already working.

## Wanted T7 affixes

- Two lists: **Available affixes** and **Wanted affixes**.
- Double-click or press Enter to add/remove an affix. Add, Remove and Clear buttons are also available.
- Search by name or ID; sort A–Z or Z–A. Filtering does not discard your chosen affixes.
- Up to 64 choices. An empty wanted list accepts any T7 affix.
- Click **Apply selection** to update the pickup rule. Failed requests keep your selection; character changes clear the previous character's selection.

## Craft affixes up to T7

Open **Crafting → Forge Settings → Affix upgrade limit**, enable **Craft affixes up to T7**, then use **Apply changes**.

Existing T5/T6 affixes can be upgraded with shards to T6/T7. The item gains Exalted rarity and real higher-tier values. The setting is off by default, resets with normal crafting defaults, and stops at T7. Shards, Forging Potential and level requirements still apply, subject to your other crafting settings and native critical successes.

For extended T6/T7 upgrades, use plain shards or **Basic Glyph of Hope**. Other glyphs keep the game's usual tier limits. Ordinary T1–T5 crafting remains on the game's original path.

The update also keeps startup working when Windows supplies an invalid redirected console handle.

## Updating

Close Last Epoch and EpochPact. Extract the **complete new ZIP**, open `EpochPact/EpochPact.exe`, and select **Game setup → Reinstall / update mod**. Keep `EpochPact.exe.config` and `_internal` beside the EXE. Use the EXE and DLLs from the same package.

[Measured tests and limitations](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.13/docs/t7-affixes-validation-2026-10-12.md).
Earlier intermittent full-game shutdown crashes remain unresolved; this task's measured game closes returned exit code zero. Local Defender checks are not clearance by every antivirus engine or Nexus.

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
