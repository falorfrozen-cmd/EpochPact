# In-game port checkpoint — 20 October 2026

The owner stopped Computer Use with physical Escape during selection of the second test window.
No further Computer Use input was sent. The test session was closed and restored before stopping.
**This is an unfinished development checkpoint, not a release or a completed port.**

Base: `ingame-ui`, starting at `1fa1bdb`. No merge, publication or GitHub push was performed.

## Added

- Character browser: three tabs, categories, search, 24-row pagination, favorites and hidden-stat
  preferences. Its static names, keys, modes, units and bounds are generated from the desktop's
  actual `ui/stat-model.js` by `tools/ingame_stat_catalog.js`.
- Native stat modal: current contribution read, Flat/Increase/More modes, percentage scaling,
  typed number, slider, +/- controls, advanced mode selector, apply and reset. Commands use the
  exact existing `statraw` key and verify the offline character's id and name before submission.
- Custom modal infrastructure: isolated watched-input owner, stale-reply token, focus cleanup,
  modal select coordinates and input routing.
- A non-input live-session helper, `tools/ingame_live_session.py`, using the existing isolated
  EpFeedbackTest clone/restore mechanism. It refuses an already running owner game, binds reads
  to PID plus creation time, and guards the restore against a different process/manifest.

## Checks actually performed

| Check | Result / limit |
|---|---|
| Python suite | 356 passed before the final modal/lifecycle repairs; the three subsequently added session safety tests also passed separately |
| JavaScript suite | 237 passed, including two new catalog/model parity cases |
| Asset tests | 10 passed in the full Python run, including translator/native integration; earlier standalone run skipped the unavailable translator binary |
| Native builds | Research and two isolated player builds succeeded; the new stat fixture passed with assertions explicitly enabled despite `/DNDEBUG` |
| All native executables | Not yet run as a final complete suite |
| Initial live background build | 0 faults; 482 measured texts, 0 line-count differences |
| Native `panel kern` | Executed and recorded; body 1632.07 px, 9,236 pair records; enabling kerning did not change that width. Kerning parity with Chrome remains unresolved |
| Initial live Character action | Real clicks/typing applied +5 Dexterity to EpFeedbackTest; exact native key `22:0:0:0` reported owned added=5 and gameValue=5 |
| Initial live Reset | Real click restored owned contribution and gameValue to zero |
| Initial live slider / mode | Actual mouse drag changed Increase draft to 51%; percent mode showed -100 to 100 slider bounds |
| First live shutdown | **Failed: 0xC0000005**; core log also recorded incomplete hook restoration, error 5. Do not classify this as fixed |
| Second live session | New player DLL loaded and UI built with 0 faults. Computer Use was stopped before new UI interaction checks |
| Second live shutdown | Normal exit code 0. This session did **not** open the new stat modal, so it does not prove the first crash is resolved |
| Save/runtime restoration | Both sessions restored 54 original save files byte-for-byte, archived three clone files, restored all original runtime file hashes, and left real preferences unchanged |

## Repairs after the first live run — compiled, still need live verification

1. The modal select opened behind its dialog. `OpenList` now raises its Unity sibling and modal
   hit routing prioritizes the popup. Check visible options and selecting More.
2. Reset cleared the actual bonus but left the typed draft at 5. It now also resets the draft.
3. The browser's shared panel frame/padding was missing. It now follows the workshop group pattern.
4. New global editor `Pin` handles were not part of IL2CPP cleanup. `ReleaseStatRoots` now resets
   them before runtime shutdown. This is a plausible shutdown defect; its causal role in the
   observed crash has not been established. Reproduce modal use and normal shutdown to verify.
5. The initial very short Computer Use F6 press was not detected. A best-effort low-bit latch was
   added alongside the held-key edge. Real F6 open/close still needs retesting.

## Evidence and isolated builds

All paths below are under `C:\Users\falor\EpochPactLab\builds`:

- `ingame-chatgpt-research` — first research build.
- `ingame-chatgpt-player` — first player build.
- `ingame-chatgpt-player-2\player\version.dll` — latest compiled player build, **not a player release**.
- `ingame-chatgpt-live-2026-10-20\session.json` — first restoration evidence and failing shutdown.
- Same directory: `baseline-probes.json`, `stat-applied.json`, `character-bonus-applied.png`,
  `modal-popup-layer-issue.png`.
- `ingame-chatgpt-live-2026-10-20-2\session.json` — second restoration evidence, exit code 0.
- `ingame-chatgpt-desktop-reference` — actual desktop `stats` and `stat-dexterity` screenshots/rects.
- Build and test `.log` files have the corresponding `ingame-chatgpt-*` names.

The preview server was stopped. The test game was closed. The original installed runtime was
restored, so reopening the owner's game does not install this development DLL.

## Follow-up: Character parity and background checks (after ee72d08)

The owner now permits Computer Use only when necessary and prefers background native commands.
This follow-up used **no mouse/keyboard input and no window activation**. One passive Computer Use
capture did not show the target game, so it is **not valid visual evidence** and was not saved.

Implemented (not a claim of finished pixel parity):

- Full-width Flat/Increase buttons, desktop track/thumb sprites, bonus-card gradient, proper
  184×44 stepper, pp unit label, category chips and transparent favorite stars.
- Valid drafts survive mode changes and Refresh. Each mode falls back to its actual contribution
  with the desktop's percentage scale. Successful Apply clears only that mode's draft; successful
  Reset clears all drafts. A refused Reset no longer zeros the displayed draft prematurely.
- Summaries use the desktop's units for every mode even when an alias exposes only one mode.
  Numbers use en-US grouping and up to six decimals. Linked-stat notes are limited to sheet rows;
  derived entries include “Calculated by the game”.
- Eight Summons minion/companion shortcuts, with the desktop's exact alias keys. No new game
  property mapping was invented.
- Read-only view commands: `panel page:stats`, `panel page:summons`,
  `panel stat:sheet:0`, `panel stat:aliases:64`, `panel stat:raw:0`, and `panel dismiss`.
  An optional `:extra` opens a row's existing secondary modifier. Unknown addresses are rejected.
  These commands do not simulate arbitrary clicks or execute stat/save writes.
- `tools/ingame_stat_live_check.py` verifies the isolated process/character before running those
  view checks. It explicitly does not replace input, visual, Apply/Reset or dropdown tests.
- Recovery success is now separate from game exit success: `ingame_live_session finish` restores
  files first, records `normalShutdownConfirmed`, then fails its process if shutdown failed.
  A second call cannot turn an already-restored crashed test into a passing result.

Final compiled player: `builds/ingame-chatgpt-player-4/player/version.dll` (outside this repo).
It includes the read-only navigation commands and all native changes above. No release or push.

| Follow-up verification | Result / boundary |
|---|---|
| Complete Python suite | **361 passed**, including 10 asset/translator integration cases and five isolated-session guard/recovery tests |
| JavaScript suite | **238 passed**, including exact desktop metadata/units and minion-shortcut coverage |
| Native suite | **24 executables exited 0** in player-4; the translator executable also ran through Python's asset tests |
| Live sessions 3, 4, 5 | Each opened 11 stat views, obtained valid current contributions, found all eight minion shortcuts, rejected a non-view address, and removed the modal's watched field on close; **0 UI faults**, **498 measured texts / 0 line-count drift / 0 first-pass errors** |
| Native modal input lifecycle | One watched value field per modal after repeated rebuilds, none after dismiss. This tests allocation/cleanup, not real typing/focus |
| Shutdown session 3 | **0xC0000005** after passive Computer Use capture; recovery succeeded |
| Shutdown session 4 | **0xC0000005** with no further Computer Use calls, while its previous kernel/client remained alive; recovery succeeded |
| Shutdown session 5 | After resetting the Computer Use kernel, the **same player-4 DLL and same 11-view test passed**, normal exit **0**, with the stat modal still open at shutdown |
| File safety | Each restored 54 original save files byte-for-byte, archived three clone files, restored original runtime hashes, and left real preferences unchanged |

### Shutdown investigation — do not hide the failing runs

WER dumps from the initial failure (PID 22636) and follow-up session 3 (PID 51136) resolve to:

`UnityPlayer.dll+0x109ad1 RuntimeStatic<PlatformAccessibilityManager,0>::StaticDestroy+0x51`

The fault reads through a null `rcx` at offset `0xe0`, during Unity `RuntimeCleanup`, after our
IL2CPP shutdown hook has returned. Matching Unity symbols were downloaded from Unity's official
symbol store following its [Windows debugging instructions](https://docs.unity.com/en-us/engine/6000.6/manual/platform-specific/windows/develop/debugging/instructions).
The decoded session-3 stack is in `builds/ingame-chatgpt-live-2026-10-20-3/crash-symbols.txt`.
The dump/PDB files are development evidence, not player-package content.

The clean session after resetting the Computer Use kernel suggests a retained Windows
accessibility client may trigger this path. **This is an inference, not a confirmed engine bug,
not proof of a fix, and not a reason to discard the failures.** No game binary or accessibility
setting was patched. The hook-restoration error 5 also occurred during the clean exit, so that
message alone does not explain the crash. The explicit stat-root cleanup remains correct, but
it did not by itself eliminate the failure.

The valid final no-input run is `builds/ingame-chatgpt-live-2026-10-20-5/session.json` and
`stat-view-checks.json`. Logs use the `ingame-chatgpt-*-4-tests` / `native-4-tests` names.
The game is closed and the original installed runtime is restored.

Still unverified: new visual parity, long-label/modal overflow, advanced dropdown layering,
actual F6, typing/slider interaction after the changes, and applying/resetting the updated modal.
Settings and the remaining pages below are still incomplete. Nothing here makes the full port
finished or suitable to release.

## Resume in this order

1. Read `HANDOVER-ingame-window.md`, this checkpoint and port-map section 5. Do not load Falor.
2. Review the Character screenshot against the desktop reference: it is functional but visual
   parity is **not** complete. Basic mode buttons need full-width distribution, the bonus card
   needs the desktop gradient/slider look, and category/star styles and spacing need refinement.
   The browser's shared frame repair needs a new image. Derived-stat notes and precise locale
   formatting also require review.
3. Prefer read-only background commands for live tests; use Computer Use only when actually
   necessary, per the owner's latest instruction. Its accessibility-client/shutdown interaction
   needs isolation before relying on that tool's tests. Use a fresh output directory:
   `py -3 -m tools.ingame_live_session start <new-output-folder> --loader <latest-player-version.dll>`.
   Close and restore using `py -3 -m tools.ingame_live_session finish <same-output-folder>`.
4. Recheck modal dropdown layering, fields/focus after repeated close/reopen, all three browser
   tabs, search, favorites/hidden persistence in the isolated preferences file, percentage/raw
   units, slider, reset and F6. Verify the test clone identity before changing a stat. Test shutdown
   after actually opening/editing the stat modal; preserve crash evidence if it recurs.
5. Rerun the Python, JavaScript and all native fixture suites against the final source/build.
6. Continue the remaining port-map pages: Settings/startup replay, loot filter file operations,
   Monolith Navigator, Unique Atlas, Stash Assistant, game setup status,
   global search, hide-part controls, HUD overlays, gamepad and other resolutions.
7. The three save-writing Crafting actions remain unexecuted, per the owner's safety rules. The
   fixed original full live harness's final Custom Items step has not been rerun in this work.

Never describe the remaining pages, the modal repairs, or the shutdown issue as completed from
these build results alone. No release/merge/announcement without the owner's approval.
