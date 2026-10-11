# EpochPact 0.1.2-dev.9

## Fixes

- Inventory shortcuts now display Stash, Stash all and Vendor instead of Confirm new blessing.
- Shortcuts sit beside the inventory and avoid its resource bar at different UI scales. They hide when there is no safe space.
- Stash and Vendor now toggle their windows.
- Nearby chest/shrine/portal automation uses cached objects and bounded work instead of repeated whole-scene scans. It pauses while you inspect items or use inventory windows.
- The Windows folder package includes the CLR configuration needed when downloaded managed DLLs carry an Internet mark.
- Custom gear templates use the correct existing backend command.

## Additions

- Free respec actions for skills, passives and the Weaver tree, with recovery backups.
- Actual per-summon combat DPS, last damage and a saved damage baseline for comparison.
- Choose which summon stats/columns to display; hide unwanted character stats.
- Favorite or hide navigation sections and restore them from Settings.
- Copyable in-game stash search examples.

Extract the complete ZIP and keep EpochPact.exe.config and _internal beside EpochPact.exe. Update with the game closed; use Game setup to install the matching native files. Offline only. No separate Python installation is required.

Actual DPS needs combat against a target; it cannot predict exact damage from an idle gear swap. This pass does not certify every portal or summon family. Intermittent full-game exit 0xC0000005 also reproduced in a vanilla comparison after an automated window query. See [the separate validation report](player-feedback-validation-2026-10-12.md) for scope and limitations.

Source/releases: https://github.com/falorfrozen-cmd/EpochPact

Support: https://discord.gg/q6aexZZSAf
