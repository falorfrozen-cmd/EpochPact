# 0.1.2-dev.46: release texts

dev.46: Forging Potential on drops (player request), both editions. Details and live checks:
`docs/player-conveniences.md` (Forging Potential on drops).

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev46-desktop\share\EpochPact-0.1.2-dev.46.zip` (36,289,341 bytes) | GitHub only | `e3059e46dd2bc788b0b42d3bd189cb65f8fb375e12673d7088774b3602d54d1d` |
| `release-dev46-package\EpochPact-0.1.2-dev.46-InGame.zip` (3,554,232 bytes) | GitHub and Nexus | `391d8bc7db49f3fd2688ff1bfaaa53c8cd4bcff40866a0b8b826c688630e29ad` |

Both ZIPs carry the same `version.dll` (`067aa522…`); `EpochPact.Core.dll` `77dc2f1c…`,
`EpochPact.exe` `dca964ca…`. Built from a1bcb02 (Forging Potential on drops 6442e6e). Package checks passed
(desktop verify, connection test); the player DLLs have no test commands (`fpcheck`, `idolcheck`, `spawncheck`);
Microsoft Defender (signatures 1.463.1.0) found nothing. Shipping check with this exact `version.dll` (`builds\fp-player-live`, EpFeedbackTest clone): version
0.1.2-dev.46, test commands unknown, 8 items at x1 then 8 at x2 picked up (Normal 16/21 -> 32/40, Magic 14/16 -> 32/40,
Rare 27/29 -> 52/58, Exalted 30/31 -> 63/63), all kept after a restart. VirusTotal: `docs/security/virustotal-20261028-dev46.json`.

## GitHub release

Tag `v0.1.2-dev.46`, title: EpochPact 0.1.2-dev.46 — Forging Potential on drops

```markdown
## Which download?

- **EpochPact-0.1.2-dev.46.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.46-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Forging Potential on drops.** New setting in General settings (1–10x): new Normal, Magic, Rare and Exalted items (drops, rewards, Nemesis and Spawn Items) get their Forging Potential multiplied, rounded, up to 63. 63 is the most an item can have in Last Epoch (the game cuts it there and saves keep no more), so x2 roughly doubles it and x3 or more gives most items 63. Crafting uses it up as usual. Shops, gamblers and items you already have do not change; Uniques and Sets have none.

Last Epoch 1.5.3 (Steam), offline only. VirusTotal: version.dll 0/63, InGame ZIP 0/67.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.46 (file description unchanged)
- Changelog (one line per entry):
  - New setting in General settings: Forging Potential on drops (1–10x). New items drop with more Forging Potential, up to the game's maximum of 63.
- Description (`description-dev46.bbcode`): version line `0.1.2-dev.46`; General line names Forging Potential on
  drops; Safety line "VirusTotal (28 October 2026): version.dll 0/63, ZIP 0/67".

Published 2026-10-28 with the owner's approval ("tamam dev46 yayınla"): GitHub release `v0.1.2-dev.46`
(latest, both ZIPs, digests checked; main = b389d2b; the app updater offers it as installable from 0.1.2-dev.45),
Nexus main file updated to 0.1.2-dev.46 (dev.45 archived, changelog with the file, mod version updated with it,
download available at once) and page description (version, General and Safety lines).

## Discord announcement (draft; the owner sends it)

> @everyone
> **EpochPact 0.1.2-dev.46**
> • **Forging Potential on drops:** new setting in *General settings* (1–10x). New items drop with more Forging Potential, up to 63 (the most Last Epoch allows on an item), so you can craft more on them.
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.46
> **Manual install, no EXE:** copy the new `version.dll` over the old one (GitHub link above, or Nexus: https://www.nexusmods.com/lastepoch/mods/44)
