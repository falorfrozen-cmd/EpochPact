# Loot/damage windows and custom item tiers (2026-10-14)

Player requests (AL4NoSTb, items 2–5): a hide button; windows that resize and
keep their place; compact windows without the "EpochPact Loot Feed" title bar,
with spacing and font options; background-only transparency and window colours.
Second request: custom items should not always be T7 — each affix should take
a tier from T1 to T8 (T8 is primordial, one per item) and a roll, random or
1–100%.

## Loot feed and damage windows

- The windows have no Windows frame and a transparent window background
  (pywebview `transparent=True`, `ui/overlay.js`). A bar at the top (always or
  on hover) moves the window; a corner grip resizes it; **−** hides it; **⚙**
  opens the options: Gold/Materials (feed) or Compact (damage), background
  colour, background opacity 0–100%, text colour, text size 10–22 px, row
  spacing and when the bar shows. Only the background becomes see-through;
  text gets a dark outline below 60%. Damage Start/Stop/Reset sit in the bar.
- **Open/Hide loot window** and **Open/Hide damage window** in EpochPact follow
  the windows' real state, also after **−** in a window.
- Measured: a transparent WebView2 window opened while the app runs is shown
  with an opaque grey background in about half of the cases (2 of 4 windows in
  two of three runs); one hide/show without activation after the page loads
  made 12 of 12 clear. A frameless window opens smaller by its frame; the
  remembered size is set exactly once the page has loaded.
- Real desktop windows over a magenta backdrop (screen pixels), run twice,
  12/12 each: default 85% background (46,16,52 as computed), 0% shows the
  backdrop, 50% custom colour blends evenly (143,24,159), options saved, resize
  from the page (460×380), drag by the bar (+60/+40), place and size
  remembered, − hides, hidden windows are not reopened, reopened at the
  remembered place and size. The main window buttons switched Open/Hide in all
  eight steps.
- Closing EpochPact: pywebview does not close the other windows with the main
  one (the program kept running behind a lone loot window), and in the owner's
  test a loot window closed during EpochPact's close was saved as closed. Now
  the small windows close with the main window, and only − or Hide forgets a
  window; any other close keeps it for the next start (logged in startup.log).
  Owner's check on the real PC: loot window open, EpochPact closed, started
  again — the loot window came back at its place.

## Custom item tiers and rolls

- Each affix is sent as `ID:tier:roll` (tier 1–8, roll 1–100 or `r`); a plain
  ID keeps the earlier behaviour (T7, item-wide roll). Parsing and the roll
  byte are in `native/core/craft_rules.hpp` (`craft_rules_test.exe`); the
  catalog now reports each affix's tier count (684 affixes have T8, 49 stop at
  T7, 423 have one tier).
- T8 uses the game's own `ItemData.MakeAffixSealedPrimordialAffix` (tier index
  7, sealed type 2, item marked primordial; the chosen roll is kept). For an
  item without a template the game sets the rarity from the regular affixes
  (`setValuesFromSerialisation`: count minus sealed affixes, at most 4), so the
  workshop sets the same value; the sealed flags of the replaced affixes are
  cleared.
- In the game (isolated EpFeatureTest clone, saves and installed files
  restored and verified): T1 1% → roll 3, T5 50% → 128, T8 100% → 255 sealed
  primordial, T7 random → random byte; the same affix at 100% gives 0.30 at T7
  and 0.53 at T8; T5 gives 0.27 at 1% and 0.32 at 100%; plain IDs still give
  T7 at 255; two T8, T8 on an affix without T8 and T9 are refused; after going
  back to character select and loading again the item's bytes are identical.

## Test run

| Suite | Result |
|---|---|
| Native test programs (with `craft_rules_test`) | 20/20 |
| JavaScript | 188/188 |
| Python | 322 OK |
| Interface text list / translations | 1,509 texts; Turkish and Russian complete |
| Compiled package / extracted ZIP | 24/24, 4/4 |
