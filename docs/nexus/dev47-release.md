# 0.1.2-dev.47: release texts

dev.47: Unique minimum roll (player request), both editions. Details and live checks:
`docs/player-conveniences.md` (Unique minimum roll).

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev47-desktop\share\EpochPact-0.1.2-dev.47.zip` (36,313,805 bytes) | GitHub only | `fb325613d6920841a7c33a8ac0853a3827d67fca87226727ebd0bb235e1ba74d` |
| `release-dev47-package\EpochPact-0.1.2-dev.47-InGame.zip` (3,561,709 bytes) | GitHub (Nexus held back for now) | `bf012afb9d4691a9cff5f9cc2576dcfe651aafe883342101f1f3f1ed6924f22a` |

Both ZIPs carry the same `version.dll` (`d9556c16…`); `EpochPact.Core.dll` `c5ffb02f…`,
`EpochPact.exe` `c40a182a…`. Built from 0afb031 (Unique minimum roll 10dfc83); the desktop edition again from a2873fa (desktop Spawn Items
identity fix in `ui/app.js`, same player files). Package checks passed
(desktop verify, connection test); the player DLLs have no test commands (`uniquecheck`, `rollsread`, `fpcheck`, `idolcheck`, `spawncheck`);
Microsoft Defender (signatures 1.463.12.0) found nothing. Shipping check with this exact `version.dll` (`builds\dev47-player-live`, EpFeedbackTest clone): version
0.1.2-dev.47, test commands unknown, `uniqueroll 100` raised the rolls of four spawned Uniques and a Set (hook
counters: items 5, rolls 19, faults 0), `fpdrops 2` still gave Rares 58/63, all seven items kept after a restart. VirusTotal: `docs/security/virustotal-20261028-dev47.json`; the only detection is Kaspersky's heuristic
`VHO:Trojan.Win32.Dllhijack.gen` (see the record: fresh builds of the dev.46 source get it too).

## GitHub release

Tag `v0.1.2-dev.47`, title: EpochPact 0.1.2-dev.47 — Unique minimum roll

```markdown
## Which download?

- **EpochPact-0.1.2-dev.47.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.47-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Unique minimum roll.** New setting in General settings (0–100 %): new Uniques, Set items and Legendaries roll every value in the best part of its range. At 80 each value comes from the best fifth of its range; at 100 every value is the best it can be (a perfect roll). The game decides which end is best for each value, so a downside rolls as small as it can. This covers drops, rewards, Nemesis, Spawn Items and Weaver cocoons when they open; gamblers, crafting rerolls and items you already have do not change. Off at 0.
- **Desktop app: Spawn Items works again.** It refused every spawn with "The loaded character has changed" unless the Circle of Fortune page had been read for the same character. The F6 window was not affected.

Last Epoch 1.5.3 (Steam), offline only. VirusTotal: version.dll 1/71, InGame ZIP 1/68. The only detection is Kaspersky's heuristic `VHO:Trojan.Win32.Dllhijack.gen`: it flags the way the mod is loaded (a `version.dll` the game loads from its own folder), a fresh build of the previous version gets the same, and every other engine finds nothing.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

Not uploaded: the owner keeps Nexus on 0.1.2-dev.46 for now (2026-10-29) because of the Kaspersky heuristic.
The texts below are ready for later.

- Version: 0.1.2-dev.47 (file description unchanged)
- Changelog (one line per entry):
  - New setting in General settings: Unique minimum roll (0–100 %). New Uniques, Set items and Legendaries roll their values in the best part of each range; at 100 every value is the best it can be.
- Description (`description-dev47.bbcode`): version line `0.1.2-dev.47`; General line names best-rolled
  Uniques; Safety line with the new VirusTotal results.

Published 2026-10-29 on GitHub only, with the owner's approval ("Nexusmodstan uzak duralım, GitHub'a hem exe hem
non-exe yayınlayalım"): release `v0.1.2-dev.47` (latest, both ZIPs, digests checked; main = 3779187; the app updater
offers it as installable from 0.1.2-dev.46). Nexus stays on 0.1.2-dev.46.

## Discord announcement (draft; the owner sends it)

> @everyone
> **EpochPact 0.1.2-dev.47**
> • **Unique minimum roll:** new setting in *General settings* (0–100 %). New Uniques, Set items and Legendaries roll their values in the best part of each range; at 100 they drop perfectly rolled.
> • **Fix – Spawn Items in the desktop app:** it no longer refuses every spawn with *"The loaded character has changed"*. (The F6 window was not affected.)
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.47
> **Manual install, no EXE:** download `EpochPact-0.1.2-dev.47-InGame.zip` from the GitHub link above and copy the new `version.dll` over the old one (Nexus follows later).
