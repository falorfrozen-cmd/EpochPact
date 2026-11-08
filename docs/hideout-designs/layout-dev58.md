# Hideout layout corrections — 0.1.2-dev.58

The reported editor toolbar overflow was reproduced from the supplied screenshot.
This local build fixes the toolbar and the hideout page layout; it has not been
published to GitHub or Nexus.

## Changes

- Toolbar height now comes from actual native font and button measurements.
  Mixed primary/normal buttons are centred within each row and wrap if needed.
- The selected object and placement instructions have separate measured lines.
  Long object captions use native TMP ellipsis rather than truncating UTF-8 bytes.
- An opaque toolbar on its own sorted canvas covers underlying world labels.
- The toolbar hides while native game menus are open. Editing does not place
  objects through those menus.
- Page tabs, model categories, ground choices, pagination, main actions and
  object controls use measured wrapping rows. Selecting a tab/category does not
  change its button height.
- Placed model names occupy their own full-width line above their controls.
  Saved template naming has its own full-width input and a separate action row.

## Verification

Last Epoch 1.5.4, Windows x64. All live operations used the disposable offline
EpFeedbackTest character. The user's preferences and personal hideout were not
used as the test layout.

The private native renderer run `layout-18` passed at:

| Resolution | Main interface text/layout scale |
|---|---:|
| 1280 × 720 | 150% |
| 1920 × 1080 | 100% |
| 2560 × 1440 | 140% |

For both editing and holding an object, the real native control audit found:
zero buttons outside the toolbar, zero overlapping buttons, the entire toolbar
inside the viewport, and a single-line caption. Category and page-tab hit boxes
did not overlap. Actual screenshots of categories, placed objects and both
toolbar states were inspected. Opening and closing the game's Factions menu
hid and restored the toolbar at each resolution.

Evidence: `research/hideout-editor-2026-11-07/layout-18/layout-checks.json`
and the PNG files alongside it. The test runner is
`tools/hideout_layout_live.py`; private fixtures are excluded from the player DLL.

The initial menu fixture closed only older UIBase panels. It was corrected to
also close the game's newer fullscreen PanelSystem, and the complete run was
repeated successfully. Earlier failed attempts remain in the research logs.

The layout-18 game closed normally with exit code 0. All 68 original save files
were restored and verified byte for byte, and real preferences were unchanged.
All 27 native test programs and 20 Python tests also passed. These checks cover
the reported layout regressions, not every possible display configuration.

The packaged normal player DLL was then run separately in `player-19`:
protocol-3 connection, all five 60-object layouts with no failed models,
template thumbnails, add/rotate/cancel, the visible editing toolbar and local
layout persistence passed. Private test commands were absent. This game also
closed with exit code 0; all 68 original save files were verified restored and
real preferences were unchanged. See `player-19/player-smoke.json` and
`research/hideout-player-finish-19.log`.

## Player package

`dist/hideout-dev58/EpochPact-0.1.2-dev.58-InGame.zip`

- ZIP SHA-256: `b4b0263b971a4aef88955c4da2e00758266fbceff5e52a389c40e1a3d2f00656`
- Packaged version.dll SHA-256: `74080fb0b60d1ba6f1a73c6667ba7743a40228a3d92036afce2f70adf0e6096e`
- Eight allowlisted entries; archive bytes checked against its manifest.
- Normal player build; no private QA commands, test host, SDK, saves or logs.

The earlier dev.57 functional verification and measured limitations remain in
`verification.md`.
