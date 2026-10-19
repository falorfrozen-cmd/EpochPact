# Auto pickup and the loot feed: player reports (2026-10-17)

Players wrote that EpochPact sometimes stopped picking up affix shards and gold and came back to
normal about five minutes later, possibly after going to the menu to change character. Everything
below was measured live on an isolated test character (offline, test core with read-only
diagnostics; saves and the installed mod restored after every session).

## Auto pickup itself: no stop found

| Scenario | Gold picked | Shards off the ground | Collection paused |
|---|---|---|---|
| Before any character change | 3 of 3 | all | no |
| To character select and back, 3 times; right after, 30 s and 90 s later | 3 of 3 each time | all | no |
| Inventory open, then out and back | 3 of 3 | all | no |
| An item tooltip open when leaving, then back | 3 of 3 | all | no (the game clears the tooltip state) |
| Inventory full (24–44 Uniques on the ground it cannot take) | 3 of 3 | all | equipment only, as designed |
| Inventory full, opened and closed without making room | 3 of 3 | all | equipment only |

The diagnostics count every pickup tick: no tick was skipped for an item tooltip after a
character change, every pickup request found its ground record, and the scan ran throughout.

## Found and fixed: the loot feed did not show what was picked up

1. **Shards (and runes and glyphs) taken along were not listed.** When one affix shard is picked up,
   the game also takes the nearby ones (`GroundItemManager.vacuumNearbyStackableItems`). Those never
   pass through the pickup call EpochPact logs. Measured: 6 shards on the ground, all 6 in the
   crafting materials, 1–2 in the loot feed. Now the ground records are read before that call and
   every material it took is logged: 6 of 6, 8 of 8 and 5 of 5 (Collect now) listed, the same as the
   crafting materials gained, no duplicates.
2. **After the game was started again, the loot feed showed no new pickups for a while.** The game
   numbers the log from 1 at every start; the loot feed window, still open, took the new numbers for
   entries it already had and dropped them until the count passed the old one (with 120 old
   entries: 0 of 30 new pickups shown). The log now names its game session and the feed keeps
   entries per session (a core without a session is recognised by its count going back): 30 of 30
   shown. The main window's list was not affected (it starts again for each character and session).

Either one makes the feed look as if shards or gold were not collected. Tests: `tools/test_loot_log.cjs`
(restart with and without a session), live rounds in `research/live/lootlog-fix/`.

## Still open

Found on 2026-10-19 ([pickup-pause-2026-10-19.md](pickup-pause-2026-10-19.md)): a ground label picked
up by hand under the cursor paused all collection until the cursor crossed another label.

The real stop the players describe was not reproduced here. If it happens again, the useful facts are:
which window showed it (loot feed, or items left on the ground), the pickup mode and radius, whether
the game was restarted, and an in-game F8 report right then.
