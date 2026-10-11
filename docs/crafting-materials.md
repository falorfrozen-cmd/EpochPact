# Crafting material stock

Open **Crafting → Materials**, load an offline character and refresh materials. Enter the minimum
quantity per material and press **Stock all materials**, then confirm. The default
is 10,000; the allowed range is 1–1,000,000.

The action raises every current, non-obsolete shard, rune and glyph type to at
least that quantity. Higher existing quantities are kept. Repeating the same
target adds nothing once every material is stocked. Materials selected in the
forge count toward the total. No materials are granted on startup, theme change,
refresh, focus or profile load. This is a one-time stock action, not infinity.

A recovery snapshot is written before additions. Native game material storage
and save APIs are used; there is no proprietary material format. The stash can
be shared by other offline characters in the same cycle/challenge. Character-
found stashes stay separate. Additions run in small batches across game frames.
If the session or stash changes, remaining additions stop. A failure after an
earlier batch may leave those additions; use the recovery backup to undo them.
The action is never automatically repeated.

The stock is saved rather than provided by a temporary gameplay multiplier.
Keeping materials while crafting is a separate session setting under **Forge Settings** and requires
the mod. Point budget multipliers are also session settings; this feature does
not make expanded skill or passive budgets portable.

Steam Cloud synchronization and play on a Steam Deck require a separate device
test. A successful Windows save/reload test does not prove cross-device sync.
Use the same supported offline game version and let the game close normally
before synchronizing. No Steam Cloud configuration is changed by this action.
