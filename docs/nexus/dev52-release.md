# 0.1.2-dev.52: release texts

dev.52 = dev.51 + a player report (2026-10-30): "Throwing damage is added as physical damage" in Custom Items.
Details: `docs/player-conveniences.md` (Custom Items: affixes the game converts).

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev52-desktop\share\EpochPact-0.1.2-dev.52.zip` (36,661,373 bytes) | GitHub only | `421f29f43c71fbe55bce27eb66158ff8a241a42d366058d8ffbf188b1c0b9514` |
| `release-dev52-package\EpochPact-0.1.2-dev.52-InGame.zip` (3,672,787 bytes) | GitHub and Nexus | `8cf1a38c6cd98a5d439ba1f06615a118a0ba0f78989c69eb28f6074b6c516ed7` |

Both ZIPs carry the same `version.dll` (`4f7f2ca5…`); `EpochPact.Core.dll` `71f57119…`,
`EpochPact.exe` `82ad612f…`. Built from ed823bd (version; the fix 1543ed3), branch fix52 from f7adbc8 (dev.51
published). Package checks passed (desktop verify, connection test); Microsoft Defender (signatures 1.463.34.0) found
nothing. Tests: Python 396, JavaScript 245, native 26.
The player DLLs have no test commands (`groundidols`, `droptest`, `buildtest`, `stashrulestest`, `altarcheck`, `methodrva`,
`crafttest` answer "unknown command"). Shipping check with this exact `version.dll` (`builds\dev52-final-live`, EpFeedbackTest
clone): version 0.1.2-dev.52; `lootread` 1156 affixes, 9 with `convertTo` (Throwing Damage 88 -> 30, rolls on Belt, Gloves,
Amulet, Relic); the dev.51 and dev.50 checks again (out-of-range values refused, `buildset anylevel 1` and `craftcorrupted 1`
on, 629 affixes with classes, 492 idol-only, the affix hook kept with Circle of Fortune's Exalted at 2, three spawned Weaver
idols with 6 affixes each, a look kept through a restart, `transmog all none`). The fix with the test build of the same code:
`builds\convert-live1` (Dexterity T1 + Throwing Damage T7 refused on a body armor with the forge item unchanged, kept on a belt,
the same in the saved form and after a restart; a helmet with Dexterity T7 as before) and `builds\convert-ui1` (F6 Custom Items:
"Throwing Damage -> Physical Damage" on a body armor and the click refused with the reason; on a belt the tag gone and the affix
chosen).
VirusTotal (31 October 2026): version.dll 0/70, InGame ZIP 0/67.

## GitHub release

Tag `v0.1.2-dev.52`, title: EpochPact 0.1.2-dev.52 — Custom Items keeps Throwing Damage

```markdown
## Which download?

- **EpochPact-0.1.2-dev.52.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.52-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Custom Items fix: Throwing Damage no longer becomes Physical Damage.** EpochPact built every chosen affix as if the item were a helmet, and the game turns some affixes into another one on an item type where they cannot roll (Throwing Damage became Physical Damage on every base). Affixes are now built for your item's own type, so Throwing Damage stays Throwing Damage on a belt, gloves, amulet or relic. Eight affixes behaved like this, among them Increased Chance to Find Potions (became Armor) and increased Area for Area Skills (became Throwing Attack Speed).
- **Custom Items shows what the game would change.** When such an affix cannot be on the item in the forge, the affix list shows what the game turns it into ("→ Physical Damage") and does not select it; Create explains it instead of making a different item. An item made before this fix keeps its converted affix; make it again.

Last Epoch 1.5.3 (Steam), offline only. VirusTotal: version.dll 0/70, InGame ZIP 0/67.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.52 (file description unchanged)
- Changelog:
  - Custom Items: Throwing Damage (and seven other affixes the game converts) is built for the item's own type and no longer becomes Physical Damage.
  - Custom Items: an affix the game would change on the forge item shows "→ <affix>" and cannot be selected.
- Description (`description-dev52.bbcode`): version line `0.1.2-dev.52`; Safety line with the new VirusTotal results.

## Discord announcement (draft; the owner sends it)

> @everyone
> **EpochPact 0.1.2-dev.52**
> • **Custom Items fix:** Throwing Damage no longer turns into Physical Damage. Affixes are now made for your item's own type (Throwing Damage can be on a belt, gloves, amulet or relic). Seven other affixes had the same problem.
> • **Custom Items:** if the game would change an affix on the item in the forge, the list shows what it becomes ("→ Physical Damage") and does not select it.
> An item made before this fix keeps its changed affix; make it again.
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.52
> **Manual install, no EXE:** download the main file from Nexus (https://www.nexusmods.com/lastepoch/mods/44?tab=files) or `EpochPact-0.1.2-dev.52-InGame.zip` from the GitHub link above, and copy the new `version.dll` over the old one.

## Published 2026-10-31 (owner: "yayınla, nexus'a da yükle")

- GitHub: release `v0.1.2-dev.52` (Latest, not a prerelease) with both ZIPs; the asset digests are the SHA-256 values
  above; target and `main` = 20b4451 (fast-forward from 33a14d0). `/releases/latest` lists
  `EpochPact-0.1.2-dev.52.zip`, so the app's updater offers it.
- Nexus mod 44: main file "EpochPact - in-game edition (manual install)" updated to 0.1.2-dev.52 with the InGame ZIP
  (dev.51 archived), mod version 0.1.2-dev.52, the two changelog lines above; description lines changed (version,
  Safety with VirusTotal 0/70 and 0/67). The public page shows the new version and description.
- Discord: the owner sends the announcement (draft above).
