# Hideout visual redesign — 8 November 2026

User rejects the previous templates' aesthetics. Rebuild real game layouts,
not AI mockups; inspect actual rendered models and composed layouts. Keep all
editor/history/selection/input safety features. Do not overwrite personal saved
templates or real preferences/saves. Work in fix52; no publication requested.

Baseline dev63 source backup:
C:/Users/falor/EpochPactLab/backups/hideout-dev63-before-art-redesign
Final installed baseline version.dll hash:
d22cb0f5622d545092f02ff28b3c8d18ba7185e1a11d67a697e8beba2d18e2f7

Observed old world screenshot: oversized disconnected ground-atlas repeats,
random/floating garden-road strips, oversized leaf/root piles, poorly defined
architectural edges, portal placement dominates the scene. Cached real previews
show BlueCrystal/IceCrystal are plain dark individual stones; avoid them as
hero clusters. Real contact sheets in research/hideout-art-2026-11-08.

Task art direction: coherent finished floors, one strong focal monument,
ordered architecture/colonnades and portal alcoves, services grouped at front,
small curated dressing. Five distinct themes, preserve entry points/door roles.
Use exact catalog asset names, select from actual renders before authoring.

Game boot external obstacle: Last Epoch 1.5.4 currently stays at Login with
LE-52 and cycle-status null exceptions, despite --offline. Both normal native
offline action and one real GUI click failed. Steam URI launch did not start a
game. Read-only art view is being used instead to continue asset selection.
No offline/player/save-write gate was relaxed in the player build.

45: actual GUI click used sky with include_text=false; Login remained. Close
returned 0xC0000005. Saves restored 68 byte-verified, prefs unchanged.
46: Steam URI did not launch; no process ever started. Prepared state restored,
68 originals byte-verified. Not a passing launch test.
47: no GUI automation; ordinary Login close also returned 0xC0000005. Therefore
45 does NOT establish GUI automation as the crash cause. Restored all originals.

Private EPOCHPACT_TESTING only: testartpreview flag permits script-free isolated
rendering at Login and manually steps preview queue when queried. It does not
set the game offline flag, load a character, touch the active world or enable
save writes. It is excluded from normal builds. tools/hideout_art_session.py
stops session startup before character selection and waits only for Login.
48 is the current owned render-only session, started via that helper. Need run
tools.hideout_asset_art_review on its folder once ready; renders 83 candidate
models, records bounds and composes actual render contact sheets. No ongoing
computer control; no more sky/UIA needed. Keep original files backed up and
restore the owned session when done.

Source changed so far: hideout.cpp and hideout_preview.inl private read-only
fixture only. New tools: hideout_art_session.py, hideout_asset_art_review.py.
Native private out: native/build-hideout-editor63-test; latest log
native/build-hideout-art-render-only.log. No version bump/public build yet.


Asset inspection complete: 83 + 45 + 12 exact catalog candidates rendered.
Contact sheets and original per-object status are in asset-review-48/batch-a,
batch-b and the session root. All 140 render requests completed; not every
rendered asset has usable textures. White placeholders and sparse model pieces
were rejected for the authored compositions. Coral_16 is finger coral;
Circle_Fountain is a stepped bowl; Circle_Fountain_Damaged_Roses is only flowers.

First composition review (49) was rejected internally as too sparse. Twelve
small portal pads per layout consumed the decoration budget. Revised designs
replace them with continuous floors, larger arcade frames and layered planting,
still 60 objects each (16 functional, 44 decorative). All exact names are in the
catalog; curated list 50. Floors retain their original UVs and their tops are
inlaid .04-.085 units above ground, including thick floor prefabs.

Second composition geometric checks: all 300 anchors and conservative renderer
envelopes within ground, 1.5-unit irregular-edge allowance; all 80 functional
approaches reachable in the conservative planning grid, >=3 units clear. Door
separation >=8 units. These are planning checks, not native game navigation.
9 design tests passed. Existing history/selection/ghost/input logic unchanged.

Template/saved previews now render at 960x640 (models 480x320), cache-v4 rejects
old dimensions; the existing correct +Y ground triangle winding is unchanged.
Private art-only export can save an incomplete preview as evidence; it is
excluded from production builds and uses temporary preferences only.

48 and 49 both closed from the game's broken Login state with 0xC0000005.
All 68 original save files and original runtime bytes were restored after each;
real preferences unchanged. Do not report these as normal shutdown passes.
50 is the next isolated composition review, no GUI automation.


Final: 139 unique candidates / 140 render requests; five final real isolated
compositions and six cache/saved-preview checks passed in session 51. Native
27 and Python 35 passed from final source/full player build. Session 51 Login
close still failed 0xC0000005, original 68 saves and real prefs restored verified.
Normal player dev64 installed after restore, SHA 4737635210cb2f1aee25a534fa722c9ac3721bd9c92f8abf06c0046147a1202c.
Previous DLL preserved at C:\Users\falor\EpochPactLab\backups\hideout-dev64-before-install\version.dll.
Package verified: EpochPact-0.1.2-dev.64-InGame.zip, SHA fbd0278a89909e13dff4cc126a6da9b5571cfa8425c862e05cb0e7a9bcec1005.
See dev64-verification.md for the exact separation between visual/geometric
verification and unrun real-world movement/interaction tests. Game is closed.
