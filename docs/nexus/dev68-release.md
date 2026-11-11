# 0.1.2-dev.68: release texts

dev.68 = dev.67 + Cannot die for the player, and the Hideout button under the minimap hides over game windows and can be turned off
(a player's request on Nexus).

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev68-desktop\share\EpochPact-0.1.2-dev.68.zip` (39,180,095 bytes) | GitHub only | `d909858d58fe13ff219a0c28833453e9eee87786ed1c382837c4cd6196d86d1b` |
| `release-dev68-package\EpochPact-0.1.2-dev.68-InGame.zip` (4,503,627 bytes) | GitHub and Nexus | `f503d24d3982e01fc5fc7742ee28ed281653b3004a53102242616e50c4b33534` |

Both ZIPs carry the same `version.dll` (`b3e6a3a5…`, 10,510,336 bytes); `EpochPact.Core.dll` `5b046a3b…`, `EpochPact.exe` `56be0c77…`.
Built from 4ca9aa7 (worktree rel68, branch release68). Tests: Python 400, JavaScript 246, native rule tests; desktop package verify and connection checks; Windows Defender (signatures 1.463.235.0) no threats.
Clones on test builds: Cannot die 8/8 (damage taken, five lethal hits survived, also after a zone change, regeneration from 1, off = the game's death screen); Hideout button 5/5 (shown, hidden while the inventory is open, back when it closes, off, on). Release DLL on a Rogue clone: quad stash ready, Zerrick and Cannot die settings present, Cannot die on/off, Hideout button setting on, F6 What's New / Combat / Display no faults, saves unchanged.
VirusTotal: not done (version.dll is above the 10 MB the browser bridge can send; the ZIP upload stopped at a reCAPTCHA for dev.67).

## GitHub release

Tag `v0.1.2-dev.68`, title: EpochPact 0.1.2-dev.68 — Cannot die, Hideout button hides over game windows

```markdown
## Which download?

- **EpochPact-0.1.2-dev.68.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.68-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Cannot die.** Like Immortal minions, for your character: you take damage as usual, but lethal damage leaves you at 1 health and you regenerate from there (the game's own cannot-die state). F6 → Combat → Survival. Offline play only.
- **Hideout button.** The small house button under the minimap no longer covers your inventory: it hides while the inventory or another game window is open. F6 → Display & Camera → Minimap turns it off completely; F6 → Hideout still takes you there.

Last Epoch 1.5.4 (Steam), offline only. Windows Defender: no threats. VirusTotal scan of this version follows.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.68
- File description:
  New in 0.1.2-dev.68: Cannot die (damage as usual, lethal hits leave you at 1 health, F6 Combat); the Hideout button hides over game windows and can be turned off. Install: copy version.dll next to Last Epoch.exe, play offline, press F6.
- Changelog:
  - Cannot die: you take damage as usual, but lethal damage leaves you at 1 health (F6 > Combat > Survival, offline only).
  - Hideout button under the minimap: hides while the inventory or another game window is open, and can be turned off (F6 > Display > Minimap).
- Description (`description-dev68.bbcode`).
