# Crafting UI validation — 12 October 2026

This pass reorganizes the Crafting interface. It reuses the existing Python APIs,
serial IPC queue, native crafting commands and recovery snapshots. The player
core and loader bytes are unchanged from dev.9.

## Player workflow

- **Forge Settings** contains material preservation, FP cost and glyph chances.
  Advanced forge actions and reset controls are collapsed initially. Open sections
  remain open after refreshing or completing an action.
- **Materials** contains one explicit stock action, with the chosen minimum and
  character shown before confirmation. It does not enable infinite materials.
- **Custom Items** follows four steps: select equipment, choose a template,
  check searchable affixes, then review and create. Extra mixed effects are
  optional. Search and category/theme changes preserve selections without writes.

## Automated checks

| Scope | Result |
| --- | --- |
| All six JavaScript test suites | 115 passed |
| UI HTTP bridge and existing loot/crafting backend suites | 68 passed |
| Folder packaging and desktop startup suites | 14 passed |
| Final compiled dev.10 EXE, isolated installer fixtures and HTTP assets | 21 checks passed |
| JavaScript syntax and Git whitespace checks | Passed |

Fixtures cover serial glyph writes and explicit game-default reset, hidden
selected checkboxes, invalid/stale recipes, seven regular versus six mixed
affixes, the 64-effect limit, category control coverage, draft preservation,
bounded picker rendering and cached effect formatting. These are fixture results,
not a claim that every possible custom recipe was tested in gameplay.

The compiled package includes and serves the current crafting-model.js, app.js
and other resources. Its file inventory and every file hash were checked. Installer
checks use non-runnable fixtures and do not constitute a new live gameplay test.

## Live crafting

Tested the source UI through its normal UiBridge job pipeline against Steam build
25816319, public game version 1.5.2.1 / internal 1.5.2.2. A private research build
provided guarded fixture setup/inspection commands for the isolated **EpCraftTest**
character only. These helpers are excluded from the player package; production
crafting behavior was not replaced by simulated responses.

| Action | Measured result |
| --- | --- |
| Apply preservation, FP factor 0, Hope 100% and Despair 100% | Native craftread matched all six values; writes followed the shared queue |
| UI Craft once with a shard | Affix 505 advanced T2 to T4; FP stayed 63; 20 shards remained; one craft recorded; normal FP loss 6 became 0 |
| UI Craft once with a rune and Hope | One further craft; rune and glyph counts stayed 1; both refund counters advanced to 1 |
| Restore normal crafting | FP factor 1, glyph chances default, preservation off, no crafting hooks active |
| UI stock minimum 2 | All 508 shard, 12 rune and 5 glyph types reached max(previous count, 2) |
| Repeat stock target via native backend | updatedKinds 0; no additional materials |
| Cancel custom item confirmation | Original T4/T2 affixes unchanged |
| UI Exalted custom item | Two selected T7 affixes, rolls 255; inspected native serialized item |
| UI Calamity + one mixed effect, midpoint | Template 0, two T7 affixes with rolls 128 and one recipe affix; recipe definition and stat output inspected |
| UI Isadora's Revenge, maximum | Template 5, two T7 affixes with rolls 255; no mixed recipe affix |

All six confirmed UI save actions succeeded and created recovery backups. Exalted,
Unique and Set examples prove these measured paths, not every item/template/effect
combination. Hope was exercised in a real craft; Despair's configured value was
read back, but this pass did not perform a new Despair seal craft.

Browser checks found no horizontal overflow at 900×700 in Chronoforge and
1280×720 in Void Atlas. Selection searches, category changes and cancellation
were exercised through the actual controls. The crafting header is shorter to
leave more usable vertical space.

## Shutdown and restoration

After stopping the test UI's polling, the game was closed through its normal
WM_CLOSE flow. Exit code was **0 (0x00000000)** in this session. All **54** original
save files were restored and verified against their pre-test SHA-256 values;
three test files were archived. Original runtime files and any pre-existing
workshop recipe sidecar were restored as well.

This successful shutdown does not erase the intermittent full-game exit issue
described in [the earlier validation record](player-feedback-validation-2026-10-12.md).
It is separate from application startup/installer checks. No new VirusTotal
clearance is claimed for the rebuilt EXE.
