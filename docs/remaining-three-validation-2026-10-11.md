# Remaining three features: implementation and validation

Candidate: **0.1.2-dev.6**, 11 October 2026. This extends the three partial areas
in the earlier dev.5 twelve-feature audit. It does not certify every skill,
conversion, item mechanic or third-party mod combination.

## Implemented scope and measured results

| Area | Implementation | Live verification |
| --- | --- | --- |
| Summon lifetime | Owned DestroyAfterDuration timers, temporary Summoned age, and AcceleratingHealthDrain; nested ownership follows the creator chain. Player drain and permanent-summon age are excluded. | Actual Storm Totem and Ballista age advanced at half speed at 2x, while permanent Skeleton age advanced normally. Actual Wraith and Zombie health-decay hooks ran without faults. A deterministic native tick on an owned Wraith advanced decay by 2.5 normally versus 1.25 at 2x. The fixture restored its fields and avoided HP loss. |
| Projectile multiplier | Native ExtraProjectiles volleys plus moving, curved and parabolic ability objects without that component. The engine creates the extra projectiles. Extra objects cannot recursively multiply. Native upstream volleys are not counted twice. | Native Fireball 1 → 2 at 2x; 2x plus one flat extra produced 3; 8x plus 32 produced 40; reset returned to 1. Ice Bolt, Hammer Throw and Marrow Shards doubled. Lobbed Fireball used AbilityCurveMovement and produced two projectiles and two ordinary explosions at 2x. Its explosions were not separately multiplied. Lightning Blast stayed unchanged, as expected for the excluded stationary/beam path. |
| Mixed crafting | Up to 64 distinct native Unique/Set modifier lines in one immutable MultiAffix recipe, plus at most six regular T7 affixes and an optional whole Unique/Set template. Full stat keys/modes are retained. Source values are normalized against base affix scaling. | 64-line construction and native item serialization round-trip succeeded. Wrong owner, 65 lines and seven regular affixes plus a mixed recipe were rejected with identical selected-item bytes. Maximum and midpoint mixed Unique/Set lines returned their expected native stat values. A mixed item remained identical after full restart. Equipping a whole-template/mixed helmet through the native equipment API changed the actor's actual matching ailment-chance stat from 1.5 to 2.5. |

The live catalog returned **2,498** native modifier lines: 1,991 Added, 392
Increased and 115 More. The UI supports search and preserves selected lines when
filtering, refreshing or changing themes; changing character clears the selection.
Only an explicit craft action writes the selected item. Not every source line's
combat effect was individually exercised. Unique mechanics implemented outside
these stat modifier lines are not transferred by choosing a stat line.

Each projectile emission group is capped at 64. Flat extras can apply to each
separate emission of a skill. Beams, stationary areas, actors, summons and enemy
attacks are excluded. Every channelled/converted skill variant was not tested.
An Assemble Abomination channel fixture did not produce the final minion; its
complete gameplay lifecycle is **not live-verified**. Its shared native health-decay
path is implemented, but the Wraith test is not presented as an Abomination test.
AI, attack cadence and incoming combat damage are not scaled by these lifetime
hooks; this run did not benchmark combat DPS or FPS.

## Persistence and package requirements

Mixed items require the mod and `<game>/EpochPact/workshop-recipes.txt`. The native
item stores a 12-bit recipe ID; the sidecar stores the immutable definition.
Keep both with your own saves. These items do not become mod-free Steam Deck
items through Steam Cloud. Restore original gear before uninstalling; do not
overwrite another independent recipe database to combine collections.

Definitions use unused IDs 2000–4095, zero natural-drop weighting and no normal
crafting-shard or regular-affix catalog entry. New definitions are flushed and
atomically committed. Malformed files and occupied native IDs are rejected. Item
replacement still requires matching offline save/item identity, a recovery
snapshot and successful native serialization round-trip before mutation.

The rejected prototype ID 6000 exposed the native 12-bit limit during the
round-trip guard, before item mutation; the current allocator stays within the
format. An early zero-drain fixture returned 0/0 and was not accepted as proof;
the corrected acceleration fixture returned 2.5/1.25.

## Actual final player EXE

The final player core (not a research DLL) loaded the isolated saved item and
recipe database. The final compiled EXE's real HTTP serial worker loaded all
2,498 modifier lines, applied and reset projectile/lifetime settings, and
explicitly crafted a mixed item with a recovery backup and forge readback:
regular IDs 0, 1 and recipe ID 2001, all T7. Research-only `buildtest` and
`crafttest` commands were unavailable. The nine real HTTP jobs took 229–923 ms
on this machine, including snapshot/catalog operations, not a universal latency
guarantee. Headless verifier processes were stopped after explicit resets; this
run did not repeat the interactive WebView close-event test.

- 63 frontend tests passed, including mixed selection/filtering and no automatic
  crafting during rendering.
- 64 Python bridge/crafting/Melon compatibility tests passed.
- 14 native test executables passed; hook tests reported 50/50 with zero wrong
  results during concurrent install/remove.
- 20 final compiled-package checks passed: inventory hashes, no external Python
  on PATH, fresh install/update/read-only files, rollback backups, foreign-loader
  and unsupported-build refusal, HTTP authorization, resource loading and port
  collision handling. These are isolated installer fixtures, not real UAC clicks.
- The final game's normal close returned **0x00000000**. All 54 original save
  files and pre-test runtime hashes were restored and verified. Three isolated
  clone files and the test recipe database were archived; the database's original
  absence was restored. No original character was used for mutations.

Tested game: Steam Last Epoch **1.5.2.1**, build **25816319**, internal version
1.5.2.2, GameAssembly SHA-256
`d5f9fb458f0e9878e697a92b590a586a52fd77e645f8bf87bb4f2b74c6c56567`.

Final player core SHA-256:
`ca1ab90b58558d957575fd288183821505e2b230fcdb5f1cffce958f4472d143`.
Final research helper SHA-256:
`6f09003f9df5694c212a5f52427858f1bc7180b3915676ce8d4ab11ae89ab734`.
Final EXE SHA-256:
`35ba384d9ebb4685599510daee239551877cfe63f0726dff850307f000a0e6eb`.

Player ZIP: `dist/remaining-three-20261011-final/EpochPact-0.1.2-dev.6.zip`,
30,180,941 bytes, 950 files. Every ZIP entry was compared with its source bytes;
the bundled inventory records each file's size and SHA-256.
ZIP SHA-256:
`d53168045488ff7910bcb52acb2802f6b583b422b76f084f5f539bdf5eeab5e8`.
No player saves, private recipe definitions, logs, SDKs or research builds are
included. Keep the EXE beside its `_internal` folder.

Evidence is under `research/live/remaining-three-20261011/`: live-v7/v8/v9/v10,
boundaries-v7, summon-v7, equipment-and-catalog-v10, compiled-live-final,
compiled-package-final, native-tests, archive-final, final-player-close and
restoration-final JSON records. Earlier fixtures and intermediate bundles remain
separate from this final candidate.

No new VirusTotal or Nexus scan was performed. The older alpha.5 scan does not
apply to this new binary. The separately recorded intermittent UnityPlayer crash
in a MelonLoader session remains unresolved; the clean standalone exits here do
not establish universal MelonLoader compatibility. Nothing was published or
pushed by this validation run.
