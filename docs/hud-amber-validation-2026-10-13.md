# 0.1.2-dev.23 validation — effects HUD and Memory Amber (2026-10-13)

Steam Last Epoch 1.5.2.1 (internal 1.5.2.2), offline. Every live session used a
fresh isolated copy of the first character (EpEffectsTest or EpFeedbackTest);
afterwards the 54 save files were restored and byte-verified and the installed
`version.dll`, core and `installed-build.json` were put back and hash-verified
(the released dev.21 runtime). The real character was never loaded.

## Effects HUD

The candidate (`8d9ded0`, handed over from another agent) was rebuilt and its
suites rerun before any change.

| Check | Result |
|---|---|
| Player effects | Real Chill/Slow/Frenzy/Haste via the character's AilmentReceiver and natural Preparation/Grace Period; game icons; timers counting down (38.7 → 35.2 s after 3.5 s); 0 faults |
| Many effects | 12 active (test-only native UI entries with real definitions): 5 rows and "+7 more active effects"; rows 3 → "+9 more"; rows 12 at size 1.5 → 6 rows (screen space) |
| Target | Pack Grole with Chill/Slow (live receiver timers) and Ignite (no timer, `—`); target loss hides the panel |
| Inventory open | HUD stays beside the inventory, no overlap |
| Area change | Z32 → Z52: HUD stays on, one new UI discovery, 0 faults |
| Off / close | Switching off removes both panels; the game closed normally (exit 0) in all four sessions |
| Cost | Steady samples 0.2–0.4 ms; first build of rows up to 2 ms |

Defects found and fixed:

- **Size 0.7 drew no text** (title, names, timers, stack counts; only icons and
  the footer were visible). Text keeps a readable minimum size while its boxes
  scaled with the HUD; TextMeshPro draws nothing in a box shorter than one line.
  Boxes are now sized from the font size in use and the font's line height,
  read from the label's own font (`TMP_FontAsset.faceInfo`: measured 1.47 × size
  with the game font). Verified at 0.7, 1.0 and 1.5 with and without descriptions.
- **Descriptions cut to one line** ("Reduces movement speed, attack speed, an…").
  They now get two lines ("…attack speed, and cast speed.").
- **The rules test checked nothing**: it used `assert`, which the native build's
  `/DNDEBUG` removes. It now uses explicit checks over 750 layout cases (text
  boxes hold a line, row contents fit, panels on screen and apart); two
  deliberate rule breaks were caught.

## Memory Amber multiplier

| Step | Amber (pile of 10) | Reputation |
|---|---|---|
| Not a member, ×5 | +0 (the game gives none; not scaled) | +0 |
| Joined via `FactionTracker.JoinFaction(3)` | game grants 500, rank 1 | — |
| ×1 | +10 | +10 |
| ×5, twice | +50, +50 | +50, +50 |
| back to ×1 (hook removed) | +10 | +10 |

The game calls `Faction.GainFavor(10, ignoreRepGain=false, ignoreMultiplier=false)`
on The Woven (ID 3). The CoF favor multiplier stayed ×1 and unaffected; `amber 500`
is refused.

## Automated

Native 19/19 programs, Python 292, JavaScript 161 (after merging dev.22). New:
the effect layout rules, Memory Amber status reading in the bridge. Package:
player checks 24/24 (including the compiled Install update swap), connection
checks 4/4; Windows Defender found no threats in the ZIP, EXE, core and loader.
VirusTotal was not run (browser connection unavailable); hashes in
`docs/security/virustotal-20261013-dev23.json`. Live checks used the test
flavour of the core; the player core is built from the same sources.
