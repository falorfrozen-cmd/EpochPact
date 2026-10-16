# EpochPact 0.1.2-dev.33

A new damage window: four styles, the game's own icons and names, and a damage
meter that now counts every ally, including the ones that die fast.

## Damage window

- **Styles** (⚙ → Style):
  - **Quiet HUD** (default): the selected source's DPS and its seven damage
    types.
  - **Pact Bars**: every source as a ranked bar, with the selected source's
    damage types below.
  - **Split Meter**: your damage types on one side, your allies and their damage
    types on the other.
  - **Table**: the full table, as before.
- **Averaging window** in the top bar: 10 s, 60 s, 10 min, 60 min, Session (the
  whole session) or Combat (only the seconds in which you dealt damage).
- **TOTAL DPS** is always shown at the bottom: your damage plus every ally's.
- **▶ / Ⅱ** starts and stops the session, **↺** resets it.
- Click a source to see its damage types.
- **The game's own icons**: your class and mastery icon, and for each ally the
  icon of the skill that summoned it.
- **The game's own names**: allies show the name on their health bar ("Skeleton
  Warrior", "Skeleton Mage", "Primal Wolf", "Falco"), so you can tell which
  skeleton is which. Rows you hid and your window options are kept.
- **Numbers**: every digit below a million (472,930), three significant digits
  above (2.90M, 17.9M, 1.25B), with a decimal comma in Türkçe and Русский.

## Damage meter

- **Allies that die fast are counted in full.** Ignite, bleed, poison and other
  damage over time from an ally that already died (a Volatile Zombie that
  exploded) now counts for that ally; before, it was lost (Volatile Zombies
  showed about 38 % too little). An ally that hits within its first second is
  counted too.
- **live** shows only the allies of a type that are still alive.

Measured in the game: every hit compared with the health the enemy actually
lost, for all five classes, all seven damage types, damage over time, minions,
companions, the Falconer's falcon and the Ballista. Every difference was within
the rounding of the game's health numbers (under 0.001 %) and no hit was missed.

## Fixes

- A page could stop responding in Türkçe when a text's translation was the same
  as its English text (such as "Void").

## Translations

- The new texts are translated into Türkçe and Русский.

## Updating

With 0.1.2-dev.22 or later: **New version · What's new → Install update** in
the app. From an older version: close Last Epoch and EpochPact, extract the
**complete new ZIP**, open `EpochPact/EpochPact.exe`, and select **Game setup →
Reinstall / update mod**. The new damage window needs the updated mod: Install
update reinstalls it once the game is closed.

Tests: [damage window and meter](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.33/docs/dps-meter-integration-2026-10-15.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
