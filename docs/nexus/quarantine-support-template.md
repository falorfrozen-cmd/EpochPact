# Nexus security review request — prepared draft

Do not submit until the actual warning/report is attached. Do not describe the detection as a false positive before its cause is established. Preserve the exact uploaded ZIP/EXE and their hashes.

- Mod: https://www.nexusmods.com/lastepoch/mods/44
- Release: EpochPact 0.1.1-alpha.1, Falor
- File ID/category: copy from the affected Nexus file entry
- ZIP name, byte size, SHA-256: copy from publication.json and that archive's .verification.json
- Exact scan state, engine names/signatures and report URL/date: copy the displayed report; do not guess
- Source: https://github.com/falorfrozen-cmd/EpochPact
- Commit: record the pushed release commit, not an uncommitted working tree
- Build: player native /MT build; Python build/package venv with PyInstaller; `python -m tools.build_player`; `python -m tools.package_nexus_release`
- Required runtime: unsigned EpochPact version.dll loader, EpochPact.Core.dll and an external bundled-Python desktop EXE; offline-only Last Epoch 1.5.2.1
- No game executables, GameAssembly.dll, proprietary metadata, saves, private logs, SDKs or test drivers are included.
- The repository contains the project's core/loader/UI/backend/build code. Bundled third-party runtimes and fonts have separate license notices; do not imply the repository owns their implementation.
- EXE SHA-256: `75ef47123c8d5c94fb299a85d96a1524cab9c0e3a6cd0aea993dfe0b0585e68a`
- Core SHA-256: `6265d3e4f15a609f174a3b577dd81e6df2e59dea2cfbbecef66f43d8d52f7a9f`
- Attach the applicable runtime manifest, affected scan report and reproducible build/source information. Keep private logs/save files out of the support request.

Suggested wording:

Hello Nexus Mods support, I am the author of EpochPact. The file listed above was marked [exact state]. I have preserved the original artifact and recorded its SHA-256 and the displayed detections. This is an offline mod toolkit using an unsigned native DLL loader and bundled desktop runtime. I am requesting a review of the specific detections, not asserting that they are false positives. The source/build information and file manifest are linked/attached. Please let me know which component requires further investigation.
