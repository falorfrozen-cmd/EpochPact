# Independent security review — EpochPact 0.1.1-alpha.2-review.2

- **Commit under review:** `661e540bb87fb88de133106deab90376c1809f1b`
  ("Fix item-hover pickup and setup errors; harden hook pages"), branch `round1-xp-multiplier`.
- **Reviewer:** independent pass by the assistant, 2026-10-10/11. Source, candidate build and
  evidence read locally from a clean checkout at the exact commit.
- **Scope:** the files the review package lists (proxy loader, hook engine, Python packaging,
  installer/UAC, local HTTP API, the two player-bug fixes) plus the open `0xC0000005`.
- **Status of this document:** review output, not a release approval. Detections are **not**
  declared false positives.

## 0. What was independently verified (facts, not claims)

| Check | How | Result |
| --- | --- | --- |
| Artifact hashes | `Get-FileHash` on the four exact files | EXE `64eee8ab…`, core `e391eb9b…`, loader `bee68619…`, ZIP `eb9c6dec…` — **all match the review docs byte-for-byte** |
| ZIP contents | `zipfile` read of the archive | `CopyToGame/version.dll` and `CopyToGame/EpochPact/EpochPact.Core.dll` are byte-identical to the folder build and the documented hashes; no hidden extra copies |
| Authenticode | `Get-AuthenticodeSignature` | EXE, core, loader: **NotSigned** |
| Packer / sections | `pefile` | No UPX sections; standard PE layout; security directory empty (unsigned) |
| PyInstaller bootloader provenance | compare local `runw.exe` (PyInstaller 6.20.0, two venvs) | Full hash `1015b339…` and `.text` hash `b6384a19…` **match** the candidate EXE's `.text` — the bootloader provenance claim holds |
| Native controlled tests | rebuilt from this commit with `native\build.bat research <tmp>` and ran every executable | hook 50/50, items 11/11, xp 24/24, mainthread 13/13, lifecycle 6/6, review 73/73, stat_key 40, density 37, monolith 22, cof 48, loot/crafting 6260 — **all match the documented totals, no FAILs** |
| Dangerous-API sweep (native) | regex over `native/core/*` and `native/proxy/*` | No `CreateProcess`, `WinHttp/WinInet/InternetOpen`, `WSAStartup`, registry writes, `SetWindowsHookEx`, `WriteProcessMemory`, `CreateRemoteThread`, `GetAsyncKeyState`, clipboard APIs |
| Python/JS suites | attempted | **Not rerun by me**: both pinned venvs lack `pytest`; I did not modify them to install it. The 157/74 claims remain as documented, not independently reproduced here |

Not verified by me: live-game behaviour, UAC interaction, Defender/VT results (read from the
provided JSON/screenshots). No decompilation of the compiled core was performed; the hash
chain ties the candidate binary to the documented build, not to a byte-for-byte reproducible
compile.

## 1. Proven code-level findings (prioritized)

### P1-1 — The elevated installer runs a user-writable application tree
`tools/player_setup.py:21-48` elevates `sys.executable` (the EpochPact EXE) with
`--install-plugin`, and `epochpact_desktop.py:16-38` runs the whole bundled Python runtime as
administrator. The application is the unpacked ZIP in a user-writable location
(`EpochPact.spec:41-45`, onedir). Any code that can modify that tree (user-level malware,
another local user with access to the folder) executes as **administrator** the next time the
user consents to the UAC prompt. This is the classic "elevated app in a user-writable
directory" weakness; it does not bypass UAC, but it turns one consented prompt into arbitrary
admin execution.
**Fix:** keep the elevated surface tiny and verified — e.g. a small separate helper that is
Authenticode-signed and lives under `Program Files`, or verify a signed manifest covering
every module the helper will load before it elevates; do not run the full runtime elevated.
**Verification:** attempt to tamper with `_internal` and confirm elevation refuses (a fixture
test), plus a signed-helper UAC test on a scratch machine.

### P1-2 — `LoadLibraryW` inside `DllMain` (loader lock)
`native/proxy/version_proxy.cpp:52-73`; the load is `version_proxy.cpp:59`
(`GetSystemDirectoryW` → `LoadLibraryW(sys)`). Loading a DLL while the loader lock is held is
against Microsoft's guidance and can deadlock (another thread holding the loader lock and
blocking on something our DllMain needs). The review already names this a blocker
(`review-20261010.md:150-152`); I confirm it as a real design issue and not merely a
detection concern. Dependency search itself is done correctly
(`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`,
`version_proxy.cpp:44-45`) and is covered by the compiled fixture test
(`tools/verify_loader_security.py:54-71`).
**Fix:** resolve the real `version.dll` lazily on the first forwarded call (a thread-safe
`InitOnce` inside the thunks), leaving DllMain to only set state/start the core thread; or
pre-resolve `g_real` with `GetProcAddress` from an already-loaded module if Windows provides
it. This also reduces the AV-relevant "LoadLibrary in DllMain" pattern.
**Verification:** rerun `verify_loader_security.py`; add a fixture that calls a forwarded
export *before* DllMain completes and checks no deadlock.

### P2-1 — Patch-site protection restore is not checked
`native/core/hook.cpp:295-299`: `VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)`, then
`memcpy`, then `VirtualProtect(at, n, old, &old)` whose return value is ignored. If the
second call fails (rare: address-space pressure, EDR interference), the function stays
writable+executable silently — a permanent RWX region and an integrity invariant failure.
**Fix:** check the restore; on failure retry once with `old`, then `PAGE_EXECUTE_READ`, verify
with `VirtualQuery`, and report/log; treat unrepaired failure as a hard error.
**Verification:** unit-test `WriteCode` error propagation with an invalid/failing address;
add to `hook_test`.

### P2-2 — `Freeze()` can miss threads and silently skips failures
`native/core/hook.cpp:243-279`. Thread list comes from one Toolhelp snapshot; a thread created
after the snapshot can execute the bytes being patched. `OpenThread` failure (`:259`) and
`SuspendThread` failure (`:260-263`) are skipped with `continue`, so a thread that could not
be suspended is silently treated as safe. `WriteCode` retries only when a thread's RIP is
*inside* the region.
**Fix:** fail closed — any failure to open/suspend a thread aborts the patch attempt; repeat
the snapshot until two consecutive enumerations yield the same set (or use a process-wide
suspend); keep the existing RIP-inside retry.
**Verification:** extend the concurrent install/remove fixture to create threads in a loop
during patching and to deny one handle; assert install fails rather than proceeding.

### P2-3 — Hooks stay installed through IL2CPP shutdown
`native/core/lifecycle.cpp:14-23` stops frame jobs and waits for the worker before calling the
original `il2cpp_shutdown`, but **no hook is removed**. Detours can therefore run while the
runtime tears down; a detour's `g_orig` call is outside `Guarded()` (only the surrounding
feature calls are guarded), so an access violation there surfaces on the game's thread. This
is the main mod-side risk factor for the unresolved `0xC0000005` (see §4).
**Fix:** before `original()`, after `stopping` and job cancellation, restore every installed
hook (`hook::Remove`) while the domain is still valid. Trampoline/relay pages are immutable
and must stay allocated for callers already inside them. Add a lifecycle test proving removal
ordering.
**Verification:** exit matrix in §4; lifecycle fixture asserting hooks are removed before the
original shutdown call.

### P3-1 — Trampoline allocation accounting leaks on failure paths
`native/core/hook.cpp:346-356`: on `BuildTrampoline`/`SealCode` failure the page is
decommitted but `Block::used` is not rolled back and the reservation/block stays registered
(`:68-71`, `:85-89`), leaking address space per failed install; a decommitted page remains
marked used.
**Fix:** make `AllocNear` return an allocation token and release it on failure (or
recompute `used`).

### P3-2 — `Remove()` trusts the target's bytes
`native/core/hook.cpp:381-391` restores `saved` without verifying the target still contains
our `E9 <rel32>` patch. `Install()` performs the corresponding check (`:331-334`) but `Remove`
does not; a foreign patch would be overwritten.
**Fix:** verify the first 5 bytes are our jump to `relay` before restoring; otherwise refuse.

### P3-3 — `StartCore` fails silently
`native/proxy/version_proxy.cpp:33/39/46` — no diagnostics when the core cannot load (the
core's own log only exists after it starts). For support, write a tiny status file next to
the module or to the event log (last-error included). Low severity, high support value.

### P3-4 — Elevated helper's result path follows an environment override and reparse points
`epochpact_desktop.py:24-26` validates the result path against `user_root()`, which honours
`EPOCHPACT_USER_DIR` (`tools/app_paths.py:11-15`) and is not checked for reparse points. The
elevated process writes a small JSON there; impact is low, but the elevated write should use
the real `%LOCALAPPDATA%` and refuse junctions (mirroring `file_safety.hpp:15-19`).
**Fix:** drop the env override in the elevated helper path; refuse `FILE_ATTRIBUTE_REPARSE_POINT`.

## 2. The two player-bug fixes — critical assessment

### 2.1 Item-hover pickup pause (`native/core/items.cpp`)
Strengths: fail-closed when the hover metadata or the read is unavailable (`:81-95`, `:274`);
the vanilla tick still runs (`:199-200`); the scan interval is not consumed while hovering
(`:201-203`); the hover state is re-checked before **each** pickup call (`:165-186`); the
fixture test exercises the real detour, mid-pickup tooltip appearance, unreadable state and
missing metadata (`native/tests/items_test.cpp`, 11/11 in my rerun).
Limits, stated plainly:
- The fix pauses **mod** collection only. The game's own distant-pickup tick is called first
  (`:199`) and is untouched; if vanilla behaviour can dismiss a tooltip, this fix does not
  address it.
- Correctness depends on `TooltipItem.highlightedTooltipItem` being the right live field in
  1.5.2.1; the fixture proves the logic, not the semantic. A live mouse-hover test is still
  required (the package says so).
- `break` on hover leaves the remaining labels/lists for the next scan; that is intended, but
  with a large backlog the effective pickup rate is lower while the player hovers — fine for
  the stated goal, worth knowing.

### 2.2 WinError 5 / install error handling
Strengths: `PermissionError` is re-raised instead of being folded into a generic refusal
(`tools/le_session.py:213-225`); the UI separates "bundled files unreadable" (no elevation
offered) from "game folder denied" (explicit admin action) (`tools/player_setup.py:167-188`);
process enumeration no longer spawns `tasklist.exe` (`le_session.py:104-146`, Toolhelp +
`use_last_error=True`); enumeration failure **blocks** installation instead of assuming
"not running"; the installer is transactional with rollback and staging
(`le_session.py:243-298`) and junction/reparse-aware (`_linked`, `:338-341`).
Notes:
- With a failing enumeration, the plain CLI (`le_session.py install`) now raises an OSError
  traceback where it previously printed a refusal; the UI catches it, but the CLI UX
  regressed slightly. Mark it as polish, not security.
- The elevated result file is re-verified against the installed artifact hash before success
  (`player_setup.py:229-231`), so tampering with the JSON alone cannot fake a successful
  install — good.
- Not rerun here: `tools/test_player_setup.py` / `test_le_session.py` (no pytest in the pinned
  venvs). The compiled-EXE claim (17 checks) is also unverified by me.

## 3. Antivirus findings — kept strictly apart

**Proven facts (measured by me or in the evidence):**
- All three shipped PE files are unsigned (`NotSigned`); no UPX; standard sections.
- The EXE is an unsigned PyInstaller onedir bootloader whose `.text` is byte-identical to the
  official PyInstaller 6.20.0 `runw.exe` (verified locally).
- The loader is a proxy DLL that (a) exports the 17 `version.dll` names, (b) loads System32's
  `version.dll` **inside DllMain**, (c) loads `EpochPact.Core.dll` from a game subfolder and
  starts a thread (`version_proxy.cpp:44-71`).
- The core performs inline code patching: it suspends other threads
  (`hook.cpp:243-279`), temporarily makes code `PAGE_EXECUTE_READWRITE` to write jumps
  (`hook.cpp:295-299`), and flushes the instruction cache. Trampolines themselves are now
  RX and immutable (`hook.cpp:96-107`, `:336-363`).
- Detections as recorded: EXE 3/71 (Elastic, Skyhigh "Dropper", Zillya "Backdoor.XWorm"),
  core 1/68 (Cynet score 100), ZIP 1/64 (Elastic), unchanged loader 2/71 (Cynet, Symantec).
  Microsoft Defender local scans are clean; VT's Microsoft engine flagged review.1 and not
  review.2.

**Plausible trigger factors (consistent with the above, not attributed to a specific engine):**
unsigned binaries; proxy-DLL/dropper topology; inline hooking with a temporary RWX window and
thread suspension; unsigned PyInstaller runtime with a full CPython tree; ZIP carrying
`click`/Flask wheel metadata (the documented Code-Insights impersonation note). Machine-
learning engines (Cynet 100, Symantec ML, Elastic moderate) frequently fire on exactly these
patterns.

**What remains unexplained / requires vendors:** *which* feature caused each engine's verdict,
and why the same construct is clean in one engine and flagged in another. A zero-detection or
false-positive claim is not justified by anything in the package; the responsible vendors must
answer for their labels.

**Do not:** disable protection, add exclusions, obfuscate, or mutate binaries only to lower a
count. **Do:** send the prepared vendor requests with the exact hashes, sign the binaries
(Authenticode) — this is the standard, honest mitigation and also helps allow-listing — keep
hashes stable, and consider removing the DllMain load (P1-2) for real loader-lock and
heuristic reasons.

## 4. The unresolved `0xC0000005` on game exit

Evidence in the repo: one 1.5.2.1 exit returned `0`, a second returned `0xC0000005` in
`UnityPlayer.dll` (+`0x1b3d952`); a comparison run with the loader removed also produced the
same exception (`docs/shutdown-fix-2026-10-09.md:42-54`, `docs/nexus/verification.md:38`).
That is real evidence the base game can crash on exit on this machine — it does **not** prove
the mod is never a contributing cause, and the review correctly refuses to claim a fix.

Mod-side factors identified in this review: hooks remain installed for the whole shutdown
(P2-3), and detours around IL2CPP teardown are outside the `Guarded()` net for the `g_orig`
call. The worker-lifetime guard added on 2026-10-09
(`lifecycle.cpp:14-23`, `core.cpp:118-133`) is sound as far as it goes (stop jobs → detach
before `WorkerFinished` → wait → original).

**Verification plan (must be run before any release claim):**
1. Apply the P2-3 hook-removal fix; add the lifecycle ordering test.
2. Cold-start matrix on 1.5.2.1: N=20 launches with the mod (features armed) and N=20 with
   the loader/core uninstalled; record exit codes, crash dumps and event-log fault offsets.
   Compare distributions; if the mod-present set still crashes at a higher rate, bisect hooks
   by arming one feature at a time.
3. Do not describe the exit crash as "fixed" in any player-facing text until such data exists.

## 5. Build / installer / scan / live-test status (separate lines)

- **Build:** independently rebuilt from the commit; all 11 native test executables passed with
  the documented totals. Python/JS suites not rerun (no pytest in pinned venvs; not modified).
- **Installer:** reviewed; transactional, junction-aware, foreign-loader refusal, elevated
  helper input validation verified by reading the code. P1-1 and P3-4 are hardening gaps.
  Real UAC approval and the reporter's E:-drive ACL cause remain untested.
- **Scans:** Defender local clean, VT detections unchanged; my independent hash/PE/bootloader
  checks passed. No claim of safety.
- **Live game:** not run this iteration by me or (per the package) by the review. The
  tooltip-hover test and the exit matrix above are the two required live checks.

## 6. Priority actions

1. Send the prepared vendor review requests with the exact hashes (owner action; the draft is
   ready and unsent). Do not replace the quarantined uploads.
2. Authenticode-sign the EXE, loader and core.
3. Fix P1-2 (lazy real-DLL resolution) — removes both the loader-lock blocker and one
   dropper-pattern element.
4. Fix P2-1/P2-2/P2-3 (patch restore checking, freeze fail-closed, hook removal at shutdown).
5. Run the live hover test and the 2×20 exit matrix; keep results with hashes.
6. Then, and only then, reconsider feature work from `docs/feedback-20261010.md`.

## 7. Limits of this review

Source and local candidate build only; no decompilation; no live game, UAC or network tests;
Python/JS suites not reproducibly rerun in the pinned environments; Defender/VT results taken
from the provided evidence. Where the reviewed code was not visible to me, no conclusion is
made.
