# EpochPact 0.1.2-dev.23

Buffs and debuffs in the game, and a Memory Amber multiplier.

## Buffs & debuffs in game

- New **In-game effect HUD** (Display & Camera → Buffs & debuffs in game):
  panels inside Last Epoch, no Alt-Tab and no extra window. **On you** lists
  your buffs and debuffs; the target panel lists the ailments on the enemy
  whose health bar the game shows, and clears when you lose that target.
- Each row has the game's own icon, name, a description of up to two lines,
  the stack count and the time left with a bar. `—` means the game exposes no
  fixed timer for that effect (not infinite). Harmful effects are listed first;
  extra effects are counted as "+N more active effects".
- Settings: show effects on you / on the target, descriptions on or off, size
  0.7–1.5 and up to 12 rows. The panels let clicks pass through and stay on
  through area changes. Off by default; offline only.

## Memory Amber multiplier

- New **Memory Amber gain multiplier** with the general multipliers (×1–×100).
  It multiplies the Memory Amber a member of The Woven picks up; the
  reputation that amber gives rises with it. Characters that are not in The
  Woven gain no amber, so the multiplier does nothing for them.

## Updating

With 0.1.2-dev.22: **New version · What's new → Install update** in the app.
From an older version: close Last Epoch and EpochPact, extract the **complete
new ZIP**, open `EpochPact/EpochPact.exe`, and select **Game setup → Reinstall
/ update mod**.

[Measured tests](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.23/docs/hud-amber-validation-2026-10-13.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
