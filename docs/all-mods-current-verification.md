# EpochPact live verification — 2026-10-09

This report describes the earlier full-feature audit. The later
[code-review repairs and scoped live checks](code-review-fixes-2026-10-09.md)
supersede its installed DLL hash and evidence for the changed backup, cooldown
and frame-dispatch paths. Unchanged feature coverage is retained below.

The scoped checks pass on **Last Epoch 1.5.2 offline**. The installed DLL is the final player build. Gameplay drivers and research capture trackers are absent. This report distinguishes native live checks, guarded gameplay tests and retained earlier evidence; it does not certify every combat scenario or zero crashes.

| Feature | Actual evidence | Result |
| --- | --- | --- |
| Character stats | Final shipping DLL; all 134 types and 247 distinct full keys; Added / Increased / More where supported; native applicable getters | **4,449 checks, 0 failures**: apply, repeat without stacking and reset; SP46 AllAttributes offers Added only |
| Character UI | Actual browser and HTTP worker to final native IPC | Intelligence **4→8→4** from +100% and reset; 206 sheet mappings, 210 paths and 72 aliases also have retained checks |
| Monolith panels | Final shipping build; wrong identity / unsettled scene refused, all 20 Normal/Empowered choices, matching panel-ready reads and Echo focus | **27 flow checks** plus **60 repeated selections after a second cold boot** pass |
| Complete Echo | Real offline combat using ordinary equipped abilities and objective interactions; native completion, normal portal return and native reward chest | **10 checks pass**; base Stability 11 becomes 33 at x3; opening the chest produces 3 actual XP tomes |
| XP / gold / movement / cooldown / rarity | Retained unchanged implementations: actual gain paths, NavMeshAgent, three cooldown abilities and native rarity rolls | XP 5→15 at x3, gold 100→300, repeat/reset and native values pass |
| Density | Real natural packs on production objects with guarded driver | 86 boosted packs; retained snapshot includes 957 planned actors, 298 observed spawns and 26 completed packs; not a claim that every map pack spawned |
| Pickup / drops | Actual ground objects and native pickups, category off/on, missing/full potions, XP/Favor/Bones; real combat drops | Actual resource gains and eligibility verified; 152 x2 item-hook invocations recorded, plus fresh x1 kill drops |
| Campaign / waypoints | Retained rewarded campaign through unchanged native implementation, repeat and normal restart | 84 unfinished quests, normal rewards, minimum level 55, 109 actual waypoints; no duplicate rewards |
| CoF | Retained actual rank, gains, 12 lenses, prophecies, charges and reward-quality calls | 42 tuning and 9 gain checks, rank 1–12 and UI readbacks pass |
| Crafting / collections | Retained actual native material pickup, filter, LP/T7, Forge Hope/Despair, FP/shards; 486-entry collection and Echo graph | 23 loot/crafting and 14 collection harness checks, plus final player reads pass |
| Interface restart / disconnect | Restarted real HTTP server while keeping the tab open; disconnected after closing the test character | Read-only reconnection works; expired mutations require a fresh click; old character data clears and save actions disable |

Final HTTP stat reads: **112.8 ms mean, 129.2 ms maximum**, 20 reads. Automated suites: **81 Python, 15 JavaScript and 6,479 native checks** pass. These unit/adapter tests are separate from live-game proof. The earlier campaign fix measured 4.08 seconds for 84 rewarded quests; longest between-step frame gap was 53.05 ms, not a guarantee of no stutter on every PC.

## Fixes and investigation

Monolith open/focus now requires the real Rest scene, a hidden loading screen and a settled player transition. The backend opens once, waits for two matching read-only readiness results, and focuses without opening again. Reads do not travel or start an Echo. The interface recovers only an explicitly rejected expired read request; accepted or ambiguous mutations are never resubmitted automatically. Old character caches and save-action eligibility are invalidated on identity/session loss.

Earlier gold-overload, selector-reset and atomic IPC fixes remain covered by their retained evidence. The 61 controls in the earlier UI report and the 11 separately tested campaign/Monolith/sheet controls cover all 72 catalog contracts. This is scoped functionality coverage, not exhaustive combat certification.

The full Echo driver links the exact player objects: **26 non-command object files match byte-for-byte**. Its additional guarded command handler is confined to EpAllModsTest save 6 and is absent from the shipped DLL. No objective-complete flag or fake reward was written. Earlier zero-chest tests exited with too little native bonus Stability; completing additional real combat produced a chest. A separate test selected a physically hovered node; the driver was corrected to select and start the intended node in the same native action. Failed driver attempts are retained, not counted as passes.

## Observed crashes and limits

Two earlier UnityPlayer access violations are retained with dumps, one from a research build and one from an intermediate clean player build. **Their exact root causes are not established.** After the final loading/readiness fixes, two clean cold starts, 80 timeline selections, the full stat audit and actual HTTP/browser checks did not reproduce a crash. That evidence does not prove a zero-crash guarantee.

All 134 property types and all full keys were individually runtime tested, but not every ability-specific, minion or conditional combat consumer. Global x2 hooks ran during actual gameplay; an exact controlled x1/x2 random loot ratio was not established. Native pickup eligibility remains: Bones were refused at distance 10 and collected at distance 5; disabling EpochPact pickup does not disable vanilla collection. Special crafting paths, other game versions and online play are not certified. Weaver and Merchant Guild management remain informational.

## Evidence and restoration

[Structured results](all-mods-current-verification.json) list raw paths, hashes and per-control coverage. New transcripts, failed attempts, scripts and crash dumps are retained in `research/live/all-mods-20261009`; earlier proof remains in `research/live/all-mods-20261008`.

The game was closed normally. All **54 original save files** were restored from the authoritative fresh level-55 backup and independently verified byte-for-byte. Only three validated clone files were archived. Falor save 0 was not loaded or advanced during this audit. The game remains closed and the UI correctly shows disconnected. The tested final player DLL is installed:

`6969a769deef1ac2866025893133353d09bc3f3a4f1d41194b6af5b4da12d02d`

![Actual player Intelligence +100% readback](<C:/Users/falor/OneDrive/Belgeler/Last Epoch/EpochPact/docs/ui-evidence/player-intelligence-doubled.jpg>)

![Actual player campaign after interface restart](<C:/Users/falor/OneDrive/Belgeler/Last Epoch/EpochPact/docs/ui-evidence/player-campaign-reconnected.jpg>)
