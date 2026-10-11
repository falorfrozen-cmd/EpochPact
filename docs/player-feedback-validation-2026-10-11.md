# Player feedback validation — EpochPact 0.1.2-dev.5

## Scope and result

The player candidate adds MelonLoader coexistence, the Auto Pickup inspection
guard and radius, nearby world interactions, session graphics controls, camera
pitch, inventory shortcuts and a live summon table. Russian translation is not
included. The interface remains English.

The measured feature checks passed. This is **not** a claim that every third-party
mod, encounter, minion type or possible game crash has been tested. A separate
intermittent character-load crash remains unresolved below; this candidate should
not be described as crash-free or as fully cleared for public release.

Target: Windows x64, Steam build **25816319**, displayed Last Epoch **1.5.2.1**,
internal game version **1.5.2.2**, Unity **6000.4.8f1**. GameAssembly SHA-256:
`d5f9fb458f0e9878e697a92b590a586a52fd77e645f8bf87bb4f2b74c6c56567`.

## Measured feature checks

| Feature | Actual verification | Limits |
| --- | --- | --- |
| MelonLoader coexistence | Official 0.7.3, EpochPact bridge and an independent benign mod initialized; the independent mod received 300 real Unity updates. Normal offline selection and gameplay worked. | The fixture is not every third-party gameplay mod. Overlapping hooks can still conflict. |
| Loader installation | The compiled EXE installed and updated its exact bridge/core three times in an isolated Unicode-path fixture, without external Python on PATH. The official proxy, MelonLoader runtime and unrelated Mods/Plugins/UserData files remained byte-identical. Python fixtures also verified uninstall, denied-write rollback and foreign-file refusal. | No real UAC click or another player's filesystem ACL was tested. |
| Auto Pickup hover | Eight real ground items stayed on the ground for five seconds with the actual item card open and locked; collection resumed and all eight were picked up after closing it. The final combined test repeated this under MelonLoader. | The real-card test used native IPC, not a physical mouse/controller hover. |
| Pickup radius | After a native 12-unit move, radius 5 left all eight items, radius 20 collected all eight. Radius 0 collected the next batch. Stash transfers emptied the bag between batches to avoid a capacity assumption. | Only loaded drops are considered. `0` means all loaded drops. An earlier test's expectation of fitting 24 large items was invalid; the full bag correctly left two on the ground until space was made. |
| Nearby chests | A real local ChestVisualsCreator in F80 changed from ready to used; `chestsOpened` rose to 1 and stayed at 1 in the repeat sample. | The research NavMesh helper returned false even though the observed actor reached the chest and the interaction completed. The product does not use that warp helper. |
| Nearby shrines | A native spawned shrine and a natural shrine in F70 became used; `shrinesUsed` reached 2 with zero convenience faults. | Normal game conditions still apply. |
| Nearby portals | One real generic portal interaction completed; the switch returned to 0, preventing an immediate return loop. | The exact Primal Hunt cave entrance was not exercised end to end. Its implementation uses the normal CaveEntranceLogic/UsePlayerSpawnInteraction condition handler. |
| Camera | Zoom 2 and pitch 65 changed actual camera position/rotation; resets restored pitch, position and native zoom parameters. | No promise of useful visibility at every extreme angle or in every scene. |
| Graphics | Actual shadows changed 1 → 0 → 1. Enabled fog, bloom and ambient occlusion became disabled and were restored. Initially disabled reflection components stayed disabled after restoration. | Motion blur was absent in the tested scene. An absent/already-disabled effect cannot demonstrate a performance gain. No FPS benchmark was performed. |
| Inventory shortcuts | Native screenshots show Stash, Stash all and Vendor in the footer without covering inventory cells or currency counters. Stash opened normally outside town. Vendor opened both inventory and the normal shop, with stock present. One actual sale reduced inventory count 17 → 16. | Controller navigation, every UI scale and vendor buying were not independently exercised. |
| Stash all | 17 inventory entries moved while 11 equipped entries stayed equipped; item fingerprints/quantities were conserved. The final combined test also emptied the inventory between pickup batches. | A fully filled stash tab was not separately filled and tested; failed normal quick-moves leave items in inventory. |
| Live summons | An actual Rogue Ballista appeared with health/max-health 5125, armour, seven resistances and 6% global increased damage. The final combined test read it again; the final player EXE correctly returned an empty list with no summons. | Damage increase is a global stat, not skill DPS. Creation-time bonuses may require re-summoning. Other minion families were not individually exercised. |

The final combined run sent **59 commands**, with a median response time of
**51.4 ms** and maximum **839.9 ms** on this machine. These are IPC timings,
including snapshot/stash actions, not a universal latency guarantee.

The final compiled EXE was also connected through its real HTTP serial worker to
the exact shipping **player** core. English bootstrap, current character identity,
pickup radius/readback, shadows/readback/restoration, live summon/inventory reads
and Stash/Vendor operations passed. This test used an isolated saved game-path
configuration and did not open an interactive WebView2 window.

## Automated checks

- Python: **203/203**.
- JavaScript: **89/89**.
- Native pickup/inspection fixtures: **26/26**.
- Native hook fixtures: **50/50**, including concurrent install/remove stress
  with zero incorrect calls or failed installs/removes.
- Native hook failure fixtures: **14/14**.
- Native main-thread queue fixtures: **17/17**, including owned asynchronous
  bootstrap and shutdown cancellation.
- Native lifecycle fixtures: **8/8**.
- Generated MelonLoader interop-cache repair fixtures: **8/8**, using a copy of
  the actual generated cache; backup bytes, native-name attributes, idempotence,
  collisions, unexpected duplicates and corrupt input were checked.
- Compiled package: complete file inventory/hashes, Unicode paths, actual
  read-only payload/install files, recovery backups, unsupported builds, foreign
  loaders, receipt-path refusal, HTTP token/origin checks, both theme resources
  and fresh-process selection persistence passed.
- ZIP: **950 entries**, every entry's size and SHA-256 matches the verified
  folder manifest; CRC verification passes. No encrypted or nested archives,
  research core, SDK, historical snapshots or player saves are included.

## Shutdown, failures and remaining investigation

Separate MelonLoader test processes 15164, 5780, 34596, 42024 and 5172 exited
normally with **0x00000000**. The final standalone shipping-player process 38060
also loaded the isolated character and exited normally with **0x00000000**.

These successful closes do not erase earlier failures:

- Research-only checks using an unsafe ability-add fixture and forced travel
  after the clone died crashed in GameAssembly.dll at offsets `0x2f8dfa` and
  `0x2f8e31`. The unsafe ability fixture was replaced by a normal cast; explicit
  travel now refuses a dead actor. These failures were not normal shutdowns.
- A later MelonLoader run, process 2500, crashed during ordinary character load
  before the new feature checks. WER reported **0xC0000005** in UnityPlayer.dll
  at **0x1b3dadf**, report `3104e6f7-dccc-4283-9d64-1172f207a47c`.
  The saved dump confirms a write to address `0x20` through a null RAX inside
  UnityPlayer. A stack-address scan is not an unwound stack and does not identify
  the root cause. The same candidate loaded successfully in subsequent runs,
  but the failure is **unresolved** and must not be attributed to vanilla Unity,
  MelonLoader or EpochPact without further evidence.
- The older audit also records different UnityPlayer full-exit crashes with the
  mod removed. Those are separate observations, not proof of the load crash's
  cause.

Tests used an isolated solo offline clone. After the final game close, all
**54 original save files** were restored and verified byte-for-byte, the clone
files were archived, and the four original runtime-file states were restored.
Only the four MelonLoader test directories whose original absence was recorded
were moved into the ignored research archive. Unrelated files were preserved.
The source launcher's `native/build/player` payload, loader and optional bridge
were refreshed from the exact verified bundle. Their previous local build files
were backed up; this did not install the candidate into the owner's game.

## Reproducible evidence and package

Raw measurements remain locally under the ignored
`research/live/feedback-20261011/` directory: `live-v15-final.json`,
`v15-footer-vendor.json`, `v9-minion-graphics.json`, `v11-world.txt`,
`v11-chests.json`, `compiled-live-final.json`, `compiled-melon-final.json`,
`compiled-package-final.json`, `package-final-verification.txt`, per-process exit
records and the final restore manifest. Debug dumps/logs are not distributed.
The two older compiled-Melon fixture failures reused an exclusive receipt name;
the corrected fixture uses a fresh receipt for every invocation and passes.

Player ZIP: `dist/feedback-20261011-final/EpochPact-0.1.2-dev.5.zip`,
**30,143,869 bytes**,
SHA-256 `192dc0f6e4a692730973b880f106921d274f22a9fa3cfaa7b26246484d3f826f`.

| Runtime file | Bytes | SHA-256 |
| --- | ---: | --- |
| EpochPact.exe | 372498 | `90200cccd921af6e582c682f53ed25082c57c57346122ae0480350031460be72` |
| EpochPact.Core.dll | 1401856 | `1c8dfe36ba1753f168e6734420ec75f89c87382acc666ade1e6a85ecb72dbaba` |
| version.dll | 112640 | `539fbb85af9a3a78112c424ec6733871cd85a25b329afe64de35dcfc40851ca6` |
| EpochPact.MelonBridge.dll | 12288 | `cc630b712b35936752993221e695360e834dec970ddadf45927eaf7de91ed633` |

The final player binary was built before a subsequent **research-only** sale
fixture correction. That correction is excluded by preprocessing from player
builds; the final player binary was itself exercised afterward.
This new hash has **not** received a new VirusTotal or Nexus scan. An older
package's clean report does not establish this candidate's scan result.
