# 0.1.2-dev.54: release texts

dev.54 = dev.53 + two player reports (2026-11-01): finish the campaign at the current level ("complete the campaign while
remaining at level 1") and the Omen idol affix missing in Spawn Items ("Increased Lightning Damage (Doubled If You Have
Over 300 Max Mana)" for a Large Arcane Omen Idol). Details: `docs/progression.md` (Character level choice) and
`docs/player-conveniences.md` (Omen idols).

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev54-desktop\share\EpochPact-0.1.2-dev.54.zip` (36,825,063 bytes) | GitHub only | `220b433cefcfbbf78f7e10f0723c0fcb2ca02694605eb2ac5160532225b2f960` |
| `release-dev54-package\EpochPact-0.1.2-dev.54-InGame.zip` (3,724,858 bytes) | GitHub and Nexus | `15ddc19147b8f3359b9074d2d1c30b4fa814d7bb1f8283afc6e602549ee71121` |

Both ZIPs carry the same `version.dll` (`02fd64a6…`); `EpochPact.Core.dll` `28cf61b4…`,
`EpochPact.exe` `b177ff4d…`. Built from bc9962c (version; features e68f4dd), branch dev53 from facecb6 (dev.53
published). Package checks passed (desktop verify, connection test); Microsoft Defender (signatures 1.463.72.0) found
nothing. Tests: Python 397, JavaScript 246, native 26.
The player DLLs have no test commands (`crafttest`, `groundidols`, `buildtest`, `invaffixes` answer "unknown command").
Shipping check with this exact `version.dll` (`builds\dev54-final-live1`, EpFeedbackTest clone made level 1 with no
quests): version 0.1.2-dev.54; `questscomplete <id> lower` refused; `questscomplete <id> keep` completed 84 quests, level
1 and XP 20 unchanged (quest XP 0, no top-up), gold 40 -> 102,041, passive points 0 -> 15, idol slots 0 -> 8, all kept
after a restart; `spawnlist` gives the ten Omen idol bases their group (Grand 1, Large 2), `lootread` affix 254 groups
[1,2]; `spawncustom` made a Large Arcane Omen Idol with affix 254 (read back by the native check) and it was picked up;
dev.53 `setlevel` 1 -> 3. Feature checks with the test build: `builds\dev54-live2` (F6 Campaign card: Keep my current
level -> Apply -> confirm, level 1 kept with the other rewards; F6 Spawn Items: Large Arcane Omen Idol lists the three
"300 Max Mana" affixes, 254 spawned, picked up, kept after a restart), `builds\dev54-live1` (raise: 1 -> 55),
`builds\dev54-live3` (the select at full card width). The desktop app: render and bridge tests (the select, keep/raise
commands).
VirusTotal (1 November 2026): version.dll (scan pending), InGame ZIP (scan pending).

## GitHub release

Tag `v0.1.2-dev.54`, title: EpochPact 0.1.2-dev.54 — finish the campaign at your level, Omen idol affixes

```markdown
## Which download?

- **EpochPact-0.1.2-dev.54.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.54-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Finish the campaign and keep your level.** Campaign → *Complete main + side campaign* has a new **Character level** choice. *Keep my current level (no quest XP)* completes the campaign without quest experience, so a level 1 character stays level 1. Every other quest reward still comes: passive points, idol slots, gold, attributes, and Choose Mastery opens afterwards. *Raise to at least level 55* (the default) works as before.
- **Omen idols: every affix in Spawn Items.** The game lets Omen idols roll more affixes than their size: a Large Omen Idol also rolls the Huge and Adorned Idol affixes, a Grand Omen Idol the Ornate and Adorned ones. *Affixes (optional)* now lists them, e.g. "Increased Lightning Damage (Doubled If You Have Over 300 Max Mana)" on a Large Arcane Omen Idol.

Last Epoch 1.5.3 (Steam), offline only. VirusTotal: version.dll (scan pending), InGame ZIP (scan pending).
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

- Version: 0.1.2-dev.54
- File description (what is new, the owner's request):
  New in 0.1.2-dev.54: finish the campaign and keep your level (even level 1); Omen idols list all their affixes in Spawn Items. Install: copy version.dll next to Last Epoch.exe, play offline, press F6. Last Epoch 1.5.3. Not with MelonLoader.
- Changelog:
  - Campaign: finish the campaign and keep your current level (no quest XP); the other quest rewards still come.
  - Spawn Items: Omen idols list every affix the game rolls on them (Large: Huge and Adorned Idol affixes too; Grand: Ornate and Adorned).
- Description (`description-dev54.bbcode`): version line; Crafting line (Omen idols); Campaign line (keep your level);
  Safety line with the new VirusTotal results.

## Discord announcement (draft; the owner sends it)

> @everyone
> **EpochPact 0.1.2-dev.54**
> • **Finish the campaign and keep your level:** Campaign → *Complete main + side campaign* → **Character level: Keep my current level (no quest XP)**. Start a new character at level 1 with the whole campaign done: passive points, idol slots, gold and Mastery come, your level stays. The default still raises you to level 55.
> • **Omen idols, every affix:** Crafting → Spawn Items now lists all affixes an Omen idol can roll (Large Omen Idols also get the Huge and Adorned Idol affixes, Grand ones the Ornate and Adorned). E.g. "Increased Lightning Damage (Doubled If You Have Over 300 Max Mana)" on a Large Arcane Omen Idol.
> Thanks for the reports!
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.54
> **Manual install, no EXE:** download the main file from Nexus (https://www.nexusmods.com/lastepoch/mods/44?tab=files) or `EpochPact-0.1.2-dev.54-InGame.zip` from the GitHub link above, and copy the new `version.dll` over the old one.
