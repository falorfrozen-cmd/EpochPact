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
