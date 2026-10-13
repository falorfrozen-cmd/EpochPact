# EpochPact 0.1.2-dev.25

Five blessing choices after a boss; smoother chests, shrines, portals and
graphics switches.

## World & Exploration

- **Chests and shrines open right after you arrive.** Each zone loads a few
  hundred interaction handlers; they were sorted one at a time, so nearby chests
  and shrines were ignored for about 13 seconds after every zone change. They
  are now ready within a second.
- **No repeated pulls at one object.** A chest or shrine that the game still
  offers after it was triggered was triggered again every 2 seconds — a shrine
  activation alone takes the game about 70 ms. It is now retried after 2, 4 and
  8 seconds and then left alone.
- **No searches on zone changes.** New zones register their chests, shrines,
  portals and cave entrances while they load; the mod no longer searches the
  whole scene six times after each zone change (about 25 ms of a frame each).

## Blessings

- **Blessing choices 4 or 5 now appear after a Monolith boss.** The mod made the
  game roll more blessings, but the reward kept filling and showing only its own
  three (plus corruption extras). The option slots and the reward panel now use
  the chosen count too.
- Choosing **3** or **4** no longer drops the extra choices corruption gives; the
  reward used to ask for blessings that had not been rolled.

## Display & Camera — graphics effects

- Bloom, motion blur, ambient occlusion, reflections and volumetric fog are
  applied from the game's own lists of effects once a second (about 0.1 ms)
  instead of searching the scene up to nine times after every zone change.
  Reflection probes are found with one quick search per zone. The shadow
  setting is only written when it differs.

## Updating

With 0.1.2-dev.22 or later: **New version · What's new → Install update** in
the app. From an older version: close Last Epoch and EpochPact, extract the
**complete new ZIP**, open `EpochPact/EpochPact.exe`, and select **Game setup →
Reinstall / update mod**.

Measured tests: [performance](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.25/docs/performance-validation-2026-10-13.md),
[blessings and trees](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.25/docs/blessings-trees-validation-2026-10-13.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
