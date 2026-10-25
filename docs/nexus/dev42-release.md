# 0.1.2-dev.42: release texts

dev.42: saved profiles apply reliably; character bonuses are remembered per character; stored
colours come back after a restart (F6); profiles can be exported and imported as files.
Report: `docs/startup-profiles-2026-10-25.md`.

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev42-desktop\share\EpochPact-0.1.2-dev.42.zip` (36,080,682 bytes) | GitHub only | `c677f317df27106f644c9e22ef0cb202769bc7a77810a4f26aa2635e32eca143` |
| `release-dev42-package\EpochPact-0.1.2-dev.42-InGame.zip` (3,487,491 bytes) | GitHub and Nexus | `c7ac027c1863b43d9e543a444421a96ffe2d5d9b8fb571ae06fba164d197661e` |

Both ZIPs carry the same `version.dll` (`e875e265…`); `EpochPact.Core.dll` `edcd380b…`,
`EpochPact.exe` `2b502401…`. Built from 4301d73 (with remembered character bonuses; the earlier
dev.42 build from 73feb5e is in the `release-dev42-*-old` folders, never published). Package
checks passed (desktop verify, connection test); Microsoft Defender (signatures 1.461.194.0) found
nothing. VirusTotal: `docs/security/virustotal-20261025-dev42.json`.

## GitHub release

Tag `v0.1.2-dev.42`, title: EpochPact 0.1.2-dev.42 — Character bonuses remembered, saved profiles fixed, profile export/import

```markdown
## Which download?

- **EpochPact-0.1.2-dev.42.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.42-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Character bonuses are remembered.** Bonuses from the Character bonuses page (health and mana regeneration, area, resistances and all others) are kept for each character and applied again when that character loads after a restart or a character change. Before, they were gone when the game closed. Reset all bonuses removes them; with Apply at startup Off they are not applied.
- **Saved profiles apply reliably.** At startup and after a character change, the in-game window now checks the game's values first and sends only what differs: a full profile in about 10–15 seconds instead of 20, last used settings in about 1 second. It says "Applying …" and "Applied …", and the window shows the applied values.
- **A refused setting no longer stops the rest.** In a startup profile and in Apply changes, a setting the game refuses stays in the draft and all other settings are still applied.
- **Colours come back after a restart (F6 window).** The interaction area colour, the tier colours and the rule line colour are sent to the game again when your character is ready. Before, the window showed your colour but the game drew its default until you chose it again.
- **Export and import profiles.** Settings → Draft profiles → **Export profile** saves one profile as a small .json file (`%LOCALAPPDATA%\EpochPact\profile-exports`) to share. **Import profile…** adds a shared file as a new profile; your existing profiles are never replaced.

Last Epoch 1.5.3 (Steam), offline only. VirusTotal: version.dll 0/67, InGame ZIP 0/66. In the Desktop ZIP only Microsoft's machine-learning engine flags EpochPact.exe and EpochPact.Core.dll (1 engine; the dev.41 EXE with the same code is clean, and Microsoft Defender with cloud protection finds nothing).
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.42 (file description unchanged)
- Changelog: Character bonuses (health/mana regeneration, area and all others) are remembered for
  each character and come back after a restart. Saved profiles apply reliably at startup and after a character change (only the
  settings that differ are sent; a refused setting no longer stops the rest, also in Apply
  changes). The interaction area colour, tier colours and rule line colour come back after a
  restart. New: Export profile / Import profile in Settings → Draft profiles.
- Description (`description-dev42.bbcode`): version line `0.1.2-dev.42`; Safety line
  "VirusTotal (25 October 2026): version.dll 0/67, ZIP 0/66".

Published 2026-10-25 with the owner's approval: GitHub release `v0.1.2-dev.42` (latest, both ZIPs;
main = 971c30c; the app updater offers it as installable from 0.1.2-dev.41), Nexus main file
updated to 0.1.2-dev.42 (dev.41 archived to Old files), mod version, changelog and page
description (version and Safety lines).

## Discord announcement (draft; owner approves the text first)

> @everyone
> **EpochPact 0.1.2-dev.42**
> • **Character bonuses stay:** health/mana regen, area and every other Character bonus is remembered per character and comes back after a restart.
> • **Saved profiles fixed:** after a restart or a character change your profile is applied again, faster, with an "Applying … / Applied …" notice. A setting the game refuses no longer stops the rest of the profile (also in Apply changes).
> • **Colours stay after a restart:** interaction area, tier and rule line colours no longer need to be chosen again (F6 window).
> • **New: Export / Import profile** in Settings → Draft profiles. Share your setup as a small .json file; importing adds it as a new profile and never replaces yours.
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.42
> **Manual install, no EXE:** copy the new `version.dll` over the old one (GitHub link above, or Nexus: https://www.nexusmods.com/lastepoch/mods/44)
