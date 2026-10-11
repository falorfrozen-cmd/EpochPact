# Build-feature validation — 10–11 October 2026

Tested against Steam Last Epoch 1.5.2.1 (internal executable version 1.5.2.2),
GameAssembly SHA-256 `d5f9fb458f0e9878e697a92b590a586a52fd77e645f8bf87bb4f2b74c6c56567`.
All native mutations used the isolated offline `EpFeatureTest` clone, save ID 6.
The closure comparison used a fresh `EpCleanTest` clone, also in slot 6 after
restoring the first test. Original player saves were backed up before each clone.

## Measured results

| Feature | Evidence | Scope |
| --- | --- | --- |
| Skill XP | Native ApplyAbilityXp gain 11 at 3× added 33 to specialised trees; reset returned neutral. | Real native XP method invoked on the clone; not a combat-kill test. |
| Passive budget | Natural 68 became 94 at 1.5×; a repeated edit stayed 94; reset returned 68. | Level-derived points only; arithmetic fixtures also cover fractional gains and overflow. |
| Skill budget | Native skill level 20 plus one equipment level produced 42 at 2×. | Isolated borrowed skill fields were restored after the test. |
| Weaver budget | Borrowed rank-10 native budget 13 became 29 at 1.5× plus 10; reset restored the original. | Native faction getter; borrowed rank was not saved. |
| Tree bypass | Skill node 34, Weaver node 42 and Spellblade passive node 48 rejected allocation normally and accepted it with their respective bypass enabled. | Real native allocation on the clone; per-node caps still rejected full nodes. |
| Offhand | A staff, shield and catalyst were accepted in the offhand with a staff in the main hand. | Real equipment containers; dual staffs were visible in the inventory. |
| Full sets | One Invoker piece added four missing entries: cast speed and three skill-level entries. Reset removed them; re-enabling did not duplicate them. | Exact-key contribution sums and entry counts read back from the actor. |
| Zoom | Native camera distance 17.5 became 26.25 at 1.5× and returned to 17.5. | Native camera method and field readback. |
| Blessings | Native generator returned five options with maximum implicit-roll bytes; reset returned three options with random rolls. | Reward generator invocation; no actual Monolith boss kill or reward-selection UI test. |
| Summon duration | Native temporary-summon timer at age 15/duration 10 survived at 2× and expired at 1×. | Isolated native Summoned/DestroyAfterDuration components; not each class's summons in combat. |
| Minion bonuses | Eight minion/companion stat aliases applied, read back from the native actor and reset to their original contribution lists. | Shared stat editor and IPC integration; not damage measurements for every summon. |
| Projectiles | An ordinary Fireball with one native projectile produced three at 2× plus one extra. Reset removed the hook and returned the native path. | Actual cast, native counters and captured game image; other skill patterns remain separately unverified. |
| Custom crafting | Exalted, Set and Legendary replacement items with seven T7 affixes were created. Set and Legendary base/template/affixes/rolls matched after full game restart. | Native forge, native save, full reload. Custom-item combat effects remain untested. |
| Craft rejection | Eight affixes, duplicate IDs, a wrong save ID and an unavailable template left the selected item unchanged. | Actual native command rejection, plus Python validation fixtures. |

Projectile arithmetic and the isolated native component passed. A subsequent real
Fireball cast initially missed that component path: base Fireball creates the component
only when its mutator reports extras. The implementation was corrected at that native
count decision, with the common component skipping a second multiplication. The real
cast then returned native count 1 and final count 3, with zero build-control faults.
The final ownership checks also require a non-null local offline actor, preventing
null creator and null actor pointers from matching during transitions.

Detailed JSON evidence stays in the ignored `research/live/features-20261010` folder
and is excluded from player bundles. Historical identities and live snapshots are
removed from the packaged catalog.

## Remaining gameplay coverage

- Blessing generator checks do not establish boss encounter or reward panel behaviour.
- Necromancer minions, Rogue Ballistae and Primalist totems have not each been exercised
  in real combat. Separate expiry systems are explicitly outside the generic timer.
- Other projectile skills and conversions have not all been exercised. The interface
  describes the supported component path instead of claiming every attack is multiplied.
- The workshop combines one complete Unique/Set template; it is not an unrestricted
  per-line item editor. Custom item combat effects have not been verified.

## Exit investigation

Several test-clone sessions ended with `0xC0000005` or `0xC000041D`. A comparison session
with `version.dll` removed (neither EpochPact loader nor core loaded) also ended with
`0xC000041D` using the same clone. This does not identify the cause or prove that all
vanilla sessions fail. It does prevent a clean-exit or publication-ready claim for this
candidate. Hook cleanup completed in the captured mod logs; that alone is not proof
that the shutdown issue is resolved.

A second comparison used a fresh clone with its original equipment and tree data.
It also exited with `0xC0000005` under both the candidate and vanilla. The vanilla
process loaded the Windows system VERSION.dll and neither EpochPact component. This
rules out an issue exclusive to the newly edited test equipment or new EpochPact
features, but does not establish the exact cause or guarantee clean shutdown elsewhere.

## Automated and package checks

- 120 Python tests passed across the bridge, progression, Monolith, crafting,
  shutdown and game-setup suites.
- 79 JavaScript tests passed across readiness, stat model, session and launcher suites.
- All 14 native test executables passed, including 11 build-rule cases and the
  lifecycle cleanup callback tests.
- The final onedir player bundle contains 944 payload files plus its build manifest.
  Every bundled file matched
  its recorded size and SHA-256. No research DLL, historical character snapshots,
  SDK, debug tool or nested archive was included.
- The compiled EXE installed the exact player DLL in isolated custom/Unicode paths
  without external Python. Clean installation, updating read-only owned files and
  preserving recovery copies passed. Other mod loaders and unsupported builds were
  refused without changing their files.
- First startup performed no installation or gameplay mutation. The frozen HTTP
  interface served all scripts, fonts and both theme assets. Token, Host and Origin
  checks, saved path restart and occupied-port fallback passed.
- The final compiled installer also installed the exact player DLL into the local
  game for the last smoke check. The EXE window opened correctly. Interactive
  auto-apply/theme/close-restoration checks on that final DLL were interrupted by
  the user's Escape key before a loaded character was edited; they are not claimed
  as completed. Earlier source preview checks established English menus and scrolling.

Final candidate: **0.1.2-dev.1**, offline Windows x64.

| Artifact | SHA-256 |
| --- | --- |
| EpochPact.exe | `864ee59bf5cb0dea753e6ccb1076a50ee49f85de72afab445321d40b6d1dfba8` |
| Player EpochPact.Core.dll | `36bbf8d1c7c043e087813c1bda6a93cb60fc6bab954898c11aafa6d82f00926d` |
| version.dll | `556ab427dc60727cb53cdc8cbebc3e8d8d4db005242289d6793965b117c5b71b` |

## Restoration

Both isolated tests were archived and removed after the game closed. All 54 original
save files were restored and verified byte-for-byte against their original hashes.
The original loader, original core DLL and matching installation metadata were restored.
The previously distributed alpha.5 ZIP is unchanged.

Repeated test launches exposed a retention problem in the test helper: its active
restore anchor could be pruned as an ordinary session backup. The original bytes
were recovered from another snapshot after every original hash matched. Active test
anchors are now pinned outside automatic session retention.
An isolated filesystem fixture confirmed that a pinned anchor survived repeated
ordinary retention and still restored the original bytes.

No public release or virus-scan claim is made for these new binary hashes.
