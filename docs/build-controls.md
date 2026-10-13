# Build controls and custom gear

These controls extend the offline build editor. Gameplay sliders and switches apply
after a direct edit through the existing serialized IPC queue. Startup, theme changes
and loading a draft profile do not apply gameplay settings or create items.
The player interface is English in both Chronoforge and Void Atlas.

## Skills & Trees

| Control | Range / default | Behaviour |
| --- | --- | --- |
| Skill XP | 1–100× / 1× | Multiplies positive specialised skill XP gains, separately from character XP. |
| Passive points | 1–10× / 1× | Multiplies level-derived points. Quest points remain unchanged. The total is rounded down: two eligible level points at 1.5× become three. |
| Skill points | 1–10× / 1× | Multiplies the native budget including equipment skill levels. Level 20 plus one equipment level at 2× gives 42 points. |
| Weaver points | 1–10× and 0–500 extra / 1×, 0 | Multiplies earned rank/Woven Echo points, then adds the flat amount. |
| Tree connections | Three separate switches / off | Bypasses preceding skill/passive/Weaver nodes and passive mastery thresholds, including the 20 base-class points before mastery nodes and nodes past 20 points in masteries you did not choose. Individual node caps and available point budgets remain enforced. |
| Projectiles | 1–8× and 0–32 extra / 1×, 0 | Scales native ExtraProjectiles volleys and local-player moving, curved and parabolic ability emissions, then adds extras, up to 64 per emission group. Already-created extra projectiles are not multiplied again. Stationary areas, beams, actors and summons are excluded. Separate emissions in a skill can each receive the flat extras. |

Budgets are derived from natural progress, not the last modified result, so repeated
edits do not compound. Lowering a budget preserves allocated nodes and clamps remaining
points to zero. Bonus allocations are saved by the game; respec allocations beyond the
normal budget before removing the mod. These controls do not change the chosen mastery.

The three **Free respec** buttons refund allocated skill, passive or Weaver nodes
without their usual currency cost. Skill respec retains levels, XP and equipment
skill levels. Passive respec retains mastery and earned points; skills unlocked
only by refunded allocations may be unequipped. Weaver respec retains earned
rank/echo points. Each operation requires an explicit confirmation, captures a
recovery snapshot first and checks the loaded character during its frame steps.
The native command is `treerespec <save-id> skills|passives|weaver`; no startup,
profile or theme action invokes it.

## Equipment

- **Unrestricted offhand equipment:** allows native weapons, shields and catalysts in
  the offhand alongside two-handed equipment. Normal character level/class requirements
  remain. Reset the switch and re-equip a normal combination before uninstalling.
- **Full set bonuses from one piece:** adds only the set bonuses missing at the current
  equipped-piece count. Repeated refreshes do not duplicate bonuses; resetting removes
  the mod's contributions. The normal equipped item bonuses are retained.

## General settings, Monolith and Summons

- **Camera distance:** 1–2×, default 1×. Extends overhead distance without changing FOV
  or the player's saved camera preferences.
- **Blessing choices:** native count (0), or 3, 4 or 5. Changes newly generated rewards.
- **Maximum blessing rolls:** sets newly generated blessing implicit rolls to their
  upper bound. Existing equipped blessings are not rewritten.
- **Temporary summon duration:** 1–10×, default 1×. Extends player-owned summons using
  DestroyAfterDuration, native temporary-summon age, and AcceleratingHealthDrain.
  Secondary summons follow their creator chain back to the offline player. Health
  decay advances more slowly, including its acceleration; incoming combat damage
  is unchanged. Permanent summons retain their normal age. These hooks do not
  scale AI or attack tick time. Class conversions using entirely different lifetime
  mechanisms still require their own validation.
- **Minion bonuses:** the Summons page reuses the character-stat editor for minion
  damage, health, attack/cast speed, critical stats, duration and companion limits.
  It does not introduce a second stat implementation.

## Custom gear workshop

1. Put an equipment item in the game's forge.
2. Open **Crafting → Custom Items**, refresh the selected item and load item choices.
3. Select one whole public Unique/Set template, or Exalted affixes only. Optionally
   search and select individual modifier lines from multiple Unique and Set sources.
4. Search and check up to seven distinct T7 affixes, and choose maximum or midpoint rolls. When
   adding a mixed modifier recipe, choose at most six regular affixes: the recipe
   occupies the seventh native affix slot and contains up to 64 distinct lines.
5. Use the explicit craft button and confirm the selected character/item.

This creates one replacement item after a recovery snapshot. It keeps the original
equipment base and combines one whole Unique/Set template with the selected affixes.
Materials are not consumed by this custom action. Empty affix selection keeps the
existing affix IDs. Without extra effects, their tiers stay unchanged; maximum
mode maximizes their rolls and midpoint mode keeps their existing rolls. With a
mixed effect recipe, kept affixes are rebuilt at T7 with the chosen roll quality.
Idols and crafting materials are excluded.

The native v6 format stores at most seven affixes. Eight are rejected before mutation;
otherwise the native three-bit affix count would become zero on reload. Mixed recipes
use a normal native MultiAffix definition, retaining the full property, tags, special
and extra tags, and Added/Increased/More mode of each selected line. Equipment affix
scaling is normalized so these lines retain their selected source values.
Recipes are immutable and use unused native 12-bit IDs, with zero drop weighting.
They never appear as normal crafting shards or regular affix choices.

**Mixed items require EpochPact and `<game>/EpochPact/workshop-recipes.txt`.**
The game's saved item stores the recipe ID; this sidecar stores its definition.
Keep this file when updating, include it when moving your own custom items to a
different PC, and back it up together with your saves. Do not merge two unrelated
recipe databases by overwriting one. These items are not a mod-free Steam Deck
transfer feature. Restore original gear before removing the mod. Malformed recipe
files or occupied recipe IDs are rejected rather than silently replacing definitions.

Custom bases rely on the installed offline decoder; after removing the mod the game
can normalize a Unique/Set back to its template base. Use a recovery backup first if
you want to revert these items.

The action verifies the selected item and offline save identity again before commit.
The serialized item must round-trip with the expected base, rarity, template, tiers
and rolls before replacing the forge selection. Failed commits attempt to restore
the original selection; snapshots remain available. Timed-out writes are never
automatically retried.

## Implementation and validation

The native implementation is `native/core/build_controls.cpp`,
`native/core/crafting.cpp` and `native/core/workshop.cpp`; Python uses the existing IPC and progression snapshot
backends. Catalog limits are generated by `tools/export_ui_catalog.py`.
Research fixtures require a named isolated test clone and are excluded from the
player DLL. See [the remaining-three validation record](remaining-three-validation-2026-10-11.md),
[the earlier validation record](feature-validation-2026-10-11.md)
and [the earlier gameplay record](feature-validation-2026-10-10.md) for measured
results and untested gameplay paths. A successful build alone is not gameplay proof.
