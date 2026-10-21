# Inventory pictures left behind (0.1.2-dev.38)

## Report

A player on 0.1.2-dev.35 (Discord, support ZIP with four F9 reports) saw items drawn over each
other in the inventory, and after Stash all some items stayed visible although they could not
be hovered or moved. Restarting the game did not help for long. The log: automatic sorting of
the inventory and of all stash tabs was on; Stash all moved 38 items with 0 remaining, then was
pressed again five times (0 moved each time). The first screenshot, taken before that Stash all,
already shows overlapping items.

## Cause

`ItemContainerUI.Initialize` queues one "draw this entry" action per item, and
`CreateItemsAsyncLoop` runs them over the next frames under a time budget. The queued action
(`<Initialize>b__3`) calls `AddItemUI(entry)` without checking that the entry is still in the
container. `ItemContainer.Sort` removes and re-adds every item. EpochPact's automatic sort ran on
its 250 ms tick in the first check after the window opened, so it could land while the window
was still drawing: the queue then drew the removed entries (pictures without an item, which
cannot be hovered or moved) and the item-added events drew the re-added ones over them. Stash
all, Sell all and stash rule sorting also moved items without waiting for the drawing.

On the test PC a window with 28 items finished drawing within a frame: sorting at 0-200 ms after
opening (18 tries) never left a picture, so the race could not be reproduced here.

## Fix

`native/core/inventory_tools.cpp`:

- A sort waits until every window showing the container (matched through the window's
  `Container`: the inventory, or the stash holder's current stash and its active tab) is
  initialised and not drawing, at two 250 ms ticks in a row.
- Stash all, Sell all and stash rule sorting (`InventorySettled`) wait for ten drawn frames.
- After a sort and after those actions, a window whose pictures are not exactly its container's
  items is redrawn with the game's `ReDrawWholeContainer` (`RedrawStale`).
- Once a second while the inventory or stash is open, a window whose pictures have not matched at
  two checks in a row is redrawn, whatever left them (not while an item is on the cursor).
- `invread` reports the open windows (`drawn`, `stale`) and the counters `windowRedraws`,
  `staleRepairs` and `sortsWithoutWindow`.

## Live checks (EpFeedbackTest clone, commands only)

Evidence folders under `C:\Users\falor\EpochPactLab\builds`; every run closed the game and restored
the runtime files, saves, preferences and `auto-sort.txt`.

| Run | Result |
|---|---|
| `ghostfix-live-2` (player build) | Stash and inventory opened 12 times with automatic sorting: stash windows found, 6 tab sorts after drawing, `sortsWithoutWindow` 0, no stale window, 0 faults; Sell all and Stash all clean. |
| `ghostfix-live-3` (test build, 28 items) | 8 inventory sorts after drawing and 6 stash tab sorts, no stale window; Stash all moved 28. |
| `ghostfix-live-4` (test build) | Sort at 0-200 ms after opening, 18 tries: no picture left (not reproducible on this PC). |
| `ghostfix-live-5` (test build) | A second picture of an item drawn on purpose (`inventory-ghost`): repaired after 1.67, 1.32 and 1.95 s; with such a picture, Stash all moved 28 and nothing stayed. |

Python 380, JavaScript 238 and native 26 tests pass.
