# Hideout editor verification — dev.57

Last Epoch 1.5.4, Windows x64. Local build only; no GitHub or Nexus upload. The pre-existing staged and unstaged work was preserved. The older dev.56 ZIP and source/runtime backups remain available.

## Player artifact

Version **0.1.2-dev.57**. Standard unencrypted ZIP, eight allowlisted entries; runtime is the single merged **version.dll**. No EXE, Python, SDK, private fixture, log, save or game asset is distributed. The game supplies all model/texture assets.

Package: `EpochPact-0.1.2-dev.57-InGame.zip`. ZIP SHA-256: `be83842e2152223fc275993d78073b18259b9ac1749e8bd7fb551d9c3e228755`.

Installed/packaged DLL SHA-256: `263e26c9dc63a25fcb1c32fb3dcbcf6926a2db13b069f1d88a3850eddc43cc43`. The ZIP manifest, file sizes and SHA-256 entries were verified after packaging.

## Checks

- Full normal player build succeeded. All 27 native test programs passed, including 59 hideout rule assertions. Twenty Python tests passed with the newly built native i18n executable enabled (Turkish and Russian cases).
- Actual native scene audits: five compositions, 60 objects each, zero failed/pending models, zero remaining decoration MonoBehaviours, zero outside visual bounds, all 16 service/door destinations reachable. The Ossuary audit was repeated after moving its final gate positions; the applied coordinates are recorded separately.
- Gates: the crystal animation root was inspected directly, frozen and rotated independently of the upright arch. The final in-game screenshots show upright cores.
- Go to hideout was invoked through the actual in-game UI from Z32 in the private renderer run; the normal player DLL also traversed from Z32 through the existing Monolith Hub route.
- Placement transactions passed: original transforms and file bytes stay unchanged while holding; invalid drops are refused; valid drop persists the transform; whole-edit Cancel restores exact prior bytes; pending new models are excluded from disk; object cancel removes only the pending addition.
- Real mouse test: selected Stash, moved the cursor, observed a translucent green copy with the original still in place, rotated with the toolbar, dropped it at the new position and cancelled another selection with right-click. The player remained at exactly the same position throughout. No native attack occurred after input blocking was added.
- Normal player run 14: protocol-3 connection; all five 60-object layouts; template thumbnails; add/rotate/cancel; isolated hideout.txt persistence; private QA commands absent. Normal shutdown returned **0** and all 68 original save files were restored byte for byte; actual preferences were unchanged.

## Preview performance

In run 09, 41 individual model previews completed in at most 1.789 seconds (median 0.303 seconds); eight evicted previews reloaded from the raw-pixel disk cache in 0.049–0.065 seconds. In run 13 the five template previews took 0.288–0.887 seconds. These measurements include IPC and are specific to this machine/cache state. They are not a maximum promised for every asset or PC. Completed previews survive layout changes and use a versioned disk cache. Heavy/unsupported assets have an explicit failure state rather than an endless queue.

## Loaded geometry and paths

All anchors satisfy the authored shape equations with a 1.5-unit edge-noise allowance. Actual renderer bounds were additionally checked in the game. The native width is a conservative 0.75-unit raster measurement for **player centres on the baked NavMesh**, already reduced by the agent footprint; it is not an exact wall-to-wall width. Each destination also passed a complete native path query. A whole-canopy planning envelope cannot certify the forest paths although the actual trunk colliders leave them open.

| Layout | Objects | Outside centres / visual bounds | Closest gate centres | Minimum sampled centre clearance |
|---|---:|---|---:|---:|
| The Amberleaf Sanctuary | 60 | 0 / 0 | 10.352 | 1.06 |
| The Copperworks Atrium | 60 | 0 / 0 | 9.0 | 0.44 |
| The Coral Observatory | 60 | 0 / 0 | 8.3 | 0.44 |
| The Ossuary Cloister | 60 | 0 / 0 | 8.846 | 0.44 |
| The Astral Conservatory | 60 | 0 / 0 | 8.0 | 1.94 |

## Actual game screenshots

### The Amberleaf Sanctuary

![Actual Last Epoch screenshot](screenshots-dev57/verdant-sanctum.png)

### The Copperworks Atrium

![Actual Last Epoch screenshot](screenshots-dev57/ember-foundry.png)

### The Coral Observatory

![Actual Last Epoch screenshot](screenshots-dev57/tidebound-archive.png)

### The Ossuary Cloister

![Actual Last Epoch screenshot](screenshots-dev57/ossuary-court.png)

### The Astral Conservatory

![Actual Last Epoch screenshot](screenshots-dev57/astral-conservatory.png)

### Cursor placement

![Actual green placement copy with original Stash unchanged](screenshots-dev57/mouse-green-copy.png)

### Template previews

![Actual template thumbnails](screenshots-dev57/templates-ready.png)

## Observed shutdown limitation

Runs 12 and 13, which used the Windows desktop inspector, exited with **0xC0000005** at **UnityPlayer.dll+0x109ad1**. The dump shows a null dereference immediately after `UiaDisconnectAllProviders`, before `UiaReturnRawElementProvider`; no EpochPact frame appears in the sampled return addresses. Run 14 used the identical normal player artifact without the desktop inspector and exited normally. This strongly matches Unity **UUM-146676**, documented in the [official Unity 6000.5.5f1 release notes](https://unity.com/releases/editor/whats-new/6000.5.5f1). This is an evidence-based attribution, not proof that every future shutdown issue has this cause. The mod does not patch the game engine to conceal it. All save restoration checks passed even in the failed shutdowns.

## Limits

- The full 6,134-model catalog is searchable. It has not been individually play-tested; the curated 40-model set was rendered and inspected. The known pink Mossy Rock Floor and bare-trunk models were excluded from authored compositions.
- Imported material/pose/collision quality varies. Decorations run no source scripts, animation, audio or particle simulation; a live skill or effect prefab may not make useful scenery.
- Existing personal hideouts are preserved. Select a new template explicitly to replace one; Undo retains the previous layout.
- The physical controller and every possible DPI/resolution/input-device combination have not been tested. The current physical mouse checks used the active 2560×1440 game resolution.
