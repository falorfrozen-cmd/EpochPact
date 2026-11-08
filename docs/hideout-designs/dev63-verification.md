# EpochPact dev.63 — Hideout verification

Completed 8 November 2026 in `C:/Users/falor/EpochPactLab/fix52`.
Final player ZIP was tested inside real Last Epoch, closed normally, restarted,
then tested again. The exact verified DLL is installed; the game is closed.

## Delivered

1. Visible object/selection deletion, protected exit, cleanup of green ghosts
   through the HUD, keyboard and Placed objects list.
2. Up to 50 completed edits per game session: Undo/Redo buttons, Ctrl+Z,
   Ctrl+Y and Ctrl+Shift+Z, redo branching, reset/template recovery, and
   rollback when an atomic layout write fails. One recovery snapshot survives
   restart; full 50-step history is session-local.
3. Exact position/height/rotation/scale, grid snap, adjustable Q/E rotation.
4. Ctrl-click multi-selection (32), duplicate decoration (Ctrl+D), groups
   (Ctrl+G / Ctrl+Shift+G), measured alignment and object/group locking.
   Gameplay input is restored after editing. Functional stations stay unique.
5. Full renderer-footprint checks and commit-time native NavMesh checks.
   New loss of access to a previously reachable station/gate, or trapping the
   current player, rejects the placement and restores the previous geometry.
   Whole-selection previews become invalid together. Existing broken imported
   layouts can still be repaired; this guard does not pretend they were valid.
   Smaller ground changes reject models that would extend beyond the edge.
6. Optional mouse selection of floor/path pieces, without excluding flowers
   or other walkable decoration; Placed objects always provides direct access.
7. Saved-template thumbnails/hover previews rendered from temporary data,
   content-revision cache keys, named overwrite confirmation, direct Rename
   and Delete controls, and collision protection. Previewing never applies a
   layout or writes a character save.
8. Library favorites, recently used models, detailed asset-name filters,
   cached lowercase search, alphabetical/recent sorting and pagination.

Pending copies cannot be grouped, aligned or locked before placement.
Disabled UI controls and backend guards preserve their originals/green ghosts.
Legacy at/move/here commands now move the preview rather than discard it.
Cancel validates the edit-start snapshot, disposes its old-ID preview first,
then restores the exact layout; it never commits a pending placement.

## Evidence

| Check | Result | Evidence directory/file |
| --- | --- | --- |
| Python design/package/assets | 20 passed | research/hideout-python-dev63-final.log |
| Python session/restore | 36 passed | research/hideout-session-python-dev63.log |
| Native programs | 27 passed, including 103/103 hideout rules | research/hideout-native-dev63-final.json |
| Final native history | 12 checks passed | cancel-guard-43/history-checks.json |
| Final native tools and Cancel | 9 checks passed | cancel-guard-43/tools-checks.json |
| Library, bounds and real NavMesh rollback | 4 checks passed | cancel-guard-43/library-safety-checks.json |
| Actual saved-card UI callbacks | 4 checks passed | final-private-38/template-ui-checks.json |
| Reload and error-hint layout | 3 checks passed, 720p/1080p/1440p | final-hints-39/final-hints-checks.json |
| Door/input regression | 17 non-click rejects, foreign/null/background/F6/edit rejects, Stash/vendor success, 45s idle, explicit boss success | final-private-38/door-input-checks.json |
| Exact final player ZIP | Protocol 3, private QA absent, five 60-object templates, previews, add/rotate/Cancel, zero panel faults | player-44/player-smoke.json |
| Exact final player restart | Normal close 0; layout, grid, angle, favorites/recent and named template preserved byte-for-byte; preview rendered after reload | player-44/player-persistence-checks.json |

Evidence directories in this table are under
`C:/Users/falor/EpochPactLab/fix52/research/hideout-editor-2026-11-08`;
research/*.log and native-program results are relative to the repository root.
Private fixtures invoke the real editor/UI callbacks, renderers and NavMesh.
The obstruction test adds a controlled BoxCollider in the test build; it does
not claim that every catalog asset has that collider. These fixtures are absent
from the normal player DLL. All 6,134 catalog entries are browseable; not every
individual asset was manually placed. Static-only/effect-only asset constraints
remain documented in the player README. Maximum layout size remains 400 objects.

Earlier runs caught and corrected a real pending-preview Cancel regression.
Other failed attempts were fixture assumptions (a nonblocking pillar, ghost
readiness, or a hint that fit one line). Their logs remain available in the
worklog and are not counted as passing checks. Only the verified ZIP below
should be shared; earlier dev.63 candidate folders are superseded.

## Artifact and installation

- Version: **0.1.2-dev.63**
- ZIP: `C:\Users\falor\EpochPactLab\fix52\dist\hideout-dev63-verified\EpochPact-0.1.2-dev.63-InGame.zip`
- ZIP SHA-256: `390f4c7dad2e603d733f5e309042e1b423f4bfdd0c2e7bf164f31c33a32812d0`
- Runtime: `version.dll` only; eight allowlisted archive entries in total.
- Runtime SHA-256: `d22cb0f5622d545092f02ff28b3c8d18ba7185e1a11d67a697e8beba2d18e2f7`
- Installed: `C:\Program Files (x86)\Steam\steamapps\common\Last Epoch\version.dll`
- Previous DLL backup: `C:\Users\falor\EpochPactLab\backups\hideout-dev63-before-install\version.dll`
- Package bytes match the build, tested extracted DLL and installed DLL.
- No tests, SDKs, debug tools, private logs, character saves or game binaries
  other than the project's own native runtime are in the ZIP.
- Final shutdown: **0**; restart shutdown: **0**.
- Original saves: **68 restored and byte-verified**; real preferences unchanged.
- No GitHub/Nexus publication was performed in this task.

Source/build hashes: `research/hideout-dev63-source-checkpoint.json`.
Implementation/work history: `docs/hideout-designs/editor-completion-worklog.md`.
