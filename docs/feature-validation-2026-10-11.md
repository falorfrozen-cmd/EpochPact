# EpochPact 0.1.2-dev.2 validation — 11 October 2026

This candidate adds the Auto Pickup tooltip fix to the build controls documented
in [build-controls.md](build-controls.md). It is a test candidate, not a certified
public release. The compatibility target is Steam build 25816319, displayed game
version 1.5.2.1 and internal version 1.5.2.2.

## Auto Pickup: reproduced cause and fix

The old guard checked `TooltipItem.highlightedTooltipItem`. A real ground-item
card in the newer `LE.UI.Tooltips.TooltipSystem` can be open while that legacy
field is null. Consequently the old mod scan continued collecting equipment
being inspected. Collection removes the label that opened the card.

`items.cpp` now also uses the cached native `get_ActiveItemTooltipItem` getter.
The guard runs before and after the vanilla tick and before every mod pickup.
An open card or unreadable state pauses mod collection without consuming its
scan interval. Closing the card resumes collection. The vanilla distant-pickup
tick is still called; deliberate/native pickup is not disabled.

Live comparison on an isolated offline clone:

| Measurement over twelve half-second samples | Old guard | Fixed guard |
| --- | --- | --- |
| Real native card open, legacy hover absent | Yes | Yes |
| Mod pickup calls while the card was open | 8 | 0 |
| Ground equipment remaining | 0 of 8 | 8 of 8 |
| After closing the card | Already collected | All 8 collected |

The test opened the game's real ground-item card through native IPC and held its
native tooltip lock. It did not move the user's mouse. This establishes the
previously missed V2 state and pickup pause/resume; an unlocked physical mouse
or controller hover was not independently exercised. Twenty native fixture cases
also passed, including legacy/V2 inspection, mid-tick closure, faults, destroyed
UI objects, online/unloaded gates, interval handling and managed root cleanup.

## Build controls and real interface

- Thirty live assertions passed through the shipping Python bridge and native
  game commands. All sixteen build controls applied and read back; skill 20 plus
  one gear level gave 42 points at 2x, Weaver scaling and projectile count applied,
  five maximum-roll blessings were generated, temporary summon expiry extended,
  the skill-XP hook ran and camera distance changed. No build-control faults were
  reported. Session cleanup restored all sixteen controls and native defaults.
- Eight interface assertions passed using the actual frontend DOM, local HTTP
  server and native IPC. Skill XP, extra projectiles and zoom auto-applied after
  direct edits. Both themes rendered, all sixteen controls were reachable, startup
  and theme changes sent no gameplay/save mutations, and close cleanup restored
  the three edited controls. No browser page errors occurred. This was a headless
  browser test, not an interactive WebView2 window test.
- Existing loot/crafting live checks passed: real forge Hope/Despair boundaries,
  Forging Potential costs, shard preservation, filter show/hide, material pickup,
  LP/T7 selection, normal inventory limits and neutral hook removal. Reported
  crafting and loot fault counters stayed at zero.

## Automated and compiled-package checks

- 188 Python tests and 79 JavaScript tests passed.
- All fourteen native fixture executables passed. The hook stress fixture passed
  300 install/remove cycles under a running caller with no incorrect results.
  An initial runner limit of 90 seconds was too short; the fixture subsequently
  completed and was repeated with a 300-second limit. This was not a game hang.
- The compiled onedir EXE passed twenty installer/HTTP checks: exact DLL bytes,
  read-only source and owned-update files, recovery copies, custom Unicode paths,
  conflicting-loader/unsupported-build rejection, token/Host/Origin validation,
  occupied-port fallback, saved-path restart and both theme assets. These tests
  used non-runnable fixture game files; they did not test interactive UAC approval
  or the native file picker.
- All 944 payload files matched their recorded sizes and SHA-256 values. Research
  DLLs/commands, test fixtures, developer SDKs, personal snapshots, game binaries
  and save files are excluded from the player bundle. The included Python runtime
  is required; players do not install Python separately.

Native live probes used the research build to access isolated fixtures; the
shipping bridge/frontend paths and corresponding player native source were used.
Research commands are excluded from the compiled player DLL. Fixture tests and a
successful build do not certify every gameplay consumer or hardware configuration.

## Remaining coverage and known issue

The earlier [gameplay coverage limits](feature-validation-2026-10-10.md) remain:
boss reward-panel behaviour, each class-specific summon expiry/combat path, all
projectile conversions and custom-gear combat effects are not fully verified.
The workshop combines one complete Unique/Set template with up to seven affixes;
it is not an arbitrary editor of individual modifiers from multiple templates.

The game again returned `0xC0000005` during normal full-game exit after the two
test-clone sessions. Previous vanilla comparisons also produced this error, but
the cause is unresolved. Successful EpochPact session cleanup while the game
remains running is a separate result and does not prove a clean full-game exit.
This issue prevents a publication-ready or zero-crash claim.

No VirusTotal scan was performed for these new binary hashes. A previous clean
report for alpha.5 does not transfer to this candidate.

## Restoration and evidence

All 54 original save files were restored and compared byte-for-byte against their
original hashes. Isolated test saves were archived and removed. The original
loader, core DLL, installation metadata and disabled state were restored. The
previously uploaded alpha.5 ZIP is unchanged. No public upload was made.

Detailed local evidence is retained under `research/live/tests-20261011` and is
excluded from distribution: before/after tooltip captures, live feature results,
frontend results, loot/crafting results, native fixture logs, package verification
and save/runtime restoration manifests.

| Candidate artifact | SHA-256 |
| --- | --- |
| EpochPact.exe | `f9e7d047f0919244a1868aa8d306056908f26b1f67772473fdbe8eee9af0fe31` |
| Player EpochPact.Core.dll | `0275a7673e8743c015afabf9c3683c0fb4b1599082d2a3c15cecaa4d47c0170e` |
| version.dll | `e24f5c440d78625387bf5aba6e72be49785846f3fbd98182cef92cb1cc417a02` |
