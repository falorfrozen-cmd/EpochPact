# 0.1.2-dev.44: release texts

dev.44: Legendary Potential rolls (player request) and the In-game report text fix, both editions.
Details and live checks: `docs/player-conveniences.md` (Legendary Potential rolls); the report text fix is
commit 7585d8d.

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev44-desktop\share\EpochPact-0.1.2-dev.44.zip` (36,113,386 bytes) | GitHub only | `450c54872014b156d45ecfe6a9e959f228e42075a1fcf2daae38adc8148dbb20` |
| `release-dev44-package\EpochPact-0.1.2-dev.44-InGame.zip` (3,497,648 bytes) | GitHub and Nexus | `6d3136f4c831a2bcc9321b88bb3961e62e36a465a47b2baa4ee34bf33dc9b3a0` |

Both ZIPs carry the same `version.dll` (`286bf739…`); `EpochPact.Core.dll` `4391b78b…`,
`EpochPact.exe` `c0bd941b…`. Built from 2de30d3 (Legendary Potential rolls 0e2632a, report text 7585d8d).
Package checks passed (desktop verify, connection test); the player version.dll has no test fixtures;
Microsoft Defender (signatures 1.461.217.0) found nothing. VirusTotal: `docs/security/virustotal-20261027-dev44.json`.

## GitHub release

Tag `v0.1.2-dev.44`, title: EpochPact 0.1.2-dev.44 — Legendary Potential rolls

```markdown
## Which download?

- **EpochPact-0.1.2-dev.44.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.44-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Legendary Potential rolls.** New setting in General settings (1–1000): the game rolls a new Unique's Legendary Potential this many times and keeps the highest roll. It never goes above what the game itself can roll, so how high it can go still depends on the area level and corruption. Weaver's Will Uniques and items you already have do not change. At 1 it is off.
- **In-game report text fixed (F6 window).** The In-game report text in Settings was drawn over itself because of the folder path in it; Windows paths now show as written.

Last Epoch 1.5.3 (Steam), offline only. VirusTotal: version.dll 0/70, InGame ZIP 0/66.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.44 (file description unchanged)
- Changelog (one line per entry):
  - New setting in General settings: Legendary Potential rolls (1–1000). The game rolls a new Unique's Legendary Potential this many times and keeps the highest; never above what the game itself can roll.
  - F6 window: the In-game report text in Settings shows its folder path correctly.
- Description (`description-dev44.bbcode`): version line `0.1.2-dev.44`; General line names Legendary
  Potential rolls; Safety line "VirusTotal (27 October 2026): version.dll 0/70, ZIP 0/66".

## Discord announcement (draft; the owner sends it)

> @everyone
> **EpochPact 0.1.2-dev.44**
> • **Legendary Potential rolls:** new setting in *General settings* (1–1000). Every new Unique rolls its Legendary Potential that many times and keeps the best roll.
> • **F6 window:** the In-game report text in Settings is readable again.
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.44
> **Manual install, no EXE:** copy the new `version.dll` over the old one (GitHub link above, or Nexus: https://www.nexusmods.com/lastepoch/mods/44)
