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
in batches of four. It targets the current stash tab. Equipment is left equipped;
items that do not fit stay in inventory. The final result reports moved and
remaining item counts and tells the player when the stash had no room. Actions are serialized with the normal IPC worker and
reject a changed loaded-character identity.

Stash tab memory: EpochPact remembers the stash tab that was open when the
stash closed and selects it again when the stash opens after a game restart (the
game itself keeps it within one session). The tab is stored as a single number in
`<game>/EpochPact/stash-tab.txt`. A deleted tab falls back to the game's default.

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
