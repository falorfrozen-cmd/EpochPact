# 0.1.2-dev.67: release texts

dev.67 = dev.66 + Rogue look: Zerrick (body and voice, per character), a corrupted affix on a Unique template in Custom Items,
the pickup fix after switching characters (quad stash / item pages).

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev67-desktop\share\EpochPact-0.1.2-dev.67.zip` (39,163,936 bytes) | GitHub only | `b1ffd10b0bfac41ed62ef4b247043834d5dd6061a9ca6a7e49770728f1474472` |
| `release-dev67-package\EpochPact-0.1.2-dev.67-InGame.zip` (4,498,720 bytes) | GitHub and Nexus | `bb458b143438d413b8cfaef76c1074ddb35e9c2bf1e13c92b5bf8f7906a7cf89` |

Both ZIPs carry the same `version.dll` (`4d43f963…`, 10,496,000 bytes); `EpochPact.Core.dll` `0dddae17…`, `EpochPact.exe` `e6fb8a21…`.
Built from b23eff1 (worktree rel67, branch release67). Tests: Python 400, JavaScript 246, native rule tests; desktop package verify and connection checks; Windows Defender (signatures 1.463.220.0) no threats.
Clones on test builds: Zerrick (character, voice, walk), the corrupted affix on a Unique template 8/8 over a restart, pickups after a switch (pages 13/13, quad 8/8). Release DLL on a Rogue clone: quad stash ready, Zerrick setting present, F6 no faults, saves unchanged.
VirusTotal: not done. version.dll is above the 10 MB the browser bridge can send, and the InGame ZIP upload stopped at VirusTotal's reCAPTCHA (not solved).

## GitHub release

Tag `v0.1.2-dev.67`, title: EpochPact 0.1.2-dev.67 — Rogue look: Zerrick, corrupted affixes on Uniques, pickup fix

```markdown
## Which download?

- **EpochPact-0.1.2-dev.67.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.67-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Rogue look: Zerrick.** Your Rogue can wear the body of Zerrick, the campaign's male rogue, and speak with a man's voice: his own combat lines, and the Sentinel's for what he never says (no mana, bags full). F6 → Wardrobe, chosen for each Rogue on its own. Display only: your items, stats and saves do not change. Your weapons stay in his hands; armour looks do not show on him. The character creation screen shows the game's own Rogue.
- **Custom Items: a corrupted affix on a Unique template.** It stopped with "the game changed a sealed custom affix during round-trip; nothing applied". A Unique now keeps its corrupted affix last, where the game loads it; plain items work as before.
- **Pickups after switching characters.** With quad stash tabs, picked-up items could be equipped by themselves, could not be moved, or went to other inventory pages after a character or menu switch. Fixed.

Last Epoch 1.5.4 (Steam), offline only. Windows Defender: no threats. VirusTotal scan of this version follows.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.67
- File description:
  New in 0.1.2-dev.67: Rogue look Zerrick (a man's body and voice, F6 Wardrobe), corrupted affixes on Unique templates in Custom Items, pickup fix after switching characters. Install: copy version.dll next to Last Epoch.exe, play offline, press F6.
- Changelog:
  - Rogue look: Zerrick, a man's body and voice for your Rogue, chosen for each Rogue (F6 > Wardrobe, display only).
  - Custom Items: a corrupted affix on a Unique template works ("the game changed a sealed custom affix during round-trip" fixed).
  - Pickups after switching characters: items are no longer equipped by themselves or sent to other inventory pages.
- Description (`description-dev67.bbcode`).

## Published 2026-11-10 (owner: "evet yayınla Nexusmods ve githubda")

- GitHub: release `v0.1.2-dev.67` (Latest, not a prerelease) with both ZIPs (asset digests match the table above); target and `main` = 10aeb2a (fast-forward from 88340ef).
- Nexus mod 44: main file "EpochPact - in-game edition (manual install)" updated to 0.1.2-dev.67 with the InGame ZIP (dev.66 archived), mod version 0.1.2-dev.67 (from the file), the three changelog lines above, the file description above (246 of 255 characters); description = `description-dev67.bbcode`. The public page shows the new version, description, main file and changelog.
- Release DLL on a Rogue clone (EpFeedbackTest, builds\shipping67b): quad stash ready, Zerrick setting present, F6 opened on Wardrobe, What's New (dev.67 row with the Zerrick card) and Crafting, no faults; real saves unchanged.
- VirusTotal: not done (see above). Windows Defender: no threats.
- Discord: the owner sends the announcement.
