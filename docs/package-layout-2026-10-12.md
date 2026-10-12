# Player package layout clarification — 2026-10-12

Current download: **EpochPact-0.1.2-dev.13-package.1.zip**. Runtime version remains
**0.1.2-dev.13**; this is a packaging revision, not a new native or EXE build.

The optional `CopyToGame` folder is now **Manual Installation**. README starts
with the two alternatives: use **EpochPact/EpochPact.exe → Game setup → Install mod**
(recommended), or copy the contents of **Manual Installation** yourself. The app
already has the same DLLs, so users do not perform both installations.

The original ZIP is retained locally for its existing hash and test evidence.
The new ZIP has **963 entries**, **886 runtime files**, **31,171,113 bytes**, and
SHA-256 **ebf5848e7c09946ed295346b3cc922ccf11f4757d47dc74a10f34be1e7721d10**.
All **960 payload files** other than README and the two generated manifests/checksums
have identical bytes after mapping the two manual DLL paths. EXE, `_internal`, CLR
config, loader and core are unchanged. Manual DLLs match the app's bundled DLLs.

All **12 packaging checks** passed, including unchanged runtime hashes and preservation
of existing archives. The extracted package's compiled EXE also passed **4 connection
checks** against the native protocol simulator, using the renamed manual folder.
These are packaging/simulator checks; no new gameplay changes or gameplay tests are claimed.

Reproduce the archive with:

```
py -3 -m tools.package_player_folder --bundle dist/player-0.1.2-dev.13-release/EpochPact --output dist/share/EpochPact-0.1.2-dev.13-package.1 --revision package.1
```

The package tool verifies the existing folder build and refuses to overwrite an archive.
