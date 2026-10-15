# Brief: a custom DPS meter window for EpochPact (Last Epoch)

EpochPact is an offline mod for the game Last Epoch (version 1.5). It has a small
always-on-top "Damage" window that sits on top of the game and shows damage per
second. I want a new, better-looking design for that window.

## What to make

- One self-contained HTML file (inline CSS and plain JavaScript, no frameworks)
  that shows the meter with the example data below, so I can open it in a browser.
- Put the drawing code in one function, `render(state)`, where `state` is exactly the
  JSON object described below. EpochPact calls it once per second with new data.
- Show how it looks with: one source (only "You"), several sources (You plus
  minions), the session stopped, and no damage yet.

## Limits of the window

- A small frameless window on top of the game, resizable by the player, at least
  260 x 120 px. It must stay readable over a busy game picture.
- The player can already choose: background colour and opacity, text colour, text
  size (10-22 px), row spacing, and a top bar shown always or only on hover.
  Please keep these working (use CSS variables for them).
- The top bar holds: drag area with the title, Start/Stop, Reset, options (gear),
  and hide (−).
- Offline: no internet, no web fonts from the internet, no CDN libraries.
- Runs in Microsoft Edge WebView2 (modern Chromium).
- Settings are saved in `localStorage`.

## The data the window gets now (`state`)

```json
{
 "ok": true,
 "active": true,
 "elapsedSeconds": 95.0,
 "combatSeconds": 71,
 "hits": 642,
 "unsplitHits": 0,
 "elementNames": [
  "Physical",
  "Fire",
  "Cold",
  "Lightning",
  "Necrotic",
  "Void",
  "Poison"
 ],
 "hooked": true,
 "types": [
  {
   "name": "You",
   "live": 1,
   "total": 48210.0,
   "hits": 211,
   "maxHit": 1830.0,
   "dps10": 702.4,
   "dps60": 611.0,
   "dps600": 507.5,
   "dps3600": 507.5,
   "average": 507.5,
   "combatAverage": 679.0,
   "elements": {
    "total": [
     0,
     30110.0,
     0,
     18100.0,
     0,
     0,
     0
    ],
    "hits": [
     0,
     140,
     0,
     98,
     0,
     0,
     0
    ],
    "maxHit": [
     0,
     1830.0,
     0,
     960.0,
     0,
     0,
     0
    ],
    "dps10": [
     0,
     450.1,
     0,
     252.3,
     0,
     0,
     0
    ],
    "dps60": [
     0,
     380.0,
     0,
     231.0,
     0,
     0,
     0
    ],
    "dps600": [
     0,
     316.9,
     0,
     190.6,
     0,
     0,
     0
    ],
    "dps3600": [
     0,
     316.9,
     0,
     190.6,
     0,
     0,
     0
    ]
   }
  },
  {
   "name": "Skeleton",
   "live": 4,
   "total": 21340.0,
   "hits": 431,
   "maxHit": 210.0,
   "dps10": 260.0,
   "dps60": 241.5,
   "dps600": 224.6,
   "dps3600": 224.6,
   "average": 224.6,
   "combatAverage": 300.6,
   "elements": {
    "total": [
     21340.0,
     0,
     0,
     0,
     0,
     0,
     0
    ],
    "hits": [
     431,
     0,
     0,
     0,
     0,
     0,
     0
    ],
    "maxHit": [
     210.0,
     0,
     0,
     0,
     0,
     0,
     0
    ],
    "dps10": [
     260.0,
     0,
     0,
     0,
     0,
     0,
     0
    ],
    "dps60": [
     241.5,
     0,
     0,
     0,
     0,
     0,
     0
    ],
    "dps600": [
     224.6,
     0,
     0,
     0,
     0,
     0,
     0
    ],
    "dps3600": [
     224.6,
     0,
     0,
     0,
     0,
     0,
     0
    ]
   }
  }
 ],
 "allies": {
  "total": 21340.0,
  "hits": 431,
  "maxHit": 210.0,
  "dps10": 260.0,
  "dps60": 241.5,
  "dps600": 224.6,
  "dps3600": 224.6,
  "average": 224.6,
  "combatAverage": 300.6,
  "elements": {
   "total": [
    21340.0,
    0,
    0,
    0,
    0,
    0,
    0
   ],
   "hits": [
    431,
    0,
    0,
    0,
    0,
    0,
    0
   ],
   "maxHit": [
    210.0,
    0,
    0,
    0,
    0,
    0,
    0
   ],
   "dps10": [
    260.0,
    0,
    0,
    0,
    0,
    0,
    0
   ],
   "dps60": [
    241.5,
    0,
    0,
    0,
    0,
    0,
    0
   ],
   "dps600": [
    224.6,
    0,
    0,
    0,
    0,
    0,
    0
   ],
   "dps3600": [
    224.6,
    0,
    0,
    0,
    0,
    0,
    0
   ]
  }
 }
}
```

(Example values. The real names in `types[].name` are the game's object names for
each minion kind; "You" is always the character.)

| Field | Meaning |
|---|---|
| `active` | the session is running (Start pressed) |
| `elapsedSeconds` | time since Start |
| `combatSeconds` | seconds in which any damage was dealt |
| `hits` | number of hits counted |
| `types[]` | one row per source: "You", then each minion kind |
| `types[].live` | how many of that source are alive now (shown as "×4") |
| `total`, `hits`, `maxHit` | damage dealt, hit count, biggest single hit |
| `dps10`, `dps60`, `dps600`, `dps3600` | damage per second over the last 10 s / 1 min / 10 min / 1 hour |
| `average` | damage per second since Start |
| `combatAverage` | damage per second counting only seconds with combat |
| `elements` | the same numbers split by damage type; every array has 7 values in the order of `elementNames` |
| `allies` | all minions together (same fields) |

## Damage types (all of them)

Last Epoch has exactly 7 damage types. The order is the game's own and is the order
of every array in `elements`:

| Index | Type | Suggested colour (dark background) |
|---|---|---|
| 0 | Physical | #CFC6B8 |
| 1 | Fire | #FF7A45 |
| 2 | Cold | #73C7F2 |
| 3 | Lightning | #F2D348 |
| 4 | Necrotic | #7FE0C4 |
| 5 | Void | #B48CFF |
| 6 | Poison | #9AD94A |

- "Elemental" is not a separate type; it means Fire + Cold + Lightning.
- Holy Fire, Holy Lightning, Blood, Shattered, Cosmic and similar names are only visual
  effects in the game. The damage is still one of the 7 types; no extra columns.
- One hit can carry several types at once (for example 40% Fire + 60% Lightning).

## Ailments that deal damage

Read from the game's own data (Last Epoch 1.5.0.1). "Base" is the damage over the
whole duration before the player's bonuses. "Stacks" = how many copies can be on one
target at once.

| Type | Ailment | Base | Duration | Stacks | Tags | Note |
|---|---|---|---|---|---|---|
| Physical | Bleed | 53 | 3 s | unlimited | DoT |  |
| Physical | Bone Curse | 4 | 8 s | 1 | Spell, Curse | Curse; damage each time the cursed target is hit |
| Physical | Decrepify | 200 | 10 s | 1 | Spell, DoT, Curse | Curse; target takes more damage over time |
| Physical | Disemboweled | 200 | 6 s | unlimited |  | damage per metre moved; probably an enemy mechanic |
| Physical | Hemorrhage | 300 | 3 s | unlimited | DoT | damage based on the target's current health |
| Physical | Laceration | 1 | 3 s | 1 | Melee, DoT |  |
| Fire | Brand of Trespass | 400 | 3 s | 1 | DoT, Brand |  |
| Fire | Ignite | 40 | 2.5 s | unlimited | DoT |  |
| Fire | Penance | 20 | 15 s | 1 | Spell, Curse | Curse; damage when the cursed target hits someone (at most every 0.35 s) |
| Fire | Spreading Flames | 200 | 4 s | 1 | DoT | spreads |
| Fire + Lightning | Scathing Light | 200 + 200 | 4 s | 1 | DoT | two types |
| Fire + Necrotic | Witchfire | 600 + 600 | 12 s | 1 | DoT | two types |
| Cold | Brand of Subjugation | 400 | 3 s | 1 | DoT, Brand |  |
| Cold | Frostbite | 50 | 3 s | unlimited | DoT | first 15 stacks each add 20% chance to be frozen |
| Lightning | Brand of Deception | 400 | 3 s | 1 | DoT, Brand |  |
| Lightning | Electrify | 44 | 2.5 s | unlimited | DoT |  |
| Necrotic | Anguish | 40 | 10 s | 1 | Spell, Curse | Curse; damage to all cursed enemies whenever you kill an enemy |
| Necrotic | Bone Curse | 4 | 8 s | 1 | Spell, Curse | Necrotic version of Bone Curse; damage when hit |
| Necrotic | Chained | 40 | 2 s | 1 | Spell, DoT | also immobilizes |
| Necrotic | Damned | 35 | 2.5 s | unlimited | DoT | lowers health regen |
| Necrotic | Possess | 20 | 1.5 s | 1 | Spell, DoT |  |
| Necrotic | Spirit Plague | 90 | 3 s | 1 | Spell, DoT, Curse | Curse; jumps to another enemy if the target dies before it ends |
| Necrotic | Torment | 120 | 3 s | 1 | Spell, DoT, Curse | Curse; slows movement |
| Void | Aberroth's Time Rot | 1000 | 3 s | unlimited | DoT | boss mechanic (Aberroth); also shreds void resistance |
| Void | Abyssal Decay | 100 | 5 s | 1 | Spell, DoT | single copy |
| Void | Abyssal Decay | 30 | 3 s | unlimited | Spell, DoT | stacking version |
| Void | Doom | 400 | 4 s | 4 | DoT | increases melee damage taken |
| Void | Future Strike | 60 | 3 s | unlimited | DoT | deals all its damage at once when it ends |
| Void | Mimic Feast | 200 | 15 s | 12 | DoT | blocks Void Barrier and Health Leech; probably an enemy mechanic |
| Void | Ravage | 135 | 6 s | 1 | Spell, DoT |  |
| Void | Time Rot | 60 | 3 s | 12 | DoT | increases stun duration taken |
| Poison | Acid Skin | 80 | 5 s | 1 | DoT, Curse | Curse; +20% chance to be crit |
| Poison | Pestilence | 30 | 0.5 s | 2 | DoT | very short (0.5 s) |
| Poison | Plague | 150 | 4 s | 1 | DoT | spreads between enemies |
| Poison | Poison | 28 | 3 s | unlimited | DoT | also lowers poison resistance |
| Poison | Serpent Venom | 400 | 3 s | 1 | DoT | also prevents critical strikes |
| Poison | Snake Infection | 300 | 12 s | 1 | DoT |  |

## Ailments that deal no damage themselves but trigger a damaging skill

Their damage appears as that skill's hit, not as the ailment.

| Ailment | When | Skill it triggers | Skill's damage type |
|---|---|---|---|
| Avalanche Fissure Proc Ailment | 5 stacks | Fissure | Physical |
| Bulwark Radiance | 8 stacks | Holy Nova | not set |
| Bulwark Radiance | when it ends | Judgement | Fire |
| Chosen of the Chosen | 4 stacks | Shadow Daggers | Physical |
| Disintegrate Explosion Proc Ailment | 20 stacks | Flame Burst | Fire |
| Doom Brand | when it ends | Doom Brand | Necrotic |
| Fireball Explosion Proc Ailment | 5 stacks | Flame Burst | Fire |
| Holy Aura Flame Burst Proc Ailment | 60 stacks | Holy Flame Burst | Fire |
| Poison Explosion Proc Ailment | 5 stacks | Poison Nova | Poison |
| Shadow Daggers | 4 stacks | Shadow Daggers | Physical |
| Silk | 5 stacks | Thicket Slash | Physical |
| Spark Charge | when it ends | Spark Charge | Lightning |
| Stone Stare | 4 stacks | Shadow Daggers | Physical |

Ailments with no damage at all (debuffs): All Resistances Reduction, Arcane Mark, Berserk Trigger, Blind, Chill, Chill of Aberroth, Critical Vulnerability, Curse of Aberroth, Efficacious Toxin, Exposed Flesh, Falconer's Mark, Fear, Flurry 1 Hit, Flurry 2 Hit, Frailty, Frailty of Aberroth, Grounded, Immobilize, Marked For Death, Melee Defense Shred, Netted, Root, Shock, Shock of Aberroth, Shred Armor, Shred Cold Resistance, Shred Fire Resistance, Shred Lightning Resistance, Shred Necrotic Resistance, Shred Physical Resistance, Shred Poison Resistance, Shred Void Resistance, Slow, Spiders, Stagger, The Gate, Withering. There are also about
60 buffs (Haste, Frenzy, Aspects, Shrouds and others) that deal no damage.

## Possible later (not in `state` yet)

The game also tells, for every hit: which skill it came from, which ailment if it is
damage over time, hit or damage over time, and Melee / Spell / Throwing / Bow. If your
design needs any of these (for example a "per skill" or "hits vs DoT" view), mark
which ones and show where; EpochPact can add them to `state`.
