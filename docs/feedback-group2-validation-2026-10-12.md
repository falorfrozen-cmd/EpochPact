# Player suggestions, group 2 — 0.1.2-dev.17 validation (2026-10-12)

Steam Last Epoch 1.5.2.1 (internal 1.5.2.2), offline. Suggestions 8, 9, 10 and 15.

## Changes

| # | Suggestion | Implementation |
|---|---|---|
| 8 | Loot feed / log window | Native ring buffer (300 entries) fed by detours on `GroundItemManager.pickupItem(GroundItemList, Actor, UInt32, StackableItemFlags)` and `pickupGold(GroundItemList, Actor, UInt32)`; amber pickups by EpochPact are added too. `lootlog [since]` reads it without the main thread. Loot page table; separate always-on-top pywebview window `/feed` (opacity via `SetLayeredWindowAttributes`). The game's own pickup notifications are unchanged. |
| 9 | Rule number and name | The active filter's `ItemFilter.Match` → `matchingRuleNumber`; the matching rule is the one whose `Order + 1` equals it; name = `nameOverride`, else `GetRuleDescription()` (rich-text tags removed in the UI). 0 = no enabled rule matched. |
| 10 | Universal auto-loot | New pickup category `amber`: `LE.Factions.PickupableObjectsManager` single-player set, types Memory Amber, Soul Ember and XP mote, collected with the game's `PickupObject(set, object, index)` from the end of each list; radius respected. Items, gold, potions, XP/Favor tomes and Ancient Bones were already covered. |
| 15 | Bug report | Settings → Support report: local ZIP (description, summary, core/loader/install logs tails, app logs, optional `PrintWindow` screenshot of the game window). Windows user/computer names and the profile path are masked. Nothing is uploaded. |

## Defect found and fixed during validation

`pickupGold` has two overloads with three parameters. Selecting by arity hooked
`pickupGold(Actor, UInt32, GoldPickupInteraction)` with the list overload's
signature: gold was not collected, pick-up-now retried the pile for 240 frames,
and the game crashed about 11 seconds later (UnityPlayer.dll). Overloads are now
selected by the first parameter's type (`GroundItemList`) and an ambiguous
match is refused; pick-up-now also stops after three sweeps without progress.
This defect existed only in unreleased dev.17 candidates. After the fix the same
sequence collected the gold once and the game stayed responsive for 30 s and
closed with code 0.

## Rule numbering — measured

Per-rule `Rule.Match` results compared with `ItemFilter.Match` on generated
items with the player's own filters (test core, disposable character):

| Filter | Item | Game number | Rule that actually applies |
|---|---|---|---|
| Sorcerer (5 rules) | Normal, Magic | 1 | index 4, Order 0, HIDE Normal/Magic |
| Spellblade (9 rules) | Normal, Magic | 9 | index 0, Order 8, HIDE Normal/Magic |
| Necro (3 rules) | Normal, Magic | 2 | index 1, Order 1, HIDE Normal/Magic |
| all three | Rare, Unique | 0 | no enabled rule (shown by default) |

Final player-equivalent check (Sorcerer): Normal helmet logged as
"Rule 1 · Hide all Normal/Magic items"; Rare and Unique as "No matching rule".
(On Turkish Windows the game's own rule description reads "Magıc".)

## Automated

| Suite | Result |
|---|---|
| Native | 17/17 programs (items fixture 32/32; loot/crafting 6260/6260). |
| Python | 246/246 (new: support report 6). |
| JavaScript | 143/143 (new: loot log 7). |
| Compiled package | 21/21 `verify_player_package` (serves `loot-log.js`, `feed.js`); 4/4 extracted-ZIP connection checks. |

## Real game (isolated `EpFeedbackTest` clone, test core)

Six sessions; all 54 original save files restored and byte-verified after
each; the player core was reinstalled at the end. Final-build results:

- Loot log: gold +25 (Auto Pickup), Refuge Helmet Normal/Magic/Rare and
  Fractured Crown Unique with names and rarities; 0 faults.
- Amber: Memory Amber dropped 40 units away stayed on the ground with radius 5;
  pick-up-now collected it in 3 frames (logged "Memory Amber ×10").
- Loot window: served at `/feed`, polled the real API (showed the real
  "not running"/"update required" message) and rendered real entries with
  rarity colours and cleaned rule names (checked in a browser at 380×520). The
  always-on-top desktop window and its opacity were not exercised with a real
  WebView2 window.
- Game closes: code 0 in every session except the one that exposed the gold
  overload defect above.

## Package

| File | SHA-256 |
|---|---|
| EpochPact-0.1.2-dev.17.zip (26,847,682 bytes) | `e577849ab009ae73cecd413337c8b4236cd95c6bc2e47fe8261765dc70e824e4` |
| EpochPact.exe | `f641a5dc22dcc64364376c5d13ff977fe7b8ddc78441e13f2e4420a3acbb42ae` |
| EpochPact.Core.dll | `5abf3fb73fe6b57bd201f50db9f043f53e7f655bb97c190f582102bb29943ecd` |
| version.dll | `909d0f7f0aeaea6c3887c8d4b928bfc95eaa0367f16f05e8220f68d3dfcf7caa` |

VirusTotal (2026-10-12): EXE 0/68, core 0/71, loader 1/70 (Cynet, as with every
earlier loader; cause not established). Local Defender: no threats in the ZIP.

## Not covered

Manual (player-clicked) pickups were not exercised by automation; they use the
same hooked game methods. Soul Embers and XP motes were not spawned (same code
path as Memory Amber). The support report screenshot was tested with a fake
capture; `PrintWindow` on the real game window was not exercised.
