# Persistent settings and interface language (2026-10-14)

Player request (item 1 of AL4NoSTb's list): settings and windows should come
back without being switched on again after every start. Owner request: a
**Settings → Language** choice with English, Turkish and Russian, translated in
full.

## Persistent settings

- Preferences (profiles, startup choice, theme, navigation, column and window
  choices, language) are kept in `preferences.json` in the user folder
  (`tools/preferences_store.py`; atomic write, `epochpact.*` keys only, 300 keys,
  64 KiB per value, 1 MiB in total). `ui/storage-sync.js` loads them before the
  page scripts and saves every change, so they no longer depend on the browser
  storage of one interface port.
- **Apply at startup** now defaults to **My last used settings**; *Off* is an
  explicit choice. The last used session values come back once each time an
  offline character is ready (EpochPact start, game restart, update), followed
  by the last loot mode, collection categories, wanted affixes and font.
- The loot feed and damage windows remember place and size; a window left open
  at close opens again, and a damage session left running starts again when the
  game is ready.

Checked: 16 store/placement/endpoint tests, 4 sync tests, 9 startup-profile
tests; in the browser pane the page's storage was cleared and everything came
back from the file; the real desktop window was restarted three times and kept
its place and size (the earlier 1 px shrink per restart from rounding is gone).

## Interface language

- `ui/i18n.js` translates the page as it is built and every text added later
  (notifications, dialogs, tables, tooltips, screen-reader labels). English is
  the source: `tools/extract_ui_text.py` lists 1,487 interface texts in
  `ui/i18n-source.json`; `ui/i18n-tr.json` and `ui/i18n-ru.json` translate all
  of them with matching `{0}` placeholders (`tools/i18n_check.py`).
- Texts built from parts are translated part by part: the lines of a
  confirmation, values that are themselves interface text ("Default: rank
  bonus"), parts joined with " · ", an icon or a count in front, and sentences
  added to a known description. Short templates such as "Level {0}" take numbers
  only, so game names like "Level of Fireball" are never changed.
- Names from the game (items, affixes, stats, quests, timelines, faction and
  Circle of Fortune perks, the game's own forge messages) stay as the game data
  writes them, and a loot filter rule's name is never translated. In-game
  overlays and Windows message boxes keep English.
- The language is chosen in **Settings → Language**; the loot feed and damage
  windows follow at once, and the choice is kept in `preferences.json`.

Checked in the browser pane against a preview backend: all 19 pages, the loot
feed and the damage window in Turkish and Russian, switched through the
Settings select; every remaining Latin text was game data or a product name.
English was unchanged.

## Test run

| Suite | Result |
|---|---|
| JavaScript (`node --test tools/*.cjs`) | 181/181 |
| Python (`unittest tools/test_*.py`) | 313 OK |
| Native test programs | 19/19 |
| Interface text list | 1,487 texts, up to date |
| Translations | Turkish and Russian 1,487/1,487, no placeholder mismatch |
| Compiled package (`verify_player_package`) | 24/24 |
| Extracted ZIP connection (`verify_connection_package`) | 4/4 |
