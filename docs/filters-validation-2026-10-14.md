# Loot feed filters, affix shard details, damage by type, rule line (2026-10-14)

Player requests (AL4NoSTb, items 6–8): loot feed filters (rarity, gold, amber,
resources); affix shard descriptions on hover; damage meter row and element
filters (per minion, per damage type). Owner's addition for the loot filter
rule line in tooltips: the rule exactly as written (no forced capitals or
small capitals), the rule's own colours, and a colour the player chooses.

## Loot feed filters (item 6)

- Each pickup has one category: equipment by rarity (Normal, Magic, Rare,
  Exalted, Unique, Set, Legendary), Gold, Memory Amber, Soul Ember, XP motes,
  and crafting materials by the game's item type — 101 affix shards, 102 runes,
  103 glyphs, anything else "Other materials" (`ui/loot-log.js`). The native
  loot log now reports each item's type and subtype (`items.cpp`).
- The loot window's ⚙ has one switch per category plus All/None; the old Gold
  and Materials switches carry over (Materials off hides shards, runes, glyphs
  and other materials). With every category off the window says so instead of
  "Waiting for pickups".
- Live (isolated EpFeedbackTest clone, test core): items of rarity 0/1/2/7,
  two shards from the game's own `GenerateItems.SpawnShard`, gold and an amber
  pile picked up by Auto Pickup; the log reported `item` type 0 (Normal, Magic,
  Magic, Unique "Fractured Crown" subtype 22), `material` type 101 subtypes 501
  "Strength" and 74 "Less Damage Taken on Block", and gold type −1. (The amber
  pile was not collected within the 4 s wait of that step; amber entries are
  unchanged from earlier releases.)

## Affix shard details (item 7)

- `affixinfo <id>` (read only, control `affix_info`, id 0–65535 checked before
  IPC): name, Prefix/Suffix/Special, level, the item types it rolls on
  (`ItemList.GetBaseTypeDisplayName`) and, per tier, the game's own value text
  for the lowest and highest roll (`ItemData.GetDisplayValuesOfAffixAtTier` on
  a bare item of the first type; both values for two-property affixes).
- Live: id 0 "Void Penetration" Prefix 42 on Amulet, T1 4% … T8 24%–30%;
  id 1156 "Increased Mana and Mana Regen" on Helmet/Body Armor, T1 3% / 10%–13%
  … T8 30%–35% / 75%–90%; the two picked-up shards (501, 74) read back with all
  8 tiers. 65000 → "affix is not in this game's catalog"; no id or text → usage.
- The loot window and the Recent pickups table read each shard's affix once
  (at most three tries for a failed read) and show it when pointing at the
  line: "Strength · Prefix / Rolls on: Helmet, Body Armor, … / T1 1 … T8 24 – 28
  / (Values for Helmet)". Names are escaped.

## Damage by type and row filters (item 8)

- The `ApplyDamage` detour reads the hit's damage per type (`DamageStats.damage`,
  the game's DamageType order physical, fire, cold, lightning, necrotic, void,
  poison) before the game applies it and splits the applied damage in that
  proportion (`convenience_rules.hpp` `SplitDamage`, tested: sums, NaN/∞/negative
  parts, no parts, huge values). Each ally type keeps totals, hits, biggest hit
  and one bucket per second per type; the session read adds `elements` per row
  and for the allies' total, plus `unsplitHits` (hits without any type).
- Live, run 4: the test character's Lightning Blast landed 6 hits, 199.2 damage,
  all in Lightning; the per-type totals and DPS (2.8 over 60 s) equal the row's;
  `unsplitHits` 0. Runs 1–3 had no combat (enemies next to the character never
  attacked and Ballista orders did not hit: `ApplyDamage` was not called at all),
  so a test-only `player-attack-<n>` fixture casts the character's own skills
  at the nearest enemy. Final run with the final core: 23 hits, 1431.8 damage —
  Fire 625.0 (7 hits, Fireball) and Lightning 806.9 (16 hits); the types add up
  to the row (1431.9 with the one-decimal output), DPS over 60 s 23.5 both
  ways, `unsplitHits` 0.
- **Damage type** (Summons page and damage window ⚙) shows one type in every
  column, ordered by that type, with averages from the session and combat time;
  **Rows** hides or shows each ally type, "Allies total" and "You (character)".
  The page and the window remember their own choices; changing them sends
  nothing to the game. The CSV adds one total column per type.

## Loot filter rule line

- Font: the line was a copy of the item type line, whose font
  (`LastEpochAlt-SemiboldB SDF`, style LowerCase) and the affix lines' font
  (`LastEpoch-Regular SDF`) both draw lowercase letters as small capitals — the
  first fix (affix font) still showed small capitals in the screenshot. The line
  now uses the font of the footer's "Requires: Level" line (`Caladea SDF`, real
  lowercase) and clears the LowerCase/UpperCase/SmallCaps styles: "Hide all
  items with 50 Affixes" is shown as written.
- Colour: by default as before — the colour the filter gives the item's label
  (the rule's colour preset when it recolours, otherwise the item's colour,
  grey for Hide). New **Rule line colour** (Display page): "Like the item's
  label on the ground" or "One colour for every rule" with a colour picker
  (`tiercolor rule <RRGGBB>` / `tiercolor rule label`, checked in the bridge and
  natively). Live: E0315A applied to the open tooltip at once, `XYZ` refused,
  back to label grey 8C8C8C; 0 faults, 0 tooltip mismatches. A tooltip that is
  open while the colour changes is redrawn by the game's own
  `TooltipItemManager.ForceUpdateTooltip` (as for the tier colours since
  dev.20): in two runs at once, in the final run the open tooltip kept the old
  colour (the setting itself read back E0315A); the next tooltip uses it.
- Final run with the final core: the line's font is Caladea SDF, style 0, next
  to the type line's LastEpochAlt-SemiboldB SDF (style 8) and the affix lines'
  LastEpoch-Regular SDF.

## Checks

- Native: 20 test programs pass (convenience rules now include the damage split).
- JavaScript: 196 tests (new: categories and migration, shard text, damage type
  and rows, the loot and damage windows on a fake page).
- Python: 324 tests (new: `affix_info` id bounds, rule line colour values).
- Translations: 1537 texts, Turkish and Russian complete, 0 placeholder
  mismatches.
- Compiled package and extracted ZIP: 24/24 and 4/4; Windows Defender found no
  threats in the EXE, both DLLs and the ZIP (hashes in
  `docs/security/virustotal-20261014-dev28.json`).
- Live tests restored the 54 save and runtime files byte for byte each time.
