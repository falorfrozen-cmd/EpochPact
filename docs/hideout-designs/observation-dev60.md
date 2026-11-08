# Hideout observation and remaining fixes — 0.1.2-dev.60

Local work in fix52; no GitHub/Nexus upload. The dev.59 sources were backed up
under `../backups/hideout-dev59-before-observation`. Earlier artifacts remain.

## Findings and changes

- **Resolution changes:** reproduced a 2560×1440 → 1280×720 change where panel
  state reported the new screen but retained scale 1.504. The window and HUDs
  now rebuild automatically on a screen-size change, preserving the page,
  tab, open/closed state and the selected Hideout object. The corrected scales
  were 0.752 at 720p, 1.128 at 1080p and 1.504 at 1440p in this test profile.
- **Secondary mouse button:** placement/cancellation now use the Windows
  primary/secondary mapping consistently. A swapped-button configuration
  previously used its primary button for both placement and cancellation.
- **Editor focus:** End is now handled within the editor's active-input gate.
  F6 menus, pointer-blocking UI and an unfocused game disarm the editor; held
  keys must be released and stale transitions are drained before input resumes.
  Key-down edges are tracked locally so cancellation does not depend solely on
  the unreliable recent-press bit. This follows the documented behavior of
  [GetAsyncKeyState](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate).
- **Preview fairness:** ordinary thumbnail requests no longer preempt and
  restart each other after 250 ms. A newly hovered model can preempt background
  work; its priority is renewed while inspected. The 20-second loading limit
  measures active work rather than time waiting in the queue.
- **Click-through:** custom world services and boss doors reject interaction
  while the F6 window is open, retaining the existing edit-mode guard. Closing
  F6 restores normal interaction.
- **Test isolation:** live-session backups now also restore the boss-door
  recovery ledger. A leftover entry belonging only to the disposable clone
  from run 25 was preserved in that run's evidence folder and removed from the
  live runtime. No personal character ledger entry was removed.

## Verification

Last Epoch 1.5.4, Windows x64, disposable offline EpFeedbackTest only.

- All **27 native test programs**, including **75 Hideout rule assertions**,
  passed. Focus rearming, consumed recent-press bits, swapped buttons and
  preview scheduling have explicit rule tests.
- **30 Python tests** passed, including session safety and package checks.
- `remaining-27`: a real station listener was blocked through the F6 window,
  then opened Stash after F6 closed. The same native interaction path was used
  for both checks.
- Three ordinary previews were deliberately delayed by 800 ms each. They
  completed in 2.645 seconds with **zero preemptions**. A separate priority
  request preempted background work and all queued models completed. Run 26's
  initial priority test used a non-priority request by mistake; that failed
  fixture was corrected and repeated in a fresh run, rather than counted.
- Automatic changes between 720p, 1080p and 1440p passed with F6 both open and
  closed, without issuing any layout/text-scale command. The selected object
  stayed selected, and native toolbar bounds remained inside the viewport.
- The dev.59 interaction regression was repeated: five templates, all three
  station hit boxes, Stash/vendor panels, 40 previews, dead-texture recovery,
  20 window cycles and UI-root destruction/rebuild. Fault and soft-fault counts
  stayed at **zero**.
- Run 27 closed normally with **exit code 0**. All 68 original save files were
  restored and byte-verified; actual preferences were unchanged. Runtime
  restoration also covered the recovery ledger.
- `player-28` repeated the final smoke check using the exact normal player
  DLL included in the ZIP. All five templates loaded 60 objects each without
  failed models; production thumbnails, add/rotate/cancel, the editor toolbar
  and normal `hideout.txt` persistence passed. Private renderer/UI fixtures
  were absent. Actual world and completed-thumbnail captures were inspected.
- That final player session also closed with **exit code 0**. All 68 original
  save files were restored and byte-verified, and real preferences were
  unchanged. The tested player DLL was then installed with a separate backup
  of the previous dev.59 DLL; the game was left closed.

Evidence lives under `research/hideout-editor-2026-11-07/remaining-27`,
`research/hideout-remaining-check-27.log`,
`research/hideout-interaction-regression-27.log`,
`research/hideout-native-dev60-final.json`, and
`research/hideout-python-dev60.log`.
The final distribution smoke and shutdown evidence is in
`research/hideout-editor-2026-11-07/player-28/player-smoke.json`,
`research/hideout-player-finish-28.log`, and `research/hideout-dev60-install.json`.

## Observation limits

Run 25 recorded no unexpected native-menu opening during idle, arrival,
library display, ten window cycles, screenshot capture or closing Stash. A
later Formosus transition occurred without an IPC boss-start command, but
physical input at that time was not established. The user subsequently
confirmed they were not using the game. A non-click callback vulnerability
was then reproduced and corrected in dev.62; see `door-input-dev62.md`.
The historical callback itself was not traced, so its exact origin remains
unproven. Run 27's separate 45-second idle observation stayed in the Hub
and recorded no menu or door events. Temporary call tracing exists only in
the private build, never in the player package.

Live checks use native IPC, collider rays, actual render captures and controlled
preview delays. Physical swapped-mouse hardware and physical End key delivery
were not automated; those changes were validated by stateful native rule tests
and integration checks around the same editor lifecycle. This is not a claim
that every one of the 6,134 catalog models or every input/display setup works.

## Player artifact

`dist/hideout-dev60/EpochPact-0.1.2-dev.60-InGame.zip`

- ZIP SHA-256: `081a2ee623fc4fe985e8bb9ebf32c8abcdedc8ee152e460cda3fe24e952d9f6f`
- version.dll SHA-256: `a05efad38cc7012f4409d70883f35040226c15c9193d7b949c07d57cb00b1702`
- Eight allowlisted entries, checked against the manifest and source payload.
- No tests, tracers, SDK, debug tools, private logs, saves or game assets.

The previous fixes and limits remain documented in `interaction-dev59.md`,
`layout-dev58.md` and `verification.md`.
