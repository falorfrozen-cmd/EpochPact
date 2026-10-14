# Minion counts and immortal minions (2026-10-14)

Player requests: separate count sliders for minions, companions, totems,
ballistas and forged weapons, and immortal minions.

## How the game limits summons

- `SummonEntityOnDeath.Summon` reads the summoning skill's `limit` while it
  summons; above it the game unsummons its lowest-priority minions. Skill
  mutators write that limit on the pooled ability object for each cast.
  EpochPact raises it for the offline player's own summons during that call only
  and restores it afterwards (`build_controls.cpp`, bounds in `build_rules.hpp`).
- The kind comes from the summoning ability (`CreationReferences.thisAbility`):
  `Ability.companion` → companion; name "Ballista" → ballista; a name with
  "Forged Weapon" → forged weapon; `Ability.getTagsWithFakeTags()` with the
  Totem tag (the way `SummonTracker` counts totems) → totem; everything else →
  minion. The plain `Ability.tags` has no Totem tag for Thorn Totem, and the
  object's own `companion` flag is false for Summon Bear — both were found in
  the live tests below and are not used alone.
- Companions keep their one-of-each-kind skill limits. Their total is the
  companion limit, `Stats.getMaximumCompanions` (the game multiplies it by 60
  for each companion's share), raised for the player's own stats.
- Immortal minions: the game's own `BaseHealth.AddSourceOfCannotDie` for every
  summon whose creators lead back to the player — existing ones when switched
  on, new ones in `Summoned.initialise` (where a summon registers with its
  creator; `SummonTracker.AddSummon` is not called for new summons). Switching
  it off removes exactly the sources EpochPact added and gives a minion left at
  0 health 1 health back.

## Live tests (isolated EpFeedbackTest clone, test core, The Fortress Walls)

Skills cast by the character through the game's own `Ability.CastAfterDelay`
(test-only `summon-cast-<ability id>`), counted per summoning ability.

| Skill | Native limit | Setting | Limit | Summons after casting |
|---|---|---|---|---|
| Summon Skeleton | 3 | +3 minions | 6 | 3 → 6 |
| Ballista | 2 | +2 ballistas | 4 | 2 → 4 |
| Summon Thorn Totem | 2 | +2 totems | 4 | 2 → 4 |
| Summon Storm Totem | 1 | +2 totems | 3 | 1 → 3 |
| Summon Forged Weapon | 6 | +3 forged weapons | 9 | 6 → 8 (8 casts) |
| Companions (Bear, Sabertooth, Wolf) | 2 | +2 companions | 4 | 2 → 3 at once, one of each kind |

- First run: Summon Bear was taken for a minion (+3 → four bears, which pushed
  the wolf out); second run: Thorn Totem was taken for a minion. Both fixed as
  described above; third run as in the table.
- Immortal minions: with it on, 9 existing and 4 newly cast summons (skeletons,
  ballista, wolf) got the cannot-die state; a 10,000,000 health-damage hit left a
  skeleton at 0 health and alive, the count unchanged. Off: the skeleton at 0
  health got 1 health back and stayed; the same hit then killed it (count −1).
  Without the setting the hit kills at once.
- All settings back to 0 at the end; 0 faults; the 54 save and runtime files
  were restored byte for byte after each of the four runs.

## Limits

- Ballista and forged weapons are recognised by the ability's name; a renamed
  or new ballista-like skill would count as a minion or totem.
- Immortal minions cannot be killed by damage. Skills or passives that consume
  minions through damage may no longer work while it is on; skills that remove
  minions directly (unsummon, duration) are unchanged. Not tested with every
  class's minion-consuming skill.
- More minions mean more work for the game each frame; the counts are bounded
  (20 extra per skill, 200 per skill, 20 companions).

## Checks

- Native: 20 test programs (build rules now include summon kinds and limits).
- JavaScript 198 (new: the Summons page shows the six settings without sending
  anything), Python 324.
- Translations: 1,551 texts, Turkish and Russian complete.
- Compiled package and extracted ZIP: 24/24 and 4/4; Windows Defender found no
  threats (hashes in `docs/security/virustotal-20261014-dev30.json`).
