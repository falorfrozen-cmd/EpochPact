# Install update — 0.1.2-dev.22 validation (2026-10-13)

Owner request: install a new version from inside the app, read the patch notes
first, keep playing afterwards.

| Part | How it works |
|---|---|
| Release | `tools/app_update.py`: GitHub `releases/latest` of this repository only (drafts and pre-releases refused). Installable only when the asset is `EpochPact-<version>.zip` at `github.com/falorfrozen-cmd/EpochPact/releases/download/<tag>/`, state `uploaded`, size ≤ 200 MB and GitHub records a `sha256:` digest. Patch notes: every published release newer than the app (`/releases`, at most ten). |
| Download | Background thread; redirects only to `github.com`, `objects.githubusercontent.com`, `release-assets.githubusercontent.com` over HTTPS; a dropped connection resumes with `Range`; refused unless the file's SHA-256 equals GitHub's digest. Cancel deletes the partial file. |
| Staging | `LocalAppData/EpochPact/updates/<version>/package`. Unsafe names (`..`, absolute, drive, backslash), links, more than 5,000 entries or 1 GB are refused; every ZIP entry must be listed in the package's `SHA256SUMS.txt` and match it; `runtime-manifest.json` must say kind Player, this repository and the expected version, as must `EpochPact/build-info.json`. |
| Restart | `POST /api/update/install` (session token): restores temporary game settings exactly like the window's close button, starts the staged `EpochPact.exe --apply-update`, closes the app. No PowerShell, cmd or script is used. |
| Swap | The staged EXE accepts only its own update folder layout and a complete EpochPact app folder; waits for the old app to exit; re-checks every staged file; renames the app folder to `EpochPact.previous-<old>-<id>`; copies the new one; checks every copied file; moves files the player added; any failure deletes the partial copy and renames the previous folder back, then reopens the old app with the reason. README, `SHA256SUMS.txt`, `runtime-manifest.json` and Manual Installation beside the app folder are replaced or removed only while they are still exactly what EpochPact shipped. |
| After | The new app reports the result once (`bootstrap.updated`), removes the previous folder and the download, and updates the mod in the game folder automatically as soon as Last Epoch is closed (launcher `updateModAfterAppUpdate`). |

## Real GitHub

`app_update.check('0.1.2-dev.20')` against the published release: latest
0.1.2-dev.21, installable, 27,130,630 bytes. Download through GitHub's redirect
in 21.4 s, SHA-256 `770848886e32…aff6022` equal to GitHub's digest; staging
extracted 857 files and all 856 listed checksums matched; version 0.1.2-dev.21.
Patch notes for 0.1.2-dev.19: dev.21 and dev.20. Run with an isolated user
folder, removed afterwards.

## Compiled player EXE (tools/verify_player_package.py, 24 checks)

New checks on the final EXE in an extracted Unicode folder, isolated user data,
no GitHub access:

- The update API refuses requests without the session token (403); with it,
  Download refuses (no newer release known), Install says to download first,
  nothing is created in the update folder.
- `--apply-update` run from the installed folder exits 20, changes nothing and
  reopens the app, which reports "did not start from its download folder".
- Full swap: an older install (version 0.1.2-dev.0) with a player file and a
  package root, the new version staged in the update folder. The staged EXE
  exits 0, the reopened app reports 0.1.2-dev.0 → 0.1.2-dev.22 and runs the new
  version, the player file is kept, README replaced, Manual Installation
  removed, the previous folder and the download are gone within 30 s.
  Apps reopened by the test are stopped by their exact path only.

## Interface

Checked in the browser pane against the source server with a simulated newer
release: sidebar "New version · What's new", dialog with version, size and
rendered patch notes, progress bar and Cancel, "Downloaded and checked",
Restart and update, and an error shown in red with the button kept.

## Automated

Native 18/18 programs, Python 285 (new: 20 Install update tests; every one of
18 deliberately broken safety checks fails them), JavaScript 160 (new: release
notes rendering and link allow-list, progress wording, the game-folder mod
updated once and only while the game is closed). Package: player checks 24/24,
connection checks 4/4.

## Not covered

An in-app update between two published releases can first happen from
0.1.2-dev.22 to the next release; 0.1.2-dev.21 has no Install update. The
swap was exercised with compiled binaries and a locally staged package, the
download with the real GitHub release.
