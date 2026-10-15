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

### Recent pickups and the loot window

The Loot page lists recent item, gold and amber pickups (up to 300, newest first)
with rarity, who picked it up (you or Auto Pickup) and the loot filter rule that
matched. "Rule N" is the same number the game shows with its **Item rule
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
  icons and the corrupted glow stay. Both can be used together or separately. The game's original line text is
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
  lays it out. **Rule name only** leaves just the rule's name.
- **Tier colours**: default T1 white `FFFFFF`, T2 blue `2E8BFF`, T3 gold
  `FFD700`, T4 burnt orange `FF5500`, T5 purple `AA55FF`, T6 green `00FF00`, T7
  red-pink `FF0044`, T8 cyan `00FFFF` (a player's palette; T2 and T5 slightly
  lighter so they stay readable on the dark tooltip). Each tier can be changed; your colours are
  remembered on this device and applied whenever a character is ready. A
  changed colour or setting also rebuilds the game's tooltip
  (`TooltipItemManager.ForceUpdateTooltip`), so an item shown before the change
  is not left with the old look.

### Text and font

- **Interface font** replaces the font of the game's interface text with an
  installed Windows font (the list comes from Windows). Characters the font does
  not have, such as symbols, still use the game's font. Text keeps its letter
  case: the game's font shows small capitals for lower-case letters, a Windows
  font shows them as written.
- **Interface text size** multiplies the size of interface text (0.7–1.5).
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

## Inventory

The game inventory gains **Stash**, **Stash all** and **Vendor** buttons. The same
actions are available on EpochPact's Inventory page. Stash and Vendor open the
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

Stash tab memory: EpochPact remembers the stash tab that was open when the
stash closed and selects it again when the stash opens after a game restart (the
game itself keeps it within one session). The tab is stored as a single number in
`<game>/EpochPact/stash-tab.txt`. A deleted tab falls back to the game's default.

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
  Launch offline or a link to Game setup when something is needed.

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
once a second.

**Damage type** (on the Summons page and in the damage window's ⚙) shows one
type only — Physical, Fire, Cold, Lightning, Necrotic, Void or Poison — for
every row and column. Each hit is split by the damage per type the game passes
with it (`DamageStats.damage`), in proportion, so the parts add up to the hit's
applied damage after mitigation; the CSV has every type's total. **Rows**
hides or shows each ally type, the allies' total and your character; the page
and the window remember their own choices.


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
