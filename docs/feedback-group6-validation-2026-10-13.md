# Player feedback — 0.1.2-dev.21 validation (2026-10-13)

Steam Last Epoch 1.5.2.1 (internal 1.5.2.2), offline.

| Request | Change |
|---|---|
| Not every affix line takes its tier colour (experimental, personal, champion, corrupted, exalted) | The game writes its own colour tag inside those lines (`EpochColorManager.GetColourHexForAffix`), which won over our outer tag. `Compose` now colours `text::StripColorTags(original)` (`native/core/item_tiers_text.hpp`): `<color=…>`, `</color>` and `<#RRGGBB>` are removed, every other tag stays; the original text is still kept for restoring. |
| "In-game stash search" shown twice | Rendered on the Stash Assistant page only. |
| Rule line in the loot filter's colours | `ItemFilter.Match` returns the rule's colour preset (`Nullable<int>`, read in `smartloot::RuleFor`); the line uses `EpochColorManager.GetLootfilterRuleColor(preset, rarity).color`, else `GetEpochColor(item).color`; Hide rules grey `8C8C8C`. |
| Rule line position | `tierset rulepos 0|1|2`: one line per place, cloned from `Type`. Top: header group after `Type`. Middle: body group index 0 (before the affixes). Bottom: body group after `Lore` (above the footer). Body lines copy the lore line's width and the affix text's left/right margin. |
| Only the rule name | `tierset ruleplain 0|1`. |

## Real game (isolated `EpFeedbackTest` clone, test core)

Three sessions; saves restored and byte-verified after each (54 files);
player core reinstalled.

- Affix lines as the game wrote them, then with **Colour affix lines by tier**:
  experimental `<color=#ABABFFFF><size=100%>+2 Vitality` → `T2` blue line;
  personal `<color=#E3DE98FF>…+2 Health Regen per Second` → `T1` white line;
  exalted `T7 +15 Vitality` → red-pink line; sealed corrupted line
  `+21% Fire Resistance` → `T3` gold line with the corruption glow kept.
  0 faults, 0 tooltip mismatches.
- Spellblade filter. Ground label of a helmet with affix 598 (rule "SHOW
  Helmet with Level of Enchant Weapon", recolour preset 7): label colour
  `FF002F`, rule 4. Tooltip line for the same rule: "Rule 4 · Show · SHOW
  Helmet with Level of Enchant Weapon" in `FF002F`. Affix 87 (rule 1, Hide):
  grey. Unique and rare items without a matching rule: the item's own colour
  (`EB730A`, `C184FF`).
- Positions measured in tooltip coordinates: Top between `Type` and the
  implicits; Middle above the first prefix line (rare) / below the implicits
  (unique); Bottom between `Lore` and the footer. Body lines width 447.6 and
  margin 32.4/19.2, the same indent as the affix text. Rule name only:
  "Hide all items with 50 Affixes".
- Switching the settings off restores the game's lines and hides every rule line.

## Automated

Native 18/18 programs (new: `item_tiers_text_test`, 8 checks with the lines
measured above), Python 265, JavaScript 155 (new: Stash Assistant only; rule
position choices and automatic settings). Package: player checks 21/21,
connection checks 4/4; Windows Defender found no threats in the ZIP, EXE, core
and loader. VirusTotal was not run (browser connection unavailable); hashes in
`docs/security/virustotal-20261013-dev21.json`.
