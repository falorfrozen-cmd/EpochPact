# In-game port: follow-up live verification

These checks close the explicitly unverified items in
`ingame-port-completion-2026-10-20.md`. They were performed after the owner
authorized executing the three save actions on disposable character copies.
No mouse/keyboard automation or desktop accessibility client was used.

Game: Last Epoch 1.5.2.1, the supported pinned Steam build. Player build:
`ingame-chatgpt-player-29`; private test build: `ingame-chatgpt-ui-test-28`.
The private DLL is never included in the player ZIP.

All named build and live evidence folders below are under
`C:\Users\falor\EpochPactLab\builds`. The current player ZIP is
`ingame-chatgpt-player-29\package\EpochPact-0.1.2-dev.36-InGame.zip`.

## Save actions and real restart — passed

Evidence: `ingame-chatgpt-live-2026-10-20-15/actions-checks.json` and
`actions-transcript.json`, with screenshots of each actual confirmation.

| Action | Result |
|---|---|
| Stock all materials | All 525 catalog types (508 shards, 12 runes, 5 glyphs) reached at least 100. A second stock operation targeting 10 changed zero types and reduced no existing quantity. |
| Craft once | The real forge raised the selected affix from T1 to T2, consumed exactly one shard and reduced Forging Potential. |
| Create custom item | The UI checkbox selected affix 0; Create produced exactly that T7 affix at roll 255. The native serialization round trip passed. |
| Recovery | Each successful action retained a real recovery backup. A confirmation for another action was refused. |
| Persistence | After a normal game close and a fresh process launch, every material count matched, and the custom item's exact serialized bytes were present in the loaded save. |

Both game processes in this run closed with exit code 0. The test used
EpCraftTest with its own character-found stash, not the owner's character.

## Actual screen modes — passed

Evidence: `ingame-chatgpt-live-2026-10-20-17/runtime-checks.json` and 12 native
ScreenCapture images. The game actually switched to each resolution; these were
not simulated browser viewport sizes.

- 1920 x 1080 at 100% and 150% text size.
- 1280 x 720 at 100% and 150% text size.
- 2560 x 1440 at 100% and 150% text size.

The advanced stat dialog scrolled to a reachable Apply button in all six cases;
the Custom Items page was rendered and visually inspected. Zero native UI
faults. The original resolution and full-screen mode were restored before exit.

## Existing Echo web: Show in game — passed

Evidence: `ingame-chatgpt-live-2026-10-20-16/runtime-checks.json`.
The normal timeline panel generated a seven-Echo web on the disposable clone.
The native window's actual Show in game callback focused Echo 0 in timeline 1.
The game returned `selectionVerified: true`; the character stayed in M_Rest.
No Echo was started.

## Real combat and native damage window — passed

Evidence: `ingame-chatgpt-live-2026-10-20-20/runtime-checks.json`, the command
transcript and `real-combat-dps-hud.png`. Actual abilities were cast in Z52;
the renderer was not using preview data.

| Source | Session damage | Hits | DPS 10 s |
|---|---:|---:|---:|
| You | 726.3 | 12 | 66.9 |
| Ballista | 82.9 | 1 | 8.3 |

The native table displayed the rounded totals 726 and 83, and TOTAL DPS 75
(66.9 + 8.3). The allies aggregate was not added again. Element sums matched.
Average checks account for the backend serializing seconds and rates to one
decimal place. Stop froze the data despite another attack; Reset cleared it.

An independent per-hit health audit covered 12 of the 13 hits: You recorded
648.637 and removed 648.637 health across 11 audited hits; Ballista recorded
82.931 and removed 82.930 health. One player hit was outside the nearby audited
target set and remained correctly included in the session. No uncredited hits
or health loss outside the damage hook were observed on audited targets. This
is a new native UI integration check, not a repetition of every class/ailment
benchmark in `dps-meter-integration-2026-10-15.md`.

Two actual port defects found and fixed during this verification:

1. A remembered 430 px HUD width squeezed the full table's nine columns. The
   full table now requires 900 logical px and the compact table 560, capped by
   available screen width. This applies on load, resizing and option changes.
   The live table expanded the old narrow width and its headers were inspected.
2. The native icon validator rejected string skill IDs such as `a:ba1574`.
   It now matches the desktop's bounded ASCII key grammar. Native tests cover
   real string IDs, synthetic underscore IDs, limits and malformed keys. Both
   `c:1:0` and the real Ballista icon `a:ba1574` loaded into Unity sprites.

## Automated checks and packaging

- Python: 380 passed, using the completed native build's fixtures.
- JavaScript: 238 passed; JavaScript sources were unchanged in this follow-up.
- Native: 26 test programs passed, including the icon regression cases.
- Player ZIP: eight allowlisted files; only runtime is the merged `version.dll`.
  Archived bytes match the verified source files and the SHA-256 manifest.
- Final production audit and exact package hashes are recorded in
  `ingame-port-verification-2026-10-20.json`.
- Production live run 21 passed all 19 pages, the three Crafting tabs, search,
  native measurement, collection comparisons and remembered recording. All
  four private command probes were rejected. The game closed with exit code 0.

## Test safety and failed attempts

Successful runs restored all 54 original save files byte-for-byte, restored the
original runtime and workshop-recipes.txt, archived only the validated clone
files, and verified the real EpochPact preferences file was unchanged.

The harness now waits for the actual offline character tile, not just the
CharacterSelect state, before requesting one acknowledged load. It refuses
ambiguous tiles, another process/creation time, and any non-test character.
The restart regression initially failed because the tile was still loading;
that attempt is retained as failed, and run 15 supplies the successful restart.
An unobserved shutdown is never relabeled a successful shutdown.

Other retained failed attempts exposed test assumptions: the game's current
Screen.SetResolution overload differs from an old four-argument overload;
Z32 is a camp rather than the combat test area; and audited nearby-target
damage is a subset of the full session. The private cast fixture was also
corrected to pass two world points to CastAfterDelay. These fixture changes
are excluded from the player build. Rounding-aware average bounds replaced an
incorrect calculation that treated rounded elapsed seconds as exact.

## Remaining limits

- No physical XInput controller was attached (all four slots returned 1167).
  Physical gamepad navigation is not claimed as tested.
- TMP and Chrome font metrics can still differ; universal pixel identity is
  not claimed. Other GPUs, languages and screen configurations need their own
  checks.
- Earlier accessibility-client shutdown failures remain historical evidence,
  not a universal crash-fix claim. Successful follow-up sessions closed normally.
- Desktop/native startup preference arbitration is unchanged; keep the desktop
  app closed when using the native-only route.

No public release or merge to main is authorized by this verification report.
