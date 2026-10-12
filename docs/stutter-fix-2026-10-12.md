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

## First inventory opening (0.46 s hitch)

The first opening in a session creates the shortcuts. A per-step profile written to
core.log (`inventory shortcuts created: ...`) showed where the time went:

| Step | Before | After |
|---|---|---|
| Hook installation (Button.OnPointerClick / OnSubmit) | 498 ms | 0 ms (installed at startup) |
| Panel lookup (`FindObjectsOfType(InventoryPanelUI)`) | 51 ms | 0.001 ms |
| Clone 3 buttons, labels, placement | ~2.5 ms | ~3.8 ms |
| Whole first-open check | ~550 ms | 4.4 ms |

Installing a hook briefly holds every game thread. `convenience::Init` now installs
the two Button hooks and a hook on `InventoryPanelUI.OnEnable` while the game is
still starting (`inventory shortcut hooks installed at startup` in core.log). The
OnEnable hook remembers the panel, so creation needs no scene search; the search
remains only as a fallback. If a startup installation fails, the previous lazy
installation still runs. The Button hooks act only on EpochPact's own shortcut
buttons and pass every other click to the game; menu clicks and character loading
worked normally with them installed from startup.

Measured with the same character and zone; all native, 237 Python and JavaScript
tests passed.
