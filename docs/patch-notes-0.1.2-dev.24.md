# EpochPact 0.1.2-dev.24

Passive tree allocation fix.

## Passive trees

- **Tree connections → Passives works on every node again.** Nodes could take
  one point and then refuse the rest, or take none at all — mostly in a mastery
  you did not choose (for example the Warlock tree on a Necromancer), and on
  any class. Passive nodes now take points up to their own maximum, past
  mastery thresholds, in chosen and not-chosen masteries, as long as you have
  unspent points.
- The cause: after the passive panel had shown a node, the bypass faulted on
  that node and quietly fell back to the game's normal rules.

## Updating

With 0.1.2-dev.22 or later: **New version · What's new → Install update** in
the app. From an older version: close Last Epoch and EpochPact, extract the
**complete new ZIP**, open `EpochPact/EpochPact.exe`, and select **Game setup →
Reinstall / update mod**.

[Measured tests](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.24/docs/passive-tree-validation-2026-10-13.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
