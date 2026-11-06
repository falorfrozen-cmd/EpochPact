# Last Epoch 1.5.4 compatibility (released in 0.1.2-dev.55)

Steam updated the game on 2026-11-05. Published dev.54 lists only 1.5.3 and 1.5.2.1 in `ui/supported-game-builds.json`, so
the desktop app's Game setup refuses 1.5.4; the in-game edition's loader itself loads on it.

| | Steam build | GameAssembly.dll bytes | SHA-256 |
|---|---|---|---|
| 1.5.3 | 26087601 | 97,256,960 | `323e9183f3bee600b6c9e1c469218b9de926e9044f2ca3d44fae8a0ec9a91464` |
| 1.5.4 | 26386738 | 97,338,880 | `794b95e2619edfed76b1c2a9e0acbf9c349b43e2cf1fde8bc7b21d192fbe9875` |

Checked on 1.5.4 on disposable clones: boss door clicks, Boss Rush portals, the blessing window, auto shatter, the Boss Hall,
quest echoes. The rest of the mod was not gone through feature by feature on 1.5.4; the owner played this build.
