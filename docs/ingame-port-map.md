I've finished the survey. The full implementation map is below.

# EpochPact: in-game port map for the desktop sections not yet ported

Sources read: `ui/app.js` (all 1665 lines), `ui/index.html`, `ui/style.css`, `ui/collection.css`, every `ui/*.js` helper module, `tools/ui_bridge.py`, `epochpact_ui.py`, `tools/player_setup.py`, `tools/filter_manager.py`, the Python backends, `native/core/commands.cpp` and the native modules that produce each JSON reply. Labels and descriptions come from `ui/locale-en.json`; the control contract comes from `ui/catalog.json`. The current in-game composition is in `tools/ingame_pages.py` and `native/core/ingame_panel.cpp`.

---

## 0. Shared building blocks

### 0.1 Theme tokens (Chronoforge, `:root`)
`--bg #07120f`, `--panel #0b1815`, `--ink #eee6d5`, `--muted #b3beb8`, `--gold #ac8950`, `--line #665436`, `--accent #80e2b1`, `--glow #2a9a70`, `--soft #183c2e`. Body text is Inter 14px. `h1/h2/h3` use Georgia (normal weight): h2 is 27px with margin `0 0 14px`, h3 is 20px with margin `0 0 12px`. Paragraphs: line-height 1.65, margin `10px 0`.

### 0.2 Base element and utility styles (used by many sections below)
| Selector | Values |
|---|---|
| `button` | ink text; bg `linear-gradient(160deg,#29302c55,#070d0cb8)`; border `1px solid var(--line)`; radius 3; padding `10px 15px`; hover: border accent + bg `--soft`; `:disabled` opacity .45 |
| `input, select` | bg `#080e0e`; border `1px solid #7b745a99`; radius 3; padding 10; width 100% |
| `input[type=checkbox]` | 18×18, accent-color accent |
| `input[type=range]` | height 8; border 1px gold; bg `linear-gradient(90deg,var(--glow),#343a35)`; radius 8; thumb 19×19 rotated 45°, accent colour, `3px double #775329`, glow shadow |
| `.muted` | color `--muted` |
| `.eyebrow` | 10px, letter-spacing 2px, accent |
| `.empty` | padding 25, centred, muted |
| `table` | width 100%, collapsed, 12px. `th,td`: padding `10px 12px`, border-bottom `1px solid #6c755144`, left-aligned. `th`: gold, weight 500. `td:first-child`: min-width 180 |
| `.table-wrap` | overflow-x auto |
| `.table-scroll` | overflow auto; max-width 100% |
| `details` | margin-top 15. `summary`: pointer, gold, 12px |
| `.form-grid` | grid with 2 equal columns, gap 14. `label`: flex column, gap 8, 12px, muted. `.wide` spans both columns |
| `.actions` | flex, gap 10, wrap, centred, margin-top 18. `.actions .primary`: margin-left auto |
| `.action-reason` | gold, 12px, lh 1.6, margin `12px 0 0`; `[hidden]` means display none |
| `.inline-check` | flex, gap 8, centred, 12px, margin-top 14 |
| `.badge.save` | color `#e8c690`. Badges are uppercase via CSS |
| `button.link-button` | transparent, no border, accent text, padding 0 |
| `.tabs` | flex, gap 8, mb 16, wrap. `.tabs button.selected`: border accent, bg soft |
| `.pagination` | flex, centred, gap 16, 12px, margin-top 18 (collection.css: margin `20px 0`) |
| `.pending` | accent, 11px |
| `.setting-applied` | accent |
| `.setting-error` | `#f2a594` |
| `.readout` | pre-wrap, max-height 420, bg `#050a09`, 12px Consolas |
| `.hide-part` (× on every panel and card) | absolute at top -9 / right -9; 22×22; round; 14px; opacity 0, rising to .85 when the parent panel or card is hovered |

### 0.3 `actionCard(c, fields="", extra="")` anatomy (used by most action panels)
```
<article class="control-card [affix-card if loot_affixes|weaver_affixes]">
 <div class="control-top"><h3 class="control-title">{label}</h3><span class="badge [save if game_save]">{Lifetime}</span></div>
 <p class="control-description">{description}</p>
 <form class="control-form" data-id="{id}">
  <div class="form-grid">{fields || parameterFields(c)}</div>{extra}
  <p class="action-reason" data-action-reason role="status" hidden></p>
  <div class="actions"><button type="submit" data-control-id="{id}" class="[primary if game_save or mastery_choose]">{caption}</button></div>
 </form></article>
```
- **Lifetime badge text:** session = "Session", actor = "Character", game_save = "Game save", read_only = "Read only", view = "View", recovery = "Recovery".
- **Caption:** craft_materials = "Stock all materials", craft_forge = "Craft once", craft_reset = "Restore game defaults", mastery_choose = "Choose Mastery", loot_affixes/weaver_affixes = "Apply selection", widget read = "Read live", game_save = "Apply action", widget preview = "Preview", anything else = "Apply".
- **`parameterFields` labels** (each `<label>{Label}{control}</label>`): value = "Value", name = "Character name", level = "Level (optional)", slot = "Prophecy slot", reward = "Reward", lens = "Lens", backup = "Operation backup", mode = "Selection", ids = "Affixes", category = "Pickup category". save_id is never shown.
- **Choice words** (option text for enum choices): all = All, filter = Filter, quality = Quality, materials = Materials, gold = Gold, potions = Potions, xp = XP, favor = Favor, bones = Bones, amber = Amber, normal = Normal, empowered = Empowered.
- **Default numeric field:** `<input name type=number min max step="1"|"any" value=liveDefault required>`. step is "1" when the parameter type contains "integer". `number_or_reset` gets placeholder "Normal chance (blank resets)" and is not required.

### 0.4 Submit pipeline (`formAction`), which every action card follows
1. Run `form.reportValidity()`.
2. For game_save or recovery controls, show the confirm dialog (§17.1).
   - Generic title: the control label.
   - Generic message: `{description} This action changes the game save. The backend creates a backup before applying it.`
   - Recovery message: `{description} The game will close normally and the selected operation backup will be restored.`
3. For game_save and mastery_choose, re-check after confirmation that the snapshot belongs to the loaded character and session. If not, show "The game or character changed. Review the action for the loaded character."
4. Send `{type:"control", id, args, operation: read for read/preview widgets, otherwise set}`.
   - `args` always carries `expected_session` (`connected.sessionKey`).
   - Controls with a `save_id` parameter carry `expected_id` = the relevant snapshot's `player.id`.
   - Inventory controls and craft_materials also carry `expected_name`.
5. The bridge replaces `{save_id}` with the verified live id from `identityread` and formats the command template (`commands.cpp`).
6. **After success:**
   - A notification with the player message (§0.6).
   - `syncLootCraft(id, result)` merges the reply into the cache and repaints.
   - For game_save in groups campaign, monolith, cof or skills: drop the cached read and re-read it (`progress_read`, `monolith_read`, `cof_read`, `build_read`).

### 0.5 Eligibility (`updateDisabled`, run after every render and every task)
- **A `data-control-id` button is disabled when:**
  - something is busy (tooltip "Waiting for the current operation."); or
  - the control is not eligible: offline, `state=="InGame"`, not transitioning. The reason is "Load an offline character and wait for the area to finish loading."
  - **Exception:** Refresh buttons (`data-refresh == id` of a read control) stay enabled while disconnected, with tooltip "Refresh game connection and live data.".
- **Extra disabling for buildset/effectsset controls:** if `build_read.settings[key].available===false` or `effects_read.available===false`, the control is disabled.
- **game_save and mastery_choose:** use `EpochSession.saveActionReason(snapshot, connected)`. The snapshot is the cached read named in `saveSnapshot()`:
  - inventory → `inventory_read`
  - skills → `build_read`
  - craft_materials → `craft_materials_read`
  - craft_forge/craft_custom → `craft_read.forgePreview`
  - campaign → `progress_read`
  - monolith → `monolith_read`
  - anything else → `cof_read`
- **saveActionReason texts:**
  - "Refresh live data to enable this action for the loaded character."
  - Monolith only: "The area changed. Refresh live data before editing Monoliths." and "Return to End of Time, Monolith Hub or Traveler's Rest to edit Monoliths, then refresh live data."
- **Where the reason is shown:** in the card's `p.action-reason` and as the button tooltip.

### 0.6 Player result messages (`playerResult`)
- **Defaults:**
  - Default success text: `{label} updated.`
  - " A recovery backup is available in Settings." is appended when the reply has `backup`.
  - In preview: "Preview complete. The game and saves were unchanged."
- **Specific texts** are listed per section below.
- **Error text cleanup (`playerError`):**
  - Texts mentioning main thread, "pick the job" or "paused or minimi" become "The game did not respond. Return to the game, then refresh and try again."
  - Tracebacks, braces or file paths become "The game could not complete this action. Refresh the connection and try again. Technical details were saved in the local log."

### 0.7 Python bridge logic the in-game version must reproduce natively
The in-game window calls `commands::SubmitUi(line)` directly, so these Python-side steps need native replacements:
- **Identity guard:** for any `save_id` control, run `identityread` and compare to `expected_id`/`expected_name`. Failure text: "The loaded character has changed. Refresh this section and try again."
- **`expected_session` check:** "The game restarted. Refresh live data before trying this action again. It was not applied."
- **Composite reads:**
  - `craft_custom_catalog`
  - `atlas_read` and `stash_read` (paged)
  - `echo_read`, `echo_focus`, `monolith_select` (travel and wait loops)
- **CoF and Monolith validations:** see §13 and §14.
- **`craft_custom`:** use `craftmix` when there are extra effects, otherwise `craftcustom`.
- **`undo` and all `launcher` actions:** pure Python (file system work). Desktop-only.

### 0.8 Desktop preferences storage (DESKTOP-ONLY mechanism, readable in game)
- **How it is stored:** localStorage keys `epochpact.*` are mirrored by `ui/storage-sync.js` into `%LOCALAPPDATA%\EpochPact\preferences.json`. The file format is `{"version":1,"values":{"<key>":"<JSON string>"}}` (`tools/preferences_store.py`).
- **The in-game panel already reads this file** for `epochpact.language` (`ReadLanguage()` in `native/core/ingame_panel.cpp`).
- **Writing from the game:**
  - Keep the same keys and the atomic replace (write `.tmp`, then `os.replace`).
  - The desktop app rewrites the whole file on each change, so concurrent writers can overwrite each other.
  - Keys ending `.preview` are never mirrored.
- **Keys used by these sections:**
  - `epochpact.tier-palette.v1`, `epochpact.rule-line-color.v1`, `epochpact.area-color.v1`
  - `epochpact.damage-panel.live`, `epochpact.display-preferences.v1`, `epochpact.stat-favorites.v1`
  - `epochpact.profiles.live`, `epochpact.startup-profile.live`, `epochpact.last-settings.live`, `epochpact.last-settings-replay.live`
  - `epochpact.hidden-parts.v1`, `epochpact.text-scale.v1`, `epochpact.theme`, `epochpact.language`
  - `epochpact.remember-scroll.live`, `epochpact.update-check.live`, `epochpact.update-announced.live`
  - `epochpact.collection.v1.<scope>`
  - `epochpact.loot-feed.v1`, `epochpact.damage-window.v1`, `epochpact.windows.v1`, `epochpact.auto-launch.v1`

---

## 1. game_setup (launcher panel, `ui/launcher.js`). Complexity: small (but almost entirely DESKTOP-ONLY)

### Render
`renderPage` clears the page. `EpochLauncher.mount('game_setup')` then inserts `<div data-launcher-panel>`; `mount('overview')` inserts `<div data-launcher-summary>`. The same panel also appears in `#setup-dialog` ("Welcome to EpochPact", button "Continue to EpochPact"), which opens at start when `!state.selected`.

```
<section class="panel game-setup"><div class="section-head"><h2>Game setup</h2><span class="badge">{status}</span></div>
<p class="muted">{updateRequired ? "The installed mod belongs to another EpochPact package. Close Last Epoch and reinstall using this complete download." : "Select your Last Epoch executable once. Install the mod with the game closed, then launch offline."}</p>
<label>Game executable<div class="game-path-row"><input data-game-path aria-label="Last Epoch executable path" placeholder="Select Last Epoch.exe"><button data-game-choose>Browse…</button><button data-game-save>Save path</button></div></label>
<p data-game-problem class="muted">{state.problem || "The file picker opens in your Steam folder. You can browse to any other installation."}</p>
<div class="actions"><button data-game-install>{installed||updateRequired ? "Reinstall / update mod" : "Install mod"}</button>[<button data-game-elevate>Install as administrator</button>][<button data-game-uninstall>Uninstall mod</button> if installed||updateRequired]<button class="primary" data-game-launch>{running ? "Game running" : "Launch offline"}</button></div>
<label class="inline-check"><input type="checkbox" data-game-autolaunch> Launch the game when EpochPact starts</label>
<p class="muted">{running ? "Close the game before installing or changing its location." : "Offline characters only. No gameplay settings are applied on startup."}</p>
<p data-setup-feedback class="setup-feedback" role="status"></p>
<p class="muted">Need help? <a href="https://discord.gg/q6aexZZSAf">EpochPact Discord</a></p></section>
```

- **Status badge text (first match wins):**
  - updateRequired → "Mod update required"
  - melonLoader → installed ? "MelonLoader bridge installed" : "MelonLoader detected"
  - foreignLoader → "Another mod loader is installed"
  - disabled → "Mod disabled"
  - installed → "Player mod installed"
  - selected → "Ready to install"
  - otherwise → "Choose your game"
- **Overview summary:**
  ```
  <section class="panel game-setup setup-summary"><h2>Game</h2> + badge
  <div class="actions">[<a class="button primary" href="#game_setup">Open Game setup</a> if needsSetup][<button data-game-launch> if installed && !updateRequired]</div>
  + feedback</section>
  ```
- **CSS:**
  - `.game-path-row{display:flex;gap:10px;align-items:center;margin-top:8px}`; its input flex 1; its buttons nowrap.
  - `.game-setup .actions{flex-wrap:wrap}`; `.setup-feedback:empty{display:none}`.
  - `.setup-dialog{width:min(850px,95vw);max-height:90vh;overflow:auto}`; `.setup-dialog .panel{margin-bottom:14px}`; `.setup-summary .actions{margin-top:0}`.
  - Under 1000px: the path row wraps.

### Data
- **Launcher state** comes from `/api/bootstrap.launcher` and every connection result (`result.launcher`): `{selected, executable, installed, updateRequired, running, runningUnknown?, problem, revision, foreignLoader, melonLoader, disabled}`, from `PlayerSetup.status()` in `tools/player_setup.py`.

### Actions
- **Transport:** each action sends `{type:"launcher", action, args}`.
- **`select {executable}`:** used by Save path and Browse. Browse calls `pywebview.api.choose_game_executable()`, a Windows file dialog opened in the Steam folder.
- **`install`:** on PermissionError the reply has `requiresElevation`, which makes "Install as administrator" (`elevated_install`) appear.
- **`launch`:** `le.cmd_launch(offline=True)`.
- **`uninstall`:** confirm title "Uninstall EpochPact?", message "This removes EpochPact’s loader and core from the game folder. Another loader and its mods, your saves, logs and recovery backups are kept."
- **Button disabling:**
  - Install, elevate and uninstall are disabled when busy, not selected, running, runningUnknown, or (foreignLoader && !melonLoader).
  - Launch is disabled when not installed, running or disabled.
- **Feedback:**
  - `result.message`, else "Game setup refreshed.".
  - On failure: the error text, else "Could not complete game setup. No action was automatically retried." (the error is shown in `.setting-error`).
- **Auto-launch checkbox:** stored in `epochpact.auto-launch.v1`. Feedback texts: "Last Epoch will be launched offline each time EpochPact starts." / "EpochPact no longer launches the game when it starts."
- **Footer on this page:** draft label "Game setup" / "Choose your executable, then use the setup buttons."; Apply and Discard are hidden.

### DESKTOP-ONLY
All of it: the file dialog, install, uninstall and elevated install (the game holds its DLLs, so these cannot run from inside the game), launching the game, auto-launch, and the setup dialog.

**In-game equivalent:** a read-only status panel. It could show the core version, the game folder, a "Mod running" note, and a hint that install, update and uninstall need the game closed and the EpochPact app.

---

## 2. General → Weaver's Will affixes (inside `weaverPanel()`). Complexity: medium (the dual-list picker is shared with Loot → Wanted T7 affixes)

Already ported in game: the panel heading "Weaver's Will", its Refresh button, the muted text, and the three setting cards. What remains, appended after the settings `control-grid`:

```
{cache.loot_read.affixes empty ? '<p class="note">Refresh loads the affix names and the current selection.</p>' : ''}
<div class="control-grid">{actionCard(weaver_affixes)}</div>
```

### Card
- `article.control-card.affix-card`.
- Title "Weaver's Will affixes"; badge "Session".
- Description: "Affixes that Weaver's Will upgrades prefer: an upgrade raises one of these when the item has one below T7, and a new affix is one of these when one fits the item. Otherwise the game chooses as usual. None: the game chooses every upgrade."
- The `.form-grid` holds the picker:

```
<div class="affix-picker" data-affix-kind="weaver_affixes">
 <div class="affix-tools"><label>Find an affix<input type="search" data-affix-search placeholder="Search name or ID…"></label>
  <label>Sort<select data-affix-sort><option value="az">Name · A–Z</option><option value="za">Name · Z–A</option></select></label></div>
 <div class="affix-lists">
  <div><label>Available affixes <span data-affix-available-count>{n}</span><select size="10" data-affix-list="available" aria-label="Available affixes">…</select></label><button type="button" data-affix-add>Add selected →</button></div>
  <div><label>Wanted affixes <span data-affix-selected-count>{n} / 64</span><select size="10" data-affix-list="selected" aria-label="Selected Weaver's Will affixes">…</select></label>
   <div class="actions"><button type="button" data-affix-remove>← Remove selected</button><button type="button" data-affix-clear>Clear all</button></div></div></div>
 <p class="note" data-affix-status role="status">{status} Double-click an affix or press Enter to move it. Search only filters the available list.</p></div>
```
- Below the picker: the action-reason line and the submit button "Apply selection" (not primary).
- **CSS:**
  - `.affix-card,.affix-picker{grid-column:1/-1;min-width:0}`, so both span the full row.
  - `.affix-tools,.affix-lists{display:grid;grid-template-columns:1fr 1fr;gap:18px}`; `.affix-tools{grid-template-columns:minmax(0,1fr) 190px;margin-bottom:16px}`.
  - `.affix-lists select{width:100%;height:260px;margin:8px 0 12px}`; `.affix-lists option{padding:5px 8px}`; the count span floats right in accent colour; `.affix-picker .note{margin-top:16px}`.
  - Under 650px: single column, select height 220.

### State (`ui/affix-picker.js`)
- **State object:** `{selected[], applied[], owner, query, order:'az', dirty}`.
- **Owner:** `automaticOwner()` = JSON of `[player.id, name, sessionKey, load]`. When the owner changes, the selection resets.
- **Available list:**
  - Built from `cache.loot_read.affixes` (from `lootread`, entries `{id,name,tiers}`), **not** from weaverread, minus the selected ids.
  - Filtered by `query` (lower-case substring of `name + " " + id`).
  - Sorted with `localeCompare('en',{numeric:true,sensitivity:'base'})`, then by id; `za` reverses both lists.
- **Selected list:** shows `"Affix {id} (name unavailable)"` for ids missing from the catalog.
- **Status text:**
  - dirty → "Selection changed · use Apply selection."
  - else selected non-empty → "Selection applied."
  - else → "No selection: the game chooses every Weaver's Will upgrade."
  - After a move only the status part is rewritten; the "Double-click…" suffix is dropped (quirk).

### Interactions
- **Both listboxes are single-select** (`size=10`, no `multiple`).
- **Moving items:** "Add selected →" / "← Remove selected", a double-click on an option, or Enter on a list all move the highlighted option. Enter in the search box is suppressed.
- **Clear all** moves every selected item back to available.
- **Limit:** more than 64 shows the error notification "Choose at most 64 wanted affixes."

### Data and refresh
- **Refresh button** (already in game, `data-refresh="weaver_read"`): if `loot_read.affixes` is empty it first runs a `loot_read` read (`lootread`), then `weaver_read` (`weaverread`).
- **weaverread JSON:** `{ok, ready, share{percent,hook,uniqueRolls,asked,converted}, speed{multiplier,hook,grants,upgrades}, affixes{ids[], hooks, spends, upgradesSteered, addsFiltered, addsUnfiltered, faults}}`.
- The reply's `affixes.ids` updates `applied` (and `selected` unless the picker is dirty).

### Apply
1. If `state.owner !== automaticOwner()`, show the error "Use Refresh in Weaver's Will for the loaded character before applying this selection." and stop.
2. Otherwise send `weaver_affixes {ids: selected.join(",") || "none"}`, which becomes `weaveraffixes {ids|none}`. The reply is the same weaverread JSON (`native/core/weaver.cpp`).
3. On success: notify "Weaver's Will affixes updated."; merge into `cache.weaver_read`; `receive(..., {force:true})`; re-render.
4. Desktop also stores the args in `last-settings-replay` and replays them on each new character (startup profile; see §16).

---

## 3. Loot & Pickup

Page order in app.js:
1. Automatic collection (ported)
2. **Recent pickups**
3. **Collect now**
4. Quality rules (ported)
5. **Loot filter files**
6. Loot filter window (ported, but the app also shows `<p class="note">Rule name examples: <code><color=#FF4040>red</color>   <color=yellow>yellow</color>   <b>bold</b>   <i>italic</i>   <u>underlined</u></code></p>`, which the in-game page lacks)
7. **Pickup selections**
8. **Pickup rules**

**Reads on page open:** `loot_read` (always) and `pickup_radius_read` (`pickupradius`, not in preview).

### 3a. Recent pickups (`lootPanel`). Complexity: medium
```
<section class="panel"><div class="section-head"><h2>Recent pickups</h2><div class="actions">[<button id="loot-feed-open">Open loot window|Hide loot window</button> DESKTOP]<button id="loot-log-refresh" data-refresh="loot_log" data-control-id="loot_log">Refresh</button></div></div>
<p class="muted">Items and gold you or Auto Pickup collect, with the loot filter rule that matched. "Rule" uses the same number as the game's Item rule numbering setting. The loot window stays on top of a windowed or borderless game. Point at an affix shard to see what its affix does on each tier.</p>
<div id="loot-log-live">{table}</div><div data-id="affix_info" hidden></div>
<div class="control-grid">{actionCard(loot_log_clear)}</div></section>
```

**Table** (`EpochLootLog.table`, `ui/loot-log.js`):
- Wrapper: `.table-scroll > table.loot-table`.
- Headers: When | Pickup | Tiers | Rarity | Loot filter | By.
- Shows the newest 100 rows, newest first, grouped (see below).
- **Cells:**
  - When: age `Ns`, `Nm` or `Nh`.
  - Pickup: `td.loot-{rarity lower-case, letters only}`, with a title tooltip giving the shard's affix text.
  - Tiers: `T7 T6 …` from the space-separated `tiers`.
  - Rarity: the raw rarity text.
  - Loot filter: the rule label.
  - By: "Auto Pickup" or "You".
- **Item label:**
  - gold → `+{n en-US} Gold`
  - amber → `{name}` plus ` ×{gold}` when present
  - material with count>1 → `{name} ×{count}`
  - otherwise → `name`
  - Grouped gold or amber rows append `<small class="muted">({count} pickups)</small>`.
- **Grouping:** consecutive gold, or consecutive amber/material rows with the same name, merge into one row (gold summed, count incremented). Equipment is never grouped.
- **Rule label:**
  - Empty for gold, amber and material.
  - `ruleNumber==0` → "No matching rule" (when outcome is unset or ≥0).
  - Otherwise `Rule {n} · {Show|Hide|Highlight} · {name with rich-text tags stripped}`.
- **Shard tooltip** (when kind is material, type 101, and the affix info is known), lines:
  - `{name} · {Prefix|Suffix|Special} · Level {n}`
  - `Rolls on: a, b`
  - `T{t}  {min} – {max} / {min2} – {max2}` (one line per tier)
  - `(Values for {valuesOn})` when there are several base types
- **Empty state:** "No pickups yet. Items and gold you or Auto Pickup collect appear here."
- **CSS:**
  - `.loot-table td:first-child{min-width:56px}`
  - Rarity colours: `.loot-unique,.loot-legendary{#e8a35a}`, `.loot-set{#7ee08a}`, `.loot-exalted{#c79bff}`, `.loot-rare{#f2dc6b}`, `.loot-magic{#7fb0ff}`, `.loot-material{#9fd6d0}`, `.loot-gold{#e8c690}`, `.loot-amber{#f0a64a}`.

**Data:**
- Read `loot_log` (command `lootlog`, `items::LootLog`).
- Reply: `{ok, ready, session:"<16 hex>", seq, faults, ruleOrderMin, ruleOrderMax, entries:[{seq, ageSeconds, kind:"item|gold|amber|material", name, rarity, gold, ruleNumber, rule, outcome(0 Show,1 Hide,2 Highlight,-1), tiers:"7 6", type, subtype, automatic}]}`.
- **Merge** (`merge` + `sessionOf`):
  - Key: `session|seq`.
  - Absolute time: `at = now - ageSeconds*1000`.
  - Ordering: by session start, then by seq.
  - Cap: 300 entries.
  - Entries reset when the owner changes.
- **Polling:** every 1 s the app checks; on the loot page, `loot_log` is read every **2000 ms**. It skips when hidden, busy, an auto-apply is pending, or the stat dialog is open. Refresh forces an immediate read.
- **Affix info:** after each poll, for up to 4 unknown shard affix ids (each at most 3 tries), read `affix_info {id}` (`affixinfo {id}`). Reply: `{ok,id,name,type,level,rollsOn[],tiers[{tier,min,max,min2,max2}],valuesOn}`.

**Clear card:**
- Title "Clear recent pickups"; badge "View"; description "Clear the recent pickups list. Items and gold are unchanged."; button "Apply".
- Sends `lootlogclear`, with no confirm.
- On ok: entries cleared; notify "Recent pickups cleared. Items and gold are unchanged."

**DESKTOP-ONLY:** "Open loot window" / "Hide loot window" (`pywebview.api.open_loot_feed()`, `hide_loot_feed()`, `overlay_windows()` polled every 3 s); see §17.7. In game, the equivalent is toggling an in-game HUD panel.

### 3b. Collect now. Complexity: small
```
<section class="panel"><h2>Collect now</h2>
[<p class="note">Your inventory is full. Equipment pickup is paused until you open and close your inventory; gold, potions, tomes, bones and materials are still collected.</p> if cache.pickup_radius_read.inventoryFull]
<div class="control-grid">{actionCard(pickup_now)}</div></section>
```
- **Card:** title "Pick up everything now"; badge "View"; description "Collect every drop the game has loaded right now, ignoring the radius. Your pickup rules, categories, loot filter and inventory space still apply. Runs across several frames."; button "Apply".
- **Command:** `pickupnow`, no confirm.
- **Reply:** `{ok,picked,steps,inventoryFull,skippedWhileFull}`, or an error such as "Load an offline character first." / "The game is not updating. Return to the game window and try again." / "Close the item card in the game, then try again."
- **Notify:** `Picked up {picked} drops.` plus, when inventoryFull, ` Your inventory is full: equipment waits until you open and close your inventory.`
- **inventoryFull source:** `pickupradius` reply `{ok,radius,allLoaded,intervalSeconds,inventoryFull,inventoryFullEvents,skippedWhileFull,inventoryFullPause}`. It is also merged from `pickupradius X` and `pickupinterval X` replies. The pickup_now reply does **not** repaint the note.

### 3c. Loot filter files (`filterFilesPanel`). Complexity: medium in game (all logic is in Python)
```
<section class="panel" id="filter-files"><div class="section-head"><h2>Loot filter files</h2><div class="actions">[<button id="filter-import">Import filter…</button> DESKTOP]<button id="filter-backup" [disabled if no filters]>Back up all</button><button id="filter-folder">Open folder</button><button id="filter-refresh">Refresh</button></div></div>
<p class="muted">Your Last Epoch loot filters are shared by every character and season. Export a filter to share it, import a filter someone sent you, or restore an EpochPact backup ZIP. Imports and duplicates are added as new filters; existing filters are never changed or deleted.</p>
{body}</section>
```
- **Body states:**
  - preview → `<p class="empty">Loot filter files are listed in the desktop app.</p>`
  - loading → `<p class="empty">Reading loot filters…</p>`
  - error → `<p class="note">{error}</p>`
  - empty → `<p class="empty">No loot filters yet. Create one in the game's loot filter window first.</p>`
  - list → `.table-wrap > table.filter-table`:
    - Headers: Filter | Enabled rules | Modified | (blank).
    - Row: `{name}` (plus `<span class="badge">{error||"Unreadable"}</span>` if invalid) | `{enabled} / {rules}` or "—" | date `DD/MM/YYYY` | `td.actions` with `<button data-filter-export=file>Export</button><button data-filter-duplicate=file>Duplicate</button>` (valid filters only).
  - CSS: `.filter-table td.actions{display:flex;gap:6px;justify-content:flex-end;flex-wrap:wrap}`.
- **Data:** `{type:"launcher", action:"filters"}` calls `filter_manager.list_filters()`, which returns `{ok, folder, exists, filters:[{file,size,modified(unix s),name,rules,enabled,version,valid,error?}]}`.
  - Folder: `%USERPROFILE%\AppData\LocalLow\Eleventh Hour Games\Last Epoch\Filters\*.xml`.
  - XML parsing is strict: root `ItemFilter` with `<name>` and `<rules>`; counts `Rule` elements and `isEnabled==true`; refuses any DOCTYPE/ENTITY; 2 MB limit.
- **Actions:** each is a launcher action, silent; the app notifies `r.message` and replaces the list with `r.filters` when present.
  - `filter_export {file}` copies to `%LOCALAPPDATA%\EpochPact\filter-exports\` and opens the folder.
  - `filter_duplicate {file}` writes `"{name} copy"` (unique name, `<name>` element rewritten).
  - `filter_backup` writes `filters-YYYYMMDD-HHMMSS.zip` to `filter-backups\`, keeps 20, opens the folder.
  - `filter_folder` runs `os.startfile` on the folder.
  - `filter_import {path}` imports `.xml`, or every top-level `.xml` in a backup `.zip`, as new files. The path comes from a file dialog (`choose_filter_file`).
  - The exact message texts are in `tools/filter_manager.py`, e.g. `Created "{name}". The original filter was not changed.`
- **DESKTOP-ONLY:**
  - File dialog: in game, would need Win32 `GetOpenFileNameW` on a worker thread, which alt-tabs or overlays the game.
  - Explorer folder open: `ShellExecute`, which takes focus away from the game.
  - All file work is Python. In game it would need native `std::filesystem` plus an XML reader plus ZIP (minizip or Windows `Compressed Folder` APIs).
  - Listing, duplicate and export are feasible natively; import and open folder are awkward in a fullscreen game.

### 3d. Pickup selections. Complexity: medium (shares the picker from §2)
```
<section class="panel"><h2>Pickup selections</h2><div class="control-grid">{actionCard(loot_mode)}{actionCard(loot_category)}{actionCard(loot_affixes)}</div></section>
```
- **loot_mode:**
  - Title "Pickup mode"; badge "Session"; description "All items, the current loot filter, Unique LP or wanted T7 quality, or crafting materials. Enable auto pickup separately."
  - Field: `<label>Selection<select name="mode">` with All/Filter/Quality/Materials (values all/filter/quality/materials), pre-selected `cache.loot_read.mode ?? "all"`.
  - Button "Apply"; command `lootmode {mode}`.
  - Notify "Pickup mode updated."
- **loot_category:**
  - Title "Pickup category"; description "Choose crafting materials, gold, potions, XP tomes, Favor tomes, Ancient Bones or Memory Amber, Soul Embers and XP motes ("amber") independently."
  - Fields: `<label>Pickup category<select name="category">` Materials/Gold/Potions/XP/Favor/Bones/Amber, and `<label>Value<input name="value" type="number" min=0 max=1 step=1 value=1 required>`.
  - Command `lootcategory {category} {value}`.
- **loot_affixes:**
  - Same picker as §2, with these differences:
    - It is not scoped: the selectors are global, with no `data-affix-kind` wrapper match.
    - aria-labels "Available T7 affixes" / "Selected wanted T7 affixes".
    - Empty status text "No selection accepts any T7 affix."
    - Owner-mismatch error "Refresh pickup rules for the loaded character before applying this selection."
  - Title "Wanted T7 affixes"; description "Search and sort the available affixes, then double-click to move choices between the two lists. Choose up to 64; an empty wanted list accepts any T7 affix."
  - Command `lootaffixes {ids|none}`.
- **All three reply** with the lootread state: `{ok, ready, mode, minimumLP, minimumWW, maximumWW, t7, respectFilter, materials, categories{gold,potions,xp,favor,bones,amber}, affixIds[], telemetry{acceptedRequests,rejected,faults}}`. lootread additionally includes `affixes:[{id,name,tiers}]`. The reply is merged into `cache.loot_read`; the quality settings (`loot_lp`, `loot_ww`, `loot_t7`, `loot_filter`) are refreshed from it; the page repaints.
- **No confirm** (session lifetime). Desktop replays the last args per id (and per category for loot_category).

### 3e. Pickup rules. Complexity: small
```
<section class="panel"><div class="section-head"><h2>Pickup rules</h2><button data-refresh="loot_read" data-control-id="loot_read">Refresh rules</button></div>
<div class="note">Current selection: {all→"All items" | filter→"Game loot filter" | quality→"LP and T7 quality rules" | materials→"Crafting materials" | none→"Refresh to read your rules"}.</div>
{actionCard(loot_reset)}   ← not wrapped in control-grid (full width)
</section>
```
- Developer builds also show cards for `loot_read` and `pickup_radius_read`.
- **loot_reset:** title "Reset pickup selections"; badge "Session"; description "Restore all/default selections. Auto pickup keeps its current on/off state."; button "Apply"; command `lootreset`; no confirm.
- **After loot_reset:** notify "Pickup rules reset."; the picker is re-received with `force`; desktop removes every `loot_*` replay entry.

---

## 4. Crafting (whole page, `renderCrafting`). Complexity: large

**Reads on open:** `craft_read` (`craftread`) and `craft_materials_read` (`craftmaterialsread`, not in preview). `craft_read` is re-read on every connection refresh while on this page.

**Footer:** Apply changes and Discard draft are hidden unless the Forge Settings tab is active.

**CSS, page-wide:**
- The hero shrinks when crafting tabs exist (`.main-scroll:has(.craft-categories) .hero{height:156px;padding:20px 28px}`, h1 `clamp(28px,2.5vw,38px)`).
- Within the content: `#craft-category-content .control-top .icon{display:none}`, `.control-grid:has(>article:only-child){grid-template-columns:1fr}`, `.control-description{min-height:0}`, `.control-tools{flex-wrap:wrap}`.

### 4.1 Tabs (always shown)
```
<div class="craft-categories" role="group" aria-label="Crafting categories">
 <button type="button" data-craft-section="forge" aria-pressed aria-controls="craft-category-content"><strong>Forge Settings</strong><span>Costs, materials and glyph chances</span></button>
 … materials: <strong>Materials</strong><span>Stock your crafting stash</span>
 … custom:    <strong>Custom Items</strong><span>Build an item step by step</span></div>
{pendingNote}<div id="craft-category-content">{tab html}</div>
```
- `pendingNote`: when the tab is not forge and there are dirty `craft_*` drafts: `<div class="note">{n} Forge settings have unapplied changes. <button class="link-button" data-craft-section="forge">Review Forge Settings</button></div>`.
- **CSS:**
  - `.craft-categories{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin-bottom:22px}`.
  - Button: `text-align:left;padding:16px 18px;background:var(--panel)`. `strong` 15px block; `span` block, 11px, muted, lh 1.5, mt 8.
  - `[aria-pressed=true]`: border accent, `box-shadow:inset 0 -3px var(--accent)`, bg soft.
  - Under 900px: gap 8, padding 12, 13px/10px. Under 600px: one column.
- The selected tab is remembered (`craftingSection`). Global search results can preselect it via `data-craft-target`.

### 4.2 Forge Settings tab (session settings, draft plus "Apply changes")
Content in order:
1. `<p class="note">These settings affect the game’s normal forge. Edit them here, then use <strong>Apply changes</strong> below. No item is crafted until you craft in the game or press Craft once.</p>`
2. `settingPanel('Affix upgrade limit',[craft_t7])`: card "Craft affixes up to T7" (catalog text), toggle.
3. `settingPanel('Preserve materials', …)` with overridden labels and descriptions:
   - craft_shards: "Keep affix shards" / "Keep the shard used for a craft. You need at least one to begin."
   - craft_runes: "Keep runes" / "Keep the rune used for a craft. You need at least one to begin."
   - craft_glyphs: "Keep glyphs" / "Keep the glyph used for a craft. You need at least one to begin."
4. `settingPanel('Forging Potential cost',[craft_fp])`: "Forging Potential cost" / "0 = no FP cost · 0.5 = half cost · 1 = normal cost. Does not add FP to an item." Range 0–1 step 0.1, no unit, limits "0–1 · Default: 1".
5. Custom panel:
   ```
   <section class="panel"><div class="section-head"><h2>Glyph chances</h2><span class="badge">Use Apply changes below</span></div><div class="control-grid">{glyph cards}</div></section>
   ```
   **Glyph card** (`glyphChanceCard`):
   ```
   <article class="control-card" data-setting="craft_hope"><div class="control-top"><h3>Glyph of Hope</h3><span class="badge">Session</span></div>
   <p class="control-description">Chance to prevent FP loss. Requires Basic Glyph of Hope.</p>
   <label class="craft-mode-label">Chance<select data-craft-glyph-mode="craft_hope"><option value="default">Game default</option><option value="custom">Custom percentage</option></select></label>
   default → <p class="craft-default">Uses the game’s normal chance.</p>
   custom  → <div class="control-tools"><input type="range" min=0 max=100 step=1 data-setting-range><div class="stepper"><input type="number" min=0 max=100 step="any" required data-setting-number aria-label="Glyph of Hope percentage"></div><span class="unit">%</span></div>
   <div class="limits"><span data-setting-feedback></span></div></article>
   ```
   - The second card is craft_despair: "Glyph of Despair" / "Chance to seal an eligible T1–T4 affix. Other game requirements still apply."
   - Switching the mode sets the draft to `null` (default) or `50` (custom).
   - CSS: `.craft-mode-label{display:block;color:var(--muted);font-size:12px}`; `.craft-default{color:var(--accent);font-size:13px;margin:18px 0}`.
6. `<details class="panel craft-advanced" data-craft-disclosure="advanced" [open]><summary>Advanced forge options</summary>`, containing:
   - `settingPanel('Level requirement',[craft_level as "Ignore affix crafting level" / "Skip the selected affix upgrade’s level check. Equipment level requirements stay unchanged."])`
   - `<div class="section-head"><h2>Craft from EpochPact</h2><button data-refresh="craft_read" data-control-id="craft_read">Refresh selected craft</button></div>`
   - `<p class="note">{forgePreview.canForge ? "The selected craft is ready." : forgePreview.message || "Select an item and a shard or rune in the game’s forge, then refresh."}</p>`
   - `{actionCard(craft_forge)}`
7. `<details class="panel craft-advanced" data-craft-disclosure="defaults"><summary>Restore game defaults</summary>{actionCard(craft_reset as "Restore normal crafting" / "Reset all Forge settings. Crafted items and stocked materials are kept.")}</details>`
   - CSS: `.craft-advanced>summary{font-size:15px}`; `.craft-advanced>.panel{margin:20px 0;border:0;padding:0;background:transparent}`.
   - Open or closed state is remembered in memory.

**Commands:**
- craft_t7 → `craftt7 v`; craft_shards → `craftshards v`; craft_runes → `craftrunes v`; craft_glyphs → `craftglyphs v`; craft_level → `craftlevel v`.
- craft_fp → `craftfp v`.
- craft_hope → `crafthope v|reset`; craft_despair → `craftdespair v|reset`. Apply sends `value ?? "reset"`.
- All are drafts: "Use Apply changes below". After applying any `craft_*` setting, `craft_read` is re-read once.

**craftread JSON:**
`{ok, ready, fpFactor, hopePercent|null, despairPercent|null, preserveShards, preserveRunes, preserveGlyphs, allowT7, bypassLevel, hooks{…}, telemetry{…}, forgePreview:{player{id,name}, hasItem, canForge, canCustom, message, successMessage, forgingPotential?, affixes?[{id,tier}]}}`.
The bridge renames native `preview` to `forgePreview`.

**craft_forge:**
- Title "Forge selected item once"; badge "Game save"; button "Craft once" (primary).
- Disabled unless `forgePreview.canForge===true`; reason: message or "Select an eligible item and material in the game forge, then read live."
- Generic game_save confirm. Command `craftforge {save_id}`.
- Reply `{ok, crafting{settings}, forgePreview, backup}` is merged into craft_read.
- Notify "Item forged once." plus the backup text.

**craft_reset:**
- Button "Restore game defaults"; no confirm; command `craftreset`.
- Clears dirty `craft_*` drafts. Notify "Normal crafting restored."

### 4.3 Materials tab
```
<section class="panel"><div class="section-head"><h2>Stock your materials</h2><button data-refresh="craft_materials_read" data-control-id="craft_materials_read">Refresh materials</button></div>
<p class="muted">Add shards, runes and glyphs to your saved crafting stash in one action.</p>
<p class="note">{current ? "{kinds.shards} shard types · {kinds.runes} rune types · {kinds.glyphs} glyph types" : "Load an offline character and refresh to read your crafting stash."}</p>
{actionCard(craft_materials as "Minimum stock for every material" / "Raises each material to your chosen minimum. Higher quantities are kept. This adds a saved quantity once; it does not enable infinite materials.")}
<p class="muted">To keep materials while crafting, use Preserve materials in Forge Settings.</p></section>
```
- **Field:** `<label>Minimum quantity per material<input name="value" type="number" min=1 max=1000000 step=1 value="10000" required><span class="note">Higher existing quantities are kept. Range: 1–1,000,000 per material.</span></label>`. The typed value is remembered across renders.
- **Button:** "Stock all materials" (primary); badge "Game save".
- **Confirm:** title "Stock all materials?", message lines (pre-line):
  - `Character: {name|"Loaded offline character"}`
  - `Minimum stock: {N with thousands separators} of each shard, rune and glyph.`
  - (blank line)
  - `Higher quantities are kept. Adds materials once to the shared crafting stash and creates a recovery backup. This does not enable infinite materials.`
- **Command:** `craftmaterials {save_id} {value}`. The expected id and name come from `craft_materials_read.player`.
- **Replies:**
  - `craftmaterialsread`: `{ok, player{id,name}, kinds{shards,runes,glyphs}, materials[{type:101|102|103,id,name,count}]}`
  - `craftmaterials` adds `target, updatedKinds, addedUnits, backup`.
- **Notify:** `{updatedKinds} material types stocked to at least {target}. Saved in your crafting stash.` plus the backup text.
- **Preview disabling:** "Load an offline character to read or stock real crafting materials."

### 4.4 Custom Items tab (`customWorkshop`)
```
<section class="panel craft-workshop"><div class="section-head"><h2>Custom Items</h2><button data-refresh="craft_custom_catalog" data-control-id="craft_custom_catalog">{loaded?"Refresh item choices":"Load item choices"}</button></div>
<p class="muted">Keep your item’s equipment base. Choose its template and affixes, then review the result before creating it.</p>
<form class="control-form" data-id="craft_custom">
 <section class="craft-step"><h3><span>1</span>Select the item in the game</h3><p>Open the game’s forge and place the equipment item you want to change.</p>
  <div class="craft-item-state"><span>{canCustom ? "Equipment selected · {forgingPotential??'—'} Forging Potential · {affixes.length} affixes" : "Waiting for an equipment item in the game’s forge."}</span><button type="button" data-refresh="craft_read" data-control-id="craft_read">Refresh selected item</button></div></section>
 <section class="craft-step"><h3><span>2</span>Choose a template</h3><p>Use Exalted for affixes only, or copy one Unique / Set template onto this base.</p>
  <div class="craft-template-search"><label>Find a template<input type="search" data-workshop-search="template" placeholder="Search item names..."></label><label>Type<select data-workshop-kind><option value="all">All templates</option><option value="unique">Unique</option><option value="set">Set</option></select></label></div>
  <label>Template<select name="template_id" data-workshop-template><option value="-1">Exalted · no Unique / Set template</option>{"Set · name"|"Unique · name"}…</select></label></section>
 <section class="craft-step"><h3><span>3</span>Choose T7 affixes</h3><p>Click to add or remove an affix. Choose up to {6 if any extra effects else 7}; leave all unchecked to keep the item’s current affixes.</p>
  <label>Find an affix<input type="search" data-workshop-search="affixes" placeholder="Search health, damage, resistance..."></label>
  <div data-workshop-list="affixes">{picker}</div><div class="craft-chips" data-workshop-chips="affixes">{affix rows}</div></section>
 <details class="craft-step craft-extra" data-workshop-advanced><summary>Add extra Unique / Set effects <span>Advanced · optional</span></summary>
  <p>Mix individual effects from different items. Choose up to 64 lines. They share one affix slot, so only six regular affixes can be selected.</p>
  <label>Find an effect<input type="search" data-workshop-search="modifiers" placeholder="Search a source item or effect..."></label>
  <div data-workshop-list="modifiers">{picker}</div><div class="craft-chips" data-workshop-chips="modifiers">{chips}</div></details>
 <section class="craft-step"><h3><span>4</span>Review and create</h3>
  <label>Roll quality<select name="perfect"><option value="1">Maximum rolls</option><option value="0">Midpoint rolls</option></select></label>
  <p class="muted">Each chosen affix has its own tier (T1–T8) and roll (1–100% of its tier's range, or Random). T8 is the game's primordial tier: one per item, shown for affixes that have it; the game limits how many primordial items you can wear. Roll quality applies to the template, implicits and extra effects, and is the starting roll for newly chosen affixes. Without extra effects, keeping current affixes preserves their tiers and midpoint mode keeps their existing rolls. Adding extra effects rebuilds kept affixes at T7 with the chosen rolls.</p>
  <div id="workshop-summary">{summary}</div>
  <p class="muted">Replaces the selected item once and saves a recovery backup. This custom action does not consume crafting materials.</p>
  <p class="action-reason" data-action-reason hidden></p><div class="actions"><button type="submit" class="primary" data-control-id="craft_custom">Create custom item</button></div></section></form></section>
```

**Pickers** (`workshopPicker(kind)`):
- Count line: `<div class="craft-pick-count">{matches} matches · {selected} selected</div>`.
- List: `<div class="craft-pick-list">` of `<label><input type="checkbox" data-workshop-pick="{kind}" value=id [checked]><span>{name}</span></label>`.
- Paging: shows the first 40; `<button class="quiet" data-workshop-more>Show more ({shown} of {total})</button>` adds 40. A new search resets the limit to 40.
- Empty: "No matches. Try another search." or, before loading, "Load item choices above to see the available options."
- **Search** (`craftingModel.filtered`): every whitespace-separated word must appear (lower-case) in `uiLabel||name`. The template search also filters by type (set or not set). The currently selected template stays in the list.

**Affix rows** (step 3 chips area), one per chosen affix:
```
<div class="craft-affix-row"><span class="craft-affix-name">{name}</span><label>Tier<select data-workshop-tier>T1…T{tiers}, 8 labelled "T8 · primordial"</select></label><label>Roll<span class="craft-roll"><input type="number" min=1 max=100 step=1 data-workshop-roll [disabled if random]>%</span></label><label class="inline-check"><input type="checkbox" data-workshop-random> Random</label><button type="button" class="craft-chip" data-workshop-remove="affixes" aria-label="Remove {name} from the item">×</button></div>
```
- Empty: `<span class="muted">No replacements selected. Current affixes are kept.</span>`.
- Defaults per affix: tier 7; roll 100, or 50 for midpoint. Unticking Random restores 100 or 50.
- `tiers` = `min(affix.tiers, 8)` from the catalog, default 7.

**Modifier chips:**
- `<button class="craft-chip" data-workshop-remove="modifiers">{name}<span> ×</span></button>`.
- Empty: "No extra effects selected."
- **Modifier display name:** `[source, property name (characterStats.properties, camel-case split), names of tags whose bit is set (t.id>0), ailment name (special≠0), "Variant {extra}", amount].join(" · ")`.
  - The amount uses `maximum`, or `(value+maximum)/2` when perfect="0".
  - mode 0 → `toPrecision(6)`; otherwise `(v*100).toPrecision(6)+"% increased|more"`.

**Summary** (`#workshop-summary`):
```
<dl class="craft-summary"><dt>Result</dt><dd>…</dd><dt>Affixes</dt><dd>…</dd><dt>Extra effects</dt><dd>…</dd><dt>Quality</dt><dd>…</dd></dl>
[<p class="action-reason">{problem}</p>]
[<p class="craft-portability">Extra effects require EpochPact and <strong>workshop-recipes.txt</strong> when reloading or moving this item to another PC. Keep that file with your saves.</p>]
```
- Values come from `craftingModel.summary`:
  - **Result:** "Set · X" / "Unique · X" / "Exalted · keep this item’s base".
  - **Affixes:** "1 selected affix · T7" / "N selected affixes · T7, T8"; with none selected, "Current affixes rebuilt at T7" if there are modifiers, else "Keep the current affixes".
  - **Extra effects:** "1 extra effect" / "N extra effects" / "No extra effects".
  - **Quality:** "Maximum rolls" / "Midpoint rolls" / "Midpoint for new effects · current affix rolls kept".
- **Problem texts** (`crafting-model.js problem()`), in check order:
  1. "Load item choices before creating custom gear."
  2. "Choose maximum or midpoint rolls."
  3. "Choose a template from the current item list."
  4. "Choose at most {6|7} different affixes[ when adding extra effects]."
  5. "Choose at most 64 different extra effects."
  6. "An affix is no longer in the current game catalog. Review your selection."
  7. "An extra effect is no longer in the current game catalog. Review your selection."
  8. "Choose a tier from T1 to T8."
  9. "An affix has no T8 in the game. Choose T1 to T7 for it."
  10. "Choose a roll from 1 to 100%, or Random."
  11. "An item can have one T8 (primordial) affix."
  12. "This item has seven affixes. Choose up to six affixes to leave room for extra effects."

**Submit button disabling:**
- `!forgePreview.canCustom` → "Put an equipment item in the game's forge, then refresh the selected item."
- Otherwise the problem text above.
- If the catalog was loaded for another character: "Load item choices for the current character."

**Confirm:** title "Create this custom item?", message lines:
- `Character: {name}`
- `{template}`
- `{affixes} · {effects} · {rolls}`
- (blank line)
- `Replaces the item currently selected in the game’s forge. No materials are consumed. A recovery backup is created.`
- plus, when modifiers≠none: `Extra effects require EpochPact and workshop-recipes.txt when reloading or moving this item.`

**Args:**
- `template_id` (number, -1 means Exalted).
- `affixes`: comma-joined `id:tier:roll` tokens with roll `1-100` or `r`, or `keep`.
- `perfect` 0|1.
- `modifiers`: comma-joined ids (`u123:4` / `s…`) or `none`.

**Commands:**
- With modifiers: `craftmix {save_id} {template_id} {affixes} {perfect} {modifiers}`.
- Without: `craftcustom {save_id} {template_id} {affixes} {perfect}`.
- Reply `{ok, forgePreview, backup}`.
- Notify "Create custom gear updated." plus the backup text.

**Catalog read (`craft_custom_catalog`, Python composite in `tools/build_backend.py`):**
1. Run `atlasread`, `lootread`, `identityread`, `craftmods`, `identityread`.
2. Fail if any is not ok, or if the player id/name differs: "The loaded character changed. Load workshop templates again."
3. Result: `{ok, player, items: atlasread.items[{id,name,type,baseType,set,randomDrop,level}], affixes: lootread.affixes[{id,name,tiers}], modifiers: craftmods.modifiers[{id,source,property,tags,special,extra,mode,value,maximum}]}`.
4. If the player changes, the draft resets.

**Workshop CSS:**
- `.craft-workshop>.muted{line-height:1.7}`; `.craft-workshop .control-form{display:block}`.
- `.craft-step{padding:24px 0;border-top:1px solid var(--line);margin-top:22px}`.
- `.craft-step h3{display:flex;gap:12px;align-items:center;font-size:18px;margin:0 0 14px}`; `.craft-step h3>span{display:grid;place-items:center;flex:0 0 30px;height:30px;border:1px solid var(--accent);color:var(--accent);border-radius:50%;font-size:13px}`.
- `.craft-step p{font-size:13px;line-height:1.7;color:var(--muted)}`; `.craft-step>label{display:block;font-size:12px;color:var(--muted);margin:14px 0}`.
- `.craft-item-state{display:flex;align-items:center;gap:14px;justify-content:space-between;padding:16px;background:var(--soft);border:1px solid var(--line);border-radius:4px;font-size:13px}`.
- `.craft-template-search{display:grid;grid-template-columns:2fr minmax(130px,1fr);gap:14px}`; labels muted 12px.
- `.craft-pick-count{font-size:11px;color:var(--muted);margin:14px 0 10px}`.
- `.craft-pick-list{max-height:240px;overflow:auto;border:1px solid var(--line);border-radius:4px;background:#0002}`; label `display:flex;gap:12px;padding:11px 14px;border-bottom:1px solid #92734025;font-size:13px;line-height:1.6`; a checked label gets bg soft; the checkbox is 16×16.
- `.craft-chips{display:flex;flex-wrap:wrap;gap:8px;margin-top:14px;font-size:12px}`.
- `.craft-affix-row{display:flex;flex-wrap:wrap;align-items:center;gap:8px 12px;width:100%;padding:6px 8px;border:1px solid var(--line);border-radius:3px;background:#0a121080}`; `.craft-affix-name{flex:1 1 180px}`; row labels 11px muted; selects `width:auto;padding:4px 6px`; `.craft-roll input{width:64px;padding:4px 6px}`.
- `.craft-chip{font-size:11px;padding:8px 10px;background:var(--soft);border-color:var(--accent)}` (4px 9px inside a row).
- `.craft-extra summary{font-size:15px}`; its span is 11px muted with margin-left 12.
- `.craft-summary{display:grid;grid-template-columns:110px 1fr;gap:12px;padding:18px;border:1px solid var(--line);background:var(--soft);font-size:13px;margin:18px 0}`; dt muted; dd margin 0.
- `.craft-portability{padding:12px;border-left:2px solid var(--gold);color:var(--gold)}`.

---

## 5. Character / stats (`renderStats`, `#stat-dialog`, `ui/stat-model.js`). Complexity: large

**Data source:** the static catalog only, `catalog.characterStats`: `sheetRows` (206), `aliases` (72), `properties` (134), `tags`, `ailments`, `modeLimits`. Nothing is read until a stat is opened.

### Browser
```
<section class="panel stat-browser"><div class="section-head"><div><h2>Character bonuses</h2><p class="muted">Choose a stat, adjust your bonus, then apply.</p></div><button class="quiet" data-control-id="stat_reset" id="all-stats-reset">Reset all bonuses</button></div>
<div class="tabs"><button data-stat-tab="sheet" class="selected" aria-pressed>Character sheet<small>206</small></button><button data-stat-tab="aliases">Common stats<small>72</small></button><button data-stat-tab="raw">All stats<small>134</small></button></div>
<div class="filter-row"><input id="stat-search" aria-label="Search stats" placeholder="Find health, damage, resistance…"><button id="stat-favorites-filter" aria-pressed>★ Favorites</button></div>
<div class="stat-categories" role="group" aria-label="Stat categories">{All + categories as buttons, aria-pressed}</div>
<div class="stat-results-head"><span id="stat-count">{n} stats|1 stat</span><span>☆ Save your most-used stats</span></div>
<div id="stat-results">
 <div class="actions"><button id="stat-hidden-filter" aria-pressed>{Show|Hide} hidden stats ({n})</button></div>
 <div class="stat-list">{entries}</div>
 <div class="pagination"><button data-stat-page="-1" aria-label="Previous stat page">←</button><span>{p} / {total}</span><button data-stat-page="1" aria-label="Next stat page">→</button></div></div></section>
```
- **Entry:**
  ```
  <div class="stat-entry"><button class="stat-open" data-stat-index=i><strong>{name}</strong><small>{category}[ · Calculated by the game if row.derived]</small></button>[<button class="secondary" data-stat-index=i data-secondary="true" title="Edit the secondary modifier">Extra</button> if row.modifierKey]<button class="stat-star [selected]" data-stat-favorite=i aria-label="Add|Remove {name} to/from favorites">☆|★</button><button class="quiet" data-stat-hide=i>Hide|Show</button></div>
  ```
- **Empty state:** `<div class="empty"><strong>No matching stats|No matching favorites</strong><p>Try another name or choose All categories.|Use the star beside a stat to save it here.</p><button id="stat-clear-filters">Show all stats</button></div>`.
- **Filtering:**
  - Search: lower-case substring of `name + " " + category + " " + JSON(row)`.
  - Plus the category, the favorites-only toggle, and hidden stats (unless "Show hidden" is on).
  - **Page size: 24.** Search, tab or category changes reset to page 0.
- **Names** (`ST.name`):
  - sheet: `objectName` minus `Stat( (n))?`/`Value` suffixes; special cases Int→Intelligence, Vit→Vitality, str→Strength, MeleeAtackSpeed→"Melee attack speed"; otherwise camel-case split.
  - raw: `Movespeed`→"Movement speed", otherwise split.
  - aliases: split `key.property` without the "Increased " prefix; `dotdamage`→"Damage over time"; melee/bow/throwing/spell/dot/minion prefixes ("Melee …"); element prefixes ("Fire …").
- **Categories** (`ST.category`, first match wins):
  - Minions: tag 8192 or sp 61
  - Attributes: sp 19–23, 46
  - Resistances: resistance|penetration
  - Movement & utility: movespeed|cooldown|haste|experience|drop|area
  - Defence: damagetaken|armour|dodge|ward|block|endurance|avoidance|critavoid|glancing|parry|thorns|potion
  - Health & mana: health|mana|regen|leech|healing
  - Offence: damage|attack|cast|critical|ailment|freeze|stunchance
  - Other
  - The category list shows "All" plus categories in order of first appearance.
- **Favorites:** id `tab:sp:tags:special:extra:objectName|name`, stored in `epochpact.stat-favorites.v1` (error "Favorites could not be saved on this device."). Hidden stats are stored in display preferences `hiddenStats`.
- **"Reset all bonuses"** (`stat_reset`):
  - Confirm "Remove all contributions" / "Removes only EpochPact stat contributions. Equipment stats are kept. Controls sharing the same full stat key, including movement speed, are refreshed."
  - Command `statreset` (text reply). Notify "All character bonuses reset."
- **Developer only:** `details.panel.stat-tools` "Advanced game tools" (sheet_catalog, resistance_labels, sheet_open).
- **CSS:**
  - `.stat-browser .tabs button{display:flex;gap:9px}`; tab `small` 10px muted, `padding:2px 5px;background:#0003`.
  - `.filter-row{display:flex;gap:10px;margin:20px 0 14px}`; its input flex 1, min-width 180.
  - Selected buttons: border accent, bg soft, accent text.
  - `.stat-categories{display:flex;flex-wrap:wrap;gap:6px}`; its buttons `font-size:11px;padding:7px 10px;border-color:transparent;background:#0002`.
  - `.stat-results-head{display:flex;justify-content:space-between;color:var(--muted);font-size:11px;margin:20px 0 10px}`.
  - `.stat-list{display:grid;grid-template-columns:1fr 1fr;gap:8px}`.
  - `.stat-entry{display:flex;align-items:center;gap:5px;padding:0 10px 0 0;border:1px solid #5d675366;background:#0a121080;border-radius:4px}`; hover border gold, bg soft.
  - `.stat-open{flex:1;padding:14px 15px;border:0;background:transparent}`; strong 13px weight 600; small 11px.
  - `.stat-star{font-size:20px;padding:9px;transparent;color:muted}`, selected = gold.
  - `.secondary{font-size:10px;padding:5px 8px;color:accent}`.
  - `.stat-browser .empty{grid-column:1/-1;border:1px dashed var(--line);border-radius:4px;padding:35px 20px}`; its strong 16px ink.

### Stat dialog (`ui/index.html` `#stat-dialog.stat-editor`)
```
<form id="stat-form">
 <div class="dialog-head"><div><span class="eyebrow">CHARACTER BONUS</span><h2 id="stat-title">{name}[ · Extra]</h2></div><button type="button" id="stat-close" aria-label="Close stat editor">×</button></div>
 <p id="stat-note" class="muted">{Shares its bonus with A, B, C[ and other linked stats.|.] | "Customize this stat without changing your equipment."}</p>
 <div class="stat-current"><div><small>CURRENT BONUS</small><strong id="stat-current-value">…</strong></div><button type="button" id="stat-read" class="quiet">↻ Refresh</button></div>
 <div id="stat-basic-modes" class="stat-basic-modes" role="group" aria-label="Bonus type">{mode buttons}</div>
 <section class="stat-value-card"><label for="stat-value">Your bonus</label><output id="stat-preview">…</output>
  <div class="stat-slider-row"><input id="stat-range" type="range"><div class="stepper"><button id="stat-minus">−</button><input id="stat-value" type="number" step="any" required><button id="stat-plus">+</button></div><span id="stat-value-unit" class="unit"></span></div>
  <p id="stat-unit-help" class="muted"></p><small id="stat-range-help"></small></section>
 <details id="stat-advanced"><summary>Advanced</summary><div class="form-grid"><label>Bonus calculation<select id="stat-mode">…</select></label></div><p id="stat-advanced-note" class="muted"></p>[dev: #stat-key, "Game response" pre#stat-live]</details>
 <div class="stat-editor-actions"><button type="button" id="stat-reset">Reset bonus</button><button type="submit" id="stat-apply" class="primary">Apply bonus <span>◇</span></button></div>
 <p class="stat-scope">Applies to this character. Your equipment and base stats are kept.</p></form>
```

**Opening** (`openStat(index, secondary)`):
- **Key:**
  - raw tab: `{sp: row.id, tags:0, special:0, extra:0}`
  - secondary: `row.modifierKey`
  - otherwise: `row.key`
- **Modes:**
  - sp 46 → `[added]`
  - aliases → `[row.mode]`
  - otherwise `row.modes` or `[added, increased, more]`
- **Preferred mode:**
  - aliases → `row.mode`
  - "added" when an alias with the same key is added+fraction
  - "increased" when `displayMetadata.ignoreAdded`, an increased alias exists, or the property is CriticalChance|CriticalMultiplier|AttackSpeed|CastSpeed|Movespeed
  - otherwise "added"
- **Shared note:** sheet rows with the same `sharedKey` (first 3 names).
- **Mode select options:** "Flat / direct bonus" / "Increase %" / "More % · separate multiplier". Disabled for the aliases tab.
- **Basic mode buttons:** all modes except "more". added → "Add percentage points" (fraction stats) or "Flat bonus"; increased → "Increase %". The pressed button gets `.selected`.
- On open it starts at 0, the Advanced section is closed, and it auto-reads after any busy task finishes.

**Units** (`ST.unit`):
- A stat is a fraction when it is an alias with `unit=="fraction"`, or the mode is not added, or an added+fraction alias shares the key.
- Fraction stats use scale 100 and suffix " pp" (added) or "%".
- Otherwise scale 1 and no suffix.
- `#stat-value-unit` shows "pp" for points, otherwise the suffix.

**Bounds** (`ST.bounds`):
- min/max = `modeLimits[mode] × scale` (added: ±3.4e38; increased/more: ≥-1).
- Slider minimum = `max(min, scale100 ? -100 : 0)`.
- Slider maximum = `min(max, scale100 ? 100 : sp∈{7,8,10,11,17,18,76,92} ? 2000 : sp∈{19..23,46} ? 200 : 1000)`.
- step 1. The ± buttons step by 1, clamped to min/max.

**Texts:**
- `#stat-unit-help`:
  - more → "A separate multiplier. +50% More multiplies this stat by ×1.5."
  - points → "Percentage points add directly: a 5% stat with +10 points becomes 15%, before game caps."
  - increased → "Adds to this stat's existing percentage increases. Equipment bonuses still apply."
  - otherwise → "Adds this amount to the stat. Zero removes this bonus amount."
- Range help: `Slider {min}–{max}{suffix}. You can type values outside this slider range.`
- Advanced note: "More applies a separate multiplier. Reset bonus removes all EpochPact bonuses affecting this same stat. The game still handles derived values and caps."
- Preview: `{+|}{value en-US ≤6 decimals}{suffix}[ increased| more] {name}`, or "Enter a bonus".
- Current value:
  - `bonusSummary`: each non-zero mode joined by " · ", or "No bonus"; plus " · Not active" when not attached.
  - Before a read: "Reading…" / "Refresh to read this key" / "Not available".
  - Plus ` · In game: {gameValue}` when known.

**Commands** (all go through `UiBridge._stat`):
- All tabs map to raw keys: `statraw {sp} {tags} {special} {extra}` for a read, `… reset` for a reset, `… {mode} {value:.17g}` for a set.
- When the unit is percent, the value is divided by 100.
- Control ids: aliases → `stat_alias` (args `name`, `unit`, `value`); sheet → `sheet_row` (`catalog_index`, `secondary`, `mode`, `unit`); raw → `raw_stat` (`sp`, `tags`, `special`, `extra`, `mode`, `unit`).
- The reply is text, parsed with `/EpochPact added=X increased=Y more=Z attached=[01]/` and `/\bgameValue=(-?\d+)/`. A reply without the EpochPact suffix but containing `exact-key sum … applicableMore=` means all three modes are 0 and attached.
- Errors are detected with the `text_ok` regex (`refused:|error:|failed:` …).
- A read selects the applied mode on first read.

**Notifications:**
- set and reset show `{label} updated.` ("Character sheet stat editor updated." / "Named stats updated." / "Full-key stat editor updated.").
- A read is silent.
- Key `9:0:0:0` (movement speed) also syncs the General "speed" value.

**Disabling:**
- Every dialog control except close is disabled while busy.
- Apply, Reset and Read are disabled unless the matching control is eligible.

**CSS:**
- `dialog.stat-editor{max-width:740px;padding:0;border-radius:6px;max-height:calc(100dvh - 32px)}`; form padding `27px 30px 0`; sticky `.dialog-head` with panel background.
- h2 32px.
- `#stat-note` 12px muted, margin `13px 0 20px`.
- `.stat-current{display:flex;gap:12px;padding:14px 17px;border:1px solid var(--line);border-radius:4px;background:#0002;margin-bottom:20px}`; small 9px, letter-spacing 1.3px; strong 14px weight 500.
- `.stat-basic-modes{display:flex;gap:8px;margin-bottom:12px}`; each button flex 1, `padding:11px 10px`, 12px; pressed = border accent, bg soft, accent text, `inset 0 -2px accent`.
- `.stat-value-card{border:1px solid var(--line);border-radius:5px;padding:22px;background:linear-gradient(120deg,var(--soft),transparent 80%)}`; label 11px muted.
- `#stat-preview{display:block;color:var(--accent);font:25px/1.4 Georgia;margin:9px 0 27px}`.
- `.stat-slider-row{display:flex;gap:15px}`; stepper `flex:0 0 184px;height:44px;background:#0003`; its buttons 40px; its input 102px, 17px.
- `#stat-unit-help` 12px ink, margin `23px 0 6px`; `#stat-range-help` 10px muted.
- `.stat-editor-actions{position:sticky;bottom:0;display:flex;gap:12px;background:var(--panel);border-top:1px solid var(--line);padding:17px 0 13px}`; primary margin-left auto, min-width 180.
- `.stat-scope{font-size:10px;color:var(--muted);padding:0 0 20px}`.

---

## 6. Skills & Trees. Complexity: small

**Page order:**
1. `cachedSummary("build_read")`: `<div class="note">Refresh live data. Load an offline character, then refresh to see current values.</div>`, or `<div class="note">Live reading · {player.name}</div>`. Note: buildread has no level.
2. **Skill and point budgets**
3. Experience and skill points (ported)
4. Weaver points (ported)
5. Tree connections (ported)
6. Projectiles (ported)
7. **Free respec**

### Skill and point budgets
```
<section class="panel"><div class="section-head"><h2>Skill and point budgets</h2><button data-refresh="build_read" data-control-id="build_read">Refresh live</button></div>
<div class="summary-grid"><div class="summary"><strong>{build_read.passivePoints ?? "—"}</strong><span>Passive budget</span></div><div class="summary"><strong>{build_read.weaverPoints ?? "—"}</strong><span>Weaver budget</span></div></div>
<p class="note">Point multipliers are session settings. Individual node caps still apply. Existing allocations stay when you lower a budget. Available points stop at zero until the budget covers those allocations.</p></section>
```
- **Data:** `buildread` (`builds::Read`, `native/core/build_controls.cpp`) returns `{ok, settings{key:{value,available}}, counters{…}, summons{…}, player{id,name}?, passivePoints, weaverPoints, skills[{level,gearLevels,maxPoints,unspent,xp}]}`. The player and point fields exist only when the tree is loaded.

### Free respec
```
<section class="panel"><h2>Free respec</h2><p class="note">Refund points from the selected tree group. Levels and earned budgets stay. Each action creates a recovery backup and runs only after confirmation.</p>
<div class="control-grid">{actionCard(respec_skills)}{actionCard(respec_passives)}{actionCard(respec_weaver)}</div></section>
```
- **Cards** (all badge "Game save", button "Apply action" primary):
  - "Refund all skill points": "Reset all specialized skill allocations for free. Skill levels and experience are preserved. Creates a recovery backup before refunding points."
  - "Refund all passive points": "Reset passive allocations for free. Earned points, character level and mastery choice are preserved. Creates a recovery backup first."
  - "Refund all Weaver points": "Reset Weaver tree allocations for free. Earned points and faction progress are preserved. Creates a recovery backup first."
- **Gate:** `saveActionReason(cache.build_read)`.
- **Confirm:** generic game_save confirm.
- **Command:** `treerespec {save_id} skills|passives|weaver`. Reply `{ok, player{id,name}, tree, refunded, backup}`.
- **Afterwards:** notify "{label} updated. A recovery backup is available in Settings."; drop `build_read` and re-read it.

---

## 7. Summons. Complexity: large

**Page order:**
1. **Damage meter**
2. **Death recap**
3. **Live summoned allies**
4. Summon lifetime (ported)
5. Minion counts (ported)
6. "Minion and companion bonuses": not in the in-game page yet. It is a grid of `button.settings-card data-summon-stat=idx` with `<h3>{alias name}</h3><p class="muted">Edit bonus →</p>` for aliases whose name matches `/^minion|^maxcompanions/`. Each opens the §5 dialog on the aliases tab. Muted text: "These controls use the existing character stat editor. Minion tags keep these bonuses separate from your own damage and health."

**On page open (not in preview):** read `damage_session_read`, set `deathCount = r.deaths`, then read the death recap.

### 7a. Damage meter (`damagePanel`)
```
<section class="panel" id="damage-panel"><div class="section-head"><h2>Damage meter</h2><div class="actions">
 <button class="[primary if not active]" data-damage-action="start|stop" data-control-id="damage_session">Start|Stop</button>
 <button data-damage-action="reset" data-control-id="damage_session">Reset</button>
 <button data-damage-csv [disabled if no types]>Copy as CSV</button>
 [<button data-damage-window>Open damage window|Hide damage window</button> DESKTOP]</div></div>
<p class="muted">Records damage by your allies (grouped by type) and your character only while it runs, so it costs nothing when stopped. Values stay after Stop until Reset, also when you switch windows. The damage window stays on top of a windowed or borderless game.</p>
<p class="note" id="damage-status">{status}</p>
<div id="damage-filters"><div class="damage-filters"><label>Damage type <select data-damage-element><option -1>All damage</option>Physical/Fire/Cold/Lightning/Necrotic/Void/Poison (0–6)</select></label>
 [<div class="damage-rows"><span class="muted">Rows</span><label><input type="checkbox" data-damage-row="{key}" checked> {label}</label>…</div>]</div></div>
<div id="damage-live">{table}</div><h3>Damage taken</h3><div id="damage-taken-live">{taken table}</div></section>
```
- **Status** (`EpochDamage.status`): "Not started." or `{Measuring|Stopped} · {m:ss | h:mm:ss} session · {…} in combat · {fmt hits} hits`.
- **Table** (`EpochDamage.table`, `ui/damage-meter.js`):
  - Wrapper: `.table-scroll > table.damage-table`.
  - Headers: Source (or `Source · {element}`), DPS 10 s, DPS 60 s, DPS 10 min, DPS 60 min, Avg in combat, Avg session, Total damage, Hits, Biggest hit.
  - **Rows:**
    - Allies sorted by total descending; label = `label` (or the readable object name) plus ` ×{live}` when live>1.
    - Then "Allies total" (`tr.damage-total`, only if more than one ally).
    - Then "You (character)".
  - **Element filter:** each value is `row.elements[k][element]`; averages are recomputed from the elapsed or combat seconds.
  - **Number format:** ≥1e6 → `x.xxM`; ≥1e4 → `x.xk`; otherwise rounded en-US; non-finite → "—".
  - **Empty texts:** "Every row is hidden. Choose the rows to show." / "No damage recorded yet. Start the session and fight."
- **Taken table:** compact columns (DPS 10 s, DPS 60 s, Avg in combat, Total damage). Uses `EpochDeathRecap.takenState(state)`. Labels: "Attacker", total row "All attackers", self row "You (self-inflicted)". Empty: "No damage taken yet. Start the session and fight."
- **Data:** `damagesession [start|stop|reset]` (`convenience::DamageSession`, `native/core/convenience.cpp` `DamageState`):
  ```
  {ok, active, elapsedSeconds, combatSeconds, hits, unsplitHits, elementNames[7], hooked,
   types:[{name, label, icon:"a:<skill>|c:<class>:<mastery>|", live, total, hits, maxHit, dps10, dps60, dps600, dps3600, average, combatAverage,
           elements:{total[7], hits[7], maxHit[7], dps10[7], dps60[7], dps600[7], dps3600[7]}}],   // "You" is the player
   allies:{same fields + elements},
   taken:{types[...same + dot], total, hits, maxHit, dot, combatSeconds, dps…, average, combatAverage, elements},
   deaths, deathRecap}
  ```
- **Actions:** Start, Stop and Reset send `damage_session {action}`. The reply replaces the cache and the panel is redrawn.
- **Filter persistence:** `epochpact.damage-panel.live` = `{element:-1..6, hidden:[row keys]}` (row keys are the type name, `__total` or `You`).
- **Copy as CSV:**
  - First line: `# EpochPact damage session; {status}`.
  - Header: `Source,DPS 10 s,…,Biggest hit,Physical damage,…,Poison damage`.
  - Values use `toFixed(1)`; text is quoted when it contains `,` `"` or a newline.
  - Copied to the clipboard; notify "Damage table copied as CSV."
  - In game: use the Win32 clipboard or Unity `GUIUtility.systemCopyBuffer`.
- **Polling:**
  - Every 1 s while on the summons page and visible and not busy: read `damage_session_read` if the session is active, otherwise only every 3 s.
  - `updateDamageView` replaces the table, status and taken table.
  - If active and the Start/Stop button disagree, the whole panel is redrawn.
  - If the row keys change, the filters are rebuilt.
  - If `deaths` changes (or the recap has not been read yet), the death recap is re-read.
- **CSS:**
  - `.damage-filters{display:flex;flex-wrap:wrap;gap:8px 18px;align-items:center;margin:4px 0 10px}`; its select `width:auto`.
  - `.damage-rows{display:flex;flex-wrap:wrap;gap:4px 14px}`.
  - `.damage-table td,th{white-space:nowrap}`; `tr.damage-total td{font-weight:700;border-top:1px solid #92734080}`.
- **DESKTOP-ONLY:** the damage window button (`pywebview open_damage_window`/`hide_damage_window`). In game this becomes a toggle for an in-game HUD panel; see §17.7.

### 7b. Death recap (`deathPanel` + `ui/death-recap.js`)
```
<section class="panel" id="death-panel"><div class="section-head"><h2>Death recap</h2><div class="actions"><button data-death-refresh data-control-id="death_recap_read">Refresh</button><button data-death-clear data-control-id="death_recap_clear">Clear</button></div></div>
<p class="muted">When your character dies, the hits of the 10 seconds before it are kept: who hit, with which skill or ailment, the damage by type and the health left after each hit, with the buffs and debuffs you had. Kept until the game closes; the damage window shows it under ☠.</p>
<div class="control-grid">{settingCard(death_recap)}</div><div id="death-live">{recap html}</div></section>
```
- **Setting:** `death_recap` "Record deaths" is an automatic toggle; command `deathrecap 0|1`.
- **Data:** `deathrecap [0|1|clear]` returns
  ```
  {ok, enabled, recording, recentHits,
   deaths:[{number, secondsAgo, maxHealth, total, dot, unsplit, byType[7], killer{attacker,what,dot,damage}|null,
            attackers[{name,damage,dot,hits}], hits[{t(≤0 s), attacker, what, dot, damage, health, ward, parts[7]}],
            effects[{name,stacks,buff}]}]}
  ```
  Deaths are newest first; the app shows at most 10.
- **Recap HTML** (one death, `index` 0 = newest):
  ```
  <div class="dr"><div class="dr-head"><span>Death <b>{number}</b></span><span class="muted">{N s ago | N min ago (≥90 s)}</span>[<span class="dr-nav"><button data-death-step="1" [disabled at oldest]>‹ Older</button><button data-death-step="-1" [disabled at newest]>Newer ›</button></span>]</div>
  <p class="dr-killer">Killed by <b>{attacker}</b>[ · {what}] · {Damage over time|Hit} <b>{damage}</b></p>   | "The killing blow was not recorded."
  <p class="dr-sum">Damage taken in the last 10 s <b>{total}</b>[ · damage over time <b>{dot}</b>][ · maximum health <b>{maxHealth}</b>]</p>
  [<div class="dr-types"><span class="dr-type"><i class="dm-ico dm-t{i}"></i><span>{Type}</span> <b>{v}</b> <small>{share%}</small></span>…</div>]
  [<p class="dm-note">Some hits are not split by damage type.</p>]
  [<h4>By attacker</h4> table.dr-table: Attacker | Damage (+ "<small>(damage over time N)</small>") | Share | Hits]
  [<h4>Last hits</h4> table.dr-table.dr-hits: Time ("t s" or "from … t s") | Attacker | Skill or ailment ("—", "<small>DoT</small>", "<small>×n</small>") | Damage | Health after (+ "<small>+ N ward</small>"); rows with health ≤0 get tr.dr-fatal]
  <h4>Buffs and debuffs at death</h4> <ul class="dr-effects"><li class="dr-buff|dr-debuff">{name}[ <small>×{stacks}</small>]</li></ul> | <p class="muted">None shown by the game.</p></div>
  ```
  - **Last hits:** newest first, at most 15 lines. Consecutive DoT ticks from the same attacker and the same `what` merge into one line (count, summed damage, `from` time).
  - **Number formatting:** `Intl.NumberFormat` in en-US, tr or ru (whole numbers; times with one decimal).
  - **Damage type order:** Physical, Fire, Cold, Lightning, Necrotic, Void, Poison. Icons: `.dm-ico` sprite `/ui/assets/damage-types.png` (7 frames, `background-size:700% 100%`, `.dm-t1`…`.dm-t6` positions 16.667% steps), 1.3em.
  - **Empty or error states:**
    - `reply.error` or "Waiting for an offline character…"
    - enabled===false → "The death recap is off. Turn it on to keep the hits before each death."
    - otherwise "No deaths recorded since the game started."
- **Actions:**
  - Refresh: read `death_recap_read` (`deathrecap`).
  - Clear: `death_recap_clear` (`deathrecap clear`), no confirm; reset to the newest death.
  - Older/Newer change the index.
- **CSS:**
  - `.dr{display:grid;gap:.45em}`; `.dr-head{display:flex;flex-wrap:wrap;gap:.4em .8em}` (first span gold); `.dr-nav{margin-left:auto}` with buttons `1px 8px .85em`.
  - `.dr-killer b{color:#e6a08f}`; `.dr-types{display:flex;flex-wrap:wrap;gap:.3em .8em}`.
  - `.dr h4{margin:.4em 0 0;font-size:.9em;font-weight:600;color:gold}`.
  - `.dr-table td,th{padding:2px 6px;border-bottom:1px solid rgba(183,198,175,.12)}`; `.dr-fatal td{color:#e6a08f}`.
  - `.dr-effects{display:flex;flex-wrap:wrap;gap:4px;list-style:none}`; `li{padding:1px 7px;border:1px solid var(--line);border-radius:3px}`; `.dr-buff` border `#5f9e7a80`; `.dr-debuff` border `#c0706080`.
  - `.dm-note{margin:.45em 0 0;font-size:.75em;opacity:.7}`.

### 7c. Live summoned allies
```
<section class="panel"><div class="section-head"><h2>Live summoned allies</h2><button data-refresh="summons_read" data-control-id="summons_read">Refresh allies</button></div>
<details class="display-options"><summary>Visible columns</summary><div class="column-options"><label><input type="checkbox" data-summon-option="individual">Show each ally separately</label>{13 column checkboxes}</div></details>
<div class="actions"><button id="summon-baseline">Save damage baseline</button><button id="summon-baseline-clear">Clear baseline</button></div>
<div id="summon-live">{table}</div></section>
```
- **Columns:** Measured DPS, Last damage, DPS change, Health, Armour, Damage increase, Physical, Fire, Cold, Lightning, Void, Necrotic, Poison.
  - Hidden columns are stored in display preferences `summonColumns`; "individual" is stored in `summonOptions`.
- **Table:**
  - Wrapper: `.table-scroll > table.summon-table`.
  - First header: "Ally type" (or "Ally" in individual mode).
  - Cell: `{label}[ <span class="muted">×{count}</span>]`.
  - **Grouping:** by name with "(Clone)" removed (default "Ally"). measuredDps and health/maxHealth are summed; lastDamage is the max; armour, damageIncreased and resistances are averaged.
  - **Formats:** numbers en-US with at most 1 decimal, otherwise "—". Health shows `h / max`. Damage increase and resistances end with `%`. DPS change shows `+x` or `-x`, or "—" without a baseline.
- **Text states:**
  - preview: "Load an offline character to inspect real summoned allies."
  - snapshot not current: "Waiting for live summoned allies…"
  - readError: "Live summon data is unavailable. Previous values are not shown as current."
  - empty: "No active summoned allies found."
  - Clipped samples add `<p class="note">Damage event capacity was reached. These numbers are a lower bound until a fresh five-second window is collected.</p>`.
- **Footer muted text:** "Measured DPS uses damage actually applied in a rolling five-second window ({damageWindowSeconds} seconds collected), including mitigation and critical hits. Attack a target to measure damage; changing gear alone cannot reveal exact combat DPS. Compare on the same target and encounter. Damage increase remains a separate bonus. [Grouped rows add damage and health across the group and average armour, damage increase and resistances. ]Updates every two seconds while visible."
- **Data:** `summonread` (`convenience::Summons`) returns `{ok, player, sampledAt, summons:[{name, instance, health, maxHealth, armour, resistances{Physical,Fire,Cold,Lightning,Void,Necrotic,Poison}(×100), damageIncreased(×100), damage5s, damageEvents5s, lastDamage, measuredDps}], damageWindowSeconds, damageSampleClipped, damageMethod}`.
- **Polling:** every **2000 ms** while on the summons page (same gating as 3a); only `#summon-live` is replaced.
- **Baseline:**
  - Save: if there is no current data, show the error "Refresh live summons before saving a damage baseline."; otherwise store `{instance: measuredDps}` with the owner and notify "Damage baseline saved for these active allies. Compare against the same target."
  - Clear empties the baseline.
  - The baseline resets when the owner changes. It is kept in memory only.
- **CSS:**
  - `.display-options{margin:14px 0}`; `.column-options{display:flex;gap:16px;flex-wrap:wrap;padding:16px 0}`; its labels flex, gap 8; its inputs `width:auto`.
  - `.summon-table{font-size:13px;white-space:nowrap}`; `th,td{padding:12px 14px;border-bottom:1px solid #665a3b}`; `th{color:accent;font-weight:600}`.

---

## 8. Display & Camera

### 8a. Tier colours (`tierPalette` + `ruleColorRow`). Complexity: medium (needs a colour picker widget)
```
<section class="panel"><div class="section-head"><h2>Tier colours</h2><button data-tier-reset data-control-id="tier_color">Reset colours</button></div>
<p class="muted">Used for tier digits on icons and coloured tooltip lines. Your colours are remembered on this device and applied when a character is ready.</p>
<div class="tier-palette"><label>T1<input type="color" data-tier-color="1" value="#FFFFFF" [disabled if !eligible]></label> … T8</div>
<div data-id="tier_color" hidden></div>
<div class="rule-color-row"><label>Rule line colour<select data-rule-color-mode><option value="label">Like the item's label on the ground</option><option value="custom">One colour for every rule</option></select></label><label>Colour<input type="color" data-rule-color value="#{custom||FFD27F}" [disabled unless custom]></label></div>
<p class="muted">The loot filter rule line in item tooltips. Like the label: the rule's own colour when it recolours the item's label, otherwise the item's colour (grey for Hide rules).</p><div data-id="tier_rule_color" hidden></div></section>
```
- **Default palette** (rarity order normal, magic, rare, exalted, unique, set, legendary, T8): `FFFFFF 2E8BFF FFD700 AA55FF FF5500 00FF00 FF0044 00FFFF`.
- **Storage:** palette in `epochpact.tier-palette.v1` (array of 8 hex strings); rule colour in `epochpact.rule-line-color.v1` ("label" or hex).
- **Changing a tier colour:** store it, then send `tier_color {tier, color}` (`tiercolor {1-8} {RRGGBB}`), silent.
- **Changing the rule mode or colour:** store it, then send `tier_rule_color {color}` (`tiercolor rule {RRGGBB|label}`).
- **Reset:**
  1. Remove both keys.
  2. Send `tier_color` with operation reset (`tiercolor reset`).
  3. Notify "Tier colours reset." and repaint.
- **Re-apply (`applyTierPalette`):** once per character owner, when the connection becomes offline-ready, send every non-default tier and the rule colour if it is not "label".
- **Bridge validation:** `^[0-9A-Fa-f]{6}$` (rule colour also accepts `label`).
- **CSS:**
  - `.tier-palette{display:grid;grid-template-columns:repeat(auto-fill,minmax(88px,1fr));gap:10px}`; labels `display:flex;gap:8px;font-size:12px`; colour input 44×30, padding 2.
  - `.rule-color-row{display:flex;flex-wrap:wrap;gap:12px 18px;align-items:flex-end;margin-top:16px}`; labels column, gap 6, 12px muted; select `width:auto`; colour input 56×34.
- **DESKTOP-ONLY:** the palette lives in localStorage (preferences.json). In game it can be read from and written to that file, or kept in `<game>\EpochPact\`. The in-game version also needs its own colour picker (HSV or hex field plus swatches); Unity UI has none.

### 8b. Interface font: actual heading "Text and font" (`fontPanel`). Complexity: medium
```
<section class="panel"><div class="section-head"><h2>Text and font</h2>[<button data-refresh="font_read" data-control-id="font_read">Load Windows fonts</button> if no families]</div>
<p class="muted">{font_family description} Every style a font has (Bold, Light, Semibold, Italic …) is listed. The choice is undone when you choose Game font or close EpochPact.</p>
<label>Search fonts<input type="search" data-font-search placeholder="Font name or style" [disabled until loaded]></label>
<label>Interface font <small class="muted" data-font-count>{n} fonts</small><select data-font-family data-control-id="font_family" [disabled until loaded]><option value="game">Game font</option>{faces}</select></label></section>
+ settingPanel("Text size and letter case", [font_scale, font_case])   ← also pending in game, automatic (fontset)
```
- **Description:** "Use a Windows font for the game's interface text; game restores the original fonts. Characters the font lacks use the game's font."
- **Data:** `fontread` returns `{ok, family, scale, keepCase, texts, applied, enables, restored, fontsCreated, faults, families[], styles{family:[styles]}}`.
- **Faces list:** for each family, `[family, family]` plus, for each style that is not Regular/Normal/Book/Roman, `["Family|Style", "Family — Style"]`.
- **Search:** case-insensitive substring on the label; the current choice always stays in the list. The count text updates as you type.
- **Change:** set `values.font_family`, then send `font_family {family}` with spaces encoded as `%20` and `|` as `%7C`. Command `fontset family {encoded}`, decoded natively in `fonts.cpp`; bridge regex `[A-Za-z0-9%._'&()+-]{1,96}`; silent. Desktop also replays it at startup.
- **Current value:** `values.font_family`, default "game". It is not read back from fontread.

---

## 9. World & Exploration

### 9a. Interaction area colour (`areaColorPanel`). Complexity: small
```
<section class="panel"><h2>Interaction area colour</h2><p class="muted">Colour of the drawn areas (six hexadecimal digits, for example 3FD0FF).</p>
<div class="rule-color-row"><label>Colour<input type="color" data-area-color value="#{stored||3FD0FF}" [disabled if !eligible(qol_areacolor)]></label></div><div data-id="qol_areacolor" hidden></div></section>
```
- **Change:** store in `epochpact.area-color.v1`, then send `qol_areacolor {color}` (`qolarea color RRGGBB`), silent. The reply is qol state.
- **Re-apply:** on every offline-ready connection, if the stored colour is not 3FD0FF, send it directly.
- **CSS:** same `.rule-color-row` as §8a.

### 9b. Rage facts note (inside the ported "Rage of Morditas (Season 5)" panel). Complexity: small
- **Markup:** `<p class="note" id="rage-facts">{rageFacts()}</p>`, placed between the muted text and the control grid.
- **Text:** `Now: hunt {game.configs[0].huntSeconds ?? "—"} s · Bloodrage charges at {game.chargeKills.join(", ")} kills · hunt buffs at {game.buffKills.join(", ")} kills.`
- **Empty case:** the string is empty when `rage_read.game` is missing. The empty `.note` box still renders.
- **Data:** `rageread`, and every `rageset` reply (`native/core/rage.cpp` `State`):
  ```
  {ok, available, offline, settings{spawn,hunttime,charges,buffs,blood:{value,available}}, applied, writes, faults, error,
   game?{minimumLevel, campaignRate, monolithRate, chargeKills[], buffKills[], forging[4], configs[{type, levels[2], huntSeconds, championSeconds, killSeconds[3]}]},
   original?{…}, encounter?{state, kills, charges, elapsed, finalTime, buffs}}
  ```
- **When it is read:** on world page open (not in preview, eligible) and on every rageset reply. The text is rewritten in place.

---

## 10. Inventory (whole page). Complexity: medium-large

**Reads on open:** `inventory_read` (`inventoryread`) and `inventory_tools_read` (`invread`).

### 10a. Inventory shortcuts
```
<section class="panel"><div class="section-head"><h2>Inventory shortcuts</h2><button data-refresh="inventory_read" data-control-id="inventory_read">Refresh inventory</button></div>
<p class="note">{inventoryItems != null ? "{n} inventory items. Stash all uses the game's placement: tabs whose stash priority matches an item first, otherwise the selected tab; items that do not fit stay in your inventory." : "Refresh inventory for the loaded character."}</p>
<div class="control-grid">{actionCard ×4}</div></section>
```
- **Cards:**
  - inventory_stash: "Toggle stash"; View; "Apply".
  - inventory_vendor: "Toggle vendor"; View; "Apply".
  - inventory_stashall: "Stash all inventory items"; Game save; "Apply action"; confirm.
  - inventory_sellall: "Sell all inventory items"; Game save; "Apply action"; confirm.
  - Descriptions are in `ui/locale-en.json`. stash, vendor and stashall share the text "Use the game's normal inventory interface and transactions. Stash all creates a recovery backup and leaves items that cannot be moved in your inventory."
- **Commands:** `inventoryaction {save_id} stash|vendor|stashall|sellall`, with `expected_id` and `expected_name` from `inventory_read.player`.
- **Replies:**
  - inventoryread: `{ok, player{id,name}, inventoryItems}`
  - stash/vendor: `{ok, player, action, opened}`
  - stashall: `{ok, player, moved, remaining, backup}`
  - sellall: `{ok, player, sold, kept, refused, gold, openedVendor, backup}`
- **Notifications:**
  - stash: "Stash opened in the game." / "Stash closed in the game." (based on `opened`)
  - vendor: "Vendor opened in the game." / "Vendor closed in the game."
  - stashall when remaining>0: `Moved {moved} items to your stash. {remaining} items stayed in your inventory: the stash had no room for them. Free stash space or add a stash tab, then try again.`
  - stashall otherwise: `Moved {moved} items to your stash. Your inventory is empty; equipped items were not moved.`
  - sellall: `Sold {sold} items for {gold toLocaleString} gold. {kept} items kept[, {refused} the vendor does not buy]. Sold items are in the vendor's buyback list.`
  - plus the backup text.
  - There is no automatic re-read afterwards.

### 10b. Automatic sorting and Sell all (`autoSortPanel`)
```
<section class="panel" id="auto-sort"><div class="section-head"><h2>Automatic sorting and Sell all</h2></div>
<p class="muted">Sorting uses the game's own Sort: your inventory each time it opens, a stash tab each time it is shown. These choices are remembered by EpochPact.</p>
<div class="control-grid">{settingCard(inventory_autosort)}{settingCard(inventory_autosortall)}{settingCard(inventory_sellkeep)}</div>
<h3>Stash tabs sorted automatically</h3><div id="sort-tabs">{autoSortTabs}</div></section>
```
- **Settings:** all automatic, using the existing setting-card widgets.
  - `inventory_autosort`: toggle, `invset autosort 0|1`.
  - `inventory_autosortall`: toggle, `invset autosortall 0|1`.
  - `inventory_sellkeep`: select card with choices 0, 1, 2 labelled "Sell everything" / "Keep Unique, Set and Legendary" / "Keep Unique, Set, Legendary and Exalted"; `invset sellkeep n`.
- **invread reply:** `{ok, autosort, autosortAll, sellKeep, tabs[ids], inventorySorts, tabSorts, lastSortedTab, faults, error}`.
- **`autoSortTabs`:**
  - Needs `stashRules` for the current player, otherwise: `<p class="empty">Read your stash tabs to choose which ones are sorted.</p><div class="actions"><button data-stash-rules="read" data-control-id="stash_rules_read">Read stash tabs</button></div>`.
  - No tabs: `<p class="empty">You have no stash tabs yet.</p>`.
  - Otherwise: `<div class="sort-tabs"><label class="inline-check"><input type="checkbox" data-sort-tab="{id}" data-control-id="inventory_sorttab" [checked if in tabs or autosortAll] [disabled if autosortAll]> {name||"Tab {id}"}</label>…</div>`. `.sort-tabs` has no CSS of its own.
  - Change: `inventory_sorttab {tab, value}` (`invsorttab {tab} 0|1`); the reply is the same invread JSON.

### 10c. Stash tabs from loot filter (`stashRulesPanel`)
```
<section class="panel" id="stash-rules"><div class="section-head"><h2>Stash tabs from loot filter</h2><button data-stash-rules="read" data-control-id="stash_rules_read">{loaded ? "Refresh" : "Read filter and tabs"}</button></div>
<p class="muted">One stash tab per loot filter rule, free of gold, in a folder named after the filter. Each tab gets the rule's rarity, type, class, item level, potential, faction, unique and corruption conditions as its stash priority, so the game's own stashing puts matching items there. Rules with affix or character-level conditions match more broadly in the tab priority; <b>Sort inventory by filter rule</b> uses the exact filter match. Every change creates a recovery backup.</p>
```
- **States:**
  - Not read: `<p class="empty">Select a loot filter in the game, then read it here.</p>`.
  - No filter: `<p class="note">No loot filter is active. Select one in the game's loot filter window.</p>`.
  - Loaded:
    - `<p class="note">Filter: <b>{filter}</b> · {rules} rules · {tabs} stash tabs[ · the game's tab limit is reached if !canAddTab].</p>`
    - `.table-wrap > table.stash-rules-table` with headers Rule | Type | Name | Stash tab | Matching. Row `tr[.muted if disabled]`:
      - number
      - `Show|Hide|Highlight` (types 0, 1, 2) plus " · off" when disabled
      - name or "—"
      - `<select data-stash-rule-link="{number}" data-control-id="stash_rule_assign" aria-label="Tab for rule {n}"><option value="0">—</option>{tabs}</select>`
      - unmapped conditions → `<span class="badge" title="…">Sort only: A, B</span>`; otherwise "Tab priority" if any mapped, else "—"
    - `<div class="actions"><label class="inline-check"><input type="checkbox" id="stash-rules-hidden"> Include Hide rules</label><label class="inline-check"><input type="checkbox" id="stash-rules-disabled"> Include disabled rules</label></div>`
    - `<div class="actions"><button class="primary" data-stash-rules="create" data-control-id="stash_rules_create">Create tabs for rules without a tab</button><button data-stash-rules="sort" data-control-id="stash_rules_sort">Sort inventory by filter rule</button></div>`
- **CSS:** `.stash-rules-table select{min-width:150px}`; `.stash-rules-table tr.muted td{opacity:.65}`.
- **Commands:** every one carries `expected_id` and `expected_name` from `connected.player`.
  - read: `stashrules {save_id}`
  - create: `stashrulescreate {save_id} {hidden 0|1} {disabled 0|1}`
  - assign: `stashruleassign {save_id} {tab} {rule}` (rule 0 clears)
  - sort: `stashrulesort {save_id}`
- **Replies:**
  - Base state (`stash_rules.cpp` `State`): `{ok, player{id}, tabCount, canAddTab, folders[{id,name}], tabs[{id,items,name,folder,color,priority}], filter|null, rules[{number,type,enabled,name,mapped[],unmapped[],tab|null}]}`.
  - create adds `created, existing, skipped, atLimit, notExact`.
  - sort adds `moved, full, unmatched, unlinked`.
- **Confirm before create/sort/assign.** The title is the control label ("Create tabs from loot filter" / "Sort inventory by filter rule" / "Link a tab to a rule"). Messages:
  - create: `Create one free stash tab for each [Show/Highlight ]rule of “{filter}” that has no tab yet, in a folder named after the filter. A recovery backup is created first.` ("Show/Highlight " is omitted when Include Hide rules is ticked.)
  - sort: `Move {name}'s inventory items into the tabs linked to the loot filter rule they match. Items without a linked rule stay in the inventory. A recovery backup is created first.`
  - assign: `Give this stash tab the conditions of rule {n} as its stash priority. A recovery backup is created first.`
  - clear: `Remove the rule link and stash priority from this tab. A recovery backup is created first.`
  - Cancelling re-renders the panel, which reverts the select.
- **Select logic:**
  - A non-zero tab → assign `{tab, rule:number}`.
  - "—" → assign `{tab: previousTab, rule:0}`, only if there was a previous tab.
- **Notifications:**
  - create: `{1 tab created | N tabs created}[, N already linked][, N over the game's tab limit].`
  - sort: `{1 item moved | N items moved}[, N did not fit][, N match a rule without a tab].`
  - assign: `Tab linked to rule {n}.` / `Tab link removed.`
- **Afterwards:** repaint this panel and the sort-tab list. The state is kept in memory (`stashRules`), tied to the player id.

---

## 11. Unique Atlas and Stash Assistant (`ui/collection-ui.js`). Complexity: large

**Opening resets the view state:** filter "all", page 0, no selection. Nothing is read automatically.

### Render
```
<section class="panel collection-browser"><div class="section-head"><div><span class="eyebrow">{COLLECTION BOOK|OWNED EQUIPMENT}</span><h2>{Unique Atlas|Stash Assistant}</h2></div>
 <button class="primary" data-collection-refresh="{atlas_read|stash_read}" data-control-id>↻ {data ? "Refresh collection" : "Scan collection"}</button></div>
<p class="muted">[{player.name} · {tabs} stash tabs · ]Scans the loaded stash, bags and equipment. Includes the Forge and Eternity Cache. Other characters and stashes are outside this scan.</p>
data:  <div class="collection-summary"><div><strong>{owned|total}</strong><span>{Owned types|Equipment pieces}</span></div><div><strong>{total-owned|duplicateGroups}</strong><span>{Missing types|Duplicate Unique groups}</span></div><div><strong>{wishlist size|protected size}</strong><span>{On your wishlist|Local protection markers}</span></div></div>
       <div class="collection-toolbar"><label class="collection-search">{Search items|Search groups}<input id="collection-search" type="search" placeholder="Name, base type or affix…"></label>
        <label>Show<select id="collection-filter">atlas: all "All items", owned "Owned", missing "Missing", wishlist "Wishlist", set "Set items" | stash: all "All equipment", duplicates "Duplicate Uniques", protected "Protected reminders"</select></label>
        [atlas: <label>Base type<select id="collection-base"><option value="all">All types</option>{sorted unique baseTypes}</select></label>]</div><div id="collection-results"></div>
no data: <div class="empty collection-empty"><span>✦</span><h3>Your collection starts here</h3><p>Load an offline character, then scan. Nothing is sold, moved or changed.</p></div>
```

**Results:**
```
<div class="stat-results-head"><span>{N types|N groups} · Snapshot[ · Preview]</span><small>Refresh after moving or acquiring items.</small></div>
<div class="atlas-grid">{cards}</div>
<div class="pagination"><button data-collection-page="-1">← Previous</button><span>Page {p} of {max}</span><button data-collection-page="1">Next →</button></div>
[<section id="copy-comparison" class="copy-comparison"><div class="section-head"><h2>{name}</h2><button data-collection-close>Close comparison</button></div><p class="muted">No universal “best” score: compare LP and the modifiers your build needs. Protection is an EpochPact reminder and does not prevent selling in game.</p><div class="copy-grid">{itemDetails sorted lp desc}</div></section>]
```
- **Paging:** 24 per page (atlas), 16 (stash). Sorted by `name.localeCompare(...,"en")`.
- **Search:** lower-case substring over name, baseType and every copy's affix names.
- **Atlas filters:** base type, then owned (count>0), missing, wishlist, set.
- **Stash filters:** duplicates (unique && count>1), protected (any item key protected).
- **Atlas card:**
  ```
  <article class="atlas-card owned|missing"><div class="section-head"><span class="badge">{Set|Unique} · {baseType}</span><button class="wishlist-button" data-wishlist="{id}" aria-pressed>{★|☆}</button></div><h3>{name}</h3>
  <div class="atlas-status">{count ? '<span class="owned-label">✓ {count} owned</span><strong>{Set item | "Legendary / Weaver" if bestLP null | "{bestLP} LP best"}</strong>' : '<span class="muted">Not in this scan</span>'}</div>
  <div class="atlas-bottom"><small>Level {level} · {Random drop eligible|Special acquisition}</small>[<button data-collection-select="{id}">View copies →</button>]</div></article>
  ```
- **Stash group:** `<button class="stash-group [selected]" data-collection-select="{key}"><span class="badge">{Unique / Set copies|Same base comparison}</span><h3>{name}</h3><span>{count} {copy|copies}[ · ◆ Protected]</span></button>`.
- **Item card** (`itemDetails`):
  ```
  <article class="copy-card [protected]"><div class="section-head"><h3>{name}</h3><button data-protect="{key}" aria-pressed title="Local reminder; does not lock an item in game">{◆ Protected|◇ Mark protected}</button></div>
  <strong class="copy-value">{lpLabel}</strong><p class="muted">{source}[ · Tab {tab+1}] · ({x+1}, {y+1})[ · Corrupted]</p>
  <ul class="affix-list"><li><div><span><b>T{tier}</b> {name|"Affix {id}"}[ · Sealed]</span><small>{round(roll/255*100)}% roll</small></div><meter min=0 max=255 value=roll></meter></li>… | <li class="muted">No regular affixes.</li></ul>
  [<div class="unique-rolls"><small>Unique modifiers · actual game values</small><li><div><span>{name|"Unique effect"}[ · Increased| · More]</span><strong>{value ≤2dp}{%}</strong></div>{canRoll ? '<small>Range {min}–{max}{%}[ · Lower is better]</small><meter max=255 value=roll>' : '<small>Fixed modifier</small>'}</li></div>]
  <small class="muted">Roll % is position in the modifier’s range, not its stat value.</small></article>
  ```
  - **lpLabel:** Legendary → "Legendary · LP used"; Set → "Set item"; weaversWill → `Weaver’s Will {n}`; unique → `{lp} LP`; otherwise `{fp} FP`.

### Data (Python composite, `tools/collection_backend.py`; must become native)
- **`read_items()`:**
  - Loops `stashread {offset}` pages (≤100 items per page, at most 656 pages).
  - Each page: `{ok, player, scope, revision, total, tabs, offset, next|null, items[], readMs}`.
  - Player, scope, revision and total must stay constant across pages. Otherwise: "Items or the loaded character changed during the scan. Refresh again."
  - The combined count must equal total. Otherwise: "Incomplete collection scan; no partial result was displayed."
- **Item fields** (`native/core/collection.cpp`): `{key, fingerprint, name, baseType, type, subType, uniqueId|null, kind:"Legendary|Set|Unique|Equipment", rarity, lp, weaversWill, fp, corrupted, source, tab, x, y, quantity, affixes[{id,name,tier,roll,sealed}], uniqueRolls[], implicitRolls[], uniqueMods[{name,value,min,max,percent,mode,canRoll,roll,lessIsBetter}]}`.
- **`atlas`:**
  - `atlasread` returns `{ok, player, items[{id,name,type,baseType,set,randomDrop,level}]}`.
  - Plus `read_items`, grouped by uniqueId: `count = sum(quantity)`, `bestLP = max lp of kind Unique without weaversWill`, `copies[]`.
  - Result: `{ok, player, scope, revision, items, owned, total, pages, tabs, maxPageMs}`.
- **`stash`:**
  - Group key `unique:{uniqueId}` or `base:{type}:{subType}`.
  - Group name: the unique name, or `"{baseType} · {name}"`.
  - Group fields: `{key, name, unique, count, items}`; the result also has `duplicateGroups`.
- **Peer cache:** after one scan, the other page's cache is dropped if scope, revision or player differ.

### Local markers (DESKTOP-ONLY storage)
- Key `epochpact.collection.v1.{scope}` holds `{wishlist:[atlas ids], protected:[item keys]}`.
- Save failure: "Could not save the local marker. Your game items are unchanged."

### Stash page extra: "In-game stash search" (`stashSearchHelp`)
```
<section class="panel"><h2>In-game stash search</h2><p class="muted">Copy a query and paste it into the game’s stash search box. These are search expressions, not gameplay commands.</p>
<div class="search-recipes">{<article><code>{q}</code><span>{note}</span><button type="button" data-copy-stash-query=i>Copy</button></article>}</div>
<p class="muted"><code>&amp;</code> combines requirements; <code>|</code> accepts either condition. Tooltip text searches use the game’s current language.</p><a href="https://support.lastepoch.com/hc/en-us/articles/46361652732955-Stash-Searching">Official stash search reference</a></section>
```
- **The 8 recipes:**
  - `LP2+` — "Uniques with at least 2 Legendary Potential"
  - `WW18+` — "Weaver uniques with at least 18 Weaver’s Will"
  - `FP30+` — "Equipment with at least 30 Forging Potential"
  - `exalted&2T6+` — "Exalted equipment with at least two tier 6 or higher affixes"
  - `2T7` — "Items with at least two tier 7 affixes"
  - `LP1+&boots` — "Boots with at least 1 Legendary Potential"
  - `CoF&unique` — "Circle of Fortune tagged uniques"
  - `2hsword&exalted` — "Exalted two-handed swords"
- **Copy:** writes to the clipboard and notifies "Copied. Paste into the game’s stash search box." On failure: "Clipboard unavailable. Select the query text and copy it manually." (error). In game, the copy can be the Unity clipboard, or the query could be typed directly into the game's stash search field.
- **CSS:** `.search-recipes{display:grid;gap:10px}`; `article{display:grid;grid-template-columns:minmax(150px,1fr) minmax(180px,2fr) auto;gap:12px;padding:12px;border:1px solid #92734035;border-radius:4px}`.

### collection.css values
- `.collection-summary{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin:24px 0}`; each `>div{border:1px solid var(--line);padding:18px 22px;background:linear-gradient(135deg,var(--soft),transparent);border-radius:5px}`; strong `30px Georgia accent mb 8`; span 12px muted.
- `.collection-toolbar{display:flex;gap:14px;align-items:end;margin:20px 0}`; labels min-width 160, 12px muted; `.collection-search` flex 1; its inputs and selects block, mt 7.
- `.atlas-grid,.echo-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:14px;margin:14px 0 24px}`.
- `.atlas-card,.stash-group,.echo-card{border:1px solid var(--line);border-radius:5px;padding:18px;background:linear-gradient(135deg,var(--soft),#0002)}`; their h3 `20px/1.4 Georgia;margin:16px 0`.
- `.atlas-card.owned{border-color:accent}`; `.missing{background:#0002}`.
- `.wishlist-button{font-size:22px;padding:0 5px;border:0;background:transparent;color:gold}`, pressed = accent.
- `.atlas-status{display:flex;justify-content:space-between;font-size:12px;min-height:20px}`; `.owned-label{color:accent}`.
- `.atlas-bottom{border-top:1px solid var(--line);margin-top:18px;padding-top:14px;display:flex;flex-wrap:wrap;gap:12px;justify-content:space-between}`; small 10px; button `11px;8px 10px`.
- `.collection-empty{padding:50px 20px}`, its span 45px gold.
- `.copy-comparison{border-top:1px solid var(--line);padding-top:24px;margin-top:24px}`.
- `.copy-grid{grid-template-columns:repeat(auto-fill,minmax(270px,1fr));gap:16px}`.
- `.copy-card{border:1px solid var(--line);border-radius:5px;padding:20px;background:#0002}`; h3 18px Georgia; protected = accent border plus `inset 0 0 0 1px soft`.
- `.copy-value{color:gold;font-size:21px;margin-top:20px}`.
- `.affix-list li{padding:10px 0;border-top:1px solid var(--line)}`; row flex space-between, 12px; `b` accent.
- `meter`: 7px tall, track `#ffffff12`, fill accent.
- `.unique-rolls>small{11px gold}`; `li strong{gold}`.

---

## 12. Campaign (whole page). Complexity: medium

**Read on open:** `progress_read` (if not cached). It is re-read on connection refresh while the mastery panel is open.

```
{cachedSummary("progress_read")}   → "Live reading · {name} · Level {level}"
<section class="panel"><div class="section-head"><h2>Campaign and waypoints</h2><button data-refresh="progress_read" data-control-id="progress_read">Refresh live</button></div>
<div class="control-grid">{actionCard(campaign_complete)}{actionCard(waypoints_unlock)}</div>
<details><summary>Quest rewards and coverage (85 quests / 109 waypoints)</summary><p class="muted">Completion grants normal rewards and raises the character to at least level 55. Completed quests do not grant rewards again. Test, repeatable and endgame quests are excluded.</p>
 <p class="muted">{Live quest data|Static coverage catalog}</p><div class="table-wrap"><table><thead><tr><th>Quest</th><th>Type</th><th>XP</th><th>Gold</th><th>Passive</th><th>Idol</th><th>Attribute</th></tr></thead><tbody>{name | Main|Side | xp | gold | passive | idol | attributes}</tbody></table></div></details></section>
<section class="panel"><h2>Mastery</h2><p class="note">{masteryStatus}</p>{actionCard(mastery_choose)}</section>
```
- **Quest rows:** live `progress_read.quests` filtered to `eligible`, otherwise `catalog.optionCatalogs.campaignQuests`. The summary counts come from `catalog.counts` (eligibleCampaignQuests, waypoints).
- **Mastery status:**
  - when available: "Your character has no mastery yet. Open the selection screen, then choose your mastery in the game."
  - otherwise `mastery.reason`, or "Refresh live to check mastery selection for your character."
- **Data:** `progressread` returns `{ok, player{name,id,scene,level,xp}, rewards{…}, quests[{id,name,chapter,type,state,main,visible,test,eligible,routeError,xp,gold,passive,idol,attributes}], waypoints[…], mastery{chosen, needsSelection, questCompleted, available, panelOpen, reason}}`.
  - Mastery reasons from native code: "The character's mastery state is not recognized." / "The mastery quest was not found in this game build." / "Complete The Power of Mastery first, or use Complete campaign." / "Wait for the game interface to finish loading."
- **Cards:**
  - **campaign_complete:** "Complete main + side campaign · level 55 minimum"; Game save; "Apply action".
    - Command `questscomplete {save_id}`.
    - Notify: when `mastery.needsSelection`, "Campaign completed with normal rewards. Use Choose Mastery to finish your character."; otherwise "Campaign completed with normal rewards."
    - Then re-read `progress_read`.
  - **waypoints_unlock:** "Unlock all waypoints"; command `waypointsunlock {save_id}`; notify "All waypoints unlocked."
  - **mastery_choose:** "Choose Mastery"; badge View; button primary "Choose Mastery"; no confirm.
    - Disabled unless `progress_read.mastery.available===true` (reason = mastery.reason) and the save snapshot is current.
    - Command `masteryopen {save_id}`.
    - Afterwards `progress_read.mastery = result.mastery` and repaint. Notify "Mastery selection opened in the game. Choose and confirm your mastery there."
  - Both game_save cards use the generic confirm.

---

## 13. Monolith (`renderMonolith`). Complexity: medium-large (travel composites)

**Page order in app.js:**
1. cachedSummary("monolith_read") (adds " · Monolith editing is unavailable in this area." when `editable===false`)
2. **Echo modifiers** (ported)
3. **"Boss blessing rewards"**: `settingPanel` with `build_blessingoptions` (select 0/3/4/5 labelled "Normal game choices" / "3 choices" / "4 choices" / "5 choices") and `build_blessingrolls` (toggle). **Missing from `tools/ingame_pages.py`.**
4. **Monolith management**
5. **Monolith Navigator** (appended)

**Reads on open:** `monolith_read` and `build_read` (if not cached).

### Monolith management
```
<section class="panel"><div class="section-head"><h2>Monolith management</h2><button data-refresh="monolith_read" data-control-id="monolith_read">Refresh live</button></div>
<p class="muted">Opening a timeline or showing an Echo travels to Traveler's Rest first. Refreshing data does not travel or start an Echo.</p>
<div class="form-grid"><label>Timeline<select id="timeline-target">{timelines by name}</select></label><label>Difficulty<select id="difficulty-target"><option value="normal">Normal</option><option value="empowered">Empowered</option></select></label></div>
<p class="muted">{d ? "Level {level} · {Unlocked|Locked} · Corruption {minCorruption}–{maxCorruption} · Stability 0–{maxStability}" : "Refresh the live timeline catalog."}</p>
<div class="control-grid">{actionCard(monolith_unlock)}{actionCard(monolith_select, hidden timeline+difficulty)}{corruption card}{stability card}</div></section>
```
- **Target:** default timeline = `selected.timeline || timelines[0].id`, difficulty "normal". The difficulty object `d = timeline.difficulties[index 0|1]`. Changing either select repaints.
- **Corruption and stability cards:** `actionCard(c, hidden fields + <label>{Corruption|Stability} · {min??"?"}–{max??"?"}<input name="value" type="number" step="1" min max value="{draft ?? d.run.corruption|stability}" required></label>)`.
  - Typed drafts are remembered per `owner:timeline:difficulty:id` and cleared after a successful apply.
- **monolithread JSON:** `{ok, player{id,name,scene}, editable (scene EoT|MonolithHub|M_Rest), restContextReady, stabilityMultiplier, current, selected{timeline,difficulty}, timelines[{id,name,difficulties[{index,unlocked,level,minCorruption,maxCorruption,maxStability,run{timeline,difficulty,corruption,stability,maxStability}|null}]}]}`.
- **Cards** (all Game save, "Apply action", generic confirm, monolith save gate §0.5):
  - **"Unlock Normal + Empowered timelines":** `monolithunlock {save_id}`. Notify "Normal and Empowered timelines unlocked."
  - **"Select timeline / difficulty":** `monolithselect {save_id} {timeline} {normal|empowered}`. Notify `Timeline selected. {name} ({Normal|Empowered}).`
    - Python composite (`tools/monolith_backend.py` `_ensure_rest`):
      1. If not in M_Rest with `restContextReady`, send `monolithrest {save_id}` (text must contain "normal waypoint transition requested for M_Rest").
      2. Poll `identityread` / `monolithread` every 0.2 s for up to 60 s until the scene is M_Rest and ready.
      3. Then `monolithselect`.
      4. Then wait until `monolithpanelready {save_id} {t} {d}` reports `panelReady` twice in a row (60 s).
    - Errors: "Traveler's Rest has not finished loading. Resume the game and try again." / "The timeline is still loading. The opening request was not repeated."
  - **"Corruption":** `corruption {save_id} {t} {d} {value}`. Notify `Corruption set to {v}. {name} ({difficulty}).`
  - **"Stability":** `stability …`. Notify `Stability set to {v}. …`
  - **Bridge pre-checks:** editable; timeline in catalog; difficulty unlocked ("This timeline / difficulty is not unlocked yet."); value within `[minCorruption,maxCorruption]` or `[0,maxStability]`.
  - **Afterwards:** drop `monolith_read` and re-read.

### Monolith Navigator (`navigatorPanel`, `ui/collection-ui.js`)
```
<section class="panel echo-navigator"><div class="section-head"><div><span class="eyebrow">REWARD FINDER</span><h2>Monolith Navigator</h2></div><button data-echo-refresh data-control-id="echo_read" [data-unavailable=true if no monolith player]>↻ Read Echo web</button></div>
<p class="muted">Search the selected timeline’s existing web. “Show in game” focuses the normal map; it never starts an Echo.</p>
```
- **Body:**
  - Not read, or the read belongs to another target: `<div class="empty">Read the Echo web to search rewards. Read requests do not generate new nodes.</div>`.
  - Read but not generated: `<div class="empty">No web exists for this timeline yet. Open it normally in game, then read again.</div>`.
  - Otherwise a toolbar:
    - `Search rewards or Echo names` input `#echo-search` (placeholder "Example: Unique, Exalted, Arena…")
    - `Reward` select `#echo-reward`: "Any reward" plus the reward names present
    - `State` select `#echo-status`: "All existing Echoes" / "Runnable" / "Uncompleted" / "Completed"
  - Followed by `#echo-results`.
- **Results:**
  - `{matches} matches / {total} existing Echoes` and "Bright nodes match your filters."
  - SVG `svg.echo-map` (viewBox from min/max x,y ±35; y is negated):
    - edge lines;
    - `g.echo-node[.match][.selected]` circles, r 7 (11 when selected);
    - each with `<title>{name} · {reward}</title>`.
  - Up to 200 cards:
    ```
    <article class="echo-card [selected]"><span class="badge">{Completed|Runnable|Not currently reachable}</span><h3>{reward|rewardName|"Special Echo"}</h3><p class="muted">{name} · {stability} base stability</p><div class="actions"><button data-echo-highlight=i>Highlight</button><button data-echo-focus=i data-control-id="echo_focus" data-unavailable="{!monolith_read.editable}">Show in game →</button></div></article>
    ```
  - More than 200: "First 200 matches shown. Narrow your search to see a smaller list."
- **Reward names by `rewardType` index:** Special Echo, Gold, Experience, Rare item, Rare items, Unique item, Unique items, Exalted item, Exalted items, Affix shards, Runes, Glyphs, Idols, Set items, Timeline key, Arena key, Woven Echoes, Dungeon keys, Memory Amber.
- **Commands:**
  - `echoread {save_id} {t} {d}` (Python goes through `_ensure_rest` first) returns `{ok, player, timeline, difficulty(0|1), generated, echoes[{index,x,y,name,reward,rewardType,kind,completed,runnable,stability,connections[]}]}`.
  - `echofocus {save_id} {t} {d} {index}` (Python: `_ensure_rest`; then, if `monolithpanelready` is not ready, `monolithselect` plus wait) returns `{ok, selectionVerified, focused, timeline}`. Notify "Echo focused in the game map. It has not been started."
- **CSS:**
  - `.echo-map{display:block;width:100%;height:340px;border:1px solid var(--line);background:radial-gradient(ellipse,var(--soft),#0004);border-radius:5px;margin:14px 0 22px}`.
  - `.echo-edges line{stroke:var(--line)}`; `.echo-node circle{fill:muted;opacity:.25}`; `.match circle{fill:accent;opacity:1}`; `.selected circle{stroke:gold;stroke-width:4}`.
  - `.echo-grid{max-height:660px;overflow:auto}`.

---

## 14. Circle of Fortune (`renderCof`). Complexity: medium-large

**Read on open:** `cof_read` (if not cached). After a read, the session multiplier values are mapped from `favorMultiplier`, `reputationMultiplier` and `tuning.settings.{charge,reward,exalted,t7,lp,enemy,echo,celerity,charity,duplication}`.

```
{cachedSummary("cof_read")}
<section class="panel"><div class="section-head"><h2>Circle of Fortune</h2><button data-refresh="cof_read" data-control-id="cof_read">Refresh live</button></div>
[<div class="summary-grid"><div class="summary"><strong>{rank}</strong><span>Rank / {maxRank}</span></div><div class="summary"><strong>{favor en-US}</strong><span>Favor balance</span></div><div class="summary"><strong>{reputation en-US}</strong><span>Reputation within rank</span></div><div class="summary"><strong>{Member|—}</strong><span>Circle of Fortune</span></div></div>]
<div class="control-grid">{cof_join + '<label class="inline-check"><input type="checkbox" name="switch_from_merchant"> Leave Merchant’s Guild and join CoF</label>'}{cof_rank}{cof_favor}{cof_reputation_grant}{cof_lenses_unlock}</div></section>
<section class="panel"><h2>Prophecies and lenses</h2><p class="muted">Slots unlock at ranks 1 / 3 / 6 / 9. Choose “none” to clear a reward or lens. Changing a reward resets its charges.</p>
[table: Slot | Reward | Lens | Charges | Status (Locked|Unlocked); reward/lens shown by uiLabel|name|label, "None"]
<div class="control-grid">{cofprophecy + '<div class="actions"><button type="button" data-prophecy-preview data-control-id="cofpreview">Preview changes</button></div>'}{cof_charges}</div>
<details><summary>Lens catalog · 12 lens</summary>{<p><b>{name}</b> · Rank {r}{ · Purchased| · Not purchased| · Static catalog}<br>{effect}</p>}</details></section>
+ settingPanel("Gain, drop and quality multipliers", all cof session controls)  ← draft, "Use Apply changes below"
```

**Fields:**
- cof_rank: number 1–12, default the current rank.
- cof_favor: number 0–999999, default the current favor.
- cof_reputation_grant: number 0–1000000, default 0.
- cofprophecy and cof_charges:
  - `<select name="slot">`: unlocked slots, labelled `Slot {i+1} · Rank {rankRequired}`.
  - reward: `<select name="reward">` "Clear selection (none)" plus available rewards (uiLabel), preselecting the first unlocked slot's reward.
  - lens: `<select name="lens">` "Clear selection (none)" plus lenses that are purchased and `rankRequired ≤ rank`.
  - cof_charges value: 0–99, default the slot's charges.
  - Changing the slot updates the reward and lens selects (or the charges input) to that slot's values.
- Session multiplier cards: unit "%" for `cof_double_*`, "×" for the rest. `cof_double_*` have a null default: an empty input means `reset`, and the limits read "0–100% · Default: rank bonus".

**Commands** (all game_save except the preview; each adds `expected_id` from `cof_read.player`):
- `cofjoin {id}[ switch]`
- `cofrank {id} {v}`
- `coffavor {id} {v}`
- `cofreputation {id} {v}`
- `coflenses {id}`
- `cofprophecy {id} {slot} {reward|none} {lens|none}`
- `cofcharges {id} {slot} {v}`
- `cofpreview …` (read)
- Multipliers: `coffavormult`, `cofrepmult`, `cofchargemult`, `cofrewardmult` (1–25), `cofexaltedmult`, `coft7mult`, `coflpmult`, `cofdouble enemy|echo v|reset`, `coflensmult celerity|charity|duplication v`.
- Python sends `cofjoin` with a ` switch` suffix when `switch_from_merchant` is true.

**Bridge validation** (port natively):
- Live `cofread` player matches the save.
- Multipliers need membership: "Circle of Fortune membership is required to enable this multiplier."
- The slot must exist and not be locked: "This prophecy slot is locked by rank."
- The reward must be available: "This reward is not currently available."
- The lens must be purchased and within rank: "The lens must be purchased and allowed by the current rank."
- `cofprophecy` first runs a preview; if the preview fails, its reply is returned.

**cofread JSON:** `{ok, player, cof{member,rank,favor,reputation}, favorMultiplier, maxFavor, maxCharges, reputationMultiplier, maxRank, slots[{index,rankRequired,locked,rewardId,lens,charges,chargeProgress,chargePercentage,baseFavorPerCharge,target}], effects{…}, lenses[{id,rankRequired,purchased,name,effect}], rewards[{id,rankRequired,available,name,baseFavorPerCharge}], tuning{ready, settings{…}, base{…}, telemetry, hooks, faults, maxItemsPerReward}}`.

**Notifications:**
- cof_rank: `Circle of Fortune rank set to {v}.`
- cof_favor: `Favor set to {v}.`
- cof_reputation_grant: `Added {v} Reputation.`
- cof_join: "Joined Circle of Fortune."
- cof_lenses_unlock: "Available lenses unlocked."
- cofprophecy: `Prophecy slot {n} updated.`
- cof_charges: `Prophecy slot {n} charges set to {v}.`
- plus the backup text. Afterwards, re-read `cof_read`.

**Preview:** shows the result dialog (§17.2) "Prophecy preview" with Slot / Reward / Lens / Charges (`currentCharges → chargesAfter`).

---

## 15. Factions. Complexity: small

```
<section class="panel"><div class="section-head"><h2>Factions</h2><button data-refresh="factions_read" data-control-id="factions_read">Refresh live</button></div>
<div class="note">Merchant’s Guild and Weaver management actions are not implemented. This section reads information only.</div>
<div class="control-grid">{per faction:
 <article class="control-card"><h3>{name}</h3><span class="badge">Information only</span><p class="muted">{Member|Not a member} · Rank {rank} · Favor {favor} · Reputation {reputation}</p>
 [<p>Weaver points: {earned} / {maxTotal} ({maxRank} rank + {maxEchoes} echo)</p>]
 <details><summary>Rank rewards</summary>{<p><b>{index+1} · {title}</b><br>{description, tags stripped}</p>}</details></article>}
| <p class="empty">Live faction information has not been read yet.</p></div></section>
```
- **Faction names** are the raw ids ("CircleOfFortune", …).
- **Data:** `factionread` returns `{ok, player, factions[{id,name,ranks[{index,optionalRankValue,reputationRequired,title,description,lines[]}],member,rank,favor,reputation,prophecySlots?,weaverPoints?{earned,maxRank,maxEchoes,maxTotal}}], prophecyData{…}}`.
- **Read on open:** if not cached.
- **Developer only:** "Management features in development" details.

---

## 16. Settings. Complexity: medium (mostly DESKTOP-ONLY state)

**Order:**
1. Language
2. Appearance · Theme
3. Draft profiles
4. (developer) Game connection
5. Version
6. In-game report
7. Support report
8. Recovery backups
9. Navigation and favorites
10. Text size
11. Hidden parts

The hide-part × buttons are not added on this page.

| Panel | Exact content | Behaviour / data | In-game |
|---|---|---|---|
| **Language** | `<h2>Language</h2><p class="muted">The language of the EpochPact windows. Names that come from the game — items, affixes, stats, zones — stay as the game data writes them.</p><label class="startup-profile">Language<select id="language-select">English / Türkçe / Русский</select></label>` | Stores `epochpact.language` and reloads the page | In game: write the preferences key and rebuild the panel (it already rebuilds when the language differs) |
| **Appearance · Theme** | `<p class="muted">Both themes use the same settings. Switching themes sends no game commands, and your choice is remembered.</p><div class="control-grid theme-grid">` with 2 × `button.settings-card[data-theme-pick][.selected]` containing `<img src="/ui/assets/{chronoforge\|void-atlas}-reference.png">`, `<h3>Chronoforge\|Void Atlas</h3>`, `<p class="muted">Jade time crystal · aged gold · ancient temple \| Violet time rift · horizontal navigation · Void observatory</p>` | `epochpact.theme`; hero eyebrow "CHRONOFORGE"/"VOID ATLAS"; CSS `.settings-card img{width:100%;border:1px solid var(--line);aspect-ratio:1.6;object-fit:cover;margin-bottom:15px}` | DESKTOP-ONLY (the in-game window is Chronoforge only) |
| **Draft profiles** | `<div class="note">Profiles store session settings only. Quests, rewards, balances and save actions are never stored or automatically applied.</div><div class="form-grid"><label>Profile name<input id="profile-name" placeholder="Example: Balanced"></label><label>Saved draft<select id="profile-select">…</select></label></div><div class="actions"><button id="profile-save">Save draft</button><button id="profile-load">Load draft</button><button id="profile-delete">Delete profile</button></div><label class="startup-profile">Apply at startup<select id="startup-profile">` with options `__off__` "Off — start with the game's normal values", `__last__` "My last used settings (recommended)", "Profile: {name}"`</select></label><p class="muted">Applied once each time an offline character is ready, after you start EpochPact or the game, and after an update. Closing EpochPact still returns the game to its normal values until it starts again. Your last used settings include loot modes, collection categories, wanted affixes and the font. Profiles, theme and window places are kept in EpochPact's preferences file, so they survive updates.</p>` | Profiles live in `epochpact.profiles.live` as `{name:{controlId:value}}` (session number/toggle controls + autopickup + speed). **Save:** empty name → "Enter a profile name." (error); success → "Draft saved; not applied to the game.". **Load:** puts values in the draft → "Profile loaded into the draft. Use Apply changes to update the game.". **Delete:** confirm "Delete draft profile" / `The local draft “{name}” will be deleted. Game settings are unchanged.` → "Local draft deleted.". **Startup select:** "Startup profile saved. It applies when an offline character is ready." / "Startup profile turned off. EpochPact starts with the game's normal values.". The startup engine (`applyStartupProfile`) re-sends every stored value once per character owner, then replays `last-settings-replay` (loot_mode, loot_affixes, weaver_affixes, loot_category:*, font_family). Summary notify: `Applied {name} (N settings).` | DESKTOP-ONLY storage. If the in-game window should replace the app, port the startup and replay engine with the same keys; avoid double application when both run |
| **Version** | `<p class="muted">EpochPact v{version}. At startup EpochPact can read the latest release on GitHub (no player data is sent) and show a notice when a newer version exists. Install update downloads it from that release, checks its SHA-256 and every file, and restarts EpochPact; your settings stay.</p><label class="inline-check"><input type="checkbox" id="update-check"> Check for EpochPact updates at startup</label><div class="actions"><button data-update-check-now>Check now</button></div>` | `epochpact.update-check.live`; see §17.4 | DESKTOP-ONLY |
| **In-game report** (`section#ingame-report`) | `<h2>In-game report</h2><p class="muted">While playing, press <b>{key\|"the hotkey"}</b> to save a report in <code>Last Epoch\EpochPact\reports</code>: a screenshot of that moment, game and EpochPact state, recent logs and a <code>description.txt</code> to fill in. The game shows a short notice. Nothing is uploaded; zip the folder and share it in the EpochPact Discord. Windows user and computer names are masked.</p><div class="form-grid"><label>Hotkey<select data-report-key data-control-id="report_key">F5…F12, Off</select></label></div><div class="actions"><button data-report-now data-control-id="report_now">Create report now</button><button data-refresh="report_read" data-control-id="report_read">Refresh</button>[<span class="muted">Last: {basename}</span>]</div>` | `reportread` → `{ok,key,reports,folder,last}`; `reportkey F5..F12\|off` → notify "In-game report hotkey turned off." or `In-game report hotkey: {key}.`; `reportnow` → `{ok,folder,screenshot}` → `Report saved: {basename} (in the game's EpochPact\reports folder).`, then re-read. No offline requirement. Read when Settings opens | Fully portable (native commands) |
| **Support report** | `<p class="muted">Create a ZIP for the EpochPact Discord: your description, versions, setup state and recent EpochPact logs, with an optional screenshot of the game window. Nothing is uploaded; the folder opens so you can attach the file. Your Windows user name is masked; logs can contain character names.</p><label class="wide">What happened?<textarea id="report-description" rows="4" maxlength="4000" placeholder="What did you do, what did you expect, and what happened instead?"></textarea></label><label class="inline-check"><input type="checkbox" id="report-screenshot"> Include a screenshot of the game window</label><div class="actions"><button id="report-create">Create support report</button></div>` | Launcher action `report {description, screenshot}` (Python `support_report.create`, opens Explorer) → notify message and clear the textarea | DESKTOP-ONLY; the in-game report above covers this need |
| **Recovery backups** | `<p class="muted">Changes to your game save create a backup. Restore a selected backup to undo that change; the game closes normally first.</p>` + table Action \| Time (`HH:MM:SS`) \| Result (Complete \| Error / backup available), or `<p class="empty">No save actions have been performed in this session.</p>` + `actionCard(undo)` (select "Operation backup": `{label} · {time}`) | History from the bridge (`result.history`: `{id,control,backup,time,ok}`, last 100). Recovery confirm. Python `progression.undo` closes the game, validates and overlays the backup files. Notify "Selected backup restored. You can reopen the game."; disabled "No saved operation is available to restore." | DESKTOP-ONLY (the game must be closed) |
| **Navigation and favorites** | `<h2>Navigation and favorites</h2><p class="muted">Choose visible sections and pin favorites to the top. These choices stay on this device.</p><div class="navigation-options">` one row per page: `<div><strong>{name}</strong><label><input type="checkbox" data-nav-visible=id [disabled for overview/settings]>Visible</label><label><input type="checkbox" data-nav-favorite=id>Favorite</label></div>` `</div><label class="inline-check"><input type="checkbox" id="remember-scroll"> Return to the same place when switching sections</label><button id="display-preferences-reset">Restore all sections and stats</button>` | `epochpact.display-preferences.v1` `{favorites,hidden,summonColumns,hiddenStats,summonOptions}`; reset → "All sections and stats are visible again."; `epochpact.remember-scroll.live`. CSS `.navigation-options{max-width:460px}`, row `display:grid;grid-template-columns:minmax(120px,200px) auto auto;column-gap:18px;padding:8px 0;border-bottom:1px solid #92734035`; labels flex gap 8 | Storage via preferences.json |
| **Text size** | `<p class="muted">Makes everything in this window larger or smaller. The loot and damage windows have their own text size in their ⚙.</p><label class="startup-profile">Text size<select id="text-scale">90% 100% 110% 125% 140% 150%</select></label>` | `epochpact.text-scale.v1`; applies CSS zoom | In game: multiply the canvas scale |
| **Hidden parts** (`section#hidden-parts`) | `<div class="section-head"><h2>Hidden parts</h2>[<button data-show-all-parts>Show all</button>]</div><p class="muted">Panels and cards you hid with their ×. Hiding only removes them from the page; a setting keeps the value it has.</p><ul class="hidden-parts"><li><span>{label}</span><button data-show-part=key>Show</button></li></ul>` or `<p class="empty">Nothing is hidden. Point at a panel or card and select its × to hide it.</p>` | `epochpact.hidden-parts.v1` = `{key:{label:"{Page} · {title}"}}`. Keys: `card:{controlId}`, `panel:{id}` or `panel:{page}:{h2 text}`. Hiding notifies "Hidden. Settings → Hidden parts shows it again." CSS `.hidden-parts{display:grid;gap:8px}`; `li{display:flex;justify-content:space-between;gap:12px}` | Storage via preferences.json |

`.startup-profile` and `textarea` have no CSS of their own; textarea is unstyled.

---

## 17. Global UI pieces not yet covered

### 17.1 Confirm dialog (`#confirm-dialog`)
```
<dialog id="confirm-dialog"><div class="dialog-head"><h2 id="confirm-title"></h2><button id="confirm-close" aria-label="Close">×</button></div><p id="confirm-message"></p><div class="actions"><button id="confirm-cancel">Cancel</button><button id="confirm-yes" class="primary">Apply action</button></div></dialog>
```
- **Behaviour:** modal; resolves true or false. The ×, Cancel and Esc (the `cancel` event, default prevented) all resolve false. The Yes button always reads "Apply action", including for delete and uninstall.
- **Message:** `white-space:pre-line`, so `\n` makes line breaks.
- **CSS:**
  - `dialog{max-width:690px;width:calc(100% - 32px);border:1px solid var(--gold);padding:27px;background:var(--panel);box-shadow:0 20px 100px #000b;max-height:90dvh;overflow-y:auto}`; `::backdrop{background:#000a;backdrop-filter:blur(3px)}`.
  - `.dialog-head{display:flex;gap:15px;justify-content:space-between;align-items:flex-start}`; h2 margin-bottom 14; head button `font-size:21px;padding:4px 11px`.
- **Callers:** formAction (game_save/recovery; custom item; materials), stash rules (create/sort/assign), "Reset all bonuses", profile delete, launcher uninstall.

### 17.2 Result dialog (`#result-dialog`)
```
<dialog id="result-dialog"><div class="dialog-head"><h2 id="result-title">Operation result</h2><button id="result-close" aria-label="Close">×</button></div><div id="result-text"></div></dialog>
```
- **Players** only see it for `cofpreview`. Title "Prophecy preview"; body:
  ```
  <p class="note">Preview only. No changes have been applied.</p><dl class="result-summary"><dt>Slot</dt><dd>{n}</dd><dt>Reward</dt><dd>{name|None}</dd><dt>Lens</dt><dd>{name|None}</dd>[<dt>Charges</dt><dd>{currentCharges} → {chargesAfter}</dd>]</dl>
  ```
  CSS: `.result-summary{display:grid;grid-template-columns:minmax(80px,.35fr) 1fr;gap:1rem;margin:1.5rem 0}`; dt `#aebdb7`.
- **Developer mode:** the raw `pre.readout` JSON (`#result-text .readout{max-height:65vh}`).
- **Everything else** for players becomes a notification.

### 17.3 Notification toast (`#notification.notification`)
- **Markup:** a single `div role=status aria-live=polite`; each new message replaces the text.
- **Timing:** hides after 6.5 s, or 12 s for errors (`.error`).
- **CSS:** `position:fixed;right:25px;bottom:100px;max-width:min(600px,90vw);padding:16px 20px;background:var(--panel);border:1px solid var(--accent);z-index:15;box-shadow:0 8px 25px #000b;line-height:1.6;font-size:13px`; `.error{border-color:#ee8070;color:#ffc6b9}`; bottom 130 under 800px.

### 17.4 Update dialog (`#update-dialog`) — DESKTOP-ONLY
```
<dialog id="update-dialog" class="update-dialog"><div class="dialog-head"><h2 id="update-title">Update EpochPact</h2><button data-update-close aria-label="Close">×</button></div>
<p class="muted" data-update-summary></p><div class="release-notes" data-update-notes></div>
<div class="update-progress" data-update-progress hidden><progress max="100"></progress><p data-update-status role="status"></p></div>
<p class="note" data-update-warning hidden></p><div class="actions" data-update-actions></div></dialog>
```
- **Check flow:**
  - `GET /api/update` (1.5 s after start if enabled). While `{pending:true}` it retries every 1.5 s, at most 10 times.
  - Reply: `{ok, current, latest, url, updateAvailable, installable, reason, size, release{notes,…}, canInstall}`.
  - Sidebar `#update-notice`: `New version <b>v{latest}</b> · <a data-update-open>What's new</a>`.
  - The dialog opens by itself once per new version (`update-announced`).
- **Summary text:** `v{cur} → v{latest}[ · {x.x MB}].` followed by one of:
  - "Install update works in the EpochPact app from the release ZIP; use the release page here."
  - `reason`
  - "Install update downloads the ZIP from EpochPact's GitHub release, checks it, restores temporary game settings and restarts EpochPact. Your settings stay."
- **Notes:** Markdown to HTML (`ui/release-notes.js`: headings, lists, bold, code, links only to the EpochPact GitHub or Discord). `/api/update/notes` lists up to 10 newer releases.
- **Actions by state:**
  - idle/error: "Later", `<a class="button">Release page</a>`, plus "Install update" or "Try again" (primary).
  - downloading/verifying: "Cancel".
  - ready: "Later" and "Restart and update" (primary).
- **Progress:** polled every 500 ms from `/api/update/progress`. Texts: "Downloading… X MB of Y MB (N%)" / "Checking every file of the download…" / "Downloaded and checked: …" / "Closing EpochPact to update…".
- **Warning when ready and the game is running:** "Last Epoch is running. EpochPact updates the mod files in the game folder after you close the game; until then it cannot connect to the running game."
- **CSS:** `.release-notes{max-height:46vh;overflow:auto;border:1px solid var(--line);background:var(--soft);padding:6px 16px;margin:14px 0}`.
- **In game:** omit it, or show only the version.

### 17.5 Global search (`#global-search`)
- **Topbar markup:** `<label class="top-search"><span>⌕</span><input id="global-search" placeholder="Find a setting…" aria-label="Search all settings"></label>`. Width 250px (180 under 1200px); icon 27px gold.
- **On every input:**
  - `q = trim().toLowerCase()`; an empty q re-renders the current page.
  - Matches: player-visible controls (all except developer ones: density_read, map_read, pickup_radius_read, tier_read, map_settings_read, font_read, sheet_catalog, sheet_open, resistance_labels, session_read, status, player_read, characters, play_offline, load_character) whose `label + " " + description + " " + id` contains q.
  - No ranking: catalog order is kept.
- **Results replace the page content:**
  ```
  <section class="panel"><h2>Setting search · {N} results</h2><div class="control-grid">{<button class="settings-card" data-go="{page}" [data-craft-target="materials|custom|forge"]><h3>{label}</h3><p class="muted">{description}</p><span class="badge">{Lifetime}</span></button>}</div></section>
  ```
- **Target page:** `pages[group]` if that page exists; groups session and recovery go to "settings"; anything else goes to "stats". For crafting, the target tab is `craft_materials*` → materials, `craft_custom*` → custom, otherwise forge.
- **Clicking** clears the search box and navigates.

### 17.6 "Customize navigation" (`.nav-customize`)
- **Markup:** a link `<a class="nav-customize" href="#settings">Customize navigation</a>`, always the last item in `#nav`. CSS `display:block;padding:12px 22px;font-size:.8rem;color:var(--muted)`.
- **Target:** Settings, where the "Navigation and favorites" panel (§16) controls the nav.
- **Nav building:** `EpochPreferences.visiblePages` drops hidden pages (the current page always stays) and stable-sorts favorites first. A favorite's symbol becomes ★.

### 17.7 Overlay windows (`feed.html`, `damage.html`, `overlay.js`) — DESKTOP-ONLY windows
These are separate frameless, transparent pywebview windows that follow the game window (`tools/overlay_follow.py`). Their open state, position and size are stored in `epochpact.windows.v1`, and they reopen at app start. The in-game equivalent is two in-game HUD panels drawn on their own canvas, with drag and resize inside the game screen and no OS windows.

**Shared chrome (`ui/overlay.js`):**
- Header `.overlay-bar`: title (`.overlay-drag`), ⚙ (toggles `.overlay-options`), − (hides the window, remembered as closed).
- Corner grip `.overlay-grip` resizes. Minimum sizes: feed 220×120, damage 260×120.
- **Options** (saved in each window's key):
  - Background colour (default `#091310`)
  - Background opacity 0–100 step 5 (default 85)
  - Text colour (`#eee6d5`)
  - Text size 10–22 (13)
  - Row spacing Compact/Normal/Roomy (1/4/8 px)
  - Top bar "On hover"/"Always"
- **CSS variables:** `--overlay-bg` rgba, `--overlay-ink`, `--overlay-font`, `--overlay-gap`, `--overlay-shadow` (`0 1px 2px #000,0 0 3px #000` when opacity < 60).
- The bar fades in on hover unless set to Always.

**Loot feed (`feed.html` + `feed.js`):**
- **Polling:** reads `loot_log` every 2 s while visible; merges like §3a; reads `affix_info` for up to 4 shard ids per poll.
- **List** (`EpochLootLog.compact`, limit 40, grouped):
  ```
  <ul class="loot-feed"><li class="loot-{rarity}" title="{item\ntiers\nrule\nshard}"><span>{item}[ <small>({n} pickups)</small>][ <em class="loot-tiers">T7 T6</em>]</span>[<small>{rule}</small>]</li></ul>
  ```
  - Empty texts: "Waiting for pickups…" / "No pickups in the categories you show."
- **Footer:** `Gold: {total}` and a status: "Loot log unavailable in this game build" / `r.error` / "Waiting for an offline character…" / "EpochPact is not responding." / "Open EpochPact first.".
- **Options add a "Show" filter** with All/None buttons and groups:
  - Items: Normal, Magic, Rare, Exalted, Unique, Set, Legendary (labels coloured by rarity).
  - Resources: Gold, Memory Amber, Soul Ember, XP motes.
  - Materials: Affix shards, Runes, Glyphs, Other materials.
  - Category of an entry: gold → gold; amber by name (Soul Ember → ember, XP mote → xp, otherwise amber); material by type (101 shard, 102 rune, 103 glyph, otherwise material); otherwise by rarity.
  - Stored in `epochpact.loot-feed.v1.show`.

**Damage window (`damage.html` + `damage.js` + `damage-styles.js`):**
- **Header:**
  - Title "Damage".
  - View select: Dealt / Taken.
  - Metric select: 10 s, 60 s, 10 min, 60 min, Session, Combat (hidden in the Table style).
  - ▶/Ⅱ toggle (title "Start measuring"/"Stop measuring").
  - ↺ reset.
  - ☠ death recap (`.dm-alert` red outline while an unseen death exists; title "Death recap (new death)").
  - ⚙ and −.
- **Options:**
  - Style: Quiet HUD (default) / Pact Bars / Split Meter / Table.
  - Compact checkbox (table only).
  - Damage type (All damage, Physical … Poison).
  - Rows checkboxes ("Rows appear once the session records damage." when empty).
  - The overlay options.
- **Recap panel:** `section.overlay-options.dm-recap` "Death recap" with a Close button, showing §7b HTML.
- **Data:**
  - `damage_session_read` every 1 s when active, otherwise every 3 s.
  - `damage_session {start|stop|reset}`.
  - `death_recap_read` every 3 s while the recap is open, and immediately when the death count changes.
  - `damage_icon {key}` (`damageicon a:<id>|c:<class>:<mastery>` → `{ok,key,png:base64|null}`): up to 4 per read, 3 tries each, PNG under 200000 chars.
  - Auto-resume: if `options.running` is set, start the session once per window opening.
- **Option key:** `epochpact.damage-window.v1`, holding `{compact, running, element, hidden[], style, metric, roster, view, takenHidden[], deathsSeen}` plus the overlay options.
- **Styles** (`ui/damage-styles.js`):
  - **Total:** the sum of every source's metric. Footer `TOTAL DPS` (or `DAMAGE TAKEN`) `[· {Type}]`, a record dot (`.dm-rec`, `.dm-on` when active), a large number and `DPS` (or `per s`).
  - **Quiet HUD:**
    - Selected source name and DPS (selected = the clicked ally, else You).
    - A 7-type strip with icon, short label (Phys., Fire, Cold, Light., Necro., Void, Poison) and short number.
    - An "Allies ⌄/⌃" roster button with the allies' DPS, and the allies' rows.
  - **Pact Bars:**
    - Meta "Damage by source" with an m:ss clock.
    - Rows ranked by DPS, each with a fill bar (`width = dps/largest`).
    - Caption "Damage types · {selected}" with a back button (You ↶, or "All attackers ↶" in the Taken view), then the strip.
    - The Taken view always uses Bars.
  - **Split Meter:**
    - A "You" column with 7 type lines.
    - An "Allies" column with rows plus an inspect area "{ally|All allies} · damage types".
    - "No allies recorded." when there are none.
  - **Row:** `button.dm-row[data-src]` with fill, icon (`img.dm-src-ico`, or `.dm-dot`), name, `×live`, value.
  - **Number format:** below 1M every digit; above that 3 significant digits with M or B. The short form uses k.
  - **Empty texts:** "Waiting for an offline character…", "No damage recorded yet. Start the session and fight.", "Every row is hidden. Choose the rows to show.", "Some hits are not split by damage type.".
  - **Colours:** `--dm-gold:#cbb581`, `--dm-jade:#88bfa2`. All `.dm-*` CSS is on line 111 of `ui/style.css`.

---

## 18. Differences between the in-game page list and app.js

`tools/ingame_pages.py` is missing or simplifies these parts of app.js, beyond the sections requested:
1. **Monolith:** the "Boss blessing rewards" setting panel (`build_blessingoptions`, `build_blessingrolls`) is missing. Panel order differs: app.js shows summary, Echo modifiers, Boss blessing rewards, Monolith management, then Monolith Navigator. The in-game page shows pending "Monolith" before Echo modifiers.
2. **Summons:** the "Minion and companion bonuses" panel (alias stat buttons into the stat dialog) is missing.
3. **Display:** "Interface font" is really two panels, "Text and font" and the setting panel "Text size and letter case" (`font_scale`, `font_case`). Both need adding.
4. **Loot:** "Loot filter window" lacks its note "Rule name examples: …".
5. **Skills and Equipment:** both start with the `cachedSummary("build_read")` note, which is missing.
6. **Stash page:** the "In-game stash search" panel; **Overview:** the launcher "Game" summary panel at the top.
7. **CoF:** the whole page is pending, including the session setting panel "Gain, drop and quality multipliers". That panel could reuse the existing settings renderer: draft mode, "Use Apply changes below".