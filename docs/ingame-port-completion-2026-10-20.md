# In-game interface port — implementation and verification

This continues `HANDOVER-ingame-window.md` on the private `ingame-ui` branch.
The original handover and port map describe the starting state and specification.
This document records the resulting implementation and separates its test coverage.
No public release, main-branch merge or Discord announcement is authorized by it.

Latest results: [follow-up live verification](ingame-live-validation-2026-10-20.md).
The three save actions and persistence, six real screen/text-size combinations,
Echo focus and actual combat through the native damage window are now verified.
That follow-up also fixes narrow damage tables and string skill-icon IDs. The
current player build is 29, validated in production live run 21.

## Implemented

| Area | Result |
|---|---|
| Character | Native sheet/alias/raw stat browser, search, categories, favorites/hidden rows, 24-row pagination, normal/advanced edit dialogs, drafts and reset. Long dialogs scroll with their action buttons reachable. |
| Settings | Language, shared local profiles, load into draft, delete confirmation, startup settings/replay whitelist, text size, navigation visibility/favorites, remembered scroll, hidden panels/cards, version/support report. |
| Startup | Once per loaded offline identity; validates ownership before each setting, accepts only session/actor settings and explicitly allowed choice commands. Never replays quest/reward/material/item-creation actions. |
| Loot filters | Native XML list, duplicate, export, XML/ZIP import, ZIP backup. File I/O runs outside the Unity thread. Paginated list, validated XML, bounded archives, new files only, no overwrite/executable extraction. |
| Monolith Navigator | Explicit read of an existing web, reward/status/search filters, map and incremental result cards; explicit normal game-map focus. Opening the page does not start or generate an Echo. |
| Summons | Minion bonus shortcuts to the stat editor; live allies and persistent column/individual options. |
| Unique Atlas | Complete owner/revision-checked paged scan; owned/missing/wishlist/set/type/query filters, catalog cards, copy comparisons and local wishlist markers. |
| Stash Assistant | Same complete snapshot validation, grouped copies/base comparisons, protection reminders and eight copyable stash search recipes. Markers do not silently protect items in the game's vendor. |
| Game setup | Current game/runtime information and offline readiness; installation is performed with the game closed. |
| Global interface | Functional setting search preserving catalog order and Crafting tab routes, hide/restore panels/cards, responsive native geometry, controller focus implementation. |
| Loot HUD | Separate in-game canvas, remains when F6 is closed; drag/resize, display options, item/resource/material filters, All/None, grouping, shard descriptions and hover details. |
| Damage HUD | Quiet/Bars/Split/Table, Dealt/Taken, six DPS metrics and seven elements, source rows, live source portraits, source filters, total without double-counting minions, measurement controls, death recap, persistent local placement/options. |
| Existing pages | Existing General/Loot/Crafting/Skills/Equipment/Display/World/Inventory/Campaign/Monolith/CoF/Factions layouts and commands are retained. |

The game setup page, recovery, update delivery, ZIP-path input and HUD chrome use
native adaptations. Recovery is intentionally an explanation with the game open;
restoring a loaded save is not exposed. The in-game interface uses Chronoforge;
alternate desktop themes and the desktop EXE installer are not embedded.

## Reliability fixes

- Local preference writes preserve other keys and use the same Windows named
  mutex across the native and Python writers. Independent stores writing in
  parallel are covered by regression tests.
- Dialog generations prevent stale read callbacks from attaching to a replacement
  dialog. Advanced-stat toggles cannot strand an in-flight read.
- UI text scale changes rebuild geometry; custom dialogs have bounded viewports
  and independent scroll/hit offsets.
- Navigator and collection data reject changed/incomplete owners and snapshots.
- Large browser grids are populated incrementally; a closed window does not
  rebuild HUD numbers or scan the scene every frame.
- XML parsing prohibits DTD/entities/namespaces and counts only rules inside the
  actual rules list. The XmlLite end-tag depth difference has an explicit fixture.
- Private renderer/local-preference commands are compiled only with
  `EPOCHPACT_TESTING`. The build stamps these as `test`, never `player`.
- The in-game packaging command rejects test/research flavors and mismatched
  loader/core hashes, includes only one runtime DLL and licenses, and verifies
  every ZIP member against its manifest.
- Rapid support reports create unique directories rather than overwrite the
  previous report from the same second. Visual checks wait for a fresh, complete
  PNG; merely finding an existing screenshot filename is not considered a pass.

## Automated verification

- Final full Python suite: **375 passed**.
- JavaScript suite: **238 passed**.
- Native: **26 test programs passed**, plus native translation exercised through
  the Python asset tests.
- Targeted final native asset/browser/HUD/package/preferences checks:
  **39 Python tests passed**.
- Native browser composition is compared with the actual desktop Python backend.
  All six metrics, element splits, unknown values, minion aggregation and loot
  grouping/filtering are compared with the actual desktop JavaScript modules.
- Filter tests use temporary folders: strict XML, enabled counts, escaped names,
  traversal rejection, original preservation, ZIP round trip, collision-safe imports
  and 20-backup retention without deleting unrelated files.
- Initial player build 11 caught a failed filter enabled-count assertion. It was
  fixed, rebuilt and passed in build 12; the failed attempt is not counted as green.

## Live verification

Evidence is stored outside the source/package under
`C:\Users\falor\EpochPactLab\builds\ingame-chatgpt-live-2026-10-20-*`.
Tests use only the isolated **EpFeedbackTest** character, an isolated preferences
copy and native game commands/Unity screenshots. No keyboard/mouse injection or
Computer Use is needed for these runs.

- Runs 6, 7 and 8: normal shutdown **exit code 0**, 54 original save files restored
  byte-for-byte, original runtime restored, real preferences unchanged.
- All 19 pages opened; search returned 17 Crafting-related results; 90/100/150%
  text scale checked at 2560×1440. Text measurement reported zero drift.
- Live catalog/stash reads succeeded: 486 catalog entries; the tested stash had
  14 equipment pieces in 12 groups. Data depends on the cloned save.
- Private renderer run: local profile save/load/discard, delete Cancel,
  navigation hide/favorite, card hide/restore, advanced-stat scrolling at 150%,
  all three Crafting tabs and five HUD views. Fixture total was 8,146 and did
  not multiply the four-skeleton row again. These are **renderer fixtures**,
  not a new live combat benchmark.
- Final production run and updated renderer run: see the final audit below.

## Explicit verification limits

- The original audit below used confirmation/Cancel only. The follow-up
  [live verification](ingame-live-validation-2026-10-20.md) now executes all
  three Crafting save actions, verifies persistence after a real restart,
  exercises generated Echo focus, checks six actual resolution/text-size
  combinations, and measures real combat through the native damage window.
- Physical gamepad navigation remains unverified: no XInput controller was
  attached. UI fixtures, live combat and automated desktop parity remain
  separate evidence.
- Earlier runs 1/3/4 ended in Unity accessibility shutdown with `0xC0000005`.
  Runs without the retained desktop automation client subsequently closed
  normally. This is correlation, not a universal crash fix claim.
- TMP kerning can differ from Chrome. Zero native text-measurement drift means
  native measurements agree with native rendering, not universal pixel identity
  with every browser rendering.
- Keep the desktop app closed while reviewing the native-only route. Both UIs
  share local preferences; cross-frontend startup profile arbitration is not
  implemented, so both can dispatch the same absolute startup setting.
- No antivirus clearance, public upload or production certification is claimed.

## Reproduce

```powershell
native\build.bat player <isolated-production-build>
native\build.bat player-ui-test <isolated-private-renderer-build>
py -3 -m tools.ingame_final_live_check <new-live-evidence-folder> --loader <production-build>\player\version.dll
py -3 -m tools.package_ingame_player --build <production-build> --output <new-package-folder>
```

For unit tests, set `EPOCHPACT_NATIVE_TESTS` to the completed native build,
`EPOCHPACT_BROWSER_TEST` to its `ingame_browser_test.exe`, and
`EPOCHPACT_PREFERENCES` to a throwaway preferences file. Never package the
`player-ui-test` DLL or run save-writing tests on the owner's character.

## Original audit — passed

- Production: `ingame-chatgpt-player-16`, live run **12**.
- Private renderer: `ingame-chatgpt-ui-test-17`, live run **11**. This DLL is not
  part of the player ZIP.
- Production run opened all 19 pages, checked all three Crafting tabs, search,
  90/100/150% text scale and live collection summaries against the desktop
  backend. Zero UI faults and zero native text measurement drift.
- Recording remembered from the isolated preferences resumed only after the
  offline character was ready. Both private test command families were rejected
  by the production DLL.
- Fresh renderer captures show all four damage styles, options, source portrait,
  and a scrollable advanced stat dialog. The Table fixture checks all nine
  metrics (28 value cells including the total). The five browser captures came
  from five different report folders.
- Both final games closed with **exit code 0**. Each restored all **54 original
  save files** byte-for-byte, archived the three clone files and restored the
  original runtime; the real preferences file was unchanged.
- In this idle-area sample: open **8.46 ms**, close **9.01 ms**, game frame average
  **13.34 ms open / 13.38 ms closed**, native window step average **0.054 ms**.
  These are local samples, not a broad performance guarantee.
- `panel kern` ran live: Inter had 5,592 pair records and Georgia 1,319;
  enableKerning was true. Native widths still differ from the browser reference;
  there is no claim of pixel identity for every font/locale/resolution.

Machine-readable results: `ingame-port-verification-2026-10-20.json`.

Latest local player package (follow-up production run 21 passed):
`C:\Users\falor\EpochPactLab\builds\ingame-chatgpt-player-29\package\EpochPact-0.1.2-dev.36-InGame.zip`

- ZIP: 3,419,427 bytes, eight allowlisted files, one runtime (`version.dll`).
- ZIP SHA-256: `f4730b2d5735b9f1507042af878bfea91d1ce4f9288f9175961fbb28d580df2e`.
- DLL: 7,872,512 bytes.
- DLL SHA-256: `0639bf44cb00a078661da1ff9def81df71e4fbd24d7b47275e94de1b163e8d8c`.
- All archived payload bytes match the manifest and verified source files.

No public release was created. The owner's original installed runtime is restored;
the new DLL is available in the review ZIP for installation with the game closed.
