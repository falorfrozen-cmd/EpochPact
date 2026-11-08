# 0.1.2-dev.66: release texts

dev.66 = dev.55 + ChatGPT's hideout work (local dev.56-65, never published; marked alpha by the owner) + quad stash tabs, every idol page
counts, ailment multipliers for the player's side, minions without acceleration (2026-11-08).

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev66-desktop\share\EpochPact-0.1.2-dev.66.zip` (39,024,893 bytes) | GitHub only | `cbd5ca62ec06ac6eb4540d1b22da302753fd860aebb2ea246189f1136f1e7afd` |
| `release-dev66-package\EpochPact-0.1.2-dev.66-InGame.zip` (4,452,308 bytes) | GitHub and Nexus | `e029b15134f6ac125f0c7bb6f5468bf77c80093c9c0f481f510aa5201b6e9003` |

Both ZIPs carry the same `version.dll` (`a962b1d2…`); `EpochPact.Core.dll` `fd6d3234…`, `EpochPact.exe` `dd147978…`.
Built from 7eb3fa6 (worktree rel66, branch release66). Tests: Python 407, JavaScript 246, native rule tests; clones on the test build: quad stash 8 (far items on disk inside 12 x 17 in storage tabs, back after a restart), Configure Tab check 4, ailments and idol pages 18, minions 3; hideout: ChatGPT's dev.65 checks (docs/hideout-designs/dev65-verification.md). VirusTotal (8 November 2026): version.dll 0/71; the InGame ZIP itself was not uploaded (VirusTotal asked for a CAPTCHA on the second upload); it holds this same version.dll and text files.
Not tried: a real mouse or a gamepad on the new controls; minion drift seen in play (the agent values were checked).

## GitHub release

Tag `v0.1.2-dev.66`, title: EpochPact 0.1.2-dev.66 — your own hideout (alpha), quad stash tabs

```markdown
## Which download?

- **EpochPact-0.1.2-dev.66.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.66-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Your own hideout (alpha).** Place anything of the game where you want it: an editor and a Hideout Builder with search and previews, ground shapes and textures, ready designs and templates (your own saved ones and a file you can share), its own minimap, boss doors for every timeline, Boss Rush, your stash, a vendor and the Eternity Cache. Open it from F6 → Hideout or the house button under the minimap. It is an alpha: expect rough edges and tell us what you find.
- **Quad stash tabs.** A tab of 24 × 34 cells, four times the items: tick **Quad stash** in the game's own Configure Tab window (or F6 → Inventory). The other three quarters are also kept in three storage tabs in the folder *EpochPact quad*, so without EpochPact you still find every item there.
- **Every idol page counts.** The idols and altars on your other idol pages give their bonuses too (F6 → Inventory, on by default).
- **Ailments:** stack cap ×1–30 and duration ×1–10 for the ailments you and your minions apply; enemies keep the game's values (F6 → Combat).
- **Minions without acceleration:** your summons reach full speed and stop at once, so fast minions no longer slide past their targets (F6 → Summons). More Tide Elementals: use Extra minions per skill.

Last Epoch 1.5.4 (Steam), offline only. VirusTotal: version.dll 0/71, InGame ZIP holds this same version.dll and text files.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.66
- File description:
  New in 0.1.2-dev.66: your own hideout (alpha), quad stash tabs, every idol page counts, ailment cap and duration, minions without acceleration. Install: copy version.dll next to Last Epoch.exe, play offline, press F6.
- Changelog:
  - Your own hideout (alpha): editor, ready designs, templates, its own minimap, boss doors, Boss Rush, stash, vendor, Eternity Cache.
  - Quad stash tabs (24 x 34), chosen in the game's Configure Tab window; kept safe in storage tabs without EpochPact.
  - Every idol page counts, altars too.
  - Ailment stack cap and duration for the ailments you and your minions apply.
  - Minions without acceleration.
- Description (`description-dev66.bbcode`).
