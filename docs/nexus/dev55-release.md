# 0.1.2-dev.55: release texts

dev.55 = dev.54 + Last Epoch 1.5.4 + the work up to the Boss Hall. The hideout, the ground choices and templates are NOT in it
(owner 2026-11-06: release the version without the hideout); they stay uncommitted in fix52 for the next version.

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev55-desktop\share\EpochPact-0.1.2-dev.55.zip` (37,273,009 bytes) | GitHub only | `1864afbf69d9791ec69bd9116b167d6a3c0a328526e930bac672f626c4168e67` |
| `release-dev55-package\EpochPact-0.1.2-dev.55-InGame.zip` (3,868,015 bytes) | GitHub and Nexus | `84b2d4520136e2386088464185dfb6421d5c0460203af1987963a13b484f1853` |

Both ZIPs carry the same `version.dll` (`42655439…`); `EpochPact.Core.dll` `0de9a00d…`,
`EpochPact.exe` `f76dd91a…`. Built from e72c99b, branch release55 from b9d531e (dev.54 published). Package checks passed
(desktop verify, connection test); Microsoft Defender (signatures 1.463.155.0) found nothing. Tests: Python 398, JavaScript 246; clones on the test build: Boss Hall 8, door clicks 8, boss quests 13, Boss Rush 22, no failures; the player DLL on a clone: hall up, F6 pages, no faults.
VirusTotal (6 November 2026): version.dll 0/69, InGame ZIP 0/62.
Not proven: whether the Nemesis chance really brings more Nemesis encounters (the setting changes the game's rate; the owner will try it).

## GitHub release

Tag `v0.1.2-dev.55`, title: EpochPact 0.1.2-dev.55 — Last Epoch 1.5.4, a Boss Hall of its own

```markdown
## Which download?

- **EpochPact-0.1.2-dev.55.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.55-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Works on Last Epoch 1.5.4.**
- **A Boss Hall of its own.** Monolith → *Boss doors*, then go to Fall of the Outcasts in the Monolith Hub: a rock named Boss Hall next to the waypoint takes you to a place of its own. A door for each timeline's boss (even on a locked timeline), Boss Rush through all ten, Normal / Empowered / Empowered with its Harbinger, and stations for your stash, a vendor and the Eternity Cache. The **?** next to the setting explains it.
- **Monolith:** skip quest echoes, no blessing window.
- **Crafting:** Eternity Cache anywhere with one affix choice per Legendary Potential, new affixes on Exalted items, set the Legendary Potential of your Uniques, corrupted affixes in Custom Items.
- **Inventory:** inventory and idol pages, all idol slots open, shatter what the loot filter hides.
- **Combat page:** more companions, Loot Lizard and Nemesis chance. The Nemesis chance is lightly tested: tell us how it behaves.
- **What's New page:** each version's changes with their settings right there. Overview and the pages lost controls the game already has.

Last Epoch 1.5.4 (Steam), offline only. VirusTotal: version.dll 0/69, InGame ZIP 0/62.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.55
- File description:
  New in 0.1.2-dev.55: Last Epoch 1.5.4; Boss Hall with boss doors and Rush; Eternity Cache anywhere; new affixes on Exalted items; inventory and idol pages. Install: copy version.dll next to Last Epoch.exe, play offline, press F6. Not with MelonLoader.
- Changelog:
  - Works on Last Epoch 1.5.4.
  - Boss Hall: a place of its own with boss doors, Boss Rush, Empowered and Harbinger, stash, vendor and Eternity Cache.
  - Monolith: skip quest echoes, no blessing window.
  - Crafting: Eternity Cache anywhere, new affixes on Exalted items, Legendary Potential of Uniques, corrupted affixes in Custom Items.
  - Inventory: inventory and idol pages, all idol slots open, shatter what the loot filter hides.
  - Combat page: more companions, Loot Lizard and Nemesis chance. What's New page.
- Description (`description-dev55.bbcode`).

## Discord announcement (draft; the owner sends it)

> @everyone
> **EpochPact 0.1.2-dev.55: works on Last Epoch 1.5.4**
> • **A Boss Hall of its own:** Monolith → Boss doors, then the rock named Boss Hall at Fall of the Outcasts in the Monolith Hub. A door for each boss, Boss Rush, Empowered and Harbinger, stash, vendor and Eternity Cache.
> • **Crafting:** Eternity Cache anywhere, new affixes on Exalted items, Legendary Potential of your Uniques, corrupted affixes in Custom Items.
> • **Inventory:** inventory and idol pages, all idol slots open, shatter what the filter hides.
> • **What's New** page in F6: every change with its settings right there.
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.55
> **Manual install, no EXE:** download the main file from Nexus (https://www.nexusmods.com/lastepoch/mods/44?tab=files) or `EpochPact-0.1.2-dev.55-InGame.zip` from the GitHub link above, and copy the new `version.dll` over the old one.

## Published 2026-11-06 (owner: "hideout dışında zip yapmıştık ya, onu release yapalım"; "virustotal scan yap, 0 çıkarsa nexusmodsa at")

- GitHub: release `v0.1.2-dev.55` (Latest, not a prerelease) with both ZIPs; target and `main` = 0448a3d (fast-forward from b9d531e). Published before the scans, the notes' VirusTotal line added after them.
- VirusTotal (Chrome, fresh uploads): version.dll 0/69, InGame ZIP 0/62.
- Nexus mod 44: main file "EpochPact - in-game edition (manual install)" updated to 0.1.2-dev.55 with the InGame ZIP (dev.54 archived), mod version 0.1.2-dev.55, the six changelog lines above, the file description above (251 of 255 characters). Short description 1.5.4; description lines changed (version, Made for, Loot, Crafting, Boss Hall, Inventory, Monolith, Safety). The public page shows the new version and description; the file was in Nexus's own scan right after upload.
- Discord: the owner sends the announcement (draft above).
