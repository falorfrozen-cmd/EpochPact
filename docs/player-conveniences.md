# Player conveniences

These features use the loaded offline character. They do not run campaign, reward,
stash-transfer or other save operations when the interface opens.

## Loot & Pickup

Auto Pickup pauses both EpochPact collection and the game's distant-pickup queue
while an item tooltip is pending or visible. Closing the card resumes collection;
the last item cached by the game's tooltip system does not keep collection paused.
Manual clicks remain available. Automatic collection is limited to eight pickups
per scan and uses the existing loot filter and category selections.

Each scan looks at up to 256 ground labels (and 256 entries of each other drop
list). With more on the ground, for example after a large Prophecy or a boss, the
next scan continues where the previous one stopped, so every drop is reached
within a few scans and a long list of unwanted items no longer stops collection
until the next zone. The work per scan stays the same.

**Quality rules → Minimum Weaver's Will** (0–28, 0 = off): in the Quality pickup
mode, a Unique with at least this much Weaver's Will is collected too. Such a
Unique has no Legendary Potential, so **Minimum Unique LP** alone never picks it
up. The rule is LP threshold **or** Weaver's Will threshold **or** wanted T7.

The radius slider accepts 0–60 game units. `0` preserves collection of all loaded
drops; a positive value requires a valid player position and only collects drops
inside that distance. It does not load distant or unloaded objects.

The interval control chooses how often collection scans: 0.25 s (default) up to
10 s. Longer intervals do less work per second.

When the game shows its own "inventory full" message, equipment labels are no
longer retried every scan. Gold, potions, tomes, ancient bones and crafting
materials (shards, runes, glyphs) keep being collected. Equipment collection
resumes after you open and then close your inventory. `pickupradius` reports
`inventoryFull`, `inventoryFullEvents` and `skippedWhileFull`.

**Pick up everything now** collects every drop the game has loaded, ignoring the
radius, up to 32 pickups per frame over a few frames. Loot rules, categories, the
loot filter and inventory space still apply; it clears an earlier full pause and
retries once.

**Memory Amber, Soul Embers and XP motes** are a separate pickup category
("amber"). The game normally collects them only when you walk close; with the
category enabled, Auto Pickup and Pick up everything now collect them like gold.

### Pick up from afar

**Pick up from afar** (Loot & Pickup, player request 2026-10-29: "where I click it loots, I
don't have to walk to the loot myself") picks up an item you click on the ground where it
lies, without walking there. A click on an item's name (`GroundItemLabel.ClickedItem`, also
the gamepad's) asks the game's own pickup (`requestPickup`) with the item's ground record
moved to your character's feet for that call, as auto pickup does, so the game's range check
passes; inventory space and Stash tabs on pickup still apply. When the
game keeps the item (no room in the inventory), the click goes on as usual and your character
walks there. Offline only; switched off, the hook is removed.

Live check 2026-10-29 (EpFeedbackTest clone, `builds\remote-live1`, test commands `farspawn`
and `labelclick`): an item dropped 15 units away and its label clicked. Off: the character
walked 9.7 units and picked it up on arrival. On: the character did not move (0.0) and the
item was in the inventory within 1.5 s; counters picked 1, walked 0.

### Recent pickups and the loot window

The Loot page lists recent item, gold and amber pickups (up to 300, newest first)
with rarity, who picked it up (you or Auto Pickup) and the loot filter rule that
matched. When the game takes the nearby affix shards, runes and glyphs along with
one you pick up, each of them is listed too. The list and the loot window keep
showing new pickups after the game is started again (each game session is kept
apart). "Rule N" is the same number the game shows with its **Item rule
numbering** setting (the rule's Order + 1; 0 means no enabled rule matched and
the item was shown by default). The rule's custom name is shown, otherwise the
game's own rule description. Items, gold and the filter are never changed by
reading the list. Point at an affix shard (in the list or the loot window) to
see what its affix does: prefix or suffix, level, the item types it rolls on
and the game's own value text for each tier (lowest and highest roll), read
once per affix with `affixinfo <affix id>` (values for the first item type the
affix rolls on; some types such as two-handed weapons scale them).

**Open loot window** (desktop app) opens a small window without a Windows title
bar. Point at it to see its bar: drag the bar to move the window, drag the
corner grip to resize it, **−** hides it (the button in EpochPact then reads
**Hide loot window** / **Open loot window**) and **⚙** opens its options: what
to show (**Items** by rarity: Normal, Magic, Rare, Exalted, Unique, Set,
Legendary; **Resources**: Gold, Memory Amber, Soul Ember, XP motes;
**Materials**: Affix shards, Runes, Glyphs, Other materials; **All** / **None**;
the old Gold and Materials switches carry over), background colour and
**background opacity** (0–100%;
only the background becomes see-through — text keeps its full strength and
gets a dark outline below 60%), text colour, text size, row spacing and
whether the bar is shown always or only on hover. It stays above the game
only: when EpochPact or another program is in front it goes behind it, and
while the game is minimised it is hidden (the damage window behaves the same).
It only reads the list. Exclusive fullscreen draws over every window. Both
windows remember their place, size and options; a window left open when
EpochPact closes opens again when it starts, and a damage session left running
starts again once the game is ready (hiding a window yourself forgets it).

Identical resources picked up one after another share one line: gold and
amber are added up, the same material is counted; equipment always has its own
line. The window shows the total gold, and each item shows its affix tiers and
the full rule (number, Show/Hide/Highlight, name).

### Loot filter window

- **More loot filter colours** (off by default, `tierset filtercolors 0|1`) adds 26
  colours after the game's own in its label colour list (18 → 44) and beam colour
  list (22 → 48): pinks, violets, blues, teals, greens, golds, oranges, reds,
  browns and greys. Each one is a copy of the game's own most saturated colour with
  every shade (label, button, border, shine, tooltip, beam) moved to the new hue,
  so labels on the ground look like the game's. The filter stores the colour's
  number; where EpochPact does not run, the game limits that number to its own
  list and shows its last colour. Switching it off puts the game's own lists back.
  Nothing in the saves changes. (Stash tab colours are stored in the character
  save, so they are not extended.)
- **Colours in loot filter rule names** (on by default, `tierset namecolors 0|1`):
  the game shows a rule's own name through `TMPExtensions.SanitizeUserInputForDisplay`,
  which breaks every tag. Colour (`<color=#FF4040>`, `<color=red>`, `<#40C0FF>`,
  `</color>`), bold, italic, underline and strikethrough tags (`<b>` `<i>` `<u>`
  `<s>` and their closing tags) now pass; any other tag (`<size>`, `<sprite>`,
  `<link>` …) stays plain text as before. The rule name field accepts these
  characters (no validation, 115 characters). The tooltip rule line keeps the same
  tags of a name you wrote. Example: `Hide <color=#FF4040>junk</color> <b>rings</b>`.

### Loot filter files

The game keeps every loot filter as an XML file in
`%USERPROFILE%\AppData\LocalLow\Eleventh Hour Games\Last Epoch\Filters`.
That folder is shared by all characters and seasons, so a filter made for one
season is already available in the next. The Loot page's **Loot filter files**
panel lists them with their enabled/total rule counts and offers:

- **Export**: copies the filter to `%LOCALAPPDATA%\EpochPact\filter-exports`
  and opens that folder, ready to share.
- **Import filter…**: adds a filter `.xml` someone shared, or every filter in an
  EpochPact backup `.zip`, as **new** filters. A name that already exists gets a
  suffix such as "(2)"; the filter's own name inside the file is changed to match
  so the game lists both.
- **Duplicate**: a copy named "… copy", to experiment without touching the
  original.
- **Back up all**: one ZIP in `%LOCALAPPDATA%\EpochPact\filter-backups`; the
  newest 20 are kept.

Existing filter files are never edited, replaced or deleted. Imports are checked
first: only real Last Epoch filters (an `ItemFilter` with a name and rules) up to
2 MB are accepted, and files with a document type declaration are refused. A ZIP
is checked completely before anything is written. The game watches its filter
folder: an imported or duplicated filter appears in the game's own filter list
within seconds, also while you play (measured). The game itself can also copy the
active filter to the clipboard and create one from the clipboard; the file panel
adds backups and file sharing on top of that.

## World & Exploration

Chest and shrine switches use the normal condition handler within the
**Interaction radius** (1–30 game units, 3 by default, the earlier fixed
distance). **Interaction area shape** chooses a circle or a square around each
object; the height may differ by up to the same distance (at least 3), so
objects on another floor stay out. Used or unavailable interactions are skipped.

**Show interaction areas** draws the area on the ground around every chest,
shrine and portal within 60 game units that will still be used (at most six new
outlines per quarter second); an outline disappears once its object is used.
**Interaction area colour** is remembered on this device and sent again when a
character is ready. If the game cannot draw an outline, the switch turns itself
off and `qolread` reports the reason in `area.error`. `qolarea radius|shape|show|color`
sets these values; `qolread` reports them in `area`. An object the game still
offers after a trigger is tried again after 2, 4 and 8 seconds and then left
alone. Collection and interactions pause while inspecting an item or using the
inventory, stash or vendor.

Objects are found once, by six scene searches spread over a third of a second,
when a switch is turned on. After that, new zones register their chests,
shrines, portals and cave entrances while they load (Awake/initialisation
hooks), so a zone change searches nothing. A zone queues a few hundred condition
handlers while it loads; they are sorted in batches (32, or 1 ms, per 50 ms
tick), so nearby objects are usable within about a second of arriving.
`qolread` reports `sceneSearches` and `handlersQueued`/`handlersClassified`/
`handlersPending`.

The portal switch arms **one** nearby interaction, waits ten seconds after loading
and switches itself off after use. This prevents immediate return-portal loops.
Generic portals and the Primal Hunt cave entrance use their existing game
interaction; no destination, key, quest condition or reward is fabricated.

### Rage of Morditas (Season 5)

Five settings scale the values the game loaded for the Rage of Morditas
encounter, only while offline play runs; nothing is written to the character or
the save, and 1 (or the game's own) writes the game's values back. Going online
puts them back as well. `rageread` reports the values in use and the game's own;
`rageset spawn|hunttime|charges|buffs|blood <value>` sets them.

- **Rage of Morditas chance** (1–5): the game places a Blood Crystal only in
  zones it allows (campaign zones outside some chapters, and normal Monolith
  echoes — not towns, the Arena or Woven echoes) and there rolls 30 % times the
  zone's own factor (about 0.9 to 3.3; echoes add the Weaver bonus). The
  setting multiplies the 30 %; from about 4 every allowed zone gets one.
- **Rage of Morditas hunt time** (1–5): multiplies the hunt's starting time
  (the game's 20 s, 25 s for the first band) and the time each kill adds (0.5 / 2
  / 3 s for normal, magic and rare monsters, 10 s for a champion). The game
  divides a kill's time by its escalation curve (1 up to 25 s of hunt, 2 at 30 s,
  3 at 40 s, 4 at 50 s …), so long hunts gain less per kill.
- **Kills for Bloodrage charges** (0.2–1): the share of the game's 20, 50, 90,
  140 and 200 hunt kills needed for the five charges (0.25: 5, 13, 23, 35, 50).
- **Kills for hunt buffs** (0.2–1): the same for the game's 20, 50, 90 and 140
  kills that each grant a random hunt buff.
- **Blood forging potential share**: every reward item of the encounter gets
  Ice or Blood forging potential; the game gives Blood to 50 %. Choose 0 %
  (all Ice) up to 100 % (all Blood). The Rituals of Ice and Blood keep their own
  type.

Not offered: more reward slots (the five slots are part of the save format),
anger and Morditas progress (saved with the character).

Measured on a test character (`research/live/rage4`, `rage8`): with the game's
own chance 3 of 6 allowed zones got a crystal (expected 3.2 from their factors),
with 4 all 6; a hunt started at 50 s instead of 25 s; with a quarter of the kills
the game granted the first charge and buff at 9 kills (threshold 5) and the
second at 15 (threshold 13), and each kill added 0.5 × 2 seconds before the
game's curve; 400 reward items each gave Blood 53 % (game's own), 100 %, 0 %
and 24 % (for 100, 0 and 25). Every value was back to the game's own after reset.

## Display & Camera

Zoom accepts 1–2×. Camera angle accepts 0–85°; `0` uses the normal game angle.
They borrow the native camera parameters for each call and restore those parameters
afterward, so their values do not compound or overwrite saved camera preferences.

Shadows, real-time reflection probes, screen-space reflections, bloom, motion
blur, ambient occlusion and volumetric fog have session switches. EpochPact keeps
each original renderer/profile value and restores it when its switch is disabled.
Missing scene effects are skipped; disabling an effect cannot improve performance
if that effect was already absent or disabled. Saved graphics preferences are kept.
Once a second the switches are applied to the post-processing volumes the game
has registered (its own `PostProcessManager` list) and to the volumetric fog
camera (`HxVolumetricCamera.Active`) — no scene search, about 0.1 ms. Reflection
probes have no such list: they are searched once, two seconds after a zone
loads, and when the switch is turned on (well under a millisecond in play). The
fog switch also searches for other fog cameras once when it is turned on.
Components that the game tries to re-enable later are clamped by the existing
`Behaviour.set_enabled` detour. `qolread` reports `graphicsScans` and
`maximumGraphicsMicros`.

### Minimap and overlay map

Session settings, applied to the game's own map configuration and reloaded with
the game's map reload; nothing is written to the game's settings. Closing
EpochPact or **Reset** returns the game's values.

- **Minimap size** 0.5–2.5× scales the corner minimap with its frame.
- **Minimap zoom** 0.25–4×: below 1 zooms in, above 1 shows more of the area.
- **Minimap opacity** 0–1 fades the whole corner minimap: map, frame and icons.
  0 keeps the game's own look.
- **Overlay map opacity** 0–1, beyond the game's slider range; 0 keeps your game
  setting.
- **Overlay map zoom** 0.25–4× and widens the game's overlay zoom limits to fit.

A larger minimap would cover the quest tracker below it, so the tracker moves
down with the minimap's bottom edge and returns when the size is reset. The
values are re-applied on the next frame after a new area's map loads.

### Item tiers

- **Affix tiers on item icons** writes each affix tier as coloured digits in the
  top-left corner of inventory, stash and equipment icons, highest first (for
  example `7 6 5 4`). The label is a copy of the icon's own quantity text, so it
  scales with the game's interface. Display only; items are unchanged.
- **Tier tags in tooltips** adds a coloured `T1`–`T8` tag in front of each
  prefix, suffix and sealed affix (regular, primordial and corrupted) in item
  tooltips. Sealed lines keep the game's own sealed colour; the tag shows the
  tier.
- **Colour affix lines by tier** colours the whole affix line in its tier
  colour (rich-text colour; the game resets the text's own colour property).
  Exalted, experimental, personal (champion), corrupted and similar affixes
  carry the game's own colour tag inside the line (for example
  `<color=#ABABFFFF>` for experimental), which would win over the tier colour,
  so the coloured line is written without the game's colour tags; other tags,
  icons and the corrupted glow stay. Both can be used together or separately. The tooltip of the item it is
  compared with (shown beside it while comparing) is coloured the same way, through the game's own
  `updateCompareAffixesText`. The game's original line text is
  remembered and written back exactly when they are switched off. If a tooltip
  shows a different number of affix lines than the item has, that tooltip is
  left unchanged instead of guessing (`tierread` counts it as
  `tooltipMismatches`).
- **Loot filter rule in tooltips** adds a line to every item tooltip: the
  matching rule of the active loot filter (number, Show/Hide/Highlight, name
  or the game's description), or "no rule matched". It is drawn in the colour
  the filter gives the item's label: the rule's colour preset when the rule
  recolours (`EpochColorManager.GetLootfilterRuleColor`), otherwise the item's
  own colour; Hide rules are grey because hidden items have no label.
  **Rule line colour** on the Display page can instead give every rule line
  one colour you choose (`tiercolor rule <RRGGBB>`, back with
  `tiercolor rule label`). The rule is written exactly as in the filter: the
  line uses the font of the tooltip's "Requires: Level" line, because the item
  type and affix lines' fonts draw lowercase letters as small capitals.
  **Rule line position** puts it under the item type (Top), before the affixes
  (Middle) or at the end of the tooltip below the flavour text (Bottom); the
  line is added to the header's or the body's own layout group, so the game
  lays it out. **Rule line shows** chooses its parts: everything, number + Show/Hide + name, number + name,
  Show/Hide + name, name only, number + Show/Hide or number only (`tierset ruleparts`; bits 1 "Loot
  filter:", 2 number, 4 Show/Hide/Highlight, 8 name). A rule without a name of its own shows the
  description the game makes for it ("Hide all Normal/Magic items") without its leading "Show all",
  "Hide all", "Show", "Hide", "Highlight" or "Recolor" words; a name you gave a rule is shown as written.
- **Mark Personal, Champion and Experimental affixes** (on by default) puts a small square before
  those affix lines, as the game marks corrupted ones: Personal sky blue `4FC3F7`, Champion amber
  `FFC107`, Experimental orchid `E040FB` (a TMP `<mark>`), so they stand apart from regular affixes.
  Shown with the tier tags or tier colours (`tierset special 0|1`).
- **Tier colours**: by default in the game's rarity order: T1 normal white `FFFFFF`, T2 magic blue
  `2E8BFF`, T3 rare yellow `FFD700`, T4 exalted purple `AA55FF`, T5 unique orange `FF5500`, T6 set
  green `00FF00`, T7 legendary red `FF0044`, T8 cyan `00FFFF`. Each tier can be changed; your colours are
  remembered on this device and applied whenever a character is ready. A
  changed colour or setting also rebuilds the game's tooltip
  (`TooltipItemManager.ForceUpdateTooltip`), so an item shown before the change
  is not left with the old look.

### Text and font

- **Interface font** replaces the font of the game's interface text with an
  installed Windows font (the list comes from Windows). Every style a family has
  (Light, Semilight, Semibold, Bold, Black, Italic, Bold Italic …) is listed as
  "Family — Style" (sent as `fontset family Family%7CStyle`); Windows is asked
  for 26 common style names once per family (about 1.3 s for 360 families the
  first time). A family without a Regular face loads with the first style it has.
  **Search fonts** narrows the list as you type (by name or style); the chosen
  font stays listed. Characters the font does not have, such as symbols, still
  use the game's font.
- **Interface text size** multiplies the size of interface text (0.7–1.5).
- **Letter case as written** (`fontset case 0|1`) shows names and texts the way
  they were written: the game draws many texts in capitals, lower case or small
  capitals through the text style (TMP UpperCase, LowerCase, SmallCaps); those
  styles are left out, everything else stays. Stash tab names, rule names and item
  names then keep the capitals, endings and punctuation you typed. The game's own
  font draws lower-case letters like small capitals, so choose a Windows font for
  real lower-case letters. Each text's own style is restored exactly when it is
  switched off.
- Text in panels opened later gets the same font and size. "Game font" and size
  1 restore every text's own font, material and sizes exactly; closing
  EpochPact does the same. Nothing is written to the game's settings.

### Buffs & debuffs in game

A native, click-through HUD inside the game: your buffs and debuffs and the
current target's ailments with the game's icons, names, two-line descriptions,
stacks and time left. Size, rows, descriptions and which panels show can be
set. See [effects-hud.md](effects-hud.md) for how it reads the game's own UI
and what was measured.

### Memory Amber multiplier

**Memory Amber gain multiplier** (general multipliers, ×1–×100) multiplies the
Memory Amber a member of The Woven picks up (`Faction.GainFavor` on The Woven);
the reputation from that amber rises with it. It shares the hook of the Circle
of Fortune favor multiplier and is removed when both are 1.

### Weaver's Will multiplier

**Weaver's Will multiplier** (General → Weaver's Will, ×1–×10) multiplies the
Weaver's Will the game rolls for a Unique (`ItemData.RollWeaversWill`), rounded
and never above the game's maximum of 28: at ×2 a Unique that would roll 10 gets
20. The game rolls it in one place for every source — drops and rewards, opened
cocoons (`RevealCocoonedUnique`), Eternity Cache, Soul Gambling and Unique
empowering — so the multiplier applies to all of them. Items you already have do
not change. At ×1 the hook is removed.

### Legendary Potential rolls

**Legendary Potential rolls** (General settings, 1–1000, player request 2026-10-27)
makes the game's own roll, `ItemData.RollLegendaryPotential(entry, minimum, ilvl,
corruption, cofMultiplier, ref improvedByCoF, nonCoFMultiplier)`, up to this many
times and keeps the highest; it stops early at `ItemData.MaxLegendaryPotential`, so a
Unique never gets more than the game itself can roll, and `improvedByCoF` is the kept
roll's. In 1.5.3 `RollLegendaryProperties` picks a Unique's legendary type first
(`GetRelevantLegendaryProperty`: 0 Legendary Potential, 1 Weaver's Will) and only then
rolls one of the two, so Weaver's Will Uniques are not changed; three other game
paths call the roll directly (empowering and rewards). Items you already have do not
change. At 1 the hook is removed.

Live check 2026-10-27 (EpFeedbackTest clone, `builds\lp-live1`): 40 random level-50
Uniques per setting from the game's own generator at corruption 0. 1 roll: LP 0 ×30,
LP 1 ×7 (Weaver's Will ×3); 10 rolls: LP 0 ×8, 1 ×24, 2 ×3 (Weaver's Will ×5);
1000 rolls: LP 0 ×8, 1 ×7, 2 ×17, 3 ×8 (Weaver's Will ×0); 1 roll again: LP 0 ×36,
1 ×3. How high a Unique can roll still depends on its level and corruption (no LP 4 at
corruption 0 in 1000 rolls); the Uniques left at 0 are most likely ones the game gives no
Legendary Potential there.

### Forging Potential on drops

**Forging Potential on drops** (General settings, x1–x10, player request 2026-10-28: more
Forging Potential on dropped items for a controlled crafting experience) multiplies the
Forging Potential the game rolls for a new Normal, Magic, Rare or Exalted item, rounded:
static `GenerateItems.rollForgingPotential(item, ilvl, hasUncraftableAffix,
generationContext)`. Its callers in 1.5.3 (`call_hits` on GameAssembly RVA 0x11D1360) are
`GenerateItems.GenerateAffixes` (every generated item), `SimilarItemExtensions.GetSimilarItem`
(similar-item rewards) and one function in the shop code; each hands the result to
`ItemData.SetForgingPotential`. Contexts Normal, Nemesis and Rage of Morditas are boosted
(drops, rewards, Nemesis, Spawn Items); Shop, Gambler and SoulGambler keep the game's own. Uniques and Sets have none, and
items you already have do not change. At 1 the hook is removed.

63 is the most an item can have, whatever the multiplier: `SetForgingPotential` clamps to
63 (compiled in; the `ItemData.MaxForgingPotential` const says 255), and a byte of 64, 120,
200 or 255 written straight into an item comes back as 63 from its saved form
(`RebuildID` + `setValuesFromSerialisation`). The player's example (32 at x4 = 128) is
therefore 63; for more crafting than 63 allows, the Crafting page's Forging Potential cost
setting lowers what each craft uses.

Live checks 2026-10-28 (EpFeedbackTest clone, `builds\fp-live1` and `fp-live2`): the game's
own generator, gear bases in turn, 40 items per rarity at level 100 (`fp-live2`). x1: Normal 16–32 (mean
21), Magic 14–24 (19), Rare 22–45 (29), Exalted 32–56 (40). x2: Normal 32–63 (41), Magic
28–56 (36), Rare 44–63 (55), Exalted all 63. x4 and x10: nearly all 63. Every item read the
same after its saved form was rebuilt. The roll itself on a Rare, per context at x4: Normal
113, Nemesis 111, Rage of Morditas 113 on average; Shop 20, Gambler 21, SoulGambler 29 (as at
x1). Refused: 11, 0.5 and text. Final build (`builds\fp-live3`): x3 at level 90 gives Normal
48–63, Magic 42–63, Rare and Exalted 63; eight items spawned at x2 (level 75) and picked up
had Normal 34/36, Magic 28/34, Rare 40/58, Exalted 63/63, and all eight kept their Forging
Potential and affixes after the game restarted. The General settings card shows the
setting (`2-general-800.png`); like the other multipliers it is a session setting.

### Unique minimum roll

**Unique minimum roll** (General settings, 0–100 %, player request 2026-10-28: "all uniques
that drop naturally max rolled") makes new Uniques, Set items and Legendaries roll every value
in the best part of its range. An item keeps one roll byte (0–255) per roll ID in
`ItemData.uniqueRolls` (each rollable `UniqueItemMod` of `UniqueList.Entry.mods` reads one;
several can share one) and one per implicit in `implicitRolls`. Each roll keeps its place
but within the best (100−n) % of 0–255 (`lootcraft::rules::MinimumRoll`): at 80 the best
fifth, at 100 the best end itself, at 0 the game's own (hooks removed).

Which end is best is the game's own answer: `ItemData.MaxRollIsBestForUniqueRollID(mods,
rollID)` compares each modifier's value at roll 0 and 255 and flips the answer for properties
whose info says lower is better (Damage Taken, Mana Cost, Current Health Drain, and some
Player/Ability properties such as a distance or a delay), so a downside rolls as small as it
can. Implicits are raised toward 255, as the game keeps the higher implicit roll when it
rerolls them (`RerollImplicitRollsButKeepOldIfHigher`).

Where: after `GenerateItems.RollBaseItem` returns (drops, rewards, Nemesis, Spawn Items). It
randomises the rolls before it writes `uniqueID` (1.5.3: `randomiseUniqueRolls` at
0x11C91E3, `uniqueID` at 0x11C923D), so a hook on the randomisers alone saw Unique 0's
modifiers; the first try did exactly that (only roll IDs 0–2 raised, two with the wrong end).
Also after `ItemData.RevealCocoonedUnique` (a Weaver cocoon keeps its rolls when it opens),
followed by `RebuildID` + `setValuesFromSerialisation`. Gamblers keep the game's own: while
`GenerateItems.rollItemForGambling`, `rollItemForSoulGambling` or `rollItemForFactionGambling`
runs, the rolls are left alone. Crafting rerolls (`ItemContainersManager` craft actions) and
similar-item rewards (`SimilarItemExtensions.GetSimilarItem`) call the randomisers
themselves and are not changed, nor are items you already have.

Live checks 2026-10-28 (EpFeedbackTest clone, `builds\ur-live3`, `ur-live4`, `ur-misses2`):
test command `uniquecheck` makes Uniques with the game's own generator (120 at level 100, 108
Uniques, 11 Sets, 1 Legendary), 58 Soul Gambler Uniques and 23 Weaver cocoons opened with
`RevealCocoonedUnique`, and measures each roll toward its best end. At 0: drops mean 121 of
255, lowest 0. At 100: all 380 unique rolls and 360 implicits at the best end, cocoons 63 of
63. At 80 (level 60): lowest 204, mean 230; at 50: lowest 128. The Soul Gambler stayed
random at every setting (mean 124–133). Every item read the same after its saved form was
rebuilt. Five items spawned at 100 and picked up (four Uniques, one Set) had every used roll
at its best end (a lower-is-better roll at 0) and implicits at 255, and kept them after the
game restarted. Refused: 101, -1 and text. The General settings card shows the setting with
its % unit (`card-general-1100.png`).

### Idol prefixes and suffixes

**Idol prefixes** and **Idol suffixes** (General settings, 1–3, player request 2026-10-27:
idols with 1–3 prefixes and 1–3 suffixes like other items, no crafting). The game gives a
non-unique idol one prefix and one suffix: `GenerateItems.RollAffixes` makes every such
idol Magic (2 affixes) and `IsValidAffix` allows one of each on an idol, with the limits
compiled into the game. In the saved form an item of rarity 4 or lower keeps up to 63
affixes and the loader has no idol limit, so EpochPact adds the extra ones after the game
has rolled its own:

- After `GenerateItems.GenerateAffixes`, for an idol (types 25–33) of rarity 2 that is not
  a Heretical idol (`ItemList.IsHereticalIdol`), made outside the Shop, Gambler and Soul
  Gambler contexts: drops, rewards, Nemesis and Spawn Items. Idols you already have do not
  change. Since 2026-10-29 Weaver idols and idol altars too (see below).
- Each idol rolls 1 to n prefixes and 1 to n suffixes (evenly). Each extra affix is picked
  among the standard affixes of that kind that can roll on the idol type and that the
  game's own `IsValidAffix` allows on this idol with its affixes so far (level, idol type,
  class, no repeats; the prefix and suffix counts are passed as 0), weighted by the game's
  `AffixWeighting`; `RollTier` gives the tier, the roll is random. Then the item's saved
  form is rebuilt (`RebuildID`) and read back (`setValuesFromSerialisation`). When no
  affix of a kind is left, the idol keeps fewer.
- The idol stays Magic; the tooltip lists every affix. Offline only; at 1 and 1 the hook
  is removed.

Live checks 2026-10-28 (EpFeedbackTest clone, test build command `idolcheck`, every idol
base from the game's generator, nothing dropped):

- `builds\idol-live`: at 1 / 1 86 idols of all 43 idol bases, every one 1 prefix and 1
  suffix. At 3 / 3 430 idols at item level 100: 1p1s 86, 1p2s 37, 1p3s 49, 2p1s 50, 2p2s 49,
  2p3s 39, 3p1s 40, 3p2s 38, 3p3s 42; 215 idols at level 20 spread the same way; no affix
  twice on an idol, every saved form read back the same, every one still Magic. At 3 / 1
  only prefixes were added. Six idols spawned (6, 4, 5, 5, 5 and 3 affixes) and picked up
  showed 6, 4, 5, 5, 5 and 3 lines in the game's tooltip, for example a Rogue Adorned Idol
  with five Rogue affixes. Back at 1 / 1: the hook out and 1 prefix and 1 suffix again.
- `builds\idol-live2`: 645 idols at level 100 and 344 at level 40 the same way (no repeats,
  saved forms the same). Six idols spawned and picked up (two Minor Weaver Idols kept their
  2 affixes; Grand Arcane Omen Idols and Adorned Heorot Idols got 3–5), then the game closed
  and started again: the same six tooltips, line for line.
- `builds\idol-live3`: 30 idols of every base at 3 / 3. The four Weaver idol bases (25.2,
  26.1, 27.1, 28.1) stay at 1 and 1; on every other base an idol has 1.7–2.2 prefixes and
  1.8–2.5 suffixes on average. Against 150 of the game's own idols of every base, 2285 of the
  2292 added affixes were ones the game itself rolled on that base as that kind; the other 7
  (Health Regeneration, Poison and Lightning Resistance, Shared Fire Damage on class idols
  with 20–33 prefixes in their pool) are common idol affixes the samples had not met yet
  (with 50 samples per base it was 121).

Enemy drops check 2026-10-29 (player report: idols did not drop with more affixes;
`buildsix-live3`, test commands `droptest` and `groundidols`): 120 enemy drops through the game's own
static `ItemDrop.DropItem` per round, the ground cleared before each round, at idol settings 3/3:
rarity x1 7 idols, rarity x10 2, rarity x10 with Forging Potential x3, Unique minimum roll 100 and
LP rolls 50 4; every idol had its extra affixes (the hook counted each one), all still Magic.
Settings are per session unless Apply at startup restores them; idols already dropped keep theirs.

### Idol altars and Weaver idols

**Idol prefixes** and **Idol suffixes** also cover idol altars and Weaver idols (player
request 2026-10-29: altars and Weaver idols should benefit from more prefixes and suffixes
on drop as well). Each one gets up to n − 1 more of each kind than the game gave it:

- **Idol altars** drop Rare with two prefixes and two suffixes (the base allows four); with
  3 a new altar rolls 2 to 4 prefixes and 2 to 4 suffixes, picked among the altar affixes
  (Effect of Prefixes / Suffixes for Idols in Refracted Slots, Maximum Omen Idols, Health
  per Equipped Heretical Idol, ...) by the game's own `IsValidAffix` (as a non-idol),
  `AffixWeighting` and `RollTier` at the altar's rarity. A Magic altar (Spawn Items) keeps
  its 0–2 of each kind and gains up to n − 1; Normal altars have none and stay so.
- **Weaver idols** (Small, Minor, Humble and Stout Weaver Idol) drop with one prefix and
  one suffix, at least one of them a Weaver affix. Their extra affixes come from the same
  pool the game uses for that Weaver idol: its Weaver affixes and the standard idol
  affixes, as `IsValidAffix` allows them on a Weaver idol (`isWeaverIdol` and
  `secondAffixCanBeIdolWeaver` on), weighted as the game weighs them.
- Heretical idols, shops, gamblers and items you already have do not change.

Live checks 2026-10-29 (EpFeedbackTest clone, test build):

- `builds\ench-live1`: 160 Weaver idols at 3 / 3 (40 of each Weaver base, item level 100):
  every shape from 1p1s to 3p3s, 1 to 6 Weaver affixes per idol, no affix twice, every
  saved form read back the same, all still Magic.
- `builds\ench-live2` (test command `altarcheck`, 8 altars of each of the 13 altar bases per
  rarity, item level 100): at 1 / 1 every Rare altar 2p2s (208), Magic altars 1p1s 50,
  0p2s 48, 2p0s 6, Normal altars none. At 3 / 3 the 208 Rare altars spread over 2p2s 30,
  2p3s 19, 2p4s 28, 3p2s 29, 3p3s 22, 3p4s 19, 4p2s 22, 4p3s 17, 4p4s 22; only the 14 altar
  affixes the game itself rolled at 1 / 1, every saved form read back the same. 516
  idols at 3 / 3 in the same run: plain idols 1p1s to 3p3s as before (no Weaver affix on
  them), Weaver idols 1p1s to 3p3s. Three Rare altars (8, 7 and 6 affixes) and three Weaver
  idols (4, 5 and 5) spawned and picked up, then the game closed and started again: the
  same six items, affix for affix.

### Woven Enchanter uses

**Woven Enchanter uses** (General settings, x1–x40, player request 2026-10-29: "a custom
slider on uses every time you encounter it, from 1 to 20-40") multiplies the uses a Woven
Enchanter allows each time you meet one, so you can reforge more idols there: the window's
"(1/1 USES REMAIN)" becomes 20/20 at x20. The Enchanter's limit is a
`ZoneRestrictedCraftingManager` of crafting type IdolEnchantment; after the game's own
`CalculateMaxUses` its `maxUses` becomes the game's value times the setting (EpochPact
remembers the game's value, so a second calculation is not multiplied again). A change
also applies to an Enchanter already in the scene and refreshes its window
(`OnUsesChanged`). Each craft still costs its Memory Amber, and the craft itself is the
game's. Offline only; at x1 the game's own value is put back and the hook is removed.

Live check 2026-10-29 (EpFeedbackTest clone, `builds\ench-live1`, test command
`enchantercheck`; the clone cannot reach a Woven Echo, so the check uses an Enchanter
manager of its own on a hidden object, with both use-limit types): at x1 1/1 use, after one
craft 0/1 and `CanCraft` false; at x5 5/5, a second `CalculateMaxUses` still 5, after one
craft 4/5 and `CanCraft` true, the window values (`GetUsesValuesForUI`) the same; back at x1
1/1 again and the hook out.

### Gold multiplier and Recent pickups

The gold multiplier counts the gold `GoldTracker.modifyGold` adds while
`GroundItemManager.pickupGold(GroundItemList, Actor, uint)` runs. From dev.17 the loot
log (Recent pickups) hooked that same method at start-up, so the multiplier's own hook
was refused as "already installed" and skipped without a word: the setting showed x10
but pickups were not raised (player report 2026-10-27). Turning the multiplier off also
took the loot log's hook out, so gold pickups stopped appearing in Recent pickups. Now
the loot log's hook marks every pickup for the multiplier (`loot::GoldPickupScope`);
the multiplier puts in its own pickup hook only when the loot log's is missing and only
removes what it put in.

Live check 2026-10-27 (EpFeedbackTest clone, `builds\spawn-live2`): a 25 gold pile picked
up at x1, x10 and x1 again added 25, 250 and 25 gold; Recent pickups listed each pile,
also after the multiplier was off again.

### Ancient Bones multiplier

**Ancient Bones multiplier** (General settings, x1–x100, player request 2026-10-29:
"Bones from the dino mechanic, an increase or multiplier slider") multiplies the Ancient
Bones that drop for you. Every bone drop goes through
`GroundItemManager.dropAncientBoneForPlayer(player, amount, ...)`; EpochPact multiplies
the amount there (rounded, at most 2,147,483,647), so the pile on the ground already shows
the larger number and picking it up gives that much. Bones you have and what you spend do
not change. Offline only; at x1 the hook is removed.

Live check 2026-10-29 (EpFeedbackTest clone, `builds\ench-live1`, test command `bonedrop`):
a 10-bone pile dropped through the game's call and picked up by auto pickup added 50 bones
at x5 and 10 at x1.

### Weaver's Will Unique share, upgrade speed and affixes

The game has 18 Weaver's Will Uniques; 14 of them can drop at random, so about
4 % of the Uniques it picks at random are Weaver's Will Uniques.

- **Weaver's Will Unique share** (General → Weaver's Will, 0–100 %): that percent of the Uniques
  the game picks at random (`UniqueList.rollUnique` without a required legendary
  type: enemy drops, rewards, opened cocoons) is first asked for as a Weaver's
  Will Unique with the same item type, level and drop rules; when none fits, the
  game's own pick follows. The four special Weaver's Will Uniques that never drop
  at random stay that way. 0 removes the hook.
- **Weaver's Will upgrade speed** (General → Weaver's Will, ×1–×10): after a kill near you, the
  game rolls a chance (higher with more Weaver's Will items equipped) and, at
  most once every 15 seconds, upgrades one equipped Weaver's Will item
  (`CharacterMutator.UpgradeRandomWeaversWillItem`): it adds a new affix or raises
  one by a tier and spends 1 Weaver's Will. With ×N that upgrade happens N times
  (a fraction is a chance for one more). A Weaver's Will Unique's first upgrade
  always adds an affix; up to four affixes the game then adds or raises, after
  that it only raises, up to T7.
- **Weaver's Will affixes** (General → Weaver's Will, up to 64): the
  affixes the upgrades prefer. When the item has one of them below T7, the
  upgrade raises one of them (the others below T7 are held at T7 while the game
  picks, then their real tiers are written back and the item's data rebuilt);
  a new affix is one of them when one fits the item (the game's own list of
  eligible affixes is narrowed for that pick). When none of them can be raised
  or added, the game chooses as usual. **Refresh** loads the affix names and the
  current selection; **Apply selection** sends it; none switches it off.

## Crafting → Spawn Items

**Spawn Items** (player request 2026-10-27) is the Crafting page's fourth tab, in the
in-game window and the desktop app. `spawnlist` reads the game's own lists: every base
item of `ItemList.EquippableItems` (gear, idols and idol altars; obsolete, legacy and
unique-only bases left out) and every Unique and Set of `UniqueList` the game does not
hide from players. `spawnitem <save_id> <item> <rarity> <lp> <level> <count>` then makes
the chosen item with the character's own `GenerateItems.initialiseRandomItemData` and
drops it with `GroundItemManager.dropItemForPlayer`, in a ring around the character, as a
monster drop would; nothing is saved until the player picks it up.

- Base items (`b<type>.<subtype>`): Normal, Magic, Rare or Exalted are the game's rarity
  0, 2 and 4 (the number of affixes) and 4 with `forceExalted`, never above the base's
  `maximumAffixes`; the game rolls the affixes for the item level. Idols (types 25–33,
  `maximumAffixes` 2) always come out Magic with one prefix and one suffix: the game's
  `RollAffixes` forces that whatever rarity is asked for (more with **Idol prefixes** and
  **Idol suffixes**, below).
- Uniques and Sets (`u<id>`): the game's rarity 7 (Unique) or 8 (Set); Exulis and Oculus of
  Ruin come out of the generator as Legendary (rarity 9), as the game makes them.
  Legendary Potential is the game's roll, or the chosen 0–4 written on the new item when
  the game's own `ItemData.isLegendaryPotentialItem` agrees (a Unique, not an idol, of the
  Legendary Potential type) and `UniqueList.Entry.getEffectiveLevelForLegendaryPotential` is
  under 200. The game marks the Uniques that never get Legendary Potential with 200
  (the 23 Cocooned items, Gambler's Fallacy, Avarice, Orchirian's Petals, Merophage, The
  Cuckoo); the Uniques that can have it are at 0–105. Weaver's Will Uniques roll their
  Weaver's Will and Sets have neither.
- After it is made, the item's saved form (`ItemData.id`) is rebuilt (`RebuildID`) and read
  back (`setValuesFromSerialisation`), as EpochPact's other item edits do. Without this the
  first build's Legendary Potential was shown but not saved: after a restart it was 0.
- Item level 1–100, 1–20 items at a time; offline only, and the list must belong to the
  loaded character.
- Finding an item (owner feedback 2026-10-28: the first version filtered a closed list only
  when it was opened): **Find an item** filters the results below it as you type, by name,
  item type or base (`calamity`, `helmet`, `set boots`); **All items / Base items / Uniques /
  Set items** narrow them; a click on a row chooses the item. Only the options that apply to
  it are shown: Rarity buttons for bases that take four affixes, Legendary Potential buttons
  (the game's roll, 0–4) for Uniques that can have it, a note for idols, Sets and Weaver's
  Will Uniques; How many is 1 / 5 / 10 / 20. The window's list shows 100 rows at first and
  **Show more** adds 100. The desktop app shows the same results in a list that stays open.
- Text boxes in the window show a blinking caret once clicked (owner feedback 2026-10-28).
  `TMP_InputField` makes its caret when it wakes up with its text component already set;
  added to an active object it woke up without one and never drew a caret, so the box is now
  built switched off and switched on once it is set up.

Live checks 2026-10-27 (EpFeedbackTest clone):

- `builds\spawn-live2`: Calamity with Legendary Potential 3 had LP 3; a Weaver's Will Unique
  rolled Weaver's Will 13; Isadora's Revenge came out as a Set item (rarity 8); 20 Rare rings
  all dropped. A list from another character and an unknown item were refused.
- `builds\spawn-full2` (test build command `spawncheck`, every listed item made and compared,
  nothing dropped): 457 base items at Normal, Rare and Exalted and 486 Uniques with the
  game's roll and with Legendary Potential 4 — every item of the requested base or Unique,
  rarity and number of affixes as above, the Exalted ones exalted, and the saved form equal
  to the made item. Real spawns of 11 items (an Exalted helmet, a Rare ring, a Normal sword,
  two idols, an idol altar, Calamity LP 2, Fractured Crown LP 4, a Weaver's Will Unique,
  Isadora's Revenge, plus three spawned through the window), picked up, the game closed and
  started again: all 11 were in the inventory with the same values, Legendary Potential
  included; the 14 items the character had before were unchanged. (In memory a new base
  item carries the generator's unused Unique rolls; the game does not save them for base
  items, so they read 0 after the restart.)
- In the window: search, Type, the Item list, Rarity (Exalted) and How many chosen through the
  window, then Spawn: 2 Isadora's Revenge and an Exalted Burial Mask on the ground.
- `builds\spawn-final1`: the list marks 368 Uniques with Legendary Potential; with Legendary
  Potential 4 asked for every Unique, the 366 that stay Uniques and can have it got 4, every
  other one 0, and the list agreed with the game on each (Exulis and Oculus of Ruin came out
  Legendary). The window's notes for an idol and for Gambler's Fallacy read as above.
- `builds\spawn-ui2` (2026-10-28, the new finder): typing `calam` left 1 row, Calamity in the
  Unique colour; the row, Legendary Potential 4 and 5 chosen, then Spawn: 5 on the ground.
  `helmet` with Base items: 65 rows, scrolled with the wheel; an Exalted Burial Mask spawned.
  `builds\caret-live`: the search box and the Item level box hold their caret; after a click
  the search box has the keyboard and the caret blinks (captures 0.3 s apart show it on and off).

## Inventory

The game inventory gains **Stash**, **Stash all**, **Vendor** and **Sell all**
buttons. The same actions are available on EpochPact's Inventory page. Stash and Vendor open the
normal game panels outside town, and pressing them again closes the respective
window. Shortcuts sit in their own centered row below the item grid, above the
native Transfer/Sort and currency footer; the grid is scaled uniformly to make
room and the shortcuts hide if the bounds are unsupported. Both legacy UI.Text and
TMP labels are set; localization is disabled only inside these cloned buttons.
EpochPact's buttons do not inherit the original
button's respec action or run it alongside the shortcut.

Stash all creates an operation backup, then uses the game's quick-move routine
in batches of four. It uses the game's own stash placement: items go to a tab
whose stash priority matches them, otherwise to the current stash tab. Equipment is left equipped;
items that do not fit stay in inventory. The final result reports moved and
remaining item counts and tells the player when the stash had no room. Actions are serialized with the normal IPC worker and
reject a changed loaded-character identity.

**Sell all** sells every inventory item the vendor buys through the game's own
sale (the quick move into the shop that Ctrl+click uses), after a recovery backup.
In the game it asks once more: the first press shows "Confirm sell" for four
seconds, the second sells. The vendor opens first when it is not open. **Sell all
keeps** (Inventory page, remembered): sell everything, keep Unique, Set and
Legendary (default), or also keep Exalted items. Equipped items are never sold;
items the vendor does not buy are counted and stay. Sold items are in the vendor's
buyback list as after a normal sale. The result reports items sold, kept and gold.

**Automatic sorting** uses the game's own Sort (`ItemContainer.TrySort`, the
call behind its Sort buttons): **Sort the inventory automatically** sorts the
inventory each time it opens; **Sort every stash tab automatically**, or the tabs
ticked under **Stash tabs sorted automatically**, sorts a stash tab each time it is
shown (the stash opens on it or you switch to it). Only item positions change; the
game saves them as after its own Sort. The choices are kept in
`<game>/EpochPact/auto-sort.txt` (`invread`, `invset autosort|autosortall 0|1`,
`invset sellkeep 0|1|2`, `invsorttab <tab ID> 0|1`).

Stash tab memory: EpochPact remembers the stash tab that was open when the
stash closed and selects it again when the stash opens after a game restart (the
game itself keeps it within one session). The tab is stored as a single number in
`<game>/EpochPact/stash-tab.txt`. A deleted tab falls back to the game's default.

### Stash tabs on pickup

**Stash tabs on pickup** (Loot & Pickup page; owner's choice 2026-10-29, in place of the
item categories tried in the lab) sends an item you pick up straight to the stash tab whose
**Tab Priority** it matches. Tab Priority is the game's own: set the conditions (Uniques,
type, rarity, potentials, class, level, faction, corruption) in the game's Configure Tab,
on a tab or on its folder. EpochPact asks the game which tab Ctrl+click and Stash all would
choose (`StashItemContainer.GetTabIndexForTryAddItem`) and moves the item there on the next
frame only when that tab's priority, or its folder's, matches it (`StashPriority.Match`);
otherwise the item stays in the inventory, as it does when the tab is full or the stash has
not loaded yet. Works for pickups by hand, auto pickup and pick up from afar; the game saves
once the items have settled. Offline only. Command: `stashpickup 0|1`; the status line
`stashpickup: x1, routed n, no matching tab n, tab full n, not loaded n, saves n, faults n` counts what
happened since the game started.

### Wardrobe (item looks)

**Wardrobe** (its own page in the F6 window and the app; owner's request 2026-10-29: "like
Diablo 4's wardrobe, see every look before you wear it") brings every look together:

- **The slots** on top show the picture and name of the look each slot wears; a gold mark when a
  look is stored, *Trying on* while one is tried. Click a slot to choose its looks.
- **The mirror** on the left shows your character live; drag it to turn, or use the turn
  buttons; **Close-up** frames the head and chest, **Full body** the whole character.
- **The grid** on the right has every look that fits the selected item, with the game's own item
  pictures (`ItemData.GetItemSprite`, loaded through the game's asset bundles as the inventory
  does): the item's own look first, then base items, Uniques and Sets in their colours. The
  filters **All / Base items / Uniques / Sets / In your bags** (the items of that type in your
  inventory and the open stash) show their counts; **Search looks…** filters by name. The
  wheel or the arrows scroll it.
- **Click a look to wear it**: the character wears it and keeps it at once (`transmog <slot>
  <look>`); the item's own look, or **Own look**, puts it back; **Reset all looks** clears every
  slot. (0.1.2-dev.49 only tried a look on until the page was left, with a separate Apply looks
  button below the grid; the owner pressed the window's Apply changes, which is for settings,
  and the clothes went a moment after F6 closed. Changed in 0.1.2-dev.50.) The try-on command
  `transmogpreview` stays for scripts.

**Class art.** Base helmets and body armours are made per class (Copper Circlet and Adept Robes for a Mage, Pariah
Shroud and Outcast Rags for an Acolyte ...). The game has art of them only for their own class: on another class
such a look leaves the character as it is (live check 2026-10-29, a Mage: eight other-class helmets and body
armours left the character unchanged; Mage bases, boots, sceptres, shields and every Unique and Set changed it).
So the Wardrobe and the app's lists leave out base items without an art entry for the character's class in the
game's item visual database (`GlobalAssets.ItemVisuals`, keyed by class, type and subtype; `transmogread` marks
each look `art`), with a note saying how many; Uniques and Sets are always listed.

The desktop app's Wardrobe page has the mirror picture and a list per slot (a choice applies at
once). Underneath, an equipped item can look like another item of the same type: any base item or Unique of
that type, with the game's own art. Slots: helmet, body armour, gloves, boots, belt, weapon and
off-hand. Shop cosmetics are not touched and nothing is unlocked.

- Each slot with an item has a list: the item's own look first, then the base items and the
  Uniques ("Unique · ") and Set items ("Set · ") of that type. A choice applies at once;
  **Reset all looks** takes every look off.
- **The mirror** beside the lists shows your character with the looks it wears, from the
  front; **Turn left**, **Front** and **Turn right** turn it by 45°. In the F6 window it is
  live: a camera of EpochPact's own renders only the character's own meshes on a dark
  background, one frame per frame while the section is on screen (the camera stays disabled
  otherwise). The desktop app shows a picture of the same mirror (`transmogshot <yaw>`, a JPEG),
  taken again after each change and turn.
- The game sends a character's visuals through `PlayerActorSync.ReceiveEquipGear` (armour)
  and `ReceiveEquipWeapon` (weapons and off-hands) when it loads and on every equip; EpochPact
  replaces the subtype and Unique there with the look's. So the look stays through zone
  changes, restarts and new items of the same type, until you choose the item's own look again.
  Equipping an item of another type shows that item's own look (the stored look waits for its
  type).
- Looks are kept per character in `<game>/EpochPact/transmog.txt`. Display only: the item, its
  stats and your save do not change.

Live check 2026-10-29 (EpFeedbackTest clone, `builds\tm-live1`): at load the game sent 5
armour and 2 weapon visuals through the hooks. Unique looks set on the body armour, helmet,
gloves, boots, weapon and shield changed the character in the screenshot (a blue coat and a
wooden shield became Flesh of Stone, Somnia, Hazelroot, Corsair's Boarding Shield ...); a
look of another type was refused; after a restart the six looks were applied again as the
character loaded; back to the own looks, the character looked as before.

Mirror checks 2026-10-29 (`builds\mirror-live2`, `builds\mirror-live3`): the F6 section shows the
character live beside the lists and follows each look; `transmogshot 0` and `transmogshot 180`
gave JPEGs of 41-52 KB with the whole character from the front and the back, the town's other
people behind it cut away by the clip planes; Turn right turned the F6 mirror; Refresh then
Reset all looks in F6 took all six looks off.

### Stash tabs from the loot filter

The Inventory page reads the active loot filter's rules and your stash folders
and tabs (offline only).

- **Create tabs for rules without a tab** adds one stash tab per rule, **free of
  gold**, in a folder named after the filter (the game's limit of 200 tabs still
  applies). Tab names are `<rule number> <rule name or description>`, shortened
  to 24 characters; a rule's highlight colour becomes the nearest stash tab
  colour. Show and Highlight rules are included; Hide rules and disabled rules
  only when their option is ticked. Running it again only adds tabs for rules
  that have none yet.
- Each tab gets the rule's **rarity, item type and subtype, class, item level
  (below/above), Legendary Potential, Weaver's Will, Weaver's Touch, Forging
  Potential, faction, unique and corruption** conditions as its stash priority,
  through the game's own priority objects. The game's Stash all and normal
  stashing then put matching items into that tab.
- Affix conditions and character-relative level conditions have no stash
  priority equivalent. Those rules are marked "Sort only": **Sort inventory by
  filter rule** moves inventory items into the tab linked to the rule they match
  using the game's exact loot filter match (the same rule number the loot log
  shows). Items without a matching linked rule stay in the inventory; items that
  do not fit are reported.
- The **Stash tab** column links any existing tab to a rule (its priority is
  replaced by the rule's conditions) or removes the link and priority with "—".
  Links are kept per filter in `<game>/EpochPact/stash-rule-tabs.txt`.

Every change writes a recovery backup of the saves first (the same history as
Stash all). An open stash panel is rebuilt so new folders and tabs appear at
once.

## In-game report and app

- **In-game report:** while the game window is in front, **F8** (Settings →
  In-game report: F5–F12 or off; remembered by the mod) saves a folder in
  `<game>/EpochPact/reports/<date_time>` with `screenshot.png` (taken by the
  game), `info.txt` (versions and the same read-only state the app shows),
  `core-log.txt`, recent app logs and `description.txt` to fill in. The game
  shows a short "EpochPact report saved" notice. Windows user and computer names
  and the profile path are masked; nothing is uploaded. The key is only read
  (`GetAsyncKeyState`), no input is sent. Settings → Support report in the app
  also saves its ZIP in the same folder.
- **Version:** the sidebar shows the EpochPact version. At startup the app reads
  the latest GitHub release of EpochPact once (no player data is sent) and shows
  a "New version · What's new" link when one exists. The first time a version is
  found, its What's new window also opens by itself, once; after that only the
  link stays. Settings → Version turns the check off and has **Check now**, which
  always opens the window.
- **Install update:** What's new shows the patch notes of every newer release
  and **Install update**. The player ZIP is downloaded only from that GitHub
  release (a dropped connection resumes); its SHA-256 must equal the digest
  GitHub records for the release file, and every extracted file must match the
  package's own `SHA256SUMS.txt`, otherwise nothing is changed. **Restart and
  update** restores temporary game settings as a normal close does, closes
  EpochPact, and the verified new `EpochPact.exe` replaces the app folder (the
  old one is kept until the new version has started, and put back on any
  failure), then opens the new version. Settings in LocalAppData stay; files you
  put in the app folder move along; README, checksums and Manual Installation
  beside it are replaced only while they are still the ones EpochPact shipped.
  The mod in the game folder is then updated automatically as soon as Last Epoch
  is closed (the running game holds its loaded mod files). The app folder must
  be somewhere you can change (not Program Files).
- **Scroll position:** each section reopens where it was left (Settings →
  Navigation and favorites; on by default).
- **Hide parts of a page:** pointing at a panel or card shows a small × in its
  corner; selecting it hides that panel or card on this device. **Settings →
  Hidden parts** lists everything hidden with **Show** and **Show all**. Hiding
  only removes it from the page; a setting keeps the value it has.
- **Text size:** Settings → Text size makes everything in the app window 90–150 %
  as large (remembered on this device). The loot and damage windows keep their
  own text size in their ⚙.
- **Game setup** has its own section; Overview shows a one-line status with
  Launch offline or a link to Game setup when something is needed. **Launch the
  game when EpochPact starts** (Game setup, remembered on this device) launches
  Last Epoch offline right after EpochPact opens, once per app start, when the mod
  is installed and up to date and the game is not running.
- **Settings stay as you left them:** with "My last used settings" as the startup choice,
  every setting you change is remembered at once (a value back at its default is
  forgotten). When a character is loaded, also after re-entering the game without
  closing EpochPact, all remembered settings are sent together, waiting while
  another action runs; one refused setting no longer stops the rest. One message
  says how many were applied ("Applied your last used settings (N settings).") or how many were
  refused, instead of a "saved" message per setting.

## Live summons

### Damage meter (session)

The Summons page's **Damage meter** records damage only while it runs: **Start**
installs the damage hook, **Stop** removes it (no cost while stopped; the hook is
also removed about 10 s after the Live summons table stops reading). While it
runs, damage dealt by your character and by every ally your summons created is
added per ally type: total, hits, biggest hit, DPS over the last 10 s, 60 s,
10 min and 60 min, the session average and the average over seconds with
damage ("in combat"). Values stay after Stop, when you switch windows or after
allies die, until **Reset**. **Copy as CSV** copies the table; **Open damage
window** shows it in a small always-on-top window (windowed or borderless game)
with Start/Stop/Reset in its bar and the same window options as the loot
window. A session started or reset in the main window appears in the damage
window within about three seconds, and the other way round. Allies are found
once a second, and an ally created since then is added by its first hit (its
creator chain leads to your character), so a summon that hits and dies within a
second (a Volatile Zombie) still counts. Damage over time is credited to whoever
applied the ailment; an ally that died stays in its type for a minute, so its
ignite, bleed and poison ticks after death still count. The **live** count is
the allies of that type still alive.

**Damage type** (on the Summons page and in the damage window's ⚙) shows one
type only — Physical, Fire, Cold, Lightning, Necrotic, Void or Poison — for
every row and column. Each hit is split by the damage per type the game passes
with it (`DamageStats.damage`), in proportion, so the parts add up to the hit's
applied damage after mitigation; the CSV has every type's total. **Rows**
hides or shows each ally type, the allies' total and your character; the page
and the window remember their own choices.

The damage window has four **Styles** (⚙ → Style): **Quiet HUD** (default: the
selected source's DPS and its seven damage types, the allies on request),
**Pact Bars** (every source as a ranked bar, the selected source's damage types
below), **Split Meter** (your damage types on one side, the allies and the
selected ally's types on the other) and **Table** (the full table as before).
The selector in the window's bar chooses the averaging window — 10 s, 60 s,
10 min, 60 min, Session or Combat; Session and Combat values per damage type are
each type's total divided by that time. **TOTAL DPS** at the bottom is always
the whole session: You plus every ally type (the allies' sum is not added a
second time, and "×4" is a count, not a multiplier). Choosing a source only
changes which damage types are shown; with a Damage type chosen in ⚙ the total
says which type it counts ("TOTAL DPS · Fire"). Bars are relative to the
largest source shown. A note appears when some hits could not be split by type.
Each source shows the game's own name and icon: allies as their health bar names
them ("Skeleton Warrior", "Skeleton Archer"; the table, CSV and Summons page use
the same names), the skill that summoned the ally (or the icon the game's minion
cards use) and your class or mastery icon, read once per source with
`damageicon` (read only, about 64×64 PNG). Large values are written with three
significant digits (2.90M, 17.9M, 1.25B). Sizes follow the window's
text size; at small sizes the details scroll and the total stays visible.

### Damage taken and the death recap

**Dealt / Taken** in the damage window's bar switches between the damage you and
your allies deal and the damage your character takes during the session, one
bar per attacker type ("Solar Invader", "Skeleton Archer"; "You" is damage you
did to yourself). Choosing an attacker shows its damage types; **All attackers ↶**
goes back to all of them; **DAMAGE TAKEN** at the bottom is the whole session per
second. The Summons page shows the same under **Damage taken**, below the damage
meter. Damage taken is what the game applied (after armour, resistances, block
and the like), split by type the same way as damage dealt; ward absorbs part of
it, so a hit can lower your ward and not your health.

**Death recap** (on by default; **Record deaths** on the Summons page) keeps the
hits your character took in the last 20 seconds, in memory only. When your
character dies, the 10 seconds before it are kept with the buffs and debuffs
the game showed at that moment; the last 10 deaths stay until the game closes
(**Clear** forgets them). The recap shows who dealt the killing blow and with
what, the damage taken in those 10 seconds by type and by attacker, damage over
time (the ailment's name: Poison, Ignite, Bleed …), the last hits with the
health and ward left after each, and the effects. In the damage window **☠**
opens it and is marked when a death you have not looked at was recorded. While
the death recap is on, the damage hook stays in while a character is loaded
(it only reads; with the recap off and no damage session it is removed again).
Hits that do no damage (dodged, immune) are not listed. `deathrecap` reads it,
`deathrecap 0|1` turns it off or on, `deathrecap clear` empties it.

Measured on a test character: every hit and every Poison and Ignite tick was
recorded with exactly the health and ward it removed (the killing blow records
the full hit; the health it could remove ends at 0), the Taken totals equal the
sum of the hits, and a death kept the killing blow, the ailments and the effects
the game showed (Poison ×30, Ignite ×10, Preparation, Grace Period). Results:
`research/live/taken2/`.


Allies are grouped by type by default: one row per type (for example
"Skeleton ×5", "Bone Golem", "Blood Golem"). Grouped rows add measured DPS, last
hit and health across the group and average armour, damage increase and
resistances. "Show each ally separately" in Visible columns restores one row per
ally; the choice is remembered on this device.

The Summons page refreshes every two seconds while visible and connected. It reads
living allies owned by the player and their descendants, including companions,
totems and temporary minions. Columns show current/max health, armour, seven
uncapped resistances and global increased damage. **Increased damage is not DPS**
and does not include every ability-specific conditional modifier. Separate DPS
and last-damage columns use actual native applied-damage events in a rolling
five-second window, including target mitigation and critical hits. A saved
baseline compares later samples for the same active ally. Compare under the same
target/encounter conditions; an idle gear swap cannot establish exact combat DPS.
When event capacity is reached, the UI labels the result as a lower bound until
a fresh window has been collected. Column visibility is saved locally.
The table reads actual current allies; it does not simulate hypothetical gear
changes. Some creation-time bonuses become effective on re-summoning rather
than changing an already-created ally, and the table reflects that distinction.

No active summons produces an empty table. Failed reads or a character change
hide the previous sample; historical snapshots are never shown as live values.

### Class-specific affixes, gear above your level, corrupted items (2026-10-30)

Three player requests:

- **Class tags on affixes.** `lootread` gives each affix its classes (`AffixList.Affix.classSpecificity`:
  NonSpecific 1, Primalist 2, Mage 4, Sentinel 8, Acolyte 16, Rogue 32; none for an affix every class
  gets). 629 of the 1156 affixes in 1.5.3 belong to one class or more. The Custom Items affix list (F6 and
  the app) shows them in a gold tag at the right ("Primalist"); the wanted-affix lists (Loot & Pickup,
  Weaver's Will) add " · Mage" to the name.
- **Custom Items lists equipment affixes only.** A player chose "Ward Per Second" for a dagger and it
  stopped at T1: that one is an idol affix (id 178, Grand and Ornate Idols, one tier), beside the
  equipment's "Ward per Second" (id 382, Helmet, Body Armor, Off-Hand Catalyst, eight tiers). In 1.5.3
  all 664 affixes that roll on equipment have eight tiers and all 423 one-tier affixes are idol-only.
  `lootread` marks an affix whose `canRollOn` holds only idols and idol altars (EquipmentType 25-33, 41)
  `"idol": true`; Custom Items leaves those out (the forge item is always equipment). The other affix
  lists keep them (idol altars roll affixes with eight tiers).
- **Wear gear above your level** (`buildset anylevel 0|1`, Equipment rules): see
  `docs/build-controls.md`.
- **Craft corrupted items** (`craftcorrupted 0|1`, Crafting → Forge Settings). Corruption is the
  forge's last step: an item with no Forging Potential left can be corrupted, and the forge refuses a
  corrupted one ("Corrupted items can't be modified in the Forge"). With the setting on, the forge's
  corrupted item is shown, checked and crafted as before it was corrupted, with 1 Forging Potential lent
  when it has none (the forge shows 1); after the craft it is corrupted again with 0 Forging Potential and
  its saved form rebuilt. Live (EpCraftTest clone): a corrupted item with 0 FP refused, then crafted twice
  with the setting on (affix T1 → T3), corrupted with 0 FP in memory, in its saved form and after a
  restart; switched off, the forge refuses it again.

### Custom Items: affixes the game converts (2026-10-30)

A player chose Dexterity T1 and Throwing Damage T7 and the forge showed Physical Damage T7. The game builds an
item affix with `ItemAffix(id, tier, roll, itemType, sealed)`: when the affix has
`AffixList.Affix.convertOnIncompatibleItemType` and `canRollOn` does not hold the item's type, the constructor
makes it `affixIDToConvertTo` instead; loading a saved item (`ItemData.loadAffixFromSerialisation`) does the
same. Custom Items gave every affix item type 0 (Helmet), so Throwing Damage (id 88, rolls on Belt, Gloves, Amulet,
Relic) became Physical Damage (id 30) on every base. Now:

- The affix is built with the forge item's own type. On a type where the game converts the choice, nothing is
  applied and the reply names both ("Throwing Damage cannot be on this item type: the game turns it into
  Physical Damage. Nothing was applied.").
- `lootread` gives such an affix `convertTo` and `rollsOn`; the forge preview (`craftread`) gives the item's
  `itemType`. The Custom Items affix list (app and F6) shows "→ Physical Damage" beside it and does not check
  it; Create names the problem if the forge item changed after the choice. The F6 list is built again when the
  forge item's type changes.
- 1.5.3 has nine such affixes: Spell Damage with Tempest Strike, Chance to Gain a Storm Stack when you use
  Gathering Storm and Hit an enemy, Spell Damage for Curses (Helmet only, to Mana Regeneration), Increased
  Chance to Find Potions (Belt, Amulet; to Armor), Throwing Damage, increased Area for Area Skills (Gloves,
  Amulet; to Throwing Attack Speed), Block Chance and Effectiveness (Ring, Gloves; to Necrotic Resistance),
  Spell Damage and Reduced Mana Cost (One-Handed Sceptre; to Spell Damage), and the idols' Health (already left out).
- Live (EpCraftTest clone, test build): Dexterity T1 + Throwing Damage T7 refused on a body armor with the
  forge item unchanged; on a belt both kept, the same in the saved form and after a restart; a helmet with
  Dexterity T7 as before.

## MelonLoader installation

EpochPact can recognize Windows x64 MelonLoader 0.7 installations. It preserves
the existing `version.dll`, runtime, other Mods, Plugins and UserData, and places
only `EpochPact.MelonBridge.dll` in Mods plus EpochPact's normal core files.
The bridge starts the same native offline core after MelonLoader initialization.
It does not contain gameplay Harmony patches or bundle the MelonLoader SDK.

Install MelonLoader itself using its [official project](https://github.com/LavaGang/MelonLoader).
Its .NET/runtime requirements still apply. EpochPact alone does not require
MelonLoader. Older or unrecognized loaders are left untouched rather than guessed.
Removing EpochPact removes its owned bridge/core and preserves MelonLoader.
For the tested Unity 6000.4 build, MelonLoader 0.7.3 can generate duplicate `<>O`
helper types in `MelonLoader/Il2CppAssemblies/UnityEngine.CoreModule.dll`.
The bridge repairs only those duplicate generated wrapper names, keeps a
hash-verified original in `EpochPact/backups/melon-interop`, and refuses unrelated
duplicate types or linked paths. Game DLLs are not changed. The repaired generated
cache and its backup are retained when uninstalling EpochPact; reverting that cache
would reinstate the loader failure. See the [upstream report](https://github.com/LavaGang/MelonLoader/issues/1142).
Compatibility of every third-party gameplay mod must be tested separately,
especially when two mods change the same game function.

See [the latest feedback validation report](player-feedback-validation-2026-10-12.md)
for measured tests, shutdown comparisons and remaining coverage limits.

To build the optional bridge, download the official MelonLoader **0.7.3** x64 SDK
into a separate directory and run `dotnet build native/melon/EpochPact.MelonBridge.csproj
-c Release -p:MelonLoaderSdk="<SDK directory>"`. The bridge references MelonLoader's
existing AsmResolver libraries (MIT); neither the SDK nor these dependencies is
bundled in the player package.

### Idols: always the most, and Circle of Fortune (2026-10-29)

Player report 2026-10-29: "idol force drop on extra mods is broken". Cause: CoF tuning's Exalted and T7 roll
multipliers and the idol affixes both hooked `GenerateItems.GenerateAffixes`; the hook framework holds one detour per
method, so whichever came second only found "already installed" and never ran (its status still said `hook in`),
and setting the idols back to 1 removed CoF's hook. Now the method has one hook, in `idols.cpp`: CoF's Exalted / T7
scope runs around the game's call (`idols::UseAffixScope`), the idol affixes after it.

**Always the most idol affixes** (`idolmax 0|1`, General settings): every new idol gets exactly the Idol prefixes /
Idol suffixes number of each kind (an altar one more) instead of 1 to that many.
