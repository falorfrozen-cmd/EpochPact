# 0.1.2-dev.48: release texts

dev.48: item categories (the owner's idea) and six player requests (Ancient Bones multiplier, idol altars and
Weaver idols with more affixes, Woven Enchanter uses, item looks with a mirror, pick up from afar), both editions. Details and live checks:
`docs/player-conveniences.md` (Item categories; Idol altars and Weaver idols; Woven Enchanter uses; Ancient Bones
multiplier; Item looks; Pick up from afar).

Files (C:\Users\falor\EpochPactLab\builds):

| File | Where | SHA-256 |
|---|---|---|
| `release-dev48-desktop\share\EpochPact-0.1.2-dev.48.zip` (36,657,694 bytes) | GitHub only | `1094c9431b5b7593a4656e2ca97667ff09bfaec64e870061804a1e58fe65e9fd` |
| `release-dev48-package\EpochPact-0.1.2-dev.48-InGame.zip` (3,672,112 bytes) | GitHub (Nexus held back for now) | `37e23994038c6cf16a11f042c83d80289f38fada9a412a13b111e69509edabb1` |

Both ZIPs carry the same `version.dll` (`c3e66022…`); `EpochPact.Core.dll` `3c951bd4…`,
`EpochPact.exe` `c200b545…`. Built from 798f3e6 (version 4c203e4; item categories 1ceb344; bones, altars,
Weaver idols and Enchanter bee516e; pick up from afar and item looks bd42fca; the mirror 798f3e6). Package checks passed (desktop verify, connection test); the player DLLs have no test commands
(`altarcheck`, `enchantercheck`, `bonedrop`, `invaffixes`, `methodrva`, `idolcheck`, `farspawn`, `labelclick`); Microsoft Defender
(signatures 1.463.21.0) found nothing. Tests: Python 394, JavaScript 245, native 26; translations 1868/1868.
Shipping check with this exact `version.dll` (`builds\dev48-final-live`, EpFeedbackTest clone): version
0.1.2-dev.48, test commands unknown, out-of-range values refused (`bones 101`, `enchanteruses 41`, `remotepickup 2`, an
unknown look or slot, a look of another type); the five new settings armed (hooks in); `catread` lists the eight
categories; four Unique looks set and the mirror read back front and back (JPEGs of 29-32 KB); the F6 Equipment page
showed the mirror with the looks; at idol settings 3/3 two Rare altars came with 8 and 6 affixes and two Weaver idols
with 6 and 4, all picked up; after a restart the four looks were applied again as the character loaded (`changed 4`),
the altars were still there affix for affix, and `transmog all none` took every look off. VirusTotal (29 October 2026):
version.dll 0/70, InGame ZIP 0/66.

## GitHub release

Tag `v0.1.2-dev.48`, title: EpochPact 0.1.2-dev.48 — Item categories, item looks, pick up from afar and more

```markdown
## Which download?

- **EpochPact-0.1.2-dev.48.zip — Desktop app.** Open `EpochPact.exe`; it installs the mod for you. Choose this one if you use MelonLoader. **Install update** in the app installs this file.
- **EpochPact-0.1.2-dev.48-InGame.zip — Manual install, no EXE.** Copy `version.dll` into your Last Epoch folder (next to `Last Epoch.exe`, over the old one), play offline and press **F6**. Do not use it with MelonLoader.

Both contain the same `version.dll`, so both have the in-game window.

## Changes

- **Item categories.** A column beside the game's inventory (and an Item categories section on the Inventory page of F6 and the app) with eight categories: Unique & Set, Exalted, Idols, Keys & special, Weapons, Armour, Jewellery and Other. **Set up tabs** creates a free stash tab for each, in a stash folder named EpochPact. Tick a category and the items of it you pick up go straight to its tab; untick it and they stay in your inventory. Click a category's name to open the stash on its tab from anywhere; the dot next to it shows only that category in the inventory, the rest faint. **Distribute** moves the ticked categories' items from the inventory into their tabs, **Materials** opens the game's crafting materials panel. The column can be turned off on the Inventory page.
- **Ancient Bones multiplier.** New setting in General settings (1–100x): the Ancient Bones that drop for you are multiplied, and the pile on the ground already shows the larger amount. Off at 1.
- **Idol altars and Weaver idols with more affixes.** The Idol prefixes and Idol suffixes settings now also cover idol altars and Weaver idols. Rare altars drop with 2 prefixes and 2 suffixes; at 3 they roll 2 to 4 of each, from the game's own altar affixes. Weaver idols roll 1 to 3 of each, and their extra affixes can be Weaver affixes too.
- **Woven Enchanter uses.** New setting in General settings (1–40x): each Woven Enchanter you meet allows this many times its usual uses, so "1/1 uses remain" becomes 20/20 at 20. It also applies to an Enchanter you are standing at. Each craft still costs its Memory Amber. Off at 1.
- **Item looks (transmog) with a mirror.** On the Equipment page your helmet, body armour, gloves, boots, belt, weapon and off-hand can look like any other base item or Unique of the same type, with the game's own art. A mirror beside the lists shows your character wearing them (live in the F6 window; a picture in the desktop app), and turns left and right. Looks stay through zone changes, restarts and new items of that type, until you choose the item's own look or Reset all looks. Shop cosmetics are not touched and nothing is unlocked; display only.
- **Pick up from afar.** New switch on the Loot & Pickup page: click an item's name on the ground and it goes into your inventory where it lies; your character does not walk there. With no room in the inventory the game walks you there as usual.

Last Epoch 1.5.3 (Steam), offline only. VirusTotal: version.dll 0/70, InGame ZIP 0/66.
Questions and reports: [Discord](https://discord.gg/q6aexZZSAf).
```

## Nexus (mod 44): update of the main file, InGame ZIP only

Not uploaded: the owner keeps Nexus on 0.1.2-dev.46 for now. The texts below are ready for later.

- Version: 0.1.2-dev.48 (file description unchanged)
- Changelog (one line per entry):
  - Item categories beside the inventory: a free stash tab for each category, picked-up items of ticked categories go straight to their tab, open a category's tab with one click.
  - New setting in General settings: Ancient Bones multiplier (1–100x).
  - Idol prefixes and suffixes now also cover idol altars (2 to 4 of each at 3) and Weaver idols.
  - New setting in General settings: Woven Enchanter uses (1–40x).
  - Item looks (transmog) on the Equipment page, with a mirror of your character. Game art only; shop cosmetics are not touched.
  - Pick up from afar: click an item on the ground and it comes to your inventory without walking.
- Description (`description-dev48.bbcode`): version line `0.1.2-dev.48`; General line names Ancient Bones, idol
  altars and Woven Enchanter uses; new Inventory line; Safety line with the new VirusTotal results.

## Discord announcement (draft; the owner sends it)

> @everyone
> **EpochPact 0.1.2-dev.48**
> • **Item categories:** a column beside your inventory with 8 categories (Unique & Set, Exalted, Idols, Keys & special, Weapons, Armour, Jewellery, Other). *Set up tabs* makes a free stash tab for each; tick a category and what you pick up goes straight to its tab; click a name to open its tab from anywhere.
> • **Ancient Bones multiplier:** new setting in *General settings* (1–100x).
> • **Idol altars and Weaver idols** now get more prefixes and suffixes with the *Idol prefixes / suffixes* settings (altars up to 4 of each).
> • **Woven Enchanter uses:** new setting in *General settings* (1–40x): more idol reforges each time you meet the Enchanter.
> • **Item looks (transmog):** on the *Equipment* page make your gear look like any other item or Unique of the same type, with a mirror of your character. No shop cosmetics.
> • **Pick up from afar:** switch it on in *Loot & Pickup*, click an item on the ground and it comes to you.
> **Desktop app (EXE):** in EpochPact open *What's new → Install update*, or download it here:
> https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.48
> **Manual install, no EXE:** download `EpochPact-0.1.2-dev.48-InGame.zip` from the GitHub link above and copy the new `version.dll` over the old one (Nexus follows later).
