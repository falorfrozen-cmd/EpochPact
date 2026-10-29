# 0.1.2-dev.49: release texts

dev.49 replaces the unpublished dev.48 (its item categories were dropped by the owner: the game's own Tab Priority and
Stash all already route items). Since dev.47: the Wardrobe, the skills of your other masteries, stash tabs on pickup,
the idol fix with Circle of Fortune and always-the-most idols, plus dev.48's Ancient Bones multiplier, idol altars and
Weaver idols, Woven Enchanter uses, pick up from afar and the prophecy speed fix; both editions. Details and live
checks: `docs/player-conveniences.md` (Wardrobe; Stash tabs on pickup; Idols: always the most, and Circle of Fortune),
`docs/build-controls.md` (Skills of your other masteries), `docs/cof.md` (prophecy charge speed).

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev49-desktop\share\EpochPact-0.1.2-dev.49.zip` (36,630,340 bytes) | GitHub only | `fc180590b702516866ec5f591dcb3b4a64f233455e783f7b4dbeac4bb73a5802` |
| `release-dev49-package\EpochPact-0.1.2-dev.49-InGame.zip` (3,663,141 bytes) | GitHub; Nexus if the scans are clean | `0278345aaa544403ff49d952b3683de84f6f15f7f98c88ef6e3e7e1af05a1815` |

Both ZIPs carry the same `version.dll` (`c69d5f69…`); `EpochPact.Core.dll` `07267251…`,
`EpochPact.exe` `25790b4d…`. Built from d675210 (version; features 4f1a49d; prophecy fix d77a6da). Package checks
passed (desktop verify, connection test); Microsoft Defender (signatures 1.463.30.0) found nothing. Tests: Python 396, JavaScript 244, native 26.
The player DLLs have no test commands (`groundidols`, `droptest`, `buildtest`, `stashrulestest`, `altarcheck`, `methodrva`
answer "unknown command"), and the removed item categories are gone (`catread`, `catauto` unknown). Shipping check with
this exact `version.dll` (`builds\dev49-final-live` and `builds\dev49-final-live2`, EpFeedbackTest clone): version
0.1.2-dev.49; out-of-range values refused (`stashpickup 2`, `idolmax 2`, `buildset masteryskills 2`, an unknown slot or
look); idols 3/3 with always-the-most, stash tabs on pickup, pick up from afar and the other masteries' skills switched
on (hooks in, `masteryskills` available); with Circle of Fortune's Exalted multiplier at 2 the affix hook stayed in and
three spawned Weaver idols came with 6 affixes each (3 + 3); the F6 Wardrobe page tried The Kestrel on, Apply kept it,
and after a restart it was applied again as the character loaded (`changed 1`); the mirror read back (JPEGs of
38-40 KB); `transmog all none` took it off. Stash tabs on pickup left two picked-up idols in the inventory where no tab
priority matched them (`no matching tab 2`). Earlier live checks of the same code with the test build: builds\wd-live7
(Wardrobe), builds\idol-old / idol-new / idol-max (the CoF hook conflict before and after the fix, always the most),
builds\ms-live2 (a Spellblade with 210 Sorcerer points: Arcane Ascendance and Black Hole known and on the bar with the
setting, gone with it off).
VirusTotal (29 October 2026): version.dll 0/69, InGame ZIP 0/67.

## GitHub release

Tag `v0.1.2-dev.49`, title: EpochPact 0.1.2-dev.49 — Wardrobe, other masteries' skills, stash tabs on pickup and more

```markdown
## Which download?

- **EpochPact-0.1.2-dev.49.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.49-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Wardrobe.** A new page in F6 and in the app. The seven slots show the look each one wears with the game's own item pictures; a live mirror shows your character (drag it to turn, Close-up for the head and chest); beside it every look that fits the selected item: base items, Uniques and Sets, with filters (All, Base items, Uniques, Sets, In your bags) and a search. **Click a look to try it on** at once; try several slots, then **Apply looks** keeps them or **Cancel** takes them off (a try-on also ends by itself when you leave the page). A look must be an item of the same type. Base helmets and body armours made for other classes are left out: the game has no art of them for your class. Looks stay through zone changes and restarts; display only, shop cosmetics are not touched.
- **Skills of your other masteries.** New setting on the Skills & Trees page: the skills of your class's other masteries unlock with the points you put in their passive tree, as for your own mastery (for a Forge Guard with 35 Paladin points: Judgement and Symbols of Hope). The skill a mastery gives when it is chosen (such as Holy Aura) stays with it. Switched off, those skills leave your skill bar. Other classes' skills are not included.
- **Stash tabs on pickup.** New switch on the Loot & Pickup page: an item you pick up goes straight to the stash tab whose **Tab Priority** it matches (set it in the game's Configure Tab, on a tab or its folder), the tab Ctrl+click and Stash all would choose. Items that match no tab stay in your inventory.
- **Idols: fixed, and always the most.** With Circle of Fortune's Exalted or T7 multiplier on, idols got no extra prefixes and suffixes; fixed. New switch **Always the most idol affixes** in General settings: every new idol gets exactly the number you set (3 prefixes and 3 suffixes at 3) instead of 1 to that many; idol altars one more.
- **Idol altars and Weaver idols with more affixes.** The Idol prefixes and Idol suffixes settings also cover idol altars and Weaver idols. Rare altars drop with 2 prefixes and 2 suffixes; at 3 they roll 2 to 4 of each, from the game's own altar affixes. Weaver idols roll 1 to 3 of each, and their extra affixes can be Weaver affixes too.
- **Prophecies: charge speed fixed.** With a high experience or Favor multiplier a prophecy filled at once, even with Prophecy charge speed at 0. Now the multipliers raise your Favor but not the prophecy speed; Prophecy charge speed alone decides it (1 the game's own, 0 stops prophecies, up to 100).
- **Ancient Bones multiplier.** New setting in General settings (1–100x): the Ancient Bones that drop for you are multiplied, and the pile on the ground already shows the larger amount. Off at 1.
- **Woven Enchanter uses.** New setting in General settings (1–40x): each Woven Enchanter you meet allows this many times its usual uses, so "1/1 uses remain" becomes 20/20 at 20. It also applies to an Enchanter you are standing at. Each craft still costs its Memory Amber. Off at 1.
- **Pick up from afar.** New switch on the Loot & Pickup page: click an item's name on the ground and it goes into your inventory where it lies; your character does not walk there. With no room in the inventory the game walks you there as usual.

Last Epoch 1.5.3 (Steam), offline only. VirusTotal: version.dll 0/69, InGame ZIP 0/67.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

Upload only with the owner's approval and clean scans. Nexus is on 0.1.2-dev.46, so the changelog also names dev.47's
Unique minimum roll.

- Version: 0.1.2-dev.49 (file description unchanged)
- Changelog (one line per entry):
  - Wardrobe: try any look on your character in a live mirror before you keep it (base items, Uniques and Sets, game art).
  - Skills of your other masteries: their skills unlock with the points in their passive tree (Skills & Trees).
  - Stash tabs on pickup: picked-up items go straight to the stash tab whose Tab Priority they match.
  - Idols: extra prefixes and suffixes work again with Circle of Fortune's Exalted or T7 multiplier; new switch Always the most idol affixes.
  - Idol prefixes and suffixes now also cover idol altars (2 to 4 of each at 3) and Weaver idols.
  - Prophecies no longer fill at once with high experience or Favor multipliers; Prophecy charge speed 0 stops them.
  - New settings in General settings: Ancient Bones multiplier (1–100x) and Woven Enchanter uses (1–40x).
  - Pick up from afar: click an item on the ground and it comes to your inventory without walking.
  - Unique minimum roll (from 0.1.2-dev.47): new Uniques, Sets and Legendaries roll in the best part of each range.
- Description (`description-dev49.bbcode`): version line `0.1.2-dev.49`; General line names always-the-most idols;
  Loot line names stash tabs on pickup; new Wardrobe line; Skills line names the other masteries' skills; Safety line
  with the new VirusTotal results.

## Discord announcement (draft; the owner sends it)

> @everyone
> **EpochPact 0.1.2-dev.49**
> • **Wardrobe:** a new page. Click any look (base items, Uniques, Sets) and your character wears it at once in a live mirror; *Apply looks* keeps it. Game art only, no shop cosmetics.
> • **Skills of your other masteries:** switch it on in *Skills & Trees*; e.g. a Forge Guard with 35 Paladin points gets Judgement and Symbols of Hope.
> • **Stash tabs on pickup:** switch it on in *Loot & Pickup*; picked-up items go straight to the stash tab whose *Tab Priority* they match.
> • **Idols fixed:** extra prefixes/suffixes work again with Circle of Fortune's Exalted/T7 multiplier; new *Always the most idol affixes* switch.
> • **Prophecies fixed:** high XP or Favor multipliers no longer fill them at once.
> • Also new since dev.47: Ancient Bones multiplier, more Woven Enchanter uses, idol altars and Weaver idols with more affixes, pick up from afar.
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.49
> **Manual install, no EXE:** download `EpochPact-0.1.2-dev.49-InGame.zip` from the GitHub link above and copy the new `version.dll` over the old one.

## Published 2026-10-29 (owner: "yayınla, nexus'a da yükle")

- GitHub: release `v0.1.2-dev.49` (Latest, not a prerelease) with both ZIPs; the asset digests are the SHA-256 values
  above; target and `main` = 917e35d (fast-forward from 3779187). `/releases/latest` lists
  `EpochPact-0.1.2-dev.49.zip`, so the app's updater offers it.
- Nexus mod 44: main file "EpochPact - in-game edition (manual install)" updated to 0.1.2-dev.49 with the InGame ZIP
  (dev.46 archived), mod version 0.1.2-dev.49, the nine changelog lines above; description lines changed (version,
  General, Loot & Pickup, a new Wardrobe line, Skills, Safety with VirusTotal 0/69 and 0/67). The public page shows the
  new version and description; virus scan "Safe to use".
- Discord: the owner sends the announcement (draft above).
