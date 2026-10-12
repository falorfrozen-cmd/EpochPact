# Audit fixes — 0.1.2-dev.15 validation (2026-10-12)

Fixes for the findings in [audit-dev14-claude-2026-10-12.md](audit-dev14-claude-2026-10-12.md),
excluding Nexus Mods. Steam Last Epoch 1.5.2.1 (internal 1.5.2.2), offline.

## Changes

| Finding | Change |
|---|---|
| Hook install/remove paused the game ~250 ms each (every switch that installs a hook) | `hook.cpp` enumerates only this process's threads with `NtGetNextThread` instead of a system-wide Toolhelp snapshot taken twice. Measured here: 10,800 system threads, 105 ms per snapshot; 151-thread process, 0.27 ms. Toolhelp remains the fallback. |
| Campaign completion installed/removed two hooks per run | Display/analytics batching detours are installed at game startup and stay installed; they pass every call through outside the bulk action's thread-local scope. Per-action installation remains the fallback. |
| Graphics switches searched the scene every second | Scene searches run when a switch changes and three times after the active scene changes (≈0.5, 3, 10 s), one object type per work tick. Zone changes use `SceneManager.GetActiveScene()`; the player object can outlive a zone. Nearby interactions use the same signal. |
| No uninstall in the app | **Game setup → Uninstall mod** (`PlayerSetup.uninstall` → existing `cmd_uninstall`), game must be closed, foreign loaders refused. |
| Stash all did not explain leftovers | Result text says the stash had no room. |
| Unbounded `install-backups` | Keeps the 10 newest EpochPact-owned backups; unrecognized folders are left alone. |
| Package README / docs described old button layouts | `player-folder-readme.md`, `player-conveniences.md` describe the row below the item grid. |
| Support link missing | Sidebar and Game setup link to Discord; root README has download/support links. |
| Pickup interval documented as 0.75 s | Catalog, GEMINI and README say 0.25 s. |
| Locale-dependent CLR test | Probe output decoded with the OEM code page, `errors='replace'`. |
| Outdated CE comparison | `ce-feature-additions.md` lists what is still missing and points to build controls. |
| v1 promo poster with removed slogan | Moved to `docs/promo/archive/` with a do-not-use note. |

## Automated

| Suite | Result |
|---|---|
| Native | 17/17 programs. Hook stress 50/50; hook failure 15/15, including a new check that per-process enumeration equals the Toolhelp result and that denied enumeration still prevents patching. |
| Python | 240/240 (new: uninstall, foreign-loader uninstall refusal, backup retention). |
| JavaScript | 126/126. |
| Compiled package | 21/21 `verify_player_package` checks. |
| Extracted ZIP connection | 4/4 `verify_connection_package` checks (8,262 ms slow-read case, simulator). |

## Real game (isolated clone `EpAuditTest` of the first character)

Three sessions: the first two used intermediate dev.15 cores (the first exposed
that rapid switch changes cancelled the post-load searches and that the player
object's scene does not change between zones; the second measured an all-types
search), the third used the final core. The original 54 save files were
restored and byte-verified after each session; test files were archived. All
three games closed with exit code 0.

- core.log: `progression: batching hooks installed at startup`,
  `inventory shortcut hooks installed at startup`.
- Command round trips including a hook install/remove: `xp 2` 56 ms, `xp 1` 68 ms,
  `gold 2` 97 ms, first `qolset chests 1` (five hooks plus first metadata lookups)
  397 ms. Before the fix a single hook installation measured ~250 ms
  (two inventory hooks: 498 ms, see stutter-fix-2026-10-12.md).
- Graphics (bloom, fog, ambient occlusion, reflections on), final build:
  9 type searches in the first 15 s, **0** in the following 30 s; after travelling
  to another scene the active scene changed and 9 more searches ran, then 0.
  Largest single search tick 38.9–42.1 ms. Searching all three types in one
  tick, which the dev.14 code did once a second for as long as a switch stayed
  on, measured 101 ms in the same scene (second session).
  Overrides were applied in both scenes and all restored when switched off; 0 faults.
- Campaign completion on a completed character: ok, 0 pending, 294 ms.
- Stash toggled open/closed; Stash all ran with a backup (inventory was empty,
  so a non-empty transfer and a full stash were **not** exercised).
- Monolith: travelled to the rest area, `monolithread` editable, Stability
  0 → 1 applied on timeline 1/normal. The test script did not parse the readback.

## Package

| File | SHA-256 |
|---|---|
| EpochPact-0.1.2-dev.15.zip (26,799,145 bytes, 847 entries) | `b7a689ddf44015f62b3331a3632af794c5f211ae6cc3487b9dcb8b8e28cc1315` |
| EpochPact.exe | `ac7d3397b6ac73534ebebb42f81f7f8924d25006c9476f8a2fc0c103ce51f439` |
| EpochPact.Core.dll | `881704693dac796702cbfc799a62801e7182d5c1f9d48b95d46e817cc37b1cb4` |
| version.dll | `87bc09879e08909748652212619a47c6b526c9ff4b9ae0f50f11dd115a0c65ed` |

Built with the hash-locked `requirements-build-win64.lock` environment and the
source-built PyInstaller bootloader. The dev.14 ZIP came from a different
environment with extra unused packages (cryptography, bcrypt, asgiref); dev.15
has 847 entries instead of 963.

## Not covered

Non-empty Stash all and a full stash; Monolith readback in the script; UAC
approval; another PC; every summon/skill/encounter; MelonLoader with this core;
full-game-exit crash history (three clean exits here are not a fix).

## Antivirus

VirusTotal (fresh uploads, 2026-10-12): EpochPact.exe 0/71, EpochPact.Core.dll
0/71, version.dll 1/71 (Cynet "Malicious (score: 100)", as with earlier loader
builds; cause not established). The ZIP exceeds the browser upload limit; local
Microsoft Defender (1.459.682.0) found no threats in the ZIP or the extracted app
folder. Details: [security/virustotal-20261012-dev15.json](security/virustotal-20261012-dev15.json).
This is not clearance by every engine or by Nexus Mods.
