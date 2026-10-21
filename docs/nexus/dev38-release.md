# 0.1.2-dev.38: release texts (drafts, not published)

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev38-desktop\share\EpochPact-0.1.2-dev.38.zip` (35,893,210 bytes) | GitHub only | `9d40118e5c0eebd1a05ded8b65ed1481846f61bda12f345f5a509309dddaadeb` |
| `release-dev38-package\EpochPact-0.1.2-dev.38-InGame.zip` (3,427,704 bytes) | GitHub and Nexus | `858317aab89caa624764fa2aeb85c87626ddcd56ab006903030d61e4a2e87c4d` |

The desktop ZIP keeps the name `EpochPact-<version>.zip`: the app's Install update looks for exactly
that asset. Both ZIPs carry the same `version.dll` (`1196bf61…`). Scans:
`docs/security/virustotal-20261021-dev38.json`.

## GitHub release

Tag `v0.1.2-dev.38`, title: EpochPact 0.1.2-dev.38 — in-game window (F6), one-file install, inventory fix

```markdown
## Which download?

- **EpochPact-0.1.2-dev.38.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.38-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## New since 0.1.2-dev.35

- **In-game window (F6):** every EpochPact page inside the game, with the damage window and recent pickups as in-game overlays. No desktop app needed.
- **One file:** the mod's core is built into `version.dll` (the separate loader that one antivirus flagged is gone).
- **Fixed:** items drawn over each other in the inventory, and items that stayed visible after Stash all but could not be hovered or moved. Automatic sorting could run while the window was still drawing its items; sorting, Stash all, Sell all and stash rule sorting now wait for it, and leftover pictures are redrawn.
- **Fixed:** automatic pickup no longer stops after you pick up an item label by hand.

Last Epoch 1.5.2.1 (Steam), offline only. VirusTotal: version.dll 0/71, EpochPact.exe 0/71, InGame ZIP 0/71.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- File name: EpochPact - in-game edition (manual install)
- Version: 0.1.2-dev.38
- File description (unchanged): Manual install: copy version.dll into your Last Epoch folder (next
  to Last Epoch.exe), start the game offline and press F6. No EXE needed. Last Epoch 1.5.2.1 only.
  Do not use with MelonLoader.
- Changelog: Fixed items drawn over each other in the inventory, and items that stayed visible
  after Stash all but could not be hovered or moved. Sorting, Stash all, Sell all and stash rule
  sorting now wait until the window has drawn its items, and leftover pictures are redrawn.
- Description: version line `0.1.2-dev.38`; Safety line "VirusTotal (21 October 2026):
  version.dll 0/71, ZIP 0/71".

## Discord announcement (draft; owner approves the text first)

> **EpochPact 0.1.2-dev.38**
> Fixed: items drawn over each other in the inventory, and items that stayed visible after Stash all but could not be moved (automatic sorting ran while the window was still drawing).
> • Desktop app (EXE): GitHub — or **Install update** in the app
> • Manual, one file, F6 in game: GitHub or Nexus
> https://github.com/falorfrozen-cmd/EpochPact/releases

## Reply to the player report (draft)

> Thanks for the logs, they showed exactly what happened. With automatic sorting on, EpochPact
> sorted your inventory (and stash tabs) the moment the window opened. The game draws a window's
> items over a few frames, and its sort takes every item out and puts it back, so items moved
> during that drawing left their old pictures behind: they look like items but can't be hovered
> or moved, and other items get drawn over them. Stash All then moved the real items, but the
> pictures stayed. Fixed in 0.1.2-dev.38: sorting, Stash All, Sell All and stash rule sorting
> now wait until the window has finished drawing, and a window whose pictures don't match its
> items is redrawn.
