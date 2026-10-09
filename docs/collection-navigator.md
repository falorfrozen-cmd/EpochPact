# Unique Atlas, Stash Assistant and Monolith Navigator

These features are part of EpochPact, with the existing Chronoforge and Void Atlas
themes. The interface is English. Open `EpochPact.cmd`, load an offline character,
and use **Scan collection** or **Read Echo web**. Opening the app or these pages
does not scan collections, generate Echoes, grant rewards or change saves.

## Unique Atlas

The catalog comes from the loaded game's visible Unique/Set assets, rather than
a copied item list. Owned counts include the currently loaded stash, inventory,
equipment, idols, cursor, Forge and Eternity Cache. Other characters and separate
stashes are outside the scan. “Missing” means absent from this snapshot.

Search by name, base type or owned-copy affix. Filter owned, missing, wishlist
or Set items. **View copies** compares actual LP, affix tiers, modifiers and
locations. Best LP counts only ordinary Unique copies: a Legendary has already
consumed its LP, and Weaver/Set items do not represent craftable LP copies.
Random-drop eligibility and level come from game assets; they are not a claim
about a specific boss or map's drop table.

## Stash Assistant

Copies of the same Unique/Set ID form duplicate groups. Other equipment is grouped
as **Same base comparison**, not as identical duplicates. Actual Unique modifier
names, values and ranges are read through the game's normal property APIs.
Regular affixes show their actual tier and roll position. A roll percentile is
position within a modifier's range, not the percentage value of the stat.

**Mark protected** is a local EpochPact reminder; it does not prevent selling,
salvaging or moving items in game. Neither page sells, transfers or alters items.
Wishlist and protection markers are stored in browser storage, scoped to the
loaded stash. Keep using the same browser/desktop app to retain them. When the
game supplies no individual item ID, identical copies share a content-based
marker; altering that item's content changes its marker.

Refresh after acquiring or moving items. Reads use serialized 100-item pages.
Every page must match the same player, stash, item revision and total; if anything
changes during the scan, the request fails instead of showing a partial result.

## Monolith Navigator

Choose the timeline and difficulty in the existing Monolith controls, then press
**Read Echo web**. This reads existing nodes, game reward titles, connections,
coordinates, completion and runnable state. An unopened web stays unopened.
Search rewards or Echo names and use reward/state filters; bright graph nodes
match the filters. **Highlight** only changes the local EpochPact display.

**Show in game** opens/focuses/selects the existing node through the normal game
map UI. It verifies the selected node by reading the game selection back. It
never starts or completes an Echo, grants rewards or generates a new web. It
requires the matching offline save and an appropriate Monolith hub. If the node
is no longer visible or available, it refuses. Stability is the node's base
stability, not a promise about the final gain after other modifiers.

## Native and Python contracts

| Native command | Python API | Effect |
|---|---|---|
| `atlasread` | `tools.collection_backend.atlas()` | Read catalog and combine ownership |
| `stashread [offset]` | `tools.collection_backend.stash()` | Read all pages and group copies |
| `echoread <save-id> <timeline> normal\|empowered` | `tools.monolith_backend.echoes()` | Read existing web |
| `echofocus <save-id> <timeline> normal\|empowered <index>` | `tools.monolith_backend.focus_echo()` | Select existing node without starting it |

Runtime members are resolved by name, not game addresses. These features add no
continuous scan, gameplay hook or watcher. Reads run as main-thread jobs through
the existing serialized command channel; the existing dispatcher remains.

## Build and verified scope

`native\build.bat player` builds the player DLL and regression executables without
research dumps, capture trackers, fixture commands or research housekeeping.
`native\build.bat player-core` builds only that core. Existing default/`core` builds
retain research tooling for development. `py -3 tools/le_session.py install`
installs the verified `native\build\player` artifact only while the game is closed.
Research/test installations require an explicit `--flavor research` / `--flavor test`.

Verification on Last Epoch **1.5.2** used a freshly backed-up isolated EpCraftTest
clone: a real 486-type catalog, four Fractured Crown copies with 0/2 LP across
inventory and stash, actual modifier percentages and T7 affixes, a Forge item,
and a seven-node Echo web. Focus selection was read back twice; nonexistent
nodes/wrong save IDs were rejected, and an unopened timeline remained unopened.
The clone is restored through `py -3 -m tools.collection_live_check restore`.
After seeding the isolated clone in a research build, `verify-player` checks the
installed non-research DLL's catalog, stash, graph and verified focus, and confirms
that research trackers and fixture commands are absent.
See [verification evidence](collection-navigator-verification.json).

The small live stash fitted one page and read below the millisecond-resolution
timer. This is not a performance result for a large stash. Multi-page consistency
and refusal paths are tested separately with deterministic backend tests.
