# EpochPact 0.1.2-dev.4 — crafting material validation

This candidate retains the dev.3 mastery recovery and earlier feature controls
and adds Crafting → Stock all materials. Tests used a separate character-found
offline stash on an isolated level 55 clone, not the user's original character.
Game: Steam build 25816319, displayed 1.5.2.1, internal 1.5.2.2.

## Live game results

- The current game catalog returned **508 shard types, 12 runes and 5 glyphs**.
  Legacy/obsolete rune and glyph subtypes were excluded. Shards come from affixes
  with the game's HasShard flag. Equipment-only subtype enumeration was found
  unsuitable during testing and replaced by the non-equipment catalog API.
- All 525 types rose from zero to exactly 10,000. Native readback matched every
  type/ID/count. Initial stock took 2.631 seconds; material reads took 41–61 ms
  in this session. Additions ran in batches of eight types per game update.
- Repeating 10,000 added zero units. Raising to 10,001 added exactly one per
  type. Lowering the target back to 10,000 kept all 10,001 quantities unchanged.
- A normal game exit and reload retained all 525 quantities at 10,001. The
  game's regular savedShards and materialsList fields contained those values;
  no active multiplier or startup grant was needed.
- Wrong character ID, zero, 1,000,001, fractional values and extra command
  arguments were refused. The full material read before/after was identical.
- The actual final compiled EXE served the player UI without external Python.
  Its real Stock all materials button raised every type to the upper allowed
  bound of 1,000,000. Cancelling confirmation sent no mutation. Confirming sent
  exactly one native stock request tied to the loaded ID and name.
- Page/theme changes retained the entered target and sent only reads. No
  browser JavaScript exceptions occurred. The action appeared in recovery
  history. The final compiled Settings recovery button closed the game normally
  and restored the pre-action stock of 10,001 for all 525 types.
- Recovery testing caught a missing operation in the backup validation allowlist.
  It was fixed, covered by a restoration regression test and included in the
  final rebuilt EXE. The earlier intermediate EXE is not the packaged artifact.

## Automated and package checks

- 199 Python tests passed, including material bounds, stale identities, reused
  slots, exact dispatch and scoped restoration of shards/runes/glyphs.
- 87 JavaScript tests passed, including explicit confirmation, cancellation,
  draft preservation, cache separation and absence of automatic stock actions.
- 20 final compiled package/installer/HTTP checks passed. These cover inventory
  hashes, Unicode paths, read-only source DLLs, updates, conflicting loaders,
  unsupported builds and HTTP session guards. They are separate from gameplay.
- Final EXE SHA-256:
  `435471d74a0ccc82713389258a00ec6954423dd5dcec7aa71d0fba9d49315659`.
- Final player core SHA-256:
  `46eeaf54ca02778557eb533dab9d4c1e9f50bca70dfaad34633acec5f4b40478`.
- Unchanged loader SHA-256:
  `539fbb85af9a3a78112c424ec6733871cd85a25b329afe64de35dcfc40851ca6`.

## Limits

Actual Steam Cloud synchronization, Steam Deck/Linux play, interactive UAC and
the file picker were not tested here. Session point-budget multipliers are not
made persistent by a material stock action. Mod-free gameplay on another device
is not certified by a Windows save/reload result. Forge-slot counting reuses the
existing material counter; a separate new stocked-forge interaction matrix was
not run. All other features were not re-certified by these material checks.

Earlier dev.2 and vanilla full-game exits produced 0xC0000005 in prior testing.
No such result was observed in the material sessions, but the earlier general
exit issue is not declared universally fixed. These changed binaries have no
new VirusTotal report. A clean older archive's report does not describe this one.
