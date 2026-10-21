# 0.1.2-dev.39: release texts (drafts, not published)

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev39-desktop\share\EpochPact-0.1.2-dev.39.zip` (35,924,739 bytes) | GitHub only | `d6a97f1c6a9674dfb9c63484def13b0cc312065428da3e873040e866246cf39d` |
| `release-dev39-package\EpochPact-0.1.2-dev.39-InGame.zip` (3,437,958 bytes) | GitHub and Nexus | `2db5e69130d2118ade90b24adffc8da53456adfc036ca0c4656d98b3420ffbca` |

Both ZIPs carry the same `version.dll` (`0353afbf…`). Scans: `docs/security/virustotal-20261021-dev39.json`.

## GitHub release

Tag `v0.1.2-dev.39`, title: EpochPact 0.1.2-dev.39 — in-game window: full screen, half screen, resize

```markdown
## Which download?

- **EpochPact-0.1.2-dev.39.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.39-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## New

- **In-game window: full screen, half screen, move and resize.** The **Window** button in the window's top bar offers Full screen, Left half, Right half and Default size. Drag the top bar to move the window, drag the dots in its bottom-right corner to resize it, or double-click the top bar for full screen. The text keeps its size, the pages reflow, and the window remembers its place.
- In a narrow window (or at 150% text size) a setting's slider gets a row of its own.

Last Epoch 1.5.2.1 (Steam), offline only. VirusTotal: version.dll 0/71, EpochPact.exe 0/71, InGame ZIP 0/71.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.39 (file description unchanged)
- Changelog: In-game window: a Window button (Full screen, Left half, Right half, Default size);
  drag the top bar to move the window, drag the corner dots to resize it, double-click the top bar
  for full screen. It remembers its place.
- Description: version line `0.1.2-dev.39`; Safety line "VirusTotal (21 October 2026): version.dll
  0/71, ZIP 0/71" (unchanged numbers).

## Discord announcement (draft; owner approves the text first)

> **EpochPact 0.1.2-dev.39**
> • **New:** the F6 window can be full screen, half screen, moved and resized. Use the **Window** button in its top bar (Full screen, Left half, Right half, Default size), drag the top bar to move it, drag the dots in its bottom-right corner to resize it, or double-click the top bar for full screen. It remembers its place.
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.39
> **Manual install, no EXE:** copy the new `version.dll` over the old one (GitHub link above, or Nexus: https://www.nexusmods.com/lastepoch/mods/44)
