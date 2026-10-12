# Player feedback — 0.1.2-dev.20 validation (2026-10-13)

Steam Last Epoch 1.5.2.1 (internal 1.5.2.2), offline.

| Request | Change |
|---|---|
| Rule name on hover | `tierset rules 0|1`: in the `UITooltipItem.UpdateAffixesText` post-detour, `ItemData.getAsUnpacked` → `ItemFilter.Match` (the existing `smartloot::RuleFor`) → a TMP line cloned from the tooltip's `Type` line and added to the header's own `SimpleLayoutGroup` (`AddElement`, `SetItemOrder` after `Type`). Hidden when no filter is active or the setting is off. |
| Rule details in the loot list | Loot entries carry `outcome` (Show/Hide/Highlight) and `tiers` (display tiers, highest first); the full rule name is shown. |
| Merge identical resources | `loot-log.js` groups consecutive gold, amber and same-name material entries (gold/amber summed, materials counted); equipment is never grouped; the loot window shows the gold total. |
| Windows bound to the game | `tools/overlay_follow.py`: every 250 ms — game or the small window in front → `HWND_TOPMOST`; another window in front → not topmost and placed directly below that window (never below a topmost window); game minimised → hidden; no game → ordinary window. |

## Real game (isolated `EpFeedbackTest` clone, test core)

Saves restored and byte-verified after every session; player core reinstalled.

- Spellblade filter, Magic Refuge Helmet: tooltip line "Loot filter: Rule 9 ·
  Hide · Hide all Normal/Magic items" directly under "Helmet"; another item
  "Rule 7 · Hide · Hide 8 Types"; Rare items "no rule matched (shown by
  default)". The first version of the line (a sibling without the layout
  group's element list) was drawn above the item name; fixed before release.
- Loot log entries: Magic helmets `ruleNumber 9, outcome 1 (Hide), tiers "2"`
  and `"1 1"`; Rare `tiers "2 2 2"`, `"2 2 1 1"`; gold entries without tiers.
  0 faults.

## Windows (real Win32, three throwaway windows)

Game in front → feed topmost and above the game; another window in front → feed
not topmost and below it; game minimised → feed hidden; restored → visible and
topmost again (7/7). The foreground was set by the test (a background process
may not take the foreground); every window change used the real API.

## Automated

Native 17/17 programs, Python 265 (new: overlay follower 4), JavaScript 154
(new: grouping, full rule labels).
