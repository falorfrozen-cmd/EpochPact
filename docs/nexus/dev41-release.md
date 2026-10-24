# 0.1.2-dev.41: release texts

dev.41 carries the unreleased dev.40 (Last Epoch 1.5.3 support) and the T7 forge fix.

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev41-desktop\share\EpochPact-0.1.2-dev.41.zip` (35,927,953 bytes) | GitHub only | `6f8c2381663c2898e02094590a303157babfbf7c533e9e1a7df1047a7bf9bab5` |
| `release-dev41-package\EpochPact-0.1.2-dev.41-InGame.zip` (3,439,320 bytes) | GitHub and Nexus | `86c47f774c535353298abbfaa8725e2a73c0bc4ca2d36174472d92bdbfdaf2d1` |

Both ZIPs carry the same `version.dll` (`ca6fad53…`). Scans: `docs/security/virustotal-20261024-dev41.json`.

## GitHub release

Tag `v0.1.2-dev.41`, title: EpochPact 0.1.2-dev.41 — Last Epoch 1.5.3, T7 crafting fix

```markdown
## Which download?

- **EpochPact-0.1.2-dev.41.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.41-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Last Epoch 1.5.3 supported.** The desktop app's Game setup no longer refuses 1.5.3 with "Unsupported Last Epoch build". 1.5.2.1 still works.
- **Fix: Craft affixes up to T7.** After one affix reached T6/T7, the forge would not let you choose another affix. Every T5/T6 affix that can reach T7 now shows its upgrade arrow in the forge, so you can raise them one after another. The usual rules still apply: shards of that affix, Forging Potential left, no glyph or Basic Glyph of Hope.

Last Epoch 1.5.3 (Steam), offline only. VirusTotal: version.dll 0/70, EpochPact.exe 0/71, InGame ZIP 0/67.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.41 (file description unchanged)
- Changelog: Last Epoch 1.5.3 supported. Fix for "Craft affixes up to T7": after one affix reached
  T6/T7 the forge would not let you choose another affix; every T5/T6 affix that can reach T7 now
  shows its upgrade arrow.
- Description (`description-dev41.bbcode`): version line `0.1.2-dev.41 | Last Epoch 1.5.3 (Steam)`;
  "Made for Last Epoch 1.5.3 (1.5.2.1 works too)"; Safety line "VirusTotal (24 October 2026):
  version.dll 0/70, ZIP 0/67".
- File description: "... No EXE needed. For Last Epoch 1.5.3 (1.5.2.1 works too). Do not use with
  MelonLoader." (was "Last Epoch 1.5.2.1 only").

Published 2026-10-24: GitHub release `v0.1.2-dev.41` (latest; the app updater offers it from
0.1.2-dev.39), Nexus main file, mod version, file description and page description.

## Discord announcement (draft; owner approves the text first)

> @everyone
> **EpochPact 0.1.2-dev.41**
> • **Last Epoch 1.5.3 supported.** The desktop app no longer says "Unsupported Last Epoch build" after the game update.
> • **Fix, Craft affixes up to T7:** after one affix reached T6/T7, the forge would not let you choose another one. Every T5/T6 affix that can go to T7 now shows its upgrade arrow, so you can raise them one after another.
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.41
> **Manual install, no EXE:** copy the new `version.dll` over the old one (GitHub link above, or Nexus: https://www.nexusmods.com/lastepoch/mods/44)
