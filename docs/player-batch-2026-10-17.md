# Player batch of 13 requests, death recap and Season 5 settings (2026-10-17)

Everything below was measured live on the isolated test character EpFeedbackTest (offline, test core
with read-only diagnostics and test fixtures); saves and the installed mod were restored and verified
after every session. The player's own loot filter files were copied before the sessions that opened
the game's filter window and compared afterwards; when the game had rewritten one, it was put back
byte for byte. App pages were checked on a separate interface server with its own preferences file;
the player's `preferences.json` was verified unchanged.

## Results per request

| # | Request | State | Measured |
|---|---|---|---|
| 1 | More fonts | Done | 361 Windows families and 758 faces offered (every style: Light, Semibold, Bold, Black, Italic, Bold Italic …); Segoe UI Semibold, Arial Bold Italic, Segoe UI Light and Georgia applied to 3001 texts, no faults; bad style and malformed names refused. First font read 1.3 s, then 0.04 s. |
| 2 | Font search | Done | Search box narrows the list by name or style, the chosen font stays listed, count shown; app tests. |
| 3 | Rule line without "Show All", choose what it shows | Done | Rule line shows: everything / number+Show/Hide+name / number+name / Show/Hide+name / name only / number+Show/Hide / number only — all 7 checked on real tooltips. Unnamed rules lose "Hide all"/"Show" etc. ("items with 50 Affixes", "8 Types", "Normal/Magic items"). |
| 4 | Colour words in loot filter rule names | Done | A rule named `Colour <color=#FF4040>red</color> and <b>bold</b> <size=60>big` showed red and bold in the game's filter window and in the tooltip rule line; `<size>` stayed text. The game's name field accepts the characters (no validation, 115 characters). Off gives the game's display. |
| 5 | Letter case in tab names, filter and items | Done (display) | Letter case as written removes the game's capitals/lower-case/small-caps text styles on 3035 texts and restores them exactly; with a Windows font names show as typed. The game's own font still draws lower case as small capitals. Word endings and symbols are whatever the player types; the game's name fields accept them. |
| 6 | More colours in loot filter and stash | Loot filter done; stash not done | 26 more colours: label colours 18 → 44, beam colours 22 → 48, the game's Select Color grid shows all 44 with its own button style; off restores 18/22. Without EpochPact a rule with a new colour shows the game's last colour (the game limits the number; checked). Stash tab colours are stored in the character save, so they were left alone (save risk). |
| 7 | Automatic sorting, per tab | Done | Inventory: a scattered item moved back by the game's Sort on opening (and when switched on while open); off: not sorted. Stash: the chosen tab and "every tab" sorted when shown. |
| 8 | Fourth button Sell all | Done | Four buttons (Stash, Stash all, Vendor, Sell all) inside the inventory, no overlap. First press shows "Confirm sell" for 4 s, second sells: 2 items sold for 15 gold, vendor opened by itself; recovery backup written. Sell all keeps Unique/Set/Legendary by default (or everything, or also Exalted). |
| 9 | Stash rules in the game interface | Not done | EpochPact's Inventory page already turns loot filter rules into stash tab priorities and sorts by the exact filter rule; an in-game editor for that would be a new game window and was not built. |
| 10 | Tier colour order and a square for special affixes | Done | Default palette in rarity order (white, blue, yellow, purple, orange, green, red, cyan). Experimental (orchid) and Personal (sky blue) squares seen on real tooltips; off: no squares. |
| 11 | Settings not kept | Done | Settings kept across two game re-entries and an app restart, one summary message instead of one per setting (earlier session, research/live/persist1). |
| 12 | Launch the game when EpochPact starts | Done | On an interface server whose launch only records the request: ticked → saved in the preferences file; next app start sent one launch; a reload did not launch again. |
| 13 | Affix colours missing when comparing | Done | The compared item's tooltip is coloured too (game's `updateCompareAffixesText`); 0 mismatches (earlier session, research/live/compare2). |

Also in this batch (earlier sessions): **Damage taken and the death recap** (53 hits recorded = lost,
session total equal to recorded hits) and **Rage of Morditas (Season 5) settings** (spawn chance,
hunt time, charge and buff thresholds, Blood share; reset gives the game's values back).

## Tests

- Native: 22 of 22 test programs (new checks: rule words, rule line parts, rule name tags, extra filter
  colours, the four-button toolbar).
- Interface (Node): 235 of 235. Python: 332 of 332. Translations: 1727 texts, Turkish and Russian
  complete, no placeholder mismatch.
- Release 0.1.2-dev.34: player core `7d56b7fc…`, EXE `7ac80ca9…`, ZIP `5113dba2…`; compiled package
  checks 24 of 24 (update swap 6.6 s), extracted-ZIP connection checks 4 of 4, Windows Defender: no
  threats (docs/security/virustotal-20261017-dev34.json).
