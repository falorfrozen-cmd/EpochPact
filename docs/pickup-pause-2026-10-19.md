# Auto pickup stopping after a manual pickup or a full inventory (2026-10-19)

Players reported that auto pickup sometimes stopped for minutes, and a promo-video session saw equipment
left on the ground after the inventory had been full and was emptied with Sell all. Everything below was
measured live on the isolated test character EpFeedbackTest (offline, test core, real I/Esc key presses
and real mouse moves and clicks sent to the game window); saves and the installed mod were restored and
verified after every session.

## Cause 1: a ground label picked up under the cursor stopped all collection

`TooltipItem.OnPointerEnter` opens the item tooltip (`TooltipSystem.OpenItemTooltip`) and sets
`TooltipItem.highlightedTooltipItem`; `OnPointerExit` closes the tooltip and clears the field;
`OnDisable` closes the tooltip but does not clear the field (GameAssembly disassembly). Ground item
labels carry a `TooltipItem`. When a label is clicked (picked up by hand) while the cursor is on it, the
label is hidden and pooled with the field still naming it. EpochPact treated that field as "the player is
inspecting an item" and paused every pickup.

| Old core, measured | Result |
|---|---|
| Hover a label, click it (inventory 7 to 8), move the cursor away | pause reason "highlighted item" stays, scans stop |
| Two gold piles and later equipment dropped | nothing collected by EpochPact |
| Inventory opened and closed with I | still paused |
| How it ended | after about 5 minutes, when the cursor crossed another label |

The same pause also stopped automatic chest and shrine interactions (they check the same state).

The pooled label is later handed out again for another drop, so "is the label shown" is not enough
either (measured: the pause came back on the next drop). The fix: the field alone pauses only for
750 ms after it changes (the moment before the tooltip state appears); after that, the tooltip state
decides (tooltip lock, legacy tooltip open, pending hover, V2 card), which is set while a label or item
is really hovered. Measured with the fixed core: a real hover still pauses (tooltip card shown, reason
"item tooltip open"); after a click pickup collection continues; the reused label does not pause; the
inventory and equipment hover pause is unchanged.

## Cause 2: the "inventory full" pause missed the inventory being closed

After the game's own "inventory full" message, equipment waits until the player opens and closes the
inventory. That check ran after the hover pause, so a pause covering the whole time the inventory was
open hid the open/close, and equipment stayed skipped after the inventory had been emptied (old core:
inventory with room, 24 equipment labels left on the ground). It now runs before the hover pause
(fixed core: flag cleared at the close while a pause was on, 19 of 19 collected).

## Cause 3: collection turned off and on again kept the "inventory full" pause

With collection off its hook is out, so emptying the inventory was not seen. Turning collection on now
tries equipment again; if there is still no room the game shows its message once more and equipment
waits. Old core: 25 labels left on the ground. Fixed core: 17 of 17 collected.

## Not a fault: Sell all with collection on

In the video session's first case the leftover ground items refilled the inventory right after Sell
all, so the later drops did not fit. With real I presses: full, Sell all, close, the 22 leftover items
were collected (inventory 1 to 23). Closing the inventory with I while the cursor rests on an item does
not leave a pause behind (inventory items use the legacy tooltip, which closes with the panel).

## Tests

`items_test`: 48 checks (new: the short pause of a fresh highlight, a highlight left with no tooltip,
the same object hovered again, a new or re-set highlight, the inventory closed during a tooltip pause,
collection off and on). Against the previous `items.cpp` the new checks fail. All 22 native test
programs pass.

Not changed: Sell all leaves the vendor panel open after the inventory is closed (seen in every session).
