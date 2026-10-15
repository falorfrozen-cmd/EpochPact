# Weaver's Will: pickup threshold, multiplier, Unique share, upgrade speed and affixes (2026-10-14/15)

Player request: Weaver's Will 1–28 in the drop quality rules. Owner requests: a
Weaver's Will multiplier; a higher Weaver's Will Unique drop rate; faster
Weaver's Will upgrades ("experience"); choosing which affixes the upgrades raise
and add instead of the game's random choice.

## How the game stores it

- A Unique drops with either Legendary Potential or Weaver's Will. A Weaver's
  Will Unique has `ItemData.legendaryPotential` 0 and `ItemData.weaversWill`
  1–28. `ItemData.RollWeaversWill` (called only from `RollLegendaryProperties`)
  rolls tiers 5/8/11/14/17, adds 0–7 and up to 4 lucky steps, and clamps the
  result to 28; the game's `ItemData.MaxWeaversWill` literal is 28 as well.
- EpochPact's Quality pickup mode accepted `unique && lp >= minimumLP` or a
  wanted T7. With the default Minimum Unique LP 2, a Weaver's Will Unique was
  never collected, whatever its Weaver's Will.

## Change

- `loot_crafting_rules.hpp`: `minimumWW` (0 = off); a Unique passes with
  `lp >= minimumLP` **or** `weaversWill >= minimumWW`; T7 stays an OR condition.
- `smart_loot.cpp`: reads `ItemData.weaversWill`, `lootww <0..max>` where max is
  the game's `ItemData.MaxWeaversWill` (read from the literal field at start,
  28 if unreadable); `lootread` reports `minimumWW` and `maximumWW`.
- App: Loot & Pickup → Quality rules → **Minimum Weaver's Will** (0–28), next
  to Minimum Unique LP. Texts translated.
- Test fixture (test character only): `drop-item-<distance>-7-w<n>` drops a
  generated Unique with Weaver's Will n and LP 0.

## Live test (isolated EpFeedbackTest clone, test core)

Quality mode, Minimum Unique LP 2, T7 and loot filter off, radius 0. Three
Uniques dropped: Weaver's Will 18, Weaver's Will 10, and one with LP 0 and no
Weaver's Will.

| Minimum Weaver's Will | Uniques picked |
|---|---|
| 0 (off) | none — the earlier behaviour |
| 18 | 1 (the Weaver's Will 18 Unique) |
| 10 | 1 more (the Weaver's Will 10 Unique) |

The Unique without Weaver's Will and LP 0 stayed on the ground. `lootww -1`,
`lootww 29` and `lootww x` were refused; `lootww 28` was accepted. The core log
shows `ItemData.MaxWeaversWill 28`. 0 faults; 54 save/runtime files restored.

## Automated tests

- `loot_crafting_test`: off by default, boundary 18/17, LP 0 without Weaver's
  Will stays out, LP threshold still accepts, a non-unique cannot qualify, the
  loot filter still hides, threshold 1 accepts any Weaver's Will.
- Python: `loot_ww` accepts whole numbers 0–28 only; reset sends `lootww 0`.
- JavaScript: Quality rules shows the setting next to Minimum Unique LP and
  takes the value the game reports.

## Weaver's Will multiplier

- `items.cpp` hooks `ItemData.RollWeaversWill` (feature `weaverswill`, ×1–×10,
  offline only, ×1 removes the hook) and returns
  `rules::ScaleWeaversWill(roll, x, MaxWeaversWill)`: the roll times x, rounded,
  never below the roll and never above 28. App: General → **Weaver's Will
  multiplier**, applied again like the other multipliers.
- Test fixture (test character only): `drop-item-<distance>-7-ww` asks the
  game's generator for a random Unique of the Weaver's Will legendary type
  (`requireLegendaryType`, `WeaversWill`, item level 50), so the Weaver's Will is
  the game's own roll through `RollLegendaryProperties`.

Live test (same clone): Uniques dropped by the generator, Weaver's Will read
from each dropped item.

| Multiplier | Uniques | Weaver's Will min–max | Average |
|---|---|---|---|
| ×1 (hook out) | 40 | 5–15 | 10.45 |
| ×2 | 40 | 10–28, every value even | 19.45 |
| ×10 | 10 | 28–28 | 28 |
| back to ×1 (hook removed) | 10 | 6–15 | 9.9 |

Every dropped Unique had LP 0. The core log shows the first boosted rolls
"5 -> 10 (x2)" and "5 -> 28 (x10)"; `weaverswill 11`, `0.5` and `x` were refused;
status reported 50 boosted rolls and the hook out at the end; the game stayed
responsive; 54 save/runtime files restored.

Automated: `loot_crafting_test` (×1, ×2, maximum, rounding, no Weaver's Will,
a roll above the maximum, invalid multipliers); Python (status line read,
1–10 only); JavaScript (General shows it after the rarity multiplier and
applies it like the other multipliers).

## Where the game rolls Weaver's Will

`ItemData.RollWeaversWill` has one caller, `ItemData.RollLegendaryProperties`,
and that is called from `GenerateItems.RollBaseItem` (drops and rewards),
`ItemData.RevealCocoonedUnique` (opened cocoons), `ChangeToUniqueOfSameItemType`,
`EmpowerUniqueCore`, `RandomizeLegendaryPotential`, `GetSimilarItem`, the Eternity
Cache (`tryToSealEternityCache`) and Soul Gambling. The multiplier therefore
applies to every source.

## Further live tests (2026-10-15)

**Everything together** — ×2, Quality mode, Minimum Weaver's Will 20, auto
pickup on, 12 Uniques of the Weaver's Will type rolled by the generator:
values 16, 28, 18, 18, 14, 18, 12, 28, 22, 16, 18, 20 (all even or 28). Exactly
the four at 20 or more were picked up (Knowledge of an Erased Mage ×2, Ambitions
of an Erased Acolyte, Cradle of the Erased); the eight below 20 stayed on the
ground, value for value.

**Enemy kills** — test fixtures warp to each spawner in turn and kill enemies
through their own `BaseHealth.HealthDamage`, so the game's death and loot drop
run as usual; drops ×25, rarity ×10, density ×5. Zone levels read from
`DifficultyManager.areaLevel`: Z52 4, A06 9 … H70 54, I140 62 (chosen, 156
spawners). In I140: 307 kills, 426 items, 24 Uniques (LP 0 ×21, LP 2 ×3) and no
Weaver's Will Unique; the multiplier was never called. The unique list read
later (`unique-types`) has 18 Weaver's Will Uniques, 14 of which can drop at
random against 316 Legendary Potential ones, so about 1 of 24 was expected and
none is within chance; the drop path itself is `RollBaseItem`, the same one the
generator tests use.

**Opened cocoons** — a generated cocooned Unique (23 kinds) opened through the
game's own `ItemData.RevealCocoonedUnique`, 150 at each multiplier; every one
turned into another Unique.

| Multiplier | Opened | Weaver's Will Uniques | Values | LP Uniques (LP 0/1/2/3) |
|---|---|---|---|---|
| ×1 | 150 | 5 | 13, 13, 14, 15, 17 | 74 / 52 / 17 / 2 |
| ×2 | 150 | 6 | 28 ×6 | 71 / 52 / 18 / 3 |

Legendary Potential is unchanged by the multiplier. All runs: 0 faults, the game
stayed responsive, 54 save/runtime files restored.

**App** — preview: Loot & Pickup → Quality rules shows Minimum Weaver's Will
next to Minimum Unique LP (2 × 2 grid, no overflow), General shows the
multiplier after the rarity multiplier; English, Türkçe and Русский texts
checked on both pages (the Russian interface uses the game's term «Воля Ткача»).
Setting 18, reading it back, refusing 29 ("Enter a value between 0 and 28.")
and reset to 0 went through the app's backend. Fixed: Minimum Weaver's Will
showed a "×" after its value like a multiplier. Both settings come back with the
last used settings after a restart (`test_startup_profile.cjs`).

## How the game upgrades Weaver's Will items

- `CharacterMutator.OnActorDeath`: when an enemy that gives experience dies near
  the player and the 15 s cooldown is over, the game rolls a chance proportional
  to `ItemsWithWeaversWillEquipped`; on success the next frame calls
  `CharacterMutator.UpgradeRandomWeaversWillItem` and the cooldown is set to 15 s.
- `UpgradeRandomWeaversWillItem` picks a random equipped Unique/Set/Legendary
  item with Weaver's Will and calls `ItemData.SpendWeaversWillToUpgrade`, then
  updates stats, the item UI and the container.
- `SpendWeaversWillToUpgrade` (rarity 7 or 9, Weaver's Will above 0): a Unique
  (7) always adds an affix and becomes Legendary (9); below four affixes the
  game rolls between adding and raising; at four it only raises. Raising picks a
  random affix below T7 (`affixTier` < 6) and adds one tier; adding takes a
  weighted pick from `ItemData.GetEligibleAffixes` (two fresh lists per call) and
  `AddAffixNoCostOrChecks`. Each upgrade spends 1 Weaver's Will, then
  `RebuildID` and `setValuesFromSerialisation` run.
- `UniqueList.rollUnique(..., requireLegendaryType, requiredLegendaryType, ...)`
  picks every random Unique. The list has 18 Weaver's Will Uniques (14 can drop
  at random; Inheritance of the Erased, Clothos Needle, Scissor of Atropos and
  Blight of Lachesis cannot) against 316 random Legendary Potential Uniques.

## Unique share, upgrade speed, affixes (native/core/weaver.cpp)

- `weaversdrop <0-100>`: for a `rollUnique` call without a required legendary
  type, that percent of the time the same call is made first with
  `requireLegendaryType = true, WeaversWill`; when it finds nothing the original
  call follows. Every other argument (level, item type, random drop) is passed
  through.
- `weaversupgrades <1-10>`: `UpgradeRandomWeaversWillItem` runs floor(x) times
  per grant, plus once more with the fraction's chance.
- `weaveraffixes <ids|none>`: during `SpendWeaversWillToUpgrade` every unwanted
  affix below T7 is held at T7 when a wanted one is below T7, so the game's own
  pick lands on a wanted affix; `GetEligibleAffixes` (only during that call) keeps
  the wanted affixes when one of them fits. Held tiers are written back by affix
  id and `RebuildID` + `setValuesFromSerialisation` rebuild the item. With no
  wanted affix that can be raised or added, the game's choice stands.
- App: General → Weaver's Will holds the multiplier, the Unique share, the
  upgrade speed and its own affix picker (the Loot & Pickup picker is separate);
  Minimum Weaver's Will stays under Loot & Pickup → Quality rules. Rules in `weaver_rules.hpp`, `weaver_rules_test` 20/20.

## Live tests of the new settings (EpFeedbackTest clone, test core)

**Share** — random Uniques from the generator (`drop-item-6-7-any`, item level 50):

| Share | Uniques | Weaver's Will Uniques |
|---|---|---|
| 0 % (hook out) | 60 | 2 (the game's own share) |
| 100 % | 60 | 60 |
| 50 % | 60 | 36 |

Opened cocoons at 100 % (with ×2 Weaver's Will): 24 of 60 became Weaver's Will
Uniques (about 3 % without the setting), all with Weaver's Will 28; cocoons of
item types without a Weaver's Will Unique keep the game's pick.

**Speed** — `UpgradeRandomWeaversWillItem` called twice on a worn Weaver's Will
Unique: ×3 spent 6 Weaver's Will (grants 2, upgrades 6), ×1 spent 2.

**Affixes** — a Weaver's Will Unique of the worn belt's type from the game's
generator (Communion of the Erased, Weaver's Will 28) put into the belt slot
(test fixture `weaver-wear-28`), upgraded through the game's own call:

- 3 upgrades with no selection: affix 66 added, then raised twice (the game's
  random choice).
- Selection 66 (on the item, T3) and 12 (eligible, 38 eligible in all), 12
  upgrades: the first added affix was 12; all 9 raises went to 66 or 12, none to
  any other affix; while 66 and 12 were already on the item two adds were the
  game's own choice (19, 52), as specified; after 66 and 12 reached T7 the later
  upgrades raised 19 and 52.
- After every upgrade the item's fields matched its serialised data
  (`setValuesFromSerialisation` changed nothing); 0 faults; nothing was lowered or
  removed.
- Refused: `weaveraffixes 999999`, `abc`, `1,`; `weaversupgrades 11`, `0.5`;
  `weaversdrop 101`.

**App** — preview: General shows the share and speed after the Weaver's Will
multiplier and the Weaver's Will panel (all Weaver's Will settings were gathered into
it before release); Refresh loaded 1,156 affix
names and the selection; an affix moved and applied came back from the backend
as the selection, the Loot & Pickup selection stayed separate; texts checked in
English, Türkçe and Русский. Automated: Python (status values, ranges, ids),
JavaScript (page, picker, apply, last used settings).

**End to end in a combat zone** — the worn Weaver's Will Unique (Weaver's Will
28), selection 12, 15, 16, upgrade speed ×3, share 100 %, Weaver's Will ×2, drops
×25, rarity ×10, density ×5; the character travelled through the End of Time to
H70 (area level 54) and killed 378 enemies through their own death path in 4
minutes.

- Enemy drops: the 5 Uniques the game rolled were all Weaver's Will Uniques
  (rollUnique: 5 rolls, 5 asked, 5 converted), with even Weaver's Will (14, 22,
  24) from the ×2 multiplier.
- Upgrades granted by the game's own kill roll: 1 grant in 378 kills (the game's
  chance with one Weaver's Will item and its 15 s cooldown), run 3 times: affix
  12 added (from the wanted ones; the eligible list was narrowed once), then 12
  raised twice; Weaver's Will 28 → 25; no unwanted add or raise, nothing
  lowered; the item matched its serialised data after every round; 0 faults.
- Every setting off afterwards (hooks out); 54 save/runtime files restored.

