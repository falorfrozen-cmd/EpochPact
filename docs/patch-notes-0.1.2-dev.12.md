# EpochPact 0.1.2-dev.12

Inventory shortcuts now sit in one horizontal row **after Sort, inside the
inventory footer**. They no longer form a floating strip over Skills, Passives
or the game world, and they reserve the visible currency counters' actual space.

- Placement follows the native inventory layout and UI scaling. A compact row
  is used when needed; if it cannot fit readably, the shortcuts stay hidden and
  the app's Inventory page remains available.
- **Stash** and **Vendor** toggle their respective windows. **Stash all** remains
  a transfer action. Closing Inventory or switching to Blessings hides the row.
- Shortcut labels remain Stash / Stash all / Vendor; the game's blessing label
  cannot rename the owned buttons.
- The existing connection, Crafting and performance fixes are retained.

Close Last Epoch and EpochPact, extract the **complete new ZIP**, run the new
`EpochPact/EpochPact.exe`, and use **Game setup → Reinstall / update mod**.
The EXE and native DLLs must come from the same package. Keep
`EpochPact.exe.config` and `_internal` beside the EXE.

[Actual inventory with Skills open](ui-evidence/inventory-footer-dev12.png).
[Validation and limitations](inventory-footer-validation-2026-10-12.md).

Known issue: intermittent full-game shutdown `0xC0000005` in UnityPlayer.dll
remains unresolved and occurred again during this release's final shutdown
test. The EpochPact app itself closed normally, reset XP from x2 to x1, and
left the game running. These are separate results. A local Defender scan
finding no threats does not establish clearance by all antivirus engines or Nexus.

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) ·
[Support](https://discord.gg/q6aexZZSAf).
