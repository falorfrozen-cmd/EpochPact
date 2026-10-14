# EpochPact 0.1.2-dev.31

Chests, shrines and portals at the distance you choose, auto pickup that keeps
going after huge drops, and an app you can tidy up.

## Fixes

- **Damage window:** starting the damage meter on the Summons page now shows the
  damage in the in-game damage window too, within a few seconds — no need to
  reopen the window. It works the other way round as well.
- **Auto pickup after huge drops:** after a Prophecy, a boss or anything else
  that leaves hundreds of items on the ground, auto pickup sometimes stopped
  until the next zone. It now works through the whole pile and keeps picking up
  what you want.

## World & Exploration → Nearby interactions

- **Interaction radius** (1–30 game units, 3 as before): how close a chest,
  shrine or portal must be before it is used.
- **Interaction area shape:** a circle or a square around each object.
- **Show interaction areas:** draws that area on the ground around every chest,
  shrine and portal that will still be used. The outline disappears once the
  object is used.
- **Interaction area colour:** any colour you like.

## App

- **Hide parts of a page:** point at a panel or card and select the small × in
  its corner to hide it, for example "Wanted T7 affixes". **Settings → Hidden
  parts** shows them again.
- **Text size:** Settings → Text size makes the app window 90–150 % as large.
- **New version pop-up:** when a new version comes out, its What's new window
  opens once by itself.

## Translations

- The new texts are translated into Türkçe and Русский.

## Updating

With 0.1.2-dev.22 or later: **New version · What's new → Install update** in
the app. From an older version: close Last Epoch and EpochPact, extract the
**complete new ZIP**, open `EpochPact/EpochPact.exe`, and select **Game setup →
Reinstall / update mod**. The new settings need the updated mod: Install update
reinstalls it once the game is closed.

Tests: [damage window, auto pickup, interaction areas, app settings](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.31/docs/conveniences-validation-2026-10-14.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
