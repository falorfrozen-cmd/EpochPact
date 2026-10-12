# Player conveniences

These features use the loaded offline character. They do not run campaign, reward,
stash-transfer or other save operations when the interface opens.

## Loot & Pickup

Auto Pickup pauses both EpochPact collection and the game's distant-pickup queue
while an item tooltip is pending or visible. Closing the card resumes collection;
the last item cached by the game's tooltip system does not keep collection paused.
Manual clicks remain available. Automatic collection is limited to eight pickups
per scan and uses the existing loot filter and category selections.

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
reading the list.

**Open loot window** (desktop app) opens a small, resizable window that stays on
top of a windowed or borderless game, with an opacity slider and Gold/Materials
filters. It only reads the list. Exclusive fullscreen draws over every window.

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

Chest and shrine switches use the normal condition handler within three game
units. Used or unavailable interactions are skipped. Collection and interactions
pause while inspecting an item or using the inventory, stash or vendor.

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
Scene effects are searched when a switch changes and three times while a newly
loaded scene settles (about 0.5, 3 and 10 seconds after it loads), not every
second; each search can take tens of milliseconds. Components that the game
tries to re-enable later are clamped by the existing `Behaviour.set_enabled`
detour. `qolread` reports `graphicsScans` and `maximumGraphicsMicros`.

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
  Both can be used together or separately. The game's original line text is
  remembered and written back exactly when they are switched off. If a tooltip
  shows a different number of affix lines than the item has, that tooltip is
  left unchanged instead of guessing (`tierread` counts it as
  `tooltipMismatches`).
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
  a "New version" link when one exists; Settings → Version turns it off.
- **Scroll position:** each section reopens where it was left (Settings →
  Navigation and favorites; on by default).
- **Game setup** has its own section; Overview shows a one-line status with
  Launch offline or a link to Game setup when something is needed.

## Live summons

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
