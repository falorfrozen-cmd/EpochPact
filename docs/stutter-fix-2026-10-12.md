# In-game stutter fix — 2026-10-12

Player report: "It's working, but my game is stuttering a lot."

## Cause

`convenience::Frame` runs `InventoryButtons()` every 250 ms while a character is
loaded. Until the inventory was opened, every call ran
`Object.FindObjectsOfType(InventoryPanelUI)` — a closed panel is inactive, so it
was never found and the search repeated forever. After the first opening, every
call re-placed the shortcuts with dozens of boxed managed calls even while the
inventory was closed.

## Fix (branch `perf/stutter-fix`)

- `InventoryButtons()` returns at once unless the inventory, stash or shop is open
  (`InventoryVisible()`: cached `UIBase` queries resolved on the instance's runtime
  class, called directly — no scene search, no managed allocation). The shortcuts
  are children of the inventory panel and hide with it.
- Telemetry in `qolread`: `inventorySearches`, `inventoryMicros`,
  `maximumInventoryMicros`.
- Desktop app: live reads on the Summons page every 2 s and the World page every
  5 s instead of every second (each read is a main-thread job).

## Measured (Steam 1.5.2.1, offline character Falor, zone Z32, all panels closed, 30 s)

| | Before (6adf5f6) | After (822c452) |
|---|---|---|
| Time per 250 ms check | 31,558 µs (31.6 ms) | 43.6 µs |
| Worst check | 53.8 ms | 0.13 ms |
| Share of wall time | 12.5 % | 0.018 % |
| Scene searches per second | 3.98 | 0 |
| Frames per second (same spot) | 94.9 | 111.9 |

Before the fix the game lost 2–3 frames four times a second. After it, opening the
inventory created the three shortcuts (STASH / STASH ALL / VENDOR, all visible,
one scene search) and closing it hid them. Frame-gap capture (`qoltest frames-*`)
requires the isolated EpFeedbackTest character, which no longer exists; the
numbers above come from the `qolread` counters over two snapshots 30 s apart.

All native tests, 237 Python tests and 126 JavaScript tests passed.

## Remaining

- The first inventory opening in a session creates the shortcuts once; this took
  0.46 s in the measurement above (unchanged by this fix). Creating them lazily
  over several frames, or caching the template lookups, would remove that one-time
  hitch.
