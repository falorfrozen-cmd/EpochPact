# T7 affix picker and forge validation — 2026-10-12

Version **0.1.2-dev.13**. Supported Steam game **1.5.2.1**, reporting internal Application version **1.5.2.2**. The GameAssembly hash matches the existing supported-build catalog.

## Real Last Epoch checks

A separate **EpCraftTest** character copy was used. Loading and feature checks used the verified native IPC; the desktop pointer was not used to control gameplay.

The final test core upgraded a shard-selected T5 affix to T6 and T7. The test armor value changed **67 → 130 → 163** using the game's own stat calculation. Rarity became Exalted. The native serializer/deserializer preserved affix IDs, tiers, rolls and rarity. T7 stopped further upgrades and turning the setting off restored the normal T5 cap.

The **final shipping player core** then upgraded a different affix T5 → T6 → T7: shards **40 → 39 → 38**, Forging Potential **63 → 49 → 46**. Each explicit forge created a recovery backup. Startup was neutral and reset removed the new setting. After a full game restart, the selected item still contained its crafted T7 affix. No native hook faults occurred in these successful runs.

Earlier checks on the shared extended-upgrade code, before the last Exalted-rarity correction, verified zero-FP refusal, Despair retaining the native cap, 100% Basic Hope preserving FP, and shard/glyph preservation. These are retained evidence, not a claim that every combination was rerun on the final artifact. One native critical success also raised a second affix and waived costs; this is the original game's critical-success branch.

Initial test candidates exposed an incorrect affix-roll field lookup and missing Exalted promotion. These were corrected before the release build. The initial failed lookup produced a backed-up failed craft; it is not counted as passing evidence.

## Actual compiled EXE interface

The final EXE served the exact shipped affix-picker script. Its two-list screen was tested against the live game catalog (1,156 affix entries): double-click add/remove, search retaining choices, reverse alphabetical sorting, explicit Apply selection, and clearing to accept any T7. Choosing Cast Speed and Block Chance in the EXE produced native affix IDs **4 and 3**; clearing produced an empty native list. The T7 toggle enabled and disabled the native setting through the EXE.

At **1280×720**, the two lists have equal 432.33-pixel widths and the same vertical position. At **640×900**, they stack at x=47 with 530.67-pixel widths and remain within the viewport. Keyboard movement, limits, stale character ownership, failed requests and pending draft preservation are additionally covered by automated tests.

Two candidate package checks exposed a missing static-route entry and a missing freeze-spec resource. Both were corrected; the final compiled-resource and HTTP checks passed. The earlier candidates are not release artifacts.

## Automated and package checks

- **237 Python tests**, **126 JavaScript tests**, **17 native test programs** passed.
- Hook engine: **50/50**, including 300 concurrent install/remove cycles with no incorrect results. An initial 45-second test timeout was insufficient; the full run subsequently completed successfully.
- The final EXE passed 21 isolated packaging/installer/HTTP checks, including custom Unicode paths, ownership protection, repeated installation, uninstall, exact native hashes, CLR config and unavailable external Python.
- The extracted release ZIP passed 4 frozen-IPC checks with the native simulator: stale read-only protocol announcement, reconnect, separate slow-read budgets and core mismatch. These simulator checks are separate from the real-game checks above.
- Final ZIP: **31,170,810 bytes**, **963 entries**. Manifest verification passed; manual DLLs match the EXE's bundled DLLs. No testhost, testing core, SDK, private logs, saves or game binaries are distributed.
- Local Microsoft Defender **1.459.676.0** reported no threats in the exact ZIP, EXE and player core. No new VirusTotal or Nexus result is asserted.

## Saves, closure and limits

Five measured full-game closes in this task returned **0 / 0x00000000**. The earlier intermittent **0xC0000005** in UnityPlayer.dll remains unresolved; these successful closes do not establish a universal crash fix. Browser testing used the compiled EXE's HTTP interface, not a new exhaustive interactive WebView2/UAC test.

All **54 original save files** were restored with exact byte verification. Three copied test-character files were archived outside the active saves. The final matching player core remains installed.

T6/T7 extension supports shard upgrades with no glyph or Basic Glyph of Hope. Other glyphs retain native tier limits. Every item/affix family, critical outcome, glyph combination and equipment interaction has not been exhaustively tested.

[Machine-readable results](t7-affixes-validation-2026-10-12.json). Private detailed evidence remains in the local research directory.

## Final artifact hashes

| File | SHA-256 |
|---|---|
| ZIP | `0f1cde24c8f27a67917bbe9efca660b3dc823233095880330dfb0614fc9bb29f` |
| EpochPact.exe | `d477f467ecd44122d2dcf62cf807ac1a77f86280a77efa84b4c30e6a4ff0f27c` |
| EpochPact.Core.dll | `2f9d34d97707972e6a8ada8fc5098690442bd2a0858fe054a7f622e3c86c676f` |
| version.dll | `d61ed60d5f94d21748cacc589fe4695790c8d33d19aaf74ddcab6182e48b39c7` |
