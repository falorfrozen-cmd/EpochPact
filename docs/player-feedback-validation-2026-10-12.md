# EpochPact 0.1.2-dev.9: player feedback validation

Test date: 12 October 2026. Windows x64, Steam build 25816319, visible game version 1.5.2.1, internal version 1.5.2.2. This report separates source checks, native live checks, compiled installer checks and shutdown results. It is not a claim that every feature, PC or third-party mod has been certified.

## Inventory and labels

The three EpochPact shortcuts previously inherited **Confirm new blessing** from the cloned vanilla button. That template uses legacy Unity UI Text, and a localization component could restore its original label. Both legacy Text and TMP text are handled; localization is disabled only inside the EpochPact clones. The game's real blessing button is unchanged.

Live checks passed at inventory scales 75%, 100%, 125%, 150% and 200%. Labels remained **Stash / Stash all / Vendor** after waiting five seconds. Rectangle checks found no intersection with the inventory or its footer. A vertical strip is placed beside the inventory; if no safe placement exists, it is hidden and the Inventory page remains available. Pure geometry checks cover narrow layouts, excluded windows and invalid bounds.

Both Stash and Vendor opened and closed on consecutive shortcut presses. The native panel state was checked after the game's UI transition. Stash All uses normal game transfers in batches of four per frame, creates a backup and leaves equipped items alone. A live transfer check moved inventory items and verified equipment was unchanged.

## Nearby interactions and performance

Repeated whole-scene searches were replaced with a scene cache plus native spawn/initialization tracking. Initial discovery is staggered across frames. Work is bounded to eight candidates or approximately 1.5 ms per sweep, one native interaction per frame, with retry and fault backoff. Item inspection and inventory/stash/vendor panels suspend automatic interactions. Portal activation remains one-shot with a loading grace period.

Native spawned chests and a shrine were discovered and used through the game interaction path. The test run reported no interaction faults. Representative idle-area frame measurements:

| Run | Samples | Median | 95th percentile | 99th percentile | Maximum | Frames over 50 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Enabled, first run | 1,368 | 10.66 ms | 13.44 ms | 17.15 ms | 91.01 ms | 1 |
| Disabled | 1,368 | 10.81 ms | 13.23 ms | 14.75 ms | 30.54 ms | 0 |
| Enabled, repeated | 1,380 | 10.81 ms | 12.91 ms | 14.29 ms | 16.01 ms | 0 |

One-time scene discovery still measured roughly 40–126 ms for individual native searches. The first-run lazy metadata spike was subsequently removed by caching typed method lookups. These are idle-area measurements on one machine, not a weak-PC or busy-combat guarantee. The rewritten portal loop has code/rule coverage; a complete portal destination transition was not repeated in this feedback pass. Do not report all portal types as live-certified.

Larger pickup radii consider more already-loaded items and may increase collection work. They do not load distant zones. Nearby chest/shrine/portal work is separate from pickup radius.

## Free respec

Skills, passives and Weaver each passed an isolated allocation/refund check. All three reached zero allocated points. Gold remained 7,773; the crafting-material dictionary was unchanged. Skill levels 18 / 18 / 8 and XP 2,402,485 / 2,400,695 / 65,000 stayed unchanged. Earned passive/Weaver budgets were retained. Each operation created a recovery snapshot and used an explicit action; opening the interface did not run a respec.

Passive respec preserves the selected mastery. Skills made unavailable by the refunded allocations may be unequipped, as in a normal respec. Node caps and normal skill specialization rules remain separate from free respec.

## Actual summon damage

The supported native ApplyDamage return ABI was inspected, then observed in real combat. The observer preserves the original result and game effects. It stores positive, finite applied damage from tracked player-owned summons in a bounded ring and expires when the Summons page stops polling.

Skeleton and Skeleton Archer combat produced 18 owned damage callbacks and 14 positive ally samples in the recorded run. Every positive sample's displayed DPS matched actual damage divided by its collected window, within rounding tolerance. Bone Golem and Skeleton Archer were also observed in the live browser table with nonzero DPS and last-hit values. Save damage baseline displayed +0 for the same snapshot; later rolling samples produced signed differences; Clear baseline restored the dash. Health, armour and resistances remained separate columns.

Early Ballista-on-Mage fixtures did not produce owned damage and **failed**; they are not counted as successful damage tests. They were replaced by actual attacking skeleton/golem summons. This does not establish coverage for every summon, ailment or ability family.

DPS is observed combat output, including mitigation and critical hits; it is not a gear-only prediction. Five seconds without damage correctly returns zero. A comparison across different targets, encounters or resummoned instances is not proof of a specific equipment item's causal effect. Capacity clipping is reported as a lower bound until a fresh window is collected.

## Interface preferences and search help

Browser checks confirmed hidden navigation sections and favorites survive reload and can be restored. Character stats can be hidden and restored. Summon column choices survive live table refreshes and reloads. Overview and Settings remain accessible.

Stash Assistant includes copyable expressions with an official stash-search reference. Copy showed its success notification. Pasting into the actual game stash was not certified in this pass: the browser automation session's virtual clipboard does not expose the OS clipboard written by the page. The visible expressions can also be copied manually.

Steam Discussion/Guide text is provided as a draft; it was not posted.

## Compiled player package

The final folder EXE passed 21 compiled checks: exact payload inventory/hashes, bundled runtime, application-scoped CLR configuration, no historical snapshots or developer archives, fresh install and update of read-only files, exact recovery backups, Unicode/custom folder paths, foreign-loader and unsupported-build refusal, installer receipt validation, startup without mutation, UI script/font/theme delivery, token/Host/Origin checks, serial HTTP installation jobs, persisted selection, independent port after a collision, and no startup exceptions. Installer fixtures never execute the fixture game or touch real saves.

These checks do not replace an interactive file-picker/UAC test or live gameplay. The user previously confirmed Game setup's interactive selection works. No claim of antivirus or Nexus clearance is made for these newly rebuilt bytes.

| Artifact | SHA-256 |
| --- | --- |
| EpochPact.exe (folder layout) | `8670be3ab6b40d63be018fd8cdc91de25c96932431fe6ba3040509ae40912304` |
| Player EpochPact.Core.dll | `3c9008e1c38ab933649f7dbaa581362cf52ad53bd4aabf9020231f6b911d382e` |
| version.dll | `8ccf5b5b6697ff46162e0dce99c9f4c06e696e3c8235a15a64ae28822f4132b4` |

The plain player ZIP contains the complete app folder, a standalone CopyToGame option, English instructions, notices and hashes. No research DLL, game binary, SDK, test fixture, private log or save is included. The ZIP is CRC/hash-verified and cannot silently replace an existing archive.

Final ZIP: **EpochPact-0.1.2-dev.9.zip**, 26,869,910 bytes, 848 entries; SHA-256 **ea621eafdfe0c54851399ac351d85c81c3ed385b9d6524206e18789a34b2f31f**. Earlier local artifacts are retained separately and are not the published download.

Source checks passed: 103 JavaScript checks; 222 Python checks before the new player-archive regression check, plus the 12-check packaging suite after that addition; native hook, item, lifecycle, stat-key, XP, loot/crafting, faction, density, progression and convenience rule suites. Successful compilation alone is not used as feature verification.

## Shutdown and isolation

Several full-game closes returned zero; several returned **0xC0000005** at **UnityPlayer.dll + 0x109ad1**. A core-disabled comparison also failed; proxy-only and vanilla comparisons without window probing passed. A final vanilla comparison, with the owned version.dll physically absent, reproduced **the same code/module/offset** after a window/accessibility state query at Login. It did not load a character or run EpochPact commands. This proves the observed crash can occur without the EpochPact loader in this test environment; it does not prove automation is its sole cause or guarantee ordinary game exits.

Full game exit and closing only the EpochPact app are distinct operations. Previously verified app-close restoration is not presented as proof that every full game exit succeeds. No speculative Unity patch or accessibility/security setting change was shipped.

Mutating live checks used only the isolated test character. Original saves were backed up first. After closing the game, 54 original save files were restored and their bytes verified; three test files were archived. The final current save folder was also archived before restoration to preserve any user activity during the test window. The four prior runtime-file states were restored and hash-checked. Private evidence, dumps and saves remain local and are excluded from distribution and Git.
