# EpochPact 0.1.2-dev.22

Install update from inside the app.

## Install update

- When a newer release exists, the sidebar shows **New version · What's new**.
  It opens the patch notes of every newer release and **Install update**.
- The player ZIP is downloaded only from EpochPact's GitHub release (a dropped
  connection resumes). Its SHA-256 must equal the digest GitHub records for
  that release file, and every file must match the package's own checksum
  list; otherwise nothing is changed.
- **Restart and update** restores temporary game settings like a normal close,
  closes EpochPact, replaces the app folder with the checked new version and
  opens it again. Your settings stay, files you put in the app folder move
  along, and the previous version is put back if anything goes wrong.
- The mod in the game folder is updated automatically as soon as Last Epoch is
  closed. If the game is running during the update, EpochPact cannot connect
  to it until you restart the game.
- Settings → Version has **Check now**. The app folder must be somewhere you
  can change (for example Documents, not Program Files).

## Updating to this version

This is the first version with Install update, so update to it by hand: close
Last Epoch and EpochPact, extract the **complete new ZIP**, open
`EpochPact/EpochPact.exe`, and select **Game setup → Reinstall / update mod**.
Later versions can be installed from the app.

[Measured tests](https://github.com/falorfrozen-cmd/EpochPact/blob/v0.1.2-dev.22/docs/updater-validation-2026-10-13.md).

Offline only. [Source](https://github.com/falorfrozen-cmd/EpochPact) · [Support](https://discord.gg/q6aexZZSAf).
