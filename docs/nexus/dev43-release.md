# 0.1.2-dev.43: release texts

dev.43: wear any number of primordial items, and Reset all settings to default (two player requests),
both editions. Details and live checks: `docs/build-controls.md` (Equipment),
`docs/startup-profiles-2026-10-25.md` (Reset all settings).

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev43-desktop\share\EpochPact-0.1.2-dev.43.zip` (36,088,925 bytes) | GitHub only | `0d8e01137c2920e478a377a20fd828c66358f91e93632f63c068873163f7fcbc` |
| `release-dev43-package\EpochPact-0.1.2-dev.43-InGame.zip` (3,489,987 bytes) | GitHub and Nexus | `e54291e9f063a14b3d42033af35255960c4a267a248119b65c1273b6b697fa85` |

Both ZIPs carry the same `version.dll` (`a81f8bbb…`); `EpochPact.Core.dll` `baa5cad1…`,
`EpochPact.exe` `b0a3025a…`. Built from 2595bcc (feature 8536bec). Package checks passed (desktop
verify, connection test); the player version.dll has no test fixtures; Microsoft Defender
(signatures 1.461.199.0) found nothing. VirusTotal: `docs/security/virustotal-20261025-dev43.json`.

## GitHub release

Tag `v0.1.2-dev.43`, title: EpochPact 0.1.2-dev.43 — Wear any number of primordial items, reset all settings

```markdown
## Which download?

- **EpochPact-0.1.2-dev.43.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.43-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Wear any number of primordial items.** New switch on the Equipment page: wear more than one primordial item (Primordial Unique or Exalted) and get the bonuses of all of them. Before, the game allowed only one. Switched off, the game's own limit returns: while you wear more than one, their bonuses stop until you take the extra items off. Your save keeps only the items you wear.
- **Reset all settings to default.** Settings → Draft profiles has a new button that returns every EpochPact setting to the game's normal value at once, also in your last used settings, so a restart does not bring them back. Saved profiles, character bonuses and changes in your save are kept.

Last Epoch 1.5.3 (Steam), offline only. VirusTotal: version.dll 0/69, InGame ZIP 0/67.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.43 (file description unchanged)
- Changelog: New switch on the Equipment page: wear any number of primordial items (Primordial
  Unique or Exalted) and get the bonuses of all of them. Switched off, the game's limit of one
  returns. New: Reset all settings to default (Settings), which returns every setting to the game's
  normal value at once; saved profiles and character bonuses are kept.
- Description (`description-dev43.bbcode`): version line `0.1.2-dev.43`; Skills/Equipment line
  names primordial items; Safety line "VirusTotal (25 October 2026): version.dll 0/69, ZIP 0/67".

## Discord announcement (draft; the owner sends it)

> @everyone
> **EpochPact 0.1.2-dev.43**
> • **Wear any number of primordial items:** turn on the new switch in *Equipment* (F6 window or desktop app) and every primordial item you wear gives its bonuses, not just one.
> • **Reset all settings to default:** one button in *Settings* puts every EpochPact setting back to the game's normal value. Your saved profiles stay.
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.43
> **Manual install, no EXE:** copy the new `version.dll` over the old one (GitHub link above, or Nexus: https://www.nexusmods.com/lastepoch/mods/44)
