# Hideout interaction and window recovery — 0.1.2-dev.59

## Reported failures and causes

1. Station models had their colliders removed, while a hidden copy of a Monolith
   rock supplied interaction. Its click surface and hover outline did not match
   the visible Stash, vendor or Cache. The station proxy now uses the visible
   model's actual world renderer bounds. The rock's colliders are disabled and
   its entire visual branch is inactive, so hover VFX cannot reveal a ghost rock.
   The proxy is rebuilt after asynchronous loading/fit and placement changes.
2. Gameplay HUDs belonged to the F6 window root. Rebuilding/destroying that
   window also removed the Hideout entry button. HUDs now have an independent
   persistent root, with separate runtime destruction and shutdown-only handle
   release. The F6 window hides the editor while open. Native game menus reduce
   the toolbar to a compact Edit/Resume entry; pressing it closes those menus
   and starts/resumes editing while retaining the selected object.
3. The reported core.log records repeated NullReferenceException failures from
   UnityEngine.RenderTexture.Release during tick hideout_results. A managed GC
   handle did not guarantee that the Unity texture's native object still lived.
   Cached textures are now checked for native validity before use/release;
   destroyed entries recover from disk or rerender. Eviction detaches the owned
   handle first, and RenderTexture.Release is called only on a live RenderTexture.

The original sources and supplied runtime log were retained under
`../backups/hideout-dev58-before-interaction-fix`.

## Real-game regression run

Last Epoch 1.5.4, isolated offline EpFeedbackTest character. No personal character
was used for mutations. `interaction-20/interaction-checks.json` records:

- All five templates: camera-to-model native collider rays hit Stash, vendor and
  Cache. Collider/renderer bounds differed by at most 0.000122 game units.
- Each template: feeding the resolved listener through the game's ObjectClick
  method opened the correct Stash and vendor panels. These were native ray and
  interaction tests, not physical mouse-coordinate automation.
- Forty curated model previews exceeded the 32-entry RAM cache. A genuinely
  destroyed native texture was recovered; eviction of a destroyed texture also
  completed successfully. No F6 window fault or soft fault was recorded.
- Twenty window open/close cycles restored the Hideout button. These use the
  same Step/Show path as F6, without injecting physical keyboard input.
- Deliberately destroying the F6 root left the Hideout button visible. Opening
  the rebuilt window and returning to its model library remained fault-free.
- Game shutdown returned 0; all 68 original save files were restored and byte
  verified; actual preferences were unchanged.

All 27 native test programs and 20 Python tests passed for the new player build.
Evidence: `research/hideout-native-dev59.json`,
`research/hideout-python-dev59.log`, and
`research/hideout-editor-2026-11-07/interaction-20`.

The normal player run `player-21` also passed all five layouts, thumbnails,
add/rotate/cancel, persistence and ten extra window cycles while holding an
object; it closed with exit code 0 and restored all 68 original save files.

The final compact-entry behavior was checked separately in `interaction-23`
using the actual native Stash panel (both native panel-state checks and PNGs).
The entry remained visible with Stash/Inventory open and after closing F6.
Pressing the actual entry callback closed Stash and expanded the editor. This
also passed with a selected object and after destroying/rebuilding the F6
window; fault and soft-fault counts stayed at zero. That game closed with exit
code 0 and restored all 68 original save files. The earlier `interaction-22`
Factions-key fixture did not reliably open a game menu; it is not counted as
proof of the native-menu case. The Stash fixture replaced it.

The exact final packaged player DLL was then verified in `player-24`: protocol
startup, entry into the Hideout, ten window cycles with native Stash open, zero
window/soft faults, and absence of private QA commands. The final screenshot
shows the Edit entry alongside native menus. Shutdown returned 0, all 68
original save files were byte-verified restored, and real preferences remained
unchanged. This was a background native integration check; physical F6 key
delivery and hardware-specific mouse/input behavior were not automated.

## Player artifact

`dist/hideout-dev59/EpochPact-0.1.2-dev.59-InGame.zip`

- ZIP SHA-256: `8aba6edd4812f0ad18052c3b2edc681e05de1b93087224afc73dbbe414e7d3dc`
- version.dll SHA-256: `e4896ee61eff1e765466e71ea983fd98bfd7d88448a1e06f536e84eb242101df`
- Eight allowlisted entries, checked against the manifest and source payload.
- No private QA fixtures, test host, SDK, game assets, logs or saves included.
- Local artifact only; no GitHub/Nexus upload in this task.

The earlier measured layout verification remains in `layout-dev58.md` and the
functional design verification remains in `verification.md`.
