# 0.1.2-dev.53: release texts

dev.53 = dev.52 + four player requests (2026-11-01): T8 on any affix, affix tiers in place, Spawn with chosen
affixes (idols, idol altars, gear) and Set character level. Details: `docs/player-conveniences.md` (T8 on every affix,
tiers in place, character level, Spawn with chosen affixes) and `docs/build-controls.md` (Change affix tiers).

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev53-desktop\share\EpochPact-0.1.2-dev.53.zip` (36,798,091 bytes) | GitHub only | `051fe87be024205b2d2da64afabfa9be6f7235c185ec55a3a02c0b590a7ab784` |
| `release-dev53-package\EpochPact-0.1.2-dev.53-InGame.zip` (3,715,678 bytes) | GitHub and Nexus | `b41a65de8acc1d9940c05e0337f1e6bd3f373873a60e818f2cf6e1708c792186` |

Both ZIPs carry the same `version.dll` (`fca7ccc8…`); `EpochPact.Core.dll` `502f4b22…`,
`EpochPact.exe` `6344dab9…`. Built from fac08c6 (rarity fix for Change affix tiers; version b80a5a1, features 14eabc1, clarity acd7d63), branch dev53 from
eed7739 (dev.52 published). Package checks passed (desktop verify, connection test); Microsoft Defender (signatures
1.463.68.0) found nothing. Tests: Python 396, JavaScript 246, native 26.
The player DLLs have no test commands (`groundidols`, `droptest`, `buildtest`, `stashrulestest`, `altarcheck`, `methodrva`,
`crafttest` answer "unknown command"). Shipping check with this exact `version.dll` (`builds\dev53-final-live2`, EpFeedbackTest
clone): version 0.1.2-dev.53; `lootread` 1156 affixes, all with `rollsOn`; `crafttiers` took the forge Rare's affix to T8 and
back (bad tier refused); `setlevel` 55 -> 57, kept after a restart (a lower level and level 1 refused); `spawncustom` made a
helmet with seven T8 affixes, picked up and kept after a restart (a Unique refused); the dev.52 and earlier checks again
(converted affixes, class tags, anylevel, craftcorrupted, Weaver idols, Wardrobe looks). Feature checks with the test build:
`builds\dev53-native1`, `builds\dev53-ui1` (F6), `builds\dev53-hold1` (the desktop app with live data: tiers, Spawn with
affixes, character level), `builds\dev53-tiers-rare2` (an ordinary Rare to T8 and back, restart).
VirusTotal (1 November 2026): version.dll (scan pending), InGame ZIP (scan pending).

## GitHub release

Tag `v0.1.2-dev.53`, title: EpochPact 0.1.2-dev.53 — T8 on any affix, affix tiers, Spawn with chosen affixes, character level

```markdown
## Which download?

- **EpochPact-0.1.2-dev.53.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.53-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **T8 on every affix.** In Crafting → Custom Items any of the chosen affixes can be T8, not only one. All of them stay T8 when the game saves and loads the item. An item holds at most seven affixes: that is the game's save format.
- **Change affix tiers in place.** Crafting → Custom Items has a new panel, *Change affix tiers*: every affix of the item in the forge with a tier from T1 to T8. *Apply tiers* changes only those tiers; rolls and the rest of the item stay, and a recovery backup is made. The confirmation lists each change ("Fire Damage: T8 → T6").
- **Spawn with chosen affixes (idols, idol altars and gear).** Crafting → Spawn Items: after choosing a base item, open *Affixes (optional)*, search the affixes that item rolls, pick up to seven and give each a tier (any of them T8), then Spawn. Leave it empty for the game's own roll.
- **Set character level.** Campaign → *Set character level*: raise your character to a level from 2 to 100 with the game's own level-ups (passive and attribute points come with them). Lowering is not offered; a recovery backup is made.

Last Epoch 1.5.3 (Steam), offline only. VirusTotal: version.dll (scan pending), InGame ZIP (scan pending).
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.53 (file description unchanged)
- Changelog:
  - Custom Items: T8 on any of the chosen affixes (up to seven, the game's limit).
  - Custom Items: Change affix tiers of the forge item in place (T1-T8), with a recovery backup.
  - Spawn Items: spawn a base item, idol or idol altar with up to seven chosen affixes, each with its tier.
  - Campaign: Set character level (2-100, raise only).
- Description (`description-dev53.bbcode`): version line; Crafting line (T8 on any affix, change affix tiers, spawn
  with chosen affixes); Campaign line (character level); Safety line with the new VirusTotal results.

## Discord announcement (draft; the owner sends it)

> @everyone
> **EpochPact 0.1.2-dev.53**
> • **T8 on every affix:** in Crafting → Custom Items any of the chosen affixes can be T8, not only one (up to seven affixes, the game's limit).
> • **Change affix tiers in place:** Crafting → Custom Items → *Change affix tiers*. Pick a new tier (T1–T8) for any affix of the item in the forge and press *Apply tiers*. No new item needed; rolls stay and a backup is made.
> • **Spawn idols, altars and gear with your own affixes:** Crafting → Spawn Items → choose an item → *Affixes (optional)*. Search, pick up to seven, set each tier, then Spawn.
> • **Set character level:** Campaign → *Set character level* (2–100, raise only). Passive and attribute points come with it.
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.53
> **Manual install, no EXE:** download the main file from Nexus (https://www.nexusmods.com/lastepoch/mods/44?tab=files) or `EpochPact-0.1.2-dev.53-InGame.zip` from the GitHub link above, and copy the new `version.dll` over the old one.
