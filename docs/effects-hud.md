# In-game effects HUD

Display & Camera → Buffs & debuffs in game controls a native Unity HUD inside
Last Epoch. It needs no Alt-Tab, external window or bundled icon atlas. It is
off by default, offline only and scoped to the current character session.

The player panel reads active BuffUIEntry objects from BuffsUIManager. Names
and descriptions come from each live UIBuff definition; sprites are the same
native Image sprites the game displays. This includes non-ailment buffs, not
just a fixed list of six or 24 examples.

The target panel follows UIActorInfo's displayed actor and its current native
ailment icons. Remaining time is measured from the actual AilmentReceiver
when available; definition durations are never presented as live timers.
Changing or losing the target clears its previous rows. Not every internal
effect appears in these native UI lists: hidden game effects are outside scope.

Panels show names, descriptions of up to two lines, stack counts and known
remaining time. Text keeps a readable minimum size at every HUD size, and each
text box is sized from that font size and the font's own line height (read
from the font in use, so a chosen interface font is measured too); a box
shorter than one line would make the game draw nothing.
`—` means the timer is unavailable or is not fixed; it does not mean infinite.
Different stack expiry times are represented by the longest remaining stack.
The screen fits at most 12 rows per panel; extra active effects are counted.
This display limit does not restrict which effect definitions can be shown.

All graphics let clicks pass through. No tooltip is opened or closed. The
feature never changes buffs, targets, combat results, skills, quests or saves.
Metadata is cached and rows are reused. Sampling runs every 250 ms while enabled;
there is one target-UI scene discovery per new BuffsUIManager owner, rather than
a scene search on every frame. HUD creation adds at most one row per panel per
sample. Unsupported metadata stops and hides the HUD instead of guessing.

Commands: `effectsread`, `effectsset enabled|player|target|descriptions 0|1`,
`effectsset scale 0.7..1.5`, `effectsset rows 1..12`. The same serial, nonce and
PID:FILETIME session validation as other commands applies. Opening EpochPact,
reading status, changing themes and loading a draft do not enable the HUD.

## Validation

Native build and presentation-rule tests are separate from live game validation.
The layout rules are checked for 750 combinations of screen size, HUD size,
descriptions and line height (every text box holds a line; name, description and
timer fit a row; panels stay on screen and apart).

Final revision, Last Epoch 1.5.2.1, three sessions on a fresh isolated character
copy (saves and the installed runtime restored and hash-verified afterwards):
real Chill/Slow/Frenzy/Haste and natural Preparation/Grace Period entries with
counting timers; 12 effects with overflow; sizes 0.7/1.0/1.5 with and without
descriptions; a target with known and unknown timers, cleared on target loss;
the inventory open beside the HUD; an area change (Z32 → Z52) keeping the HUD
with one new discovery; switching off; normal game close (exit 0). Steady
samples 0.2–0.4 ms, first build of the rows up to 2 ms. The font's measured
line height was 1.47 × size. Some presentation cases used test-only native UI
fixtures; these do not prove every effect's natural combat trigger.
See [the handoff](effects-hud-handoff-2026-10-13.md) for the earlier coverage. The interactive design's 24 painted symbols are illustrative
and are not used or distributed by the native HUD.
