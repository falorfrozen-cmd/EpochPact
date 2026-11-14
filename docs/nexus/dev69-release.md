# 0.1.2-dev.69: release texts

dev.69 = dev.68 + Simplified Chinese (a player's request), and in-game texts that no language had are now translated
into Turkish, Russian and Chinese. dev.68 was released on GitHub only; this is also the Nexus update for both.

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev69-desktop\share\EpochPact-0.1.2-dev.69.zip` (39,647,649 bytes) | GitHub only | `fc1ec032465e8679115f5f545b0cd61e6c8ce69ba1b4f194d68e50a01d86554b` |
| `release-dev69-package\EpochPact-0.1.2-dev.69-InGame.zip` (4,659,088 bytes) | GitHub and Nexus | `7a6c3251f3c42ef9fc74d991977a41e23a3a871f72a1d1099a9866727e01e895` |

Both ZIPs carry the same `version.dll` (`df758454…`, 10,937,856 bytes); `EpochPact.Core.dll` `c60616a7…`, `EpochPact.exe` `6d14d015…`.
Built from 268b8d3 (worktree rel69, branch release69). Tests: Python 400, JavaScript 247; desktop package verify and connection checks; Windows Defender (signatures 1.463.286.0) no threats.
Clones on test builds: Chinese window (YaHei glyphs in the atlas, no faults; the English window draws 简体中文 too), Turkish window, hideout page fully translated.
VirusTotal: not done (version.dll is above the 10 MB the browser bridge can send).

## GitHub release

Tag `v0.1.2-dev.69`, title: EpochPact 0.1.2-dev.69 — Simplified Chinese

```markdown
## Which download?

- **EpochPact-0.1.2-dev.69.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.69-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Simplified Chinese (简体中文).** The desktop app and the F6 window in Simplified Chinese: Settings → Language. In the game window the Chinese text uses Microsoft YaHei from Windows. Names that come from the game (items, affixes, zones) stay as the game writes them.
- **More translated texts.** Hideout editor, settings and profile texts that were still in English are now translated into Turkish, Russian and Chinese.

Last Epoch 1.5.4 (Steam), offline only. Windows Defender: no threats. VirusTotal scan of this version follows.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.69
- File description:
  New in 0.1.2-dev.69: Simplified Chinese (简体中文, F6 Settings > Language). Since 0.1.2-dev.67: Cannot die, and the Hideout button hides over game windows. Install: copy version.dll next to Last Epoch.exe, play offline, press F6.
- Changelog:
  - Simplified Chinese (简体中文): the F6 window in Chinese (F6 > Settings > Language).
  - Hideout editor and settings texts that were still in English are now translated (Turkish, Russian, Chinese).
  - Cannot die: you take damage as usual, but lethal damage leaves you at 1 health (F6 > Combat > Survival, offline only).
  - Hideout button under the minimap: hides while the inventory or another game window is open, and can be turned off (F6 > Display > Minimap).
- Description (`description-dev69.bbcode`).
