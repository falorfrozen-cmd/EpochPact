# Twelve requested features: final audit, 11 October 2026

This is the earlier **0.1.2-dev.5** audit. Subsequent implementation work on its
three partial areas is recorded in
[remaining-three-validation-2026-10-11.md](remaining-three-validation-2026-10-11.md).
Keep the results below as historical evidence, not the current feature scope.

The controls exist, but the full unrestricted wish list is **not completely
implemented or certified**. This audit distinguishes native invocation, earlier
gameplay evidence and untested variants. No production implementation changed
during this audit; the existing 0.1.2-dev.5 player package was verified, not rebuilt.

## New checks performed

- 62 passing live assertions across 69 sequential IPC commands on a fresh solo
  offline `EpFeatureTest` clone, ID 6. Original characters were not loaded.
- Research core SHA-256:
  `4532d5b4164a7112e2b36a086894418912469f1c6235fb606826737636616ffc`.
  This build includes test helpers excluded from the player core. It tests the
  current native implementation, not an assertion that the shipping EXE itself
  executed each research-only fixture.
- 17 settings applied through the shipping Python backend and read back; all
  17 restored to their previous values through the session cleanup path.
- Native budget rules: 11 passing checks, including fractional passive gains,
  unchanged quest points, equipment skill levels and overflow limits.
- Current frontend tests: 62 passed, including automatic command ordering,
  no mutation on rendering, explicit workshop actions and draft preservation.
- ZIP integrity and all 949 manifest-listed file sizes/SHA-256 values verified
  against both the unpacked bundle and ZIP entries. ZIP contains 950 entries
  including `build-info.json`. Packaged app.js and style.css match the source
  byte-for-byte. The catalog matches after the documented player packaging step
  removes developer `liveState` and `currentValues` snapshots; control definitions
  are unchanged.
- Game process 42704 closed normally with exit code `0x00000000`.
- All 54 original save files were restored and verified byte-for-byte. Three
  clone files were archived; original runtime file hashes/existence were restored.

Local raw evidence: `research/live/audit-twelve-20261011/` contains `live.json`,
`result.json`, `package.json`, `exit.json` and the backup/restore manifests.
These research files are not part of the player package.

## Request-by-request result

| Request | Implementation and evidence | Remaining limit |
| --- | --- | --- |
| 1. Specialised skill XP multiplier | Implemented. Native gain of 11 produced 22 XP at 2x in each specialised tree. | This run invoked the native XP method; it did not kill enemies to measure each skill. |
| 2. Passive points per gain | Implemented. Native total 68 became 94 at 1.5x; repeating refresh stayed 94. Fractional level gains and unchanged quest points pass rule tests. | Uses level-derived total budgets, not a replacement quest reward system. |
| 3. Skill points including gear levels | Implemented. Native level 20 plus gear level 1 produced 42 points at 2x. | Node caps remain enforced. |
| 4. Skill/passive/Weaver node requirements | Implemented. Skill node 34, Spellblade passive node 48 and Weaver node 42 were rejected with normal requirements, then accepted with their respective bypass enabled. | Point budgets/node caps remain; chosen mastery is not changed. |
| 5. Weaver multiplier and flat points | Implemented. Native earned budget was doubled then one flat point added. | No permanent rank fabrication by this setting. |
| 6. Offhand restrictions | Implemented. Staff, shield and catalyst were each accepted alongside the equipped two-handed main staff. | Ordinary item level/class eligibility remains; every base type was not tested. |
| 7. Full set from one piece | Implemented. One Invoker item gained four missing bonus entries. Reset removed them; re-enable created four without duplicates. | Every set was not individually tested. |
| 8. More zoom | Implemented. Native camera distance extended and returned to normal on reset. | Camera distance multiplier, not a new free-camera system. |
| 9. Maximum blessing rolls and five options | Implemented. Native reward generator returned five options with all implicit roll bytes 255; reset restored three choices. | Actual boss kill and reward selection UI remain untested. Existing blessings are not rewritten. |
| 10. Summon stats and custom duration | **Partial.** Shared minion bonus editor exists; earlier eight-alias apply/read/reset and real Ballista HP/damage checks are recorded in the linked reports. Current native timer survived age 15 with base duration 10 at 2x and expired after reset. | Only player-owned DestroyAfterDuration timers are extended. Separate class-specific expiry systems and every minion family are not covered or verified. |
| 11. Projectile multiplier and flat count | **Partial.** Native ordinary ExtraProjectiles component changed one to three at 2x plus one; reset restored one. Earlier ordinary Fireball gameplay verification is recorded. | Beams, lobbed/channelled Fireball conversions and other creation paths are not covered universally. |
| 12. Arbitrary crafting | **Partial.** Explicit workshop created Exalted, Set and Legendary replacements with seven distinct maximal T7 affixes. Serialization round trips passed. An eighth/duplicate affix was rejected without changing the item. Earlier full-restart persistence evidence exists. | One whole Unique/Set template plus affixes; cannot mix arbitrary individual lines from multiple templates. Seven-affix native format limit; not every modifier's combat effect verified. |

Earlier evidence is explicitly historical:
[original feature gameplay](feature-validation-2026-10-10.md),
[earlier integration validation](feature-validation-2026-10-11.md), and
[current player feedback validation](player-feedback-validation-2026-10-11.md).

## Package and release limits

Audited package: `dist/feedback-20261011-final/EpochPact-0.1.2-dev.5.zip`.
SHA-256: `192dc0f6e4a692730973b880f106921d274f22a9fa3cfaa7b26246484d3f826f`.

The clean exit in this run does not resolve the separately recorded intermittent
UnityPlayer load crash in a MelonLoader session. Its root cause remains unknown.
No new VirusTotal/Nexus scan was performed for this package. Neither the audit nor
the previous clean scan of an older package establishes that this candidate is
flawless or release-certified.
