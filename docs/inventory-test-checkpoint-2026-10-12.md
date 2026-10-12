# Inventory test checkpoint — 2026-10-12

Testing is paused at the user's request until the performance branch is finished.
No placement, button-size or performance-source changes were made in this pass.
The preferred horizontal inventory-footer layout remains the target; a slightly
larger button size is still pending.

## Recorded results

- The current loader started the test core in actual Last Epoch without requiring
  a version-API call. The loaded character was the isolated offline EpFeedbackTest
  copy, not Falor.
- Gold and Ancient Bones were read back as 2,147,483,647; Favor as 999,999.
- Native rectangle checks passed at 75%, 100%, 125% and 150% UI fixture scales.
  The three buttons cleared the inventory grid, Transfer/Sort and currency
  rectangles. Stash/Vendor toggle callbacks and inventory hide/reopen checks
  passed after waiting for native panel state to settle.
- Earlier attempts observed temporarily hidden buttons during panel/scale
  transitions. The harness subsequently waited for stable visibility. This is
  not a product fix or proof of instant appearance on every player's machine.
- **Visual validation remains incomplete.** The 100% and 150% screenshots have
  identical SHA-256 hashes. They cannot verify two different rendered scales.
  Apparent bottom-edge currency clipping requires a fresh rendering check.
- Normal game close completed with exit code **0 (0x00000000)**; no Last Epoch
  process remained. This one result does not clear the previously documented
  intermittent full-game shutdown crash.
- All **54 original save files** were restored and hash-verified; **3 isolated
  test files** were archived. The **3 pre-test installation files** were also
  restored and hash-verified.
- A separate game process appeared after restoration; this pass did not launch
  it. It was also closed normally at the user's request, with exit code 0 and no
  game process remaining. That second process is not an isolated feature-test result.

Raw attempts, native replies, screenshots, closure receipt and evidence hashes
are retained locally under the ignored
`research/live/inventory-final-20261012` directory. Private saves and logs are
not included in source commits or player packages.

Pending: the requested button-size adjustment after `perf/stutter-fix` is ready,
fresh pixel validation, a current non-empty Stash all transfer and the matching
production-core desktop flow. No new player release is certified by this checkpoint.
