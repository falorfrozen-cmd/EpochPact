# In-game window: place and size (0.1.2-dev.39)

## Request

A player (Discord) asked whether the F6 window can be full screen and resized, to keep EpochPact
on one half of the screen and the inventory on the other while changing loot filter colours.

## What it does

- **▢ Window** in the top bar opens a list: Full screen, Left half, Right half, Default size.
- The top bar moves the window (no rebuild); a double click on it (Windows' double-click time)
  switches between full screen and the default size.
- Six dots in the bottom-right corner resize it: a translucent frame shows the new size while
  dragging; on release the window is built again for that size in one frame (0.6-0.7 s on the
  test PC) and comes back on the same page.
- The place is stored as `epochpact.ingame-window.v1` in the shared preferences file
  (`{"mode": "default|full|left|right|custom", "x","y","w","h"}`, custom as screen fractions)
  and used at every build; a changed screen size rebuilds on the next F6.
- The text keeps the size of the default window (94% of the screen, as before); the layout
  (`ConfigureSize`) reflows between 960x600 (the 150% text-size layout) and 1920x1200 css px,
  and only a window smaller than that makes the text smaller.
- A card too narrow for its range (a half-screen window, or 150% text size) puts the range on a
  row of its own above the stepper, the unit and Default.
- Cursor mapping uses the stored top-left corner instead of a centred window.
- `panel window:full|left|right|default|custom:x,y,w,h` (live tests) and `panel read`
  `placement`, `inFront`, `lastPress`.

## Live checks (EpFeedbackTest clone, 2560x1440)

Evidence under `C:\Users\falor\EpochPactLab\builds`; every run restored the runtime files, saves
and preferences.

| Run | Result |
|---|---|
| `winsize-live-1` (commands) | default 1440x900 at 1.504; full 1702x957 at 1.504; left/right 960x1080 at 1.333; custom 1000x700 px -> 960x672 at 1.042; a move of the same size not rebuilt; Loot, Crafting and Settings pages in the small window; 0 faults. |
| `winsize-mouse-4` (real mouse, owner's request) | Window button -> list (4 rows) -> Full screen; double click on the top bar -> default; drag of the top bar -> moved from 197,43 to 47,3 without a rebuild; drag of the corner -> 2166x1354 to 1707x1078 px, rebuilt; click on Loot in the resized window -> Loot; Left half. |

Earlier mouse runs found two problems, both fixed: a 400 ms double click missed presses during
slow frames (now Windows' double-click time), and a narrow card squeezed the range to nothing
(now its own row). Two runs failed in the harness itself (the first click after bringing the
game to the front, a corner moved off screen).
