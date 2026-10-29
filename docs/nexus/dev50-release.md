# 0.1.2-dev.50: release texts

dev.50 = dev.49 + the Wardrobe click fix (owner report 2026-10-30: the window's Apply changes did nothing and the
clothes went 3-4 s after F6 closed; a click only tried the look on). Details: `docs/player-conveniences.md` (Wardrobe).

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev50-desktop\share\EpochPact-0.1.2-dev.50.zip` (36,621,380 bytes) | GitHub only | `0ba182b457d89afd5075141140b0098c705498de4840009ba5349c73a13cd003` |
| `release-dev50-package\EpochPact-0.1.2-dev.50-InGame.zip` (3,660,498 bytes) | GitHub and Nexus | `a0b18897674935da69af5e111638a2e673dc30be8a6fb9cbe01a50449f557740` |

Both ZIPs carry the same `version.dll` (`1320efc6…`); `EpochPact.Core.dll` `15547ef3…`,
`EpochPact.exe` `e824c186…`. Built from 9a58e29 (version; the fix 7bcdaf9). Package checks passed (desktop
verify, connection test); Microsoft Defender (signatures 1.463.30.0) found nothing. Tests: Python 396, JavaScript 244, native 26.
The player DLLs have no test commands (`groundidols`, `droptest`, `buildtest`, `stashrulestest`, `altarcheck`, `methodrva`
answer "unknown command"). Shipping check with this exact `version.dll` (`builds\dev50-final-live`, EpFeedbackTest
clone): version 0.1.2-dev.50; the dev.49 checks again (out-of-range values refused, the new settings switched on, the
affix hook kept with Circle of Fortune's Exalted at 2, three spawned Weaver idols with 6 affixes each, a look kept
through a restart, `transmog all none`). The fix itself, with the test build of the same code (`builds\wear-live1`):
in the F6 Wardrobe a click on The Kestrel (body) and on Adept Helm (helmet) stored both at once; F6 closed, 5 s later
both still worn and stored; after a restart both applied again as the character loaded; a click on the body's own look
put it back; Reset all looks cleared the helmet. The owner tried it in his own game ("çalışıyor").
VirusTotal (30 October 2026): version.dll 0/70, InGame ZIP 0/68.

## GitHub release

Tag `v0.1.2-dev.50`, title: EpochPact 0.1.2-dev.50 — Wardrobe: a click wears the look

```markdown
## Which download?

- **EpochPact-0.1.2-dev.50.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.50-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Wardrobe: a click wears the look.** In 0.1.2-dev.49 a click only tried a look on: it went again a moment after you closed F6 unless you pressed *Apply looks* below the grid (the window's *Apply changes* button is for settings and did nothing here). Now a click wears the look and keeps it at once, through zone changes and restarts; click the item's own look, or *Own look*, to put it back. *Reset all looks* clears every slot.

Everything else is as in 0.1.2-dev.49 (Wardrobe, skills of your other masteries, stash tabs on pickup, the idol fix and always-the-most idols, Ancient Bones, Woven Enchanter uses, pick up from afar, the prophecy speed fix).

Last Epoch 1.5.3 (Steam), offline only. VirusTotal: version.dll 0/70, InGame ZIP 0/68.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.50 (file description unchanged)
- Changelog: Wardrobe: a click now wears the look and keeps it at once (in 0.1.2-dev.49 it went again after F6 closed unless you pressed Apply looks).
- Description (`description-dev50.bbcode`): version line `0.1.2-dev.50`; the Wardrobe line says a click wears the look;
  Safety line with the new VirusTotal results.

## Discord announcement (draft; the owner sends it)

> @everyone
> **EpochPact 0.1.2-dev.50**
> • **Wardrobe fixed:** a click now wears the look and keeps it at once (in dev.49 the clothes went again after you closed F6 unless you pressed *Apply looks* below the grid). Click the item's own look or *Own look* to put it back.
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.50
> **Manual install, no EXE:** download the main file from Nexus (https://www.nexusmods.com/lastepoch/mods/44?tab=files) or `EpochPact-0.1.2-dev.50-InGame.zip` from the GitHub link above, and copy the new `version.dll` over the old one.
