# Player suggestions, group 3 — 0.1.2-dev.18 validation (2026-10-12)

Steam Last Epoch 1.5.2.1 (internal 1.5.2.2), offline. Suggestions 1–5 and 16.

## Changes

| # | Suggestion | Implementation |
|---|---|---|
| 1 | Minimap scale | `mapset minimapsize 0.5–2.5` scales the "Minimap" panel above the DMMap "Minimap" config rect (frame, mask, map and buff column together; pivot top-right). `QuestTrackerUI` moves down by the panel's extra height and returns on reset. |
| 2 | Minimap opacity | `mapset minimapopacity 0.05–1` sets a `CanvasGroup` alpha on that panel (added once if the panel has none; 0 = game look). The config's own `opacity` field had no visible effect and is left unchanged. |
| 3 | Overlay opacity / zoom | `mapset overlayopacity` sets the "Fullscreen" config opacity and calls `LE.UI.Minimap.Minimap.UpdateOverlayMapOpacity`; neutral is the player's own slider value (`DMMapGlobalConfigs.OpacityValue`). `mapset overlayzoom`/`minimapzoom` multiply the config zoom (300 / 30) and widen `minOverlayZoom`/`maxOverlayZoom` when needed; `DMMap.ReloadConfig` applies them. The game's `SetOverlay*` methods (which save settings) are not used. |
| 1–3 | New areas | The `Minimap.OnMapLoaded` detour (shared with map reveal, kept while either needs it) posts a re-apply for the next frame. |
| 4 | Affix tiers on items | Detours on `InventoryItemUI.UpdateContent` and `OnEnable`; a clone of the icon's own `quantityText` shows `ItemAffix.DisplayTier` values, highest first, each in its palette colour. |
| 5 | Tier colours | Tooltips: post-detour on `UITooltipItem.UpdateAffixesText(ItemData, ItemTooltipInfo)` adds `<color=#RRGGBB>Tn</color> ` to each active prefix/suffix/sealed line, paired in item order; a line-count mismatch leaves the tooltip unchanged. `UITooltipItemAffix.Init` is detoured so a pooled line drops our state before reuse. Palette `tiercolor <1-8> <RRGGBB>` / `reset`, remembered by the app (localStorage) and re-sent once per character. |
| 16 | Loot filter sharing | Desktop app only (`tools/filter_manager.py`): list, export, import (`.xml` or EpochPact backup `.zip`), duplicate, back up all. New files only (`open(..., 'xb')`), unique names, `<name>` element renamed to match; strict parse (no DTD/entities, `ItemFilter` root with `name` and `rules`, 2 MB per file, whole ZIP validated before any write). |

## Found during validation

- The tooltip shown by this build is `UITooltipItem`; `LE.UI.Tooltips.ItemTooltipAffix.OnDataUpdate` never ran (0 updates in a native tooltip). Moved to `UpdateAffixesText`; the older hook is kept only where the build has it.
- `Graphic.color` on the affix text does not change its visible colour (the game sets it), so lines keep the game's colour and only the tag is coloured. This also keeps every rarity readable.
- Map settings were re-applied in new areas only while map reveal was on (its hook). The load hook is now kept while custom map settings exist.
- Earlier screenshots were spoiled by game panels opened from stray key presses; the final run closed all panels through the game's own methods before each capture.

## Automated

| Suite | Result |
|---|---|
| Native | 17/17 programs. |
| Python | 257/257 (new: filter manager 11). |
| JavaScript | 146/146 (new: Display page controls and palette, palette push once per character, loot filter panel routing). |

## Real game (isolated `EpFeedbackTest` clone, test core)

Two sessions after the fixes above; all 54 original save files restored and
byte-verified after each; the player core was reinstalled at the end. The
loot filter folder was hashed before and after: unchanged.

- Minimap: 1.6× shows the larger minimap and frame with the quest tracker moved
  below it; 0.7× shrinks it; opacity 0.3 makes the minimap, frame and buff icons
  translucent; reset restores the original size, opacity and tracker position.
- Travel to Traveler's Rest with 1.6×: re-applied automatically (applies 8 → 9),
  the minimap stayed at 1.6×.
- Overlay (opened with `UIBase.OpenOverlayMap`): the overlay outline differs
  between opacity 1 and 0.1 in a pixel diff of the same frame region; zoom 2
  shows a wider area.
- Tier icons: 13 labels on 15 icons (2 items without affixes); digits visible
  on inventory and equipment icons.
- Tooltips: a native tooltip (`TooltipItemManager.OpenItemTooltip`, as a hover
  would) showed `T2` tags on all three affixes of a Scholar's Refuge Helmet
  (2 prefixes, 1 suffix, all tier 2); 0 mismatches; switched off, the same
  tooltip reopened without tags; 0 faults. (That session still called `Graphic.set_color` on
  the lines with no visible effect; the release build only drops that call.)
- Loot filters: an imported filter and its duplicate appeared in the game's own
  filter list (`ItemFilterManager.GetFilterFileList`) within 3 seconds while
  playing, and disappeared after the test files were removed.

## Limitations

- Loading an imported filter as the active filter was not confirmed: on the
  clone `ItemFilterManager.LoadFilter` returned true but `Filter` stayed empty,
  for imported and existing filters alike. Imported files are byte-for-byte the
  shared file except the `<name>` text, and the game lists them.
- Only the quest tracker moves with a larger minimap; other panels keep their
  positions. Sizes above 1.6× were not photographed.
- Palette changes appear in a tooltip the next time it opens.
