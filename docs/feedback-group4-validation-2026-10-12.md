# Player suggestions, group 4 — 0.1.2-dev.19 validation (2026-10-12)

Steam Last Epoch 1.5.2.1 (internal 1.5.2.2), offline. Suggestions 17, 18 and
20, and the open points of dev.18 (3, 5, 16).

## Changes

| # | Suggestion | Implementation |
|---|---|---|
| 3 | Overlay zoom | Unchanged code; measured (below). |
| 5 | Affix colour coding | `tierset lines 0|1`: the affix line is wrapped in `<color=#RRGGBB>` (TMP rich text; `Graphic.color` is reset by the game). The original text is stored per line and written back on switch-off; `UITooltipItemAffix.Init` drops the stored state when a pooled line is reused. Setting and palette changes redraw an open tooltip. |
| 16 | Loot filter sharing | Unchanged code; game loading measured (below). |
| 18 | Tabs from loot filter | `stashrulescreate`: folder via `ServerStashCommands.ServerAddNewCategory`, tabs via `ItemContainersManager.AddNewStashTab` (no gold), `CanAddNewStashTab` respected (limit 200). Rule → tab links per filter in `<game>/EpochPact/stash-rule-tabs.txt`. |
| 17 | Tabs by filter rules | Priority: `StashPriority(Potential, Rarity, Type, Class, Level, Faction, Uniques, Corruption)` built only with the game's constructors and `Deserialize(int[])` methods from the filter condition's own getters (`GetTypes/GetSubTypes`, `GetEligibleFactions`, `GetUniqueIds`). Assign/clear via `ServerStashCommands.ServerSetTabInfo`. Exact sort: `smartloot::RuleFor` (`ItemFilter.Match`) → linked tab → `ItemContainer.TryAddItem` after `TryRemoveItem` from the inventory, put back on failure. |
| 18/17 | UI refresh | `sendStashMetaDataToClient` + `StashTabbedUIControls.UpdateTabsUIAsync(folders, List<StashTab>)` (list created with `Activator.CreateInstance`). |
| 20 | Font | `TMP_FontAsset.CreateFontAsset(family, "Regular", 90)` from `Font.GetOSInstalledFontNames`; original fonts added as fallbacks; per-text original font, material, size, min and max stored; `TextMeshProUGUI.OnEnable` detour for later texts. |

## Real game (isolated `EpFeedbackTest` clone, test core)

All sessions restored the 54 original save files byte-verified afterwards and
reinstalled the player core.

- **3** Overlay at opacity 1, zoom 0.5 / 1 / 4: the overlay icons spread wide at
  0.5, medium at 1, close around the character at 4 (screenshots).
- **5** A Refuge Helmet with three T2 affixes: tags + coloured lines, then a
  palette change (T2 → 00FFFF) redrawn in the open tooltip, then lines only,
  then off: the three lines read exactly as the game wrote them.
- **16** `EpLiveImport` (imported) and `EpLiveImport copy` (duplicated) loaded by
  `ItemFilterManager.LoadFilter` as the active filter (9 rules each); the folder
  hash was unchanged after the test files were removed.
- **18** Spellblade filter (9 rules, Hide and disabled included): folder
  "Spellblade" and 9 tabs created; a second run created 0 (9 already linked);
  Show-rule highlight colour → tab colour 13. The open stash panel showed the new
  folder and tabs immediately and after reopening. 12 new save files were
  written by the game.
- **17** Rules with affix conditions (3 of 9) reported as "Sort only". Sort moved
  5 inventory items (4 Normal/Magic to rule 9's tab, 1 to rule 1's tab), 1 item
  matched no rule and stayed. Clearing and re-linking a tab succeeded. The
  game's own Stash all then put new Normal/Magic items into rule 9's tab by its
  priority.
- **20** 361 Windows fonts listed. Arial applied to 2,997 texts, size 1.2,
  a panel opened afterwards got the font through the hook (113 texts), "Segoe UI"
  (a name with a space) worked, an unknown font was refused with a message;
  Game font + size 1 restored all 3,001 texts. 0 faults.

## Player feedback round (same release)

| Request | Change | Measured in the game |
|---|---|---|
| Tier labels on corrupted affixes | Regular, primordial and corrupted sealed lines (`sealedAffix`, `sealedPrimordialAffix`, `sealedCorruptedAffix`) are tagged; `TooltipItemManager.ForceUpdateTooltip` after a setting/colour change. | Item sealed with the game's `AddRandomRegularSealedAffix`: sealed line `T6`, then `T8` after `MakeAffixSealedPrimordialAffix`; switching off restored every line exactly; 0 mismatches, 0 faults. A corrupted sealed affix could not be created on the test character; it uses the same code path. |
| New default palette | `FFFFFF 2E8BFF FFD700 FF5500 AA55FF 00FF00 FF0044 00FFFF` (player's palette; T2/T5 lightened from contrast 4.3/3.2 to 5.3/4.6 on the tooltip background). | Tags and lines drawn with the new colours. |
| In-game bug report (#15) | `report.cpp`: F8 (F5–F12/off) while the game is in front → `<game>/EpochPact/reports/<time>` with screenshot, info, logs, description; game notification. | `reportnow`: folder with `screenshot.png` (2.4 MB), `info.txt`, `core-log.txt`, `description.txt`; user name absent from info and log; notice "EpochPact/reports/2026-10-12_232546" (a `
` in the first version was read as a line break by the game's text). Key F9 saved and read back. The physical F8 press was not automated. |
| Version and update notice | Sidebar version; `/api/update` reads `releases/latest` once in the background; links only to the EpochPact releases page. | Unit tests (4). |
| Scroll position, navigation layout, Game setup | Per-section scroll memory (option); compact navigation grid; full setup panel only on Game setup, one status line on Overview. | JS tests. |

## Damage meter

`damagesession start|stop|reset`: the `ProtectionClass.ApplyDamage` detour adds
each hit to the attacker's type (one float bucket per second for 3,600 s);
allies are re-read once a second in the frame callback; the detour is removed on
Stop and after 10 s without readers.

Measured in Z52 (test character warped next to an enemy spawner, Ballista cast,
30 attack orders): RogueBallista 6 hits / 482 damage and the character 4 hits /
127 damage over 7 combat seconds; the Ballista was gone by the end (the live
summons list was empty) while the session kept its values. Stop: hook removed,
values frozen; Reset: empty; 0 faults. Frame time with the session running
13.33 ms mean / 14.26 ms p99 versus 13.44 / 17.67 without it (same spot).

## Performance

Test character idle in town, 15 s windows, frame gaps from the main-thread hook
(75 FPS cap):

| | Mean | p99 | Max | Frames > 50 ms |
|---|---|---|---|---|
| Features off, idle | 13.33 ms | 14.40 ms | 18.6 ms | 0 |
| Features off, inventory opened/closed every 1.5 s | 13.39 ms | 17.45 ms | 27.5 ms | 0 |
| All on (tier icons, tags, lines, minimap 1.3×, overlay 0.6, Arial ×1.1, Auto Pickup radius 0 every 0.25 s), idle | 13.33 ms | 14.21 ms | 16.7 ms | 0 |
| All on, inventory opened/closed | 13.43 ms | 18.86 ms | 53.0 ms | 1 |

The single 53 ms frame came with the first inventory opening after switching
to Arial (the font builds its characters on first use). 0 faults in all
modules.

## Crash found during development

Two of the first five stash sessions crashed the game (access violation; one in
`GameAssembly`, one in `UnityPlayer`), seconds after building tab priorities.
That version wrote condition fields and copied collections directly into
managed objects. Repeating the same sequence afterwards did not reproduce it,
and 140 priority builds with forced garbage collections did not crash, so the
exact cause was not proven. The released code builds priorities only through
the game's constructors and `Deserialize` methods; all three sessions with it
(two complete create, sort, link and Stash all runs, and the panel refresh check)
completed without a crash. No
version with direct writes was released.

## Automated

| Suite | Result |
|---|---|
| Native | 17/17 programs. |
| Python | 257/257. |
| JavaScript | 149/149 (new: stash rules panel and confirmations, font panel). |

## Limitations

- Stash tab priority cannot express affix or character-relative level
  conditions; those rules rely on Sort inventory by filter rule.
- Sort moves items from the inventory only, not between stash tabs.
- A Windows font shows lower-case letters where the game's font draws small
  capitals.
- Tab names are shortened to 24 characters.
