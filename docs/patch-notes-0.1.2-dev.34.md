# EpochPact 0.1.2-dev.34

Your requests: Sell all, automatic sorting, every font style, more loot filter
colours, coloured rule names, settings that stay, the game launching with
EpochPact. Also new: a death recap and Season 5 (Rage of Morditas) settings.

## Inventory

- **Sell all**, a fourth button in the game inventory (Stash, Stash all, Vendor,
  Sell all): sells every inventory item the vendor buys, with the game's own sale.
  It asks once more: the first press shows **Confirm sell** for four seconds, the
  second sells. The vendor opens by itself when it is not open. Equipped items are
  never sold, sold items are in the vendor's buyback list, and a recovery backup is
  made first. **Sell all keeps** (Inventory page): Unique, Set and Legendary
  (default), also Exalted, or nothing.
- **Automatic sorting** (Inventory page) with the game's own Sort: your inventory
  each time it opens, and every stash tab or only the tabs you tick, each time the
  tab is shown.

## Fonts and text

- **Every font style**: each Windows font is listed with all its styles (Light,
  Semibold, Bold, Black, Italic, Bold Italic …).
- **Search fonts** narrows the long list as you type.
- **Letter case as written** (Display → Text size and letter case): names and texts
  keep the capitals, endings and punctuation you typed, instead of the game's
  forced capitals or small capitals. Choose a Windows font for real lower-case
  letters (the game's own font draws them as small capitals).

## Loot filter

- **More loot filter colours** (Loot & Pickup → Loot filter window): 26 more label
  and beam colours in the game's own colour picker (44 instead of 18). Where
  EpochPact does not run, a rule with one of them shows the game's last colour.
  Stash tab colours stay as they are: they are stored in your character save.
- **Colours in loot filter rule names**: write `<color=#FF4040>word</color>`,
  `<color=yellow>…</color>`, `<b>`, `<i>`, `<u>` or `<s>` in a rule's name; the
  game's filter window and the rule line in tooltips show them.
- **Rule line shows** (Display → Item tiers): choose what the rule line in
  tooltips shows (everything, number and name, name only …). Rules without a name
  of their own no longer start with "Show All" or "Hide All".

## Item tiers

- Default tier colours follow the game's rarity order: T1 white, T2 blue, T3
  yellow, T4 purple, T5 orange, T6 green, T7 red (T8 cyan).
- **Mark Personal, Champion and Experimental affixes**: a small coloured square
  before those affix lines, as the game marks corrupted ones.
- When comparing items, the compared item's affixes are coloured too.

## Damage taken and death recap

- **Dealt / Taken** in the damage window (and **Damage taken** on the Summons
  page): the damage your character takes during the session, by attacker and
  damage type.
- **Death recap** (**☠** in the damage window, and on the Summons page): for each
  of your last ten deaths, who dealt the killing blow and with what, the damage of
  the 10 seconds before it by type and attacker, the last hits with the health and
  ward left after each (damage over time grouped, "Ignite DoT ×23"), and the buffs
  and debuffs you had.

## Season 5: Rage of Morditas

On the World page: how often the crystal appears, the hunt time, the kills for
Bloodrage charges and hunt buffs, and the Blood share of Forging Potential
rewards. Only the values the game loaded are changed, while you play offline;
nothing is saved to your character.

## App

- **Settings stay as you left them**: every change is remembered at once and
  sent again when you load a character, also after going back into the game
  without closing EpochPact. One message says how many were applied.
- **Launch the game when EpochPact starts** (Game setup).
- The loot feed lists every shard, rune and glyph the game picks up along with
  the one you took, and shows new pickups right away after the game restarts.

## Translations

- The new texts are translated into Türkçe and Русский.

## Updating

With 0.1.2-dev.22 or later: **New version · What's new → Install update** in
the app. From an older version: close Last Epoch and EpochPact, extract the
**complete new ZIP**, open `EpochPact/EpochPact.exe`, and select **Game setup →
Reinstall / update mod**. The new features need the updated mod: Install update
reinstalls it once the game is closed.

Tests: [your requests, death recap and Season 5](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.34/docs/player-batch-2026-10-17.md),
[loot feed](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.34/docs/pickup-feedback-2026-10-17.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
