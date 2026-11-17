# 0.1.2-dev.70: release texts

dev.70 = dev.69 + a fix for a player's report: bonus skill points from the skill point multiplier were taken back after a
game restart when a +skill bonus from gear dropped before the multiplier was set again.

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev70-desktop\share\EpochPact-0.1.2-dev.70.zip` (39,658,233 bytes) | GitHub only | `3a52aa7b330490f7f1dc1ca6239d71ae7861478cbf54b22d35ed5046f28a3f1e` |
| `release-dev70-package\EpochPact-0.1.2-dev.70-InGame.zip` (4,662,407 bytes) | GitHub and Nexus | `5869453afab4eb5d7f3f6d786d23a8b086ee4a4caa6598d56d579bdbaf93038f` |

Both ZIPs carry the same `version.dll` (`5c1d8a56…`, 10,945,024 bytes); `EpochPact.Core.dll` `efd94f9b…`, `EpochPact.exe` `e37f4c5d…`.
Built from worktree fix70, branch dev70. Tests: Python 407, JavaScript 247; desktop package verify and connection checks; Windows Defender (signatures 1.463.337.0) no threats.
Clone test on a test build of the fix: 36 skill points spent (natural maximum 18), gear bonus drop with the multiplier not set: all 36 kept; control with multiplier 1 remembered: the game took them back to 18.
VirusTotal: not done (version.dll is above the 10 MB the browser bridge can send).

## GitHub release

Tag `v0.1.2-dev.70`, title: EpochPact 0.1.2-dev.70 — Skill points kept after restart

```markdown
## Which download?

- **EpochPact-0.1.2-dev.70.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.70-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Skill points kept after a restart.** Bonus skill points from the skill point multiplier were taken back when the game was restarted and a +skill bonus from gear dropped before the multiplier was set again. EpochPact now remembers the multiplier you last set, and the game keeps your spent points. After updating, set the multiplier once (or let your startup profile apply it); from then on restarts keep your points. Points that were already taken back need to be spent again.

Last Epoch 1.5.4 (Steam), offline only. Windows Defender: no threats.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.70
- File description:
  New in 0.1.2-dev.70: bonus skill points from the skill point multiplier are no longer taken back after a game restart. Install: copy version.dll next to Last Epoch.exe, play offline, press F6.
- Changelog:
  - Skill points: bonus points from the skill point multiplier are no longer taken back after you restart the game; the multiplier you last set is remembered (F6 > Skills).
- Description (`description-dev70.bbcode`).
