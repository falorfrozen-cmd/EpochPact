# 0.1.2-dev.42: release texts

dev.42: saved profiles apply reliably; profiles can be exported and imported as files.
Report: `docs/startup-profiles-2026-10-25.md`.

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev42-desktop\share\EpochPact-0.1.2-dev.42.zip` (36,013,054 bytes) | GitHub only | `d3decf6325ba2fb1985175d07cf0400f7fe8f990cbe7741d497a95d4d7ae22b6` |
| `release-dev42-package\EpochPact-0.1.2-dev.42-InGame.zip` (3,465,422 bytes) | GitHub and Nexus | `9e983971c551549ca03b4856a9f32bcca6e1e0e5be23f6df7276bc6a8611a445` |

Both ZIPs carry the same `version.dll` (`5fa9daa2…`); `EpochPact.Core.dll` `40386b10…`,
`EpochPact.exe` `81af229c…`. Package checks passed (desktop verify, connection test); Microsoft
Defender (signatures 1.461.194.0) found nothing. VirusTotal not scanned yet.

## GitHub release

Tag `v0.1.2-dev.42`, title: EpochPact 0.1.2-dev.42 — Saved profiles fixed, profile export/import

```markdown
## Which download?

- **EpochPact-0.1.2-dev.42.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.42-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Saved profiles apply reliably.** At startup and after a character change, the in-game window now checks the game's values first and sends only what differs: a full profile in about 10 seconds instead of 20, last used settings in about 1 second. It says "Applying …" and "Applied …", and the window shows the applied values.
- **A refused setting no longer stops the rest.** In a startup profile and in Apply changes, a setting the game refuses stays in the draft and all other settings are still applied.
- **Export and import profiles.** Settings → Draft profiles → **Export profile** saves one profile as a small .json file (`%LOCALAPPDATA%\EpochPact\profile-exports`) to share. **Import profile…** adds a shared file as a new profile; your existing profiles are never replaced.

Last Epoch 1.5.3 (Steam), offline only. VirusTotal: (after scanning).
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.42 (file description unchanged)
- Changelog: Saved profiles apply reliably at startup and after a character change (only the
  settings that differ are sent; a refused setting no longer stops the rest, also in Apply
  changes). New: Export profile / Import profile in Settings → Draft profiles.
- Description: version line `0.1.2-dev.42`; Safety line with the new VirusTotal results.

## Discord announcement (draft; owner approves the text first)

> @everyone
> **EpochPact 0.1.2-dev.42**
> • **Saved profiles fixed:** after a restart or a character change your profile is applied again, faster, with an "Applying … / Applied …" notice. A setting the game refuses no longer stops the rest of the profile (also in Apply changes).
> • **New: Export / Import profile** in Settings → Draft profiles. Share your setup as a small .json file; importing adds it as a new profile and never replaces yours.
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.42
> **Manual install, no EXE:** copy the new `version.dll` over the old one (GitHub link above, or Nexus: https://www.nexusmods.com/lastepoch/mods/44)
