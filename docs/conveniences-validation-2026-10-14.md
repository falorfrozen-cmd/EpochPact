# Damage window, auto pickup, interaction areas and app settings (2026-10-14)

Player requests: the damage window showed nothing while the Summons page
measured; auto pickup stopped after large drops (for example a Prophecy) until
the next zone; not every chest opened, so the chest/shrine/portal distance
should be adjustable with a visible circle or square in a chosen colour;
panels such as "Wanted T7 affixes" should stay hidden; the app text should be
larger; a new version should be announced with a pop-up.

## Causes and changes

- **Damage window.** The window read the session only while it believed the
  meter was running, so a session started in the main window never reached it
  until it was reopened. Both the window (`ui/damage.js`) and the Summons page
  (`ui/app.js`) now also read every three seconds while stopped, and once a
  second while it runs.
- **Auto pickup after large drops.** Each scan read the first 256 ground labels
  and only then filtered them. With more than 256 labels, the first 256 always
  being unwanted (normal items on a long Prophecy or boss drop) meant wanted
  drops behind them were never reached until the zone changed. The scan now
  continues where the previous one stopped (`pickup_rules.hpp` `NextWindow`,
  `items.cpp` `SnapshotList`): still at most 256 per scan and list, every label
  is reached within a few scans.
- **Interaction area.** `Near()` used a fixed three game units. It now uses
  `rules::InArea` with **Interaction radius** (1–30, default 3) and
  **Interaction area shape** (circle or square on the ground plane; the height
  may differ by up to the radius, at least 3). **Show interaction areas** draws a
  `LineRenderer` outline (48 points for a circle, 4 for a square, shader
  `Sprites/Default` with fall-backs) around every chest, shrine and portal within
  60 game units that will still be used, at most six new outlines per update
  every 250 ms; used objects lose their outline. If no outline can be drawn the
  switch turns off and `qolread.area.error` says why. `qolarea radius|shape|show|color`
  sets the values; the app remembers the colour on this device.
- **Hidden parts.** Every panel and card on a page gets a × on hover; a hidden
  part is remembered on this device (by control id, panel id or English panel
  title). Settings → Hidden parts lists them with Show and Show all.
- **Text size.** Settings → Text size scales the app window 90–150 %.
- **New version.** The first time the startup check finds a version it has not
  announced, its What's new window opens once by itself; afterwards only the
  sidebar link stays. Check now always opens it.

## Live tests (isolated EpFeedbackTest clone, test core)

| Test | Result |
|---|---|
| Damage window opened first, session started from the main window, 16 attacks | Window showed "Measuring · 0:33 session · 13 hits" without reopening; Stop kept 13 hits |
| 300 normal items dropped on top of 3 uniques, before the fix (`research/live/pickup2old`) | 0 of 3 uniques picked (15 616 rejections, 0 accepted) |
| Same, with the rotating scan (`research/live/pickup2new`) | 3 of 3 uniques picked, 0 faults |
| Chest 6 units away, radius 3 | Not opened (`chestsOpened` 0), outline shown |
| Same chest, radius 8 | Opened (`chestsOpened` 1), outline removed |
| Circle radius 3, square, colour FF3366 radius 4 (`research/live/area5`) | Outline visible on the ground around the chest in each screenshot; square and colour rebuilt the outline |
| Radius 8 after the screenshots | Chest opened, loot on the ground, no outline left |
| `qolarea radius 0`, `radius 31`, `shape 2`, `color XYZ123` | Refused with the allowed range; nothing changed |
| Show off, reset to radius 3 circle 3FD0FF | Outlines removed, 0 faults, game alive, 54 save/runtime files restored |

The screenshot fixture now also closes the world map before a capture
(`panels-close`, test character only); earlier captures were covered by the
map and inventory.

## Automated tests

- Native: `items_test` (300-label rotation: scan 1 picks nothing, scan 2 the
  uniques; ≤ 256 per scan; no cursor reads from the start; `NextWindow` edges),
  `convenience_rules_test` (circle/square/height bounds, outline points).
  20 native test programs pass.
- JavaScript (202): the damage window shows a session started elsewhere; the
  World page shows the area settings and only a custom colour is sent; a hidden
  card stays hidden, Settings lists it, nothing is sent to the game; a new
  version opens its window once, Check now always.
- Python (325): `qol_areacolor` accepts six hexadecimal digits only, radius and
  shape stay in range, resets send `qolarea color 3FD0FF` and `qolarea radius 3`.
- UI text: 1569 texts, Turkish and Russian complete.
