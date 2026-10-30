# 0.1.2-dev.51: release texts

dev.51 = dev.50 + three player requests (2026-10-30): class tags on affixes, gear above your level, crafting
corrupted items; and a player report: Custom Items offered idol affixes for gear (they stop at T1). Details: `docs/player-conveniences.md` (Class-specific affixes, gear above your level, corrupted
items) and `docs/build-controls.md` (Gear above your level).

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev51-desktop\share\EpochPact-0.1.2-dev.51.zip` (36,643,848 bytes) | GitHub only | `234f1f23f055fb2285b71c9c573bbb39746cd438290c7a6016815ecacb0ad667` |
| `release-dev51-package\EpochPact-0.1.2-dev.51-InGame.zip` (3,667,186 bytes) | GitHub and Nexus | `200909f9cf64715c4113fb30b611e8d1fafbc9ea441eb57c3691df0756bbdb9f` |

Both ZIPs carry the same `version.dll` (`0db7cdf5…`); `EpochPact.Core.dll` `42879632…`,
`EpochPact.exe` `83400050…`. Built from a14bd48 (the Custom Items idol fix; version 698b5d9, the features f23253e). Package checks passed (desktop
verify, connection test); Microsoft Defender (signatures 1.463.34.0) found nothing. Tests: Python 396, JavaScript 244, native 26.
The player DLLs have no test commands (`groundidols`, `droptest`, `buildtest`, `stashrulestest`, `altarcheck`, `methodrva`,
`crafttest` answer "unknown command"). Shipping check with this exact `version.dll` (`builds\dev51-final-live`, EpFeedbackTest
clone): version 0.1.2-dev.51; out-of-range values refused (`buildset anylevel 2`, `craftcorrupted 2` among them); `buildset
anylevel 1` and `craftcorrupted 1` on (forge and capability hooks in); `lootread` 1156 affixes, 629 with classes, 492 idol-only;
the dev.50 checks again (the affix hook kept with Circle of Fortune's Exalted at 2, three spawned Weaver idols with 6 affixes
each, a look kept through a restart, `transmog all none`); after the restart both new session settings are off again. The
features with the test build of the same code: `builds\three-live2` (helmet 65 and gloves 85 worn by a level 55 character with
the setting on, refused off, stats applied, still worn after a restart; a corrupted 0-FP forge item refused, then crafted twice
T1 -> T3, still corrupted with 0 FP in memory, in its saved form and after a restart), `builds\three-live4` and
`builds\idol-live1` (Custom Items with the class tags; "ward per" lists 14 affixes instead of 30, the idols' "Ward Per Second"
gone). The owner tried the three requests on the EpCraftTest clone with the test items ("çalışıyor").
VirusTotal (30 October 2026): version.dll 0/70, InGame ZIP 0/65.

## GitHub release

Tag `v0.1.2-dev.51`, title: EpochPact 0.1.2-dev.51 — class tags on affixes, gear above your level, corrupted crafting

```markdown
## Which download?

- **EpochPact-0.1.2-dev.51.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.51-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Class-specific affixes are marked.** When you choose affixes for a custom item (Crafting → Custom Items), an affix that belongs to one class shows that class in a tag beside it ("Primalist", "Mage"…). The wanted-affix lists (Loot & Pickup, Weaver's Will) add the class after the name.
- **Custom Items lists equipment affixes only.** Idol affixes are no longer offered for gear: they cannot roll on equipment and have one tier, so one picked by mistake (an idol's "Ward Per Second" beside the gear's "Ward per Second") stopped at T1.
- **Wear gear above your level** (Equipment → Equipment rules). Equip items whose level requirement is above your character's level. Class, faction and the other requirements stay. Switched off, what you already wear stays on.
- **Craft corrupted items** (Crafting → Forge Settings, then *Apply changes*). A corrupted item in the forge can be crafted as before it was corrupted. It has no Forging Potential left, so a craft costs none (the forge shows 1), and the item stays corrupted.

Last Epoch 1.5.3 (Steam), offline only. VirusTotal: version.dll 0/70, InGame ZIP 0/65.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.51 (file description unchanged)
- Changelog:
  - Custom Items: class-specific affixes show their class in a tag; idol affixes are no longer offered for gear (they stopped at T1).
  - Equipment: Wear gear above your level (class and other requirements stay).
  - Crafting: Craft corrupted items (no Forging Potential cost, the item stays corrupted).
- Description (`description-dev51.bbcode`): version line `0.1.2-dev.51`; the Crafting line (craft corrupted items,
  class-specific affixes marked); the Skills, Equipment, Summons line (gear above your level); Safety line with the new
  VirusTotal results.

## Discord announcement (draft; the owner sends it)

> @everyone
> **EpochPact 0.1.2-dev.51**
> • **Class-specific affixes marked:** in Crafting → Custom Items, an affix that belongs to one class shows the class beside it.
> • **Custom Items fix:** idol affixes are no longer in the list for gear (an idol's "Ward Per Second" stopped at T1).
> • **Wear gear above your level:** Equipment → Equipment rules. Class and the other requirements stay.
> • **Craft corrupted items:** Crafting → Forge Settings (then Apply changes). No Forging Potential cost, the item stays corrupted.
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.51
> **Manual install, no EXE:** download the main file from Nexus (https://www.nexusmods.com/lastepoch/mods/44?tab=files) or `EpochPact-0.1.2-dev.51-InGame.zip` from the GitHub link above, and copy the new `version.dll` over the old one.

## Published 2026-10-30 (owner: "yayınla, nexus'a da yükle")

- GitHub: release `v0.1.2-dev.51` (Latest, not a prerelease) with both ZIPs; the asset digests are the SHA-256 values
  above; target and `main` = 33a14d0 (fast-forward from 181da5d). `/releases/latest` lists
  `EpochPact-0.1.2-dev.51.zip`, so the app's updater offers it.
- Nexus mod 44: main file "EpochPact - in-game edition (manual install)" updated to 0.1.2-dev.51 with the InGame ZIP
  (dev.50 archived), mod version 0.1.2-dev.51, the three changelog lines above; description lines changed (version,
  Crafting, Skills/Equipment/Summons, Safety with VirusTotal 0/70 and 0/65). The public page shows the new version and
  description; virus scan "Safe to use".
- Discord: the owner sends the announcement (draft above).
