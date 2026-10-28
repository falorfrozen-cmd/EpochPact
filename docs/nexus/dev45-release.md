# 0.1.2-dev.45: release texts

dev.45: Spawn Items with a practical finder, idols with up to 3 prefixes and 3 suffixes (player requests), the
gold multiplier fix and a caret in the F6 window's text boxes, both editions. Details and live checks:
`docs/player-conveniences.md` (Crafting → Spawn Items, Idol prefixes and suffixes, Gold multiplier and Recent
pickups).

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev45-desktop\share\EpochPact-0.1.2-dev.45.zip` (36,282,330 bytes) | GitHub only | `8fca61f5566354dd019dff104c5876b3e7b3d3233fcf6e235e5526fa72b866f6` |
| `release-dev45-package\EpochPact-0.1.2-dev.45-InGame.zip` (3,552,245 bytes) | GitHub and Nexus | `d40abf37fa7ee6cbfabf5e3689b7aae2d0c026fb99a5c76b8db5d7fbcb6cc488` |

Both ZIPs carry the same `version.dll` (`1e6d439a…`); `EpochPact.Core.dll` `e173d5ad…`,
`EpochPact.exe` `e92ba906…`. Built from a5f2bb0 (gold fix 5110bf2, Spawn Items bc5bb17 and 2bba9eb, idols, finder
and caret a5f2bb0). Package checks passed (desktop verify, connection test); the player DLLs have no test
commands (`idolcheck`, `spawncheck`, QA actions); Microsoft Defender (signatures 1.463.1.0) found nothing.
VirusTotal: `docs/security/virustotal-20261028-dev45.json`.

## GitHub release

Tag `v0.1.2-dev.45`, title: EpochPact 0.1.2-dev.45 — Spawn Items, idols with more affixes

```markdown
## Which download?

- **EpochPact-0.1.2-dev.45.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.45-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Spawn Items.** New tab on the Crafting page. Type a name or an item type (for example `calamity`, `helmet` or `set boots`) and the list below narrows as you type. Use **All items / Base items / Uniques / Set items** to filter it, then click an item. Only the options that fit that item are shown: rarity (Normal, Magic, Rare, Exalted) for base items, Legendary Potential (the game's roll or 0–4) for Uniques that can have it, item level (1–100) and how many (1, 5, 10 or 20). The game makes the item with its own rules and drops it next to your character, like a monster drop. Once you pick it up it is saved like any other item. Weaver's Will Uniques and Set items roll their own values.
- **Idols with more affixes.** Two new settings in General settings: **Idol prefixes** and **Idol suffixes** (1–3). At 3, each new idol rolls 1 to 3 prefixes and 1 to 3 suffixes instead of the game's 1 and 1. This covers idols from drops, rewards, Nemesis and Spawn Items; shop and gambler idols are not changed. The extra affixes follow the game's own rules for that idol (level, idol type, one class, no repeats, the usual weights). Weaver and Heretical idols and idols you already have do not change. At 1 the game's own rules apply.
- **Gold multiplier works again.** Since 0.1.2-dev.17 the setting was accepted but gold pickups were not raised, and turning it off also stopped gold from appearing in Recent pickups. Both are fixed.
- **Text boxes in the F6 window** now show a blinking cursor when you click them.

Last Epoch 1.5.3 (Steam), offline only. VirusTotal: version.dll 0/71, InGame ZIP 0/67.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.45 (file description unchanged)
- Changelog (one line per entry):
  - New Crafting tab, Spawn Items: find any base item, Unique or Set as you type and drop it at your feet (rarity, Legendary Potential, item level, up to 20).
  - New settings in General settings: Idol prefixes and Idol suffixes (1–3). New idols roll up to 3 of each, by the game's own affix rules.
  - The gold multiplier raises gold pickups again (broken since 0.1.2-dev.17).
  - F6 window: text boxes show a blinking cursor when clicked.
- Description (`description-dev45.bbcode`): version line `0.1.2-dev.45`; General line names idol prefixes and
  suffixes; Crafting line names Spawn Items; Safety line "VirusTotal (28 October 2026): version.dll 0/71, ZIP 0/67".

Published 2026-10-28 with the owner's approval ("devam et", then "nexusmodsta yayınla"): GitHub release
`v0.1.2-dev.45` (latest, both ZIPs, digests checked; main = 9532ee4; the app updater offers it as installable
from 0.1.2-dev.44), Nexus main file updated to 0.1.2-dev.45 (dev.44 archived, changelog with the file, mod version
updated with it) and page description (version, General, Crafting and Safety lines). Nexus's own virus scan of the
new file was still running when this was written.

## Discord announcement (draft; the owner sends it)

> @everyone
> **EpochPact 0.1.2-dev.45**
> • **Spawn Items:** new tab on the *Crafting* page. Type a name or type (e.g. *calamity*, *helmet*), click the item, choose rarity or Legendary Potential and how many: it drops at your feet.
> • **Idols with more affixes:** new *Idol prefixes* and *Idol suffixes* settings in *General settings* (1–3). New idols roll up to 3 prefixes and 3 suffixes, by the game's own rules.
> • **Gold multiplier** works again.
> • **F6 window:** text boxes show a blinking cursor.
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.45
> **Manual install, no EXE:** copy the new `version.dll` over the old one (GitHub link above, or Nexus: https://www.nexusmods.com/lastepoch/mods/44)
