# Last Epoch 1.5.0.1 — character sheet stat map

Every line the C screen shows, mapped to the game's own code: the `SP` enum value, the
`AT` tag when the same stat is split by damage type/weapon/minion, and the `AilmentID`
when the line is an ailment. Sources: our runtime dump (`dump.cs`, `fields.tsv`) and
`research/tools/disasm.py`. The game renders these lines with `CharacterStatDisplay`
(prefab data): it holds `property` (SP), `specialTag`, `tags` (AT), `extraTag`,
`modifierProperty`/`minionStat`, and `ignoreAdded`/`ignoreIncreased` flags — so "Added X"
and "Increased X" are the same SP, displayed from different parts of the same modifier.

Modifying any of these in game: every active modifier is a `Stats.Stat` object in the
player's `Stats.stats` list (`BaseStats` inherits it, +0x88 on the stats object). A
`Stats.Stat` holds `property` (SP) at +0x10, `specialTag` +0x11, `tags` (AT) +0x14,
`extraTag` +0x18, `addedValue` +0x1C, `increasedValue` +0x20 (a **fraction**: 0.1 = +10%).
That is how the `speed` command already writes Movement Speed.

## The SP enum (the master list, 134 values)

| # | Member | # | Member | # | Member |
|---|---|---|---|---|---|
| 0 | Damage | 45 | IncreasedStunChance | 90 | PotionHealthConvertedToWard |
| 1 | AilmentChance | 46 | AllAttributes | 91 | WardOnPotionUse |
| 2 | AttackSpeed | 47 | IncreasedPotionDropRate | 92 | WardRegen |
| 3 | CastSpeed | 48 | PotionHealth | 93 | OverkillLeech |
| 4 | CriticalChance | 49 | PotionSlots | 94 | ManaBeforeWardPercent |
| 5 | CriticalMultiplier | 50 | HasteOnHitChance | 95 | IncreasedStunDuration |
| 6 | DamageTaken | 51 | HealthLeech | 96 | MaximumHealthGainedAsEnduranceThreshold |
| 7 | Health | 52 | ElementalResistance | 97 | ChanceToGain30WardWhenHit |
| 8 | Mana | 53 | BlockEffectiveness | 98 | PlayerProperty |
| 9 | Movespeed | 54 | None | 99 | ManaSpentGainedAsWard |
| 10 | Armour | 55 | IncreasedStunImmunityDuration | 100 | AilmentConversion |
| 11 | DodgeRating | 56 | StunImmunity | 101 | PerceivedUnimportanceModifier |
| 12 | StunAvoidance | 57 | ManaDrain | 102 | IncreasedLeechRate |
| 13 | FireResistance | 58 | AbilityProperty | 103 | MoreFreezeRatePerStackOfChill |
| 14 | ColdResistance | 59 | Penetration | 104 | IncreasedDropRate |
| 15 | LightningResistance | 60 | CurrentHealthDrain | 105 | IncreasedExperience |
| 16 | WardRetention | 61 | MaximumCompanions | 106 | PhysicalAndVoidResistance |
| 17 | HealthRegen | 62 | GlancingBlowChance | 107 | NecroticAndPoisonResistance |
| 18 | ManaRegen | 63 | CullPercentFromPassives | 108 | DamageTakenBuff |
| 19 | Strength | 64 | PhysicalResistance | 109 | IncreasedChanceToBeStunned |
| 20 | Vitality | 65 | CullPercentFromWeapon | 110 | DamageTakenFromNearbyEnemies |
| 21 | Intelligence | 66 | ManaCost | 111 | BlockChanceAgainstDistantEnemies |
| 22 | Dexterity | 67 | FreezeRateMultiplier | 112 | ChanceToBeCrit |
| 23 | Attunement | 68 | IncreasedChanceToBeFrozen | 113 | DamageTakenWhileMoving |
| 24 | ManaBeforeHealthPercent | 69 | ManaEfficiency | 114 | ReducedBonusDamageTakenFromCrits |
| 25 | ChannelCost | 70 | IncreasedCooldownRecoverySpeed | 115 | DamagePerStackOfAilment |
| 26 | VoidResistance | 71 | ReceivedStunDuration | 116 | IncreasedAreaForAreaSkills |
| 27 | NecroticResistance | 72 | NegativePhysicalResistance | 117 | GlobalConditionalDamage |
| 28 | PoisonResistance | 73 | ChillRetaliationChance | 118 | ArmourMitigationAppliesToDamageOverTime |
| 29 | BlockChance | 74 | SlowRetaliationChance | 119 | WardDecayThreshold |
| 30 | AllResistances | 75 | Endurance | 120 | EffectOfAilmentOnYou |
| 31 | DamageTakenAsPhysical | 76 | EnduranceThreshold | 121 | ParryChance |
| 32 | DamageTakenAsFire | 77 | NegativeArmour | 122 | CircleOfFortuneLensEffect |
| 33 | DamageTakenAsCold | 78 | NegativeFireResistance | 123 | TrackerProperty |
| 34 | DamageTakenAsLightning | 79 | NegativeColdResistance | 124 | UnimportanceModifier |
| 35 | DamageTakenAsNecrotic | 80 | NegativeLightningResistance | 125 | FreezeImmunity |
| 36 | DamageTakenAsVoid | 81 | NegativeVoidResistance | 126 | ChanceToCastForAbility |
| 37 | DamageTakenAsPoison | 82 | NegativeNecroticResistance | 127 | ChanceToCastForTags |
| 38 | HealthGain | 83 | NegativePoisonResistance | 128 | AilmentImmunity |
| 39 | WardGain | 84 | NegativeElementalResistance | 129 | AbilityRetaliationChance |
| 40 | ManaGain | 85 | Thorns | 130 | IdolAltarProperty |
| 41 | AdaptiveSpellDamage | 86 | PercentReflect | 131 | GlobalConditionalPenetration |
| 42 | IncreasedAilmentDuration | 87 | ShockRetaliationChance | 132 | GlobalConditionalCritChance |
| 43 | IncreasedAilmentEffect | 88 | LevelOfSkills | 133 | GlobalConditionalCritMulti |
| 44 | IncreasedHealing | 89 | CritAvoidance | | |

## The AT tag enum (how one SP covers weapon/damage/minion variants)

`None=0, Physical=1, Lightning=2, Cold=4, Fire=8, Void=16, Necrotic=32, Poison=64,
Elemental=128, Spell=256, Melee=512, Throwing=1024, Bow=2048, DoT=4096, Minion=8192,
Totem=16384, PetResisted=32768, Potion=65536, Buff=131072, Channelling=262144,
Transform=524288, LowLife=1048576, HighLife=2097152, FullLife=4194304, Hit=8388608,
Curse=16777216, Ailment=33554432, Crit_deprecated=67108864, Kill_deprecated=134217728,
Die_deprecated=268435456`

So "Increased Melee Damage" and "Increased Bow Damage" are the same `SP.Damage`, split by
`AT.Melee` / `AT.Bow`; minion lines add `AT.Minion`; `crit`/`on kill` lines use the
deprecated tags that the item data still carries.

## Ailments (AilmentID)

`Ignite=1, Bleed=2, Chill=3, Shock=5, Slow=6, Poison=7, ArmourShred=8, TimeRot=9,
Blind=14, Frostbite=23, Haste=33, Frenzy=34, Swiftness=35, Electrify=93`

Chance → `SP.AilmentChance`; duration → `SP.IncreasedAilmentDuration`; effect/damage →
`SP.IncreasedAilmentEffect` (ailment damage itself rides `SP.Damage` with the ailment);
each carries the `AilmentID`. `Stats.AilmentChanceStat/DurationStat/EffectStat` build them.

## The sheet, line by line

Confidence: **direct** = the SP is certain; **tag** = the same SP plus the listed tag;
**ailment** = SP + AilmentID; **field** = not a stat entry (a component field or a
PlayerProperty/conditional handled by a mutator, marked as inferred where it matters).

### Stats — base attributes
| Sheet line | Identity |
|---|---|
| Level | `BaseStats.level` (+0x98), not an SP |
| Vitality / Strength / Dexterity / Intelligence / Attunement | SP.Vitality=20 / Strength=19 / Dexterity=22 / Intelligence=21 / Attunement=23 (direct) |

### Health, mana, movement
| Sheet line | Identity |
|---|---|
| Health | SP.Health=7 |
| Health Regeneration | SP.HealthRegen=17 |
| Mana | SP.Mana=8 |
| Mana Regeneration | SP.ManaRegen=18 |
| Movement Speed | SP.Movespeed=9 |

### Resistances
| Sheet line | Identity |
|---|---|
| Fire / Lightning / Cold | SP.FireResistance=13 / LightningResistance=15 / ColdResistance=14 |
| Physical | SP.PhysicalResistance=64 |
| Poison / Necrotic / Void | SP.PoisonResistance=28 / NecroticResistance=27 / VoidResistance=26 |
| (the "+% all resistances" source) | SP.AllResistances=30, splitting into the seven above |

### Additional defenses
| Sheet line | Identity |
|---|---|
| Block Chance | SP.BlockChance=29 |
| Block Effectiveness | SP.BlockEffectiveness=53 |
| Armor | SP.Armour=10 |
| Dodge | SP.DodgeRating=11 |
| Stun Avoidance | SP.StunAvoidance=12 |
| Ward Retention | SP.WardRetention=16 |

### Damage — attack/cast speed
| Sheet line | Identity |
|---|---|
| Melee / Bow Attack Speed | SP.AttackSpeed=2 + AT.Melee / AT.Bow |
| Increased Melee / Bow / Throwing Attack Speed | the same SP.AttackSpeed + the same tag; the "increased" part of the modifier (CharacterStatDisplay.ignoreAdded) |
| Increased Cast Speed | SP.CastSpeed=3 |

### Critical strike
| Sheet line | Identity |
|---|---|
| Critical Strike Chance | SP.CriticalChance=4 |
| Melee / Bow / Throwing / Spell Critical Strike Chance | SP.CriticalChance=4 + AT.Melee / Bow / Throwing / Spell |
| Critical Strike Multiplier | SP.CriticalMultiplier=5 |
| Melee / Bow / Throwing / Spell Critical Strike Multiplier | SP.CriticalMultiplier=5 + the same tags |

### Damage by attack type
| Sheet line | Identity |
|---|---|
| Increased / Added Spell Damage | SP.Damage=0 + AT.Spell (increased vs added part of the same stat) |
| Increased / Added Melee Damage | SP.Damage=0 + AT.Melee |
| Increased / Added Bow Damage | SP.Damage=0 + AT.Bow |
| Increased / Added Throwing Attack Damage | SP.Damage=0 + AT.Throwing |
| Increased Damage Over Time | SP.Damage=0 + AT.DoT |

### Damage types and penetration
| Sheet line | Identity |
|---|---|
| Increased Physical / Lightning / Cold / Fire / Void / Necrotic / Poison Damage | SP.Damage=0 + AT.Physical / Lightning / Cold / Fire / Void / Necrotic / Poison |
| Physical / Lightning / Cold / Fire / Void / Necrotic / Poison Damage Penetration | SP.Penetration=59 + the same tags |
| Area Of Effect | SP.IncreasedAreaForAreaSkills=116 |
| Melee Area Of Effect | SP.IncreasedAreaForAreaSkills=116 + AT.Melee |
| Increased Stun Chance | SP.IncreasedStunChance=45 |
| Increased Melee Stun Chance | SP.IncreasedStunChance=45 + AT.Melee |
| Damage for Melee per Mana Cost | not a plain stat: a mutator field (`moreDamagePerManaCost`; the sheet reads it through a PlayerProperty/conditional) — field, inferred |

### Defense — avoidance and mitigation
| Sheet line | Identity |
|---|---|
| Parry Chance | SP.ParryChance=121 |
| Endurance | SP.Endurance=75 |
| Endurance Threshold | SP.EnduranceThreshold=76 |
| Critical Strike Avoidance | SP.CritAvoidance=89 |
| Chance To Receive A Glancing Blow | SP.GlancingBlowChance=62 |
| Reduced Bonus Damage From Crits | SP.ReducedBonusDamageTakenFromCrits=114 |
| Less Damage Taken | SP.DamageTaken=6 (the "more/less" part) |
| Less Damage over Time Taken | SP.DamageTaken=6 + AT.DoT |
| Less Damage Taken From Nearby | SP.DamageTakenFromNearbyEnemies=110 |
| More Damage Taken without Frenzy | SP.DamageTakenBuff=108 with a Frenzy condition — field/conditional, inferred |
| Damage Dealt To Attackers | SP.Thorns=85 |
| Damage Reflected | SP.PercentReflect=86 |

### Mana redirection, life gain/loss, leech, ward
| Sheet line | Identity |
|---|---|
| Damage Dealt to Mana Before Health | SP.ManaBeforeHealthPercent=24 |
| Damage Dealt to Mana Before Ward | SP.ManaBeforeWardPercent=94 |
| Health Gained On Hit | SP.HealthGain=38 + AT.Hit |
| Health Gained On Melee Hit | SP.HealthGain=38 + AT.Melee + AT.Hit (tag, inferred from data) |
| Health Gained On Kill | SP.HealthGain=38 + AT.Kill_deprecated |
| Health Gained On Block | SP.HealthGain=38 + the block flavour (specialTag/extraTag in the line item; blocked health gain) |
| Current Health Lost Per Second | SP.CurrentHealthDrain=60 |
| Damage Leeched As Health | SP.HealthLeech=51 |
| ... On Hit / Melee / Spell | SP.HealthLeech=51 + AT.Hit / Melee / Spell |
| Increased Health Leech | SP.IncreasedLeechRate=102 |
| Added Ward Per Second | SP.WardRegen=92 |
| Ward Decay Threshold | SP.WardDecayThreshold=119 |
| Ward Gained On Potion Use | SP.WardOnPotionUse=91 |
| Potion Health Gain Converted To Ward | SP.PotionHealthConvertedToWard=90 |
| Ward Gained On Hit / Melee / Crit / Kill / Block | SP.WardGain=39 + AT.Hit / Melee / Crit_deprecated / Kill_deprecated / block flavour |
| Mana Spent Gained As Ward | SP.ManaSpentGainedAsWard=99 |
| Missing Health Gained As Ward | SP.WardGain=39 + AT.LowLife (tag, inferred) |

### Minions
| Sheet line | Identity |
|---|---|
| Increased / Added Minion Damage | SP.Damage=0 + AT.Minion |
| Increased / Added Minion Spell Damage | SP.Damage=0 + AT.Minion + AT.Spell |
| Increased / Added Minion Melee Damage | SP.Damage=0 + AT.Minion + AT.Melee |
| Increased Minion Damage Over Time | SP.Damage=0 + AT.Minion + AT.DoT |
| Minion Physical/Lightning/Cold/Fire/Void/Necrotic/Poison Damage | SP.Damage=0 + AT.Minion + the element tag |
| Minion penetration lines (sheet shows them without the "Minion" prefix) | SP.Penetration=59 + AT.Minion + the element tag |
| Minion Critical Strike Chance / Multiplier | SP.CriticalChance=4 / CriticalMultiplier=5 + AT.Minion |
| Increased Minion Melee Attack Speed | SP.AttackSpeed=2 + AT.Minion + AT.Melee |
| Increased Minion Cast Speed | SP.CastSpeed=3 + AT.Minion |
| Increased Minion Movement Speed | SP.Movespeed=9 + AT.Minion |
| Increased / Added Minion Health Regen | SP.HealthRegen=17 + AT.Minion |
| Increased / Added Minion Armor | SP.Armour=10 + AT.Minion |
| Minion Dodge Rating | SP.DodgeRating=11 + AT.Minion |
| Increased Minion Health | SP.Health=7 + AT.Minion |
| Minion Power From Character Level | PlayerProperty/`CharacterMutator` scaling, not a plain SP — field, inferred |
| Increased Companion Revive Range / Speed | `SummonTracker`/`CharacterMutator` fields (PlayerProperty) — field, inferred |
| Maximum Companions | SP.MaximumCompanions=61 |

### Other — ailments
| Sheet line | Identity |
|---|---|
| Bleed Chance / Damage / Increased Bleed Duration | SP.AilmentChance=1 / SP.IncreasedAilmentEffect=43 (damage rides SP.Damage) / SP.IncreasedAilmentDuration=42, all + AilmentID.Bleed=2 |
| Poison ... | the same three + AilmentID.Poison=7 |
| Ignite ... | + AilmentID.Ignite=1 |
| Frostbite ... | + AilmentID.Frostbite=23 |
| Chill Chance / Increased Chill Duration | SP.AilmentChance=1 / SP.IncreasedAilmentDuration=42 + AilmentID.Chill=3 |
| Slow ... | + AilmentID.Slow=6 |
| Shock ... | + AilmentID.Shock=5 |
| Electrify ... | + AilmentID.Electrify=93 |
| Blind ... | + AilmentID.Blind=14 |
| Time Rot ... | + AilmentID.TimeRot=9 |
| Armor Shred ... | + AilmentID.ArmourShred=8 |

### Other — potions, mana, cooldowns, effects
| Sheet line | Identity |
|---|---|
| Potion Slots | SP.PotionSlots=49 |
| Increased Chance To Find Potions | SP.IncreasedPotionDropRate=47 |
| Health Gained On Potion Use | SP.PotionHealth=48 |
| Increased Healing Effectiveness | SP.IncreasedHealing=44 |
| Health Gained As Endurance Threshold | SP.MaximumHealthGainedAsEnduranceThreshold=96 |
| Mana Efficiency | SP.ManaEfficiency=69 |
| Increased Mana Regeneration | SP.ManaRegen=18 (increased part) |
| Increased Cooldown Recovery Speed | SP.IncreasedCooldownRecoverySpeed=70 |
| Movement Skill Cooldown Recovery | SP.IncreasedCooldownRecoverySpeed=70 with the movement-skill flavour (`CharacterMutator.increasedCooldownRecoverySpeedForMovementSkills`) |
| Increased Haste Effect | AilmentID.Haste=33 + `CharacterMutator.increasedHasteEffect` — field, inferred |
| Increased Frenzy Effect | AilmentID.Frenzy=34 + `CharacterMutator.increasedFrenzyEffect` — field, inferred |
| Freeze Rate Multiplier | SP.FreezeRateMultiplier=67 |
| Chance To Chill / Slow / Shock Attackers | SP.ChillRetaliationChance=73 / SlowRetaliationChance=74 / ShockRetaliationChance=87 |
| Current Health Lost on Skill Use | SP.CurrentHealthDrain=60 with the skill-use flavour (specialTag) |

## Coverage

- Directly an SP (exact name): the five attributes, health/mana/regen/movement, all
  resistances, the defensive block/dodge/armor/stun/ward lines, crit chance/multiplier,
  attack/cast speed, parry/endurance/glancing/crit avoidance, thorns/reflect, the
  mana-before lines, potion lines, mana efficiency, cooldown recovery, freeze rate,
  retaliation chances, maximum companions — **~60 lines**.
- Same SP split by AT (weapon/element/minion/DoT): the damage, penetration, speed, crit
  and minion sections — **~70 lines**.
- SP + AilmentID: the ailment sections — **~28 lines**.
- Not plain stats (component fields / PlayerProperty / conditionals, marked above): level,
  "Damage for Melee per Mana Cost", "More Damage Taken without Frenzy", "Minion Power From
  Character Level", companion revive lines, haste/frenzy effect — **~8 lines**.

## What this unlocks (next mods)

Because a `Stats.Stat` entry is a plain object, the same write we use for `speed` can
target **any** of these, as long as the character already has an entry for it (items,
passives and buffs create them): a `stat <name> <value>` command, or a full stat editor,
is now a table lookup away. Creating an entry from nothing needs `il2cpp_object_new` +
the list's `Add` (see `research/findings.md`, open questions).
