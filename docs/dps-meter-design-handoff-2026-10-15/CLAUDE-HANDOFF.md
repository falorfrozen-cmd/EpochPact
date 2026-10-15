# EpochPact DPS meter — Claude Code handoff

Prepared: 15 October 2026. This is a **design and integration handoff**, not a new player release.

Open [preview.html](preview.html) first. It runs offline by double-clicking the file; no server, Python, Codex, game, or account is required. Use the three buttons at the top to compare the designs. **Restore samples** restores the local demonstration after Reset.

## Prompt to give Claude Code

> Read this entire handoff, open its preview and screenshots, then integrate the three compact DPS designs into EpochPact's existing Damage overlay as selectable styles. Preserve its current transport, session controls, preferences and frameless WebView2 window. The user is handling the damage data connection; use the existing state contract rather than introducing new native hooks. Keep player damage, summon damage and the overall total separate. Do not modify unrelated work in the checkout. Remove every mock interaction from the production view, test the real renderer and existing overlay controls, and report preview, WebView2 and live game results separately. Do not call the design package a finished game integration. The concrete requirements and open work are below.

## What is ready, and what is not

| Ready in this folder | Still required in the application |
|---|---|
| Three compact designs with an interactive offline preview | A renderer consuming real `state`; the preview still uses fixtures |
| Painted icon atlas, seven damage types, separate summon sources, total footer | Real Start/Stop/Reset, options, hide, native drag and resize |
| Responsive preview layouts and source inspection | User preferences, localization, arbitrary source names/counts |
| Repeatable browser preview checks and screenshots | No-data/error states, actual WebView2 and game validation |

The user rejected large ornamental meters as impractical and asked for three smaller alternatives. **No final winning design has been selected.** Keep these three available as style choices. Quiet HUD is the designer's recommendation for ordinary play; Split Meter is the recommendation for comparing a summoner build. Those recommendations are not user approval of a default.

This package does not contain the older ornate “flagship”, Runic Ledger, Chrono Prism or Combat Ribbon studies. They are superseded for this handoff.

## Files

| File | Purpose |
|---|---|
| [preview.html](preview.html) | Standalone offline preview, with all image bytes embedded |
| [source/design-fragment.html](source/design-fragment.html) | Exact latest original design fragment; uses Codex host APIs and is reference material, **not an application entry point** |
| [screenshots/01-pact-bars.png](screenshots/01-pact-bars.png) | Pact Bars over a game screenshot |
| [screenshots/02-quiet-hud.png](screenshots/02-quiet-hud.png) | Quiet HUD with its ally roster collapsed |
| [screenshots/03-split-meter.png](screenshots/03-split-meter.png) | Split Meter showing player and ally columns |
| [assets/effects-atlas.webp](assets/effects-atlas.webp) | Extracted painted atlas for integrating the icons |
| [assets/game-context.png](assets/game-context.png) | Preview background only; omit from runtime |
| [examples/backend-state.json](examples/backend-state.json) | Exact state example extracted from the supplied brief |
| [examples/design-fixture.json](examples/design-fixture.json) | Separate visual DPS fixture; **not** backend state |
| [references/original-brief.md](references/original-brief.md) | Snapshot of the user's supplied contract and damage-type notes |
| [verification/preview-results.json](verification/preview-results.json) | Results measured against this standalone preview |
| [verification/check-preview.cjs](verification/check-preview.cjs) | Repeatable preview checks |
| [MANIFEST.json](MANIFEST.json) | File sizes and SHA-256 values for this handoff |

The standalone preview removes Codex carousel, widget-state and Tweak dependencies. Its own header switches designs. It deliberately does not save demo state. All three meter layouts retain the original design's CSS and local demonstration logic.

## The three designs

Approximate meter sizes at the default font and desktop viewport, **excluding** the game screenshot and preview navigation:

| Style | Default size | Layout and interaction |
|---|---|---|
| **Pact Bars** | 300 × 323 px | Four ranked source rows, selected source's seven-type strip, overall total last. Selecting a source changes the detail strip, never the total. `You ↶` returns to the player. |
| **Quiet HUD** | 264 × 198 px | Minimal chrome, selected source's seven-type strip, expandable Allies roster, overall total last. Controls appear on hover or keyboard focus. Expanding the roster increases height. |
| **Split Meter** | 420 × 275 px | Player's seven types on the left; ally aggregate, individual summons and selected ally details on the right; overall total last. Stacks into one column in the narrow preview. |

Design language: opaque dark green surfaces, restrained brass accents, ivory tabular numerals, jade ally rows and small painted icons. Readability takes priority over decorative frames. Do not stretch a whole bitmap to render the meter; numbers, rows and buttons are real HTML.

Pact Bars normalizes bar widths to the **largest source in that source group**, so the largest visible source fills its track. The preview is not expressing “percentage of global DPS” with those bars. If adding a percentage, label and calculate that separately.

Source selection is inspection. Clicking the selected ally again returns the compact strip to You. Quiet HUD's Allies button expands/collapses the roster. The type tiles display values and full-name tooltips; they do not currently filter the session.

## Existing implementation to reuse

Repository at preparation: `feature/dev16`, HEAD `5d78107` (`0.1.2-dev.31` in its commit title). **The checkout had unrelated uncommitted changes.** This is a reference point, not an instruction to reset or overwrite the current branch.

| Existing file | Relevant behavior |
|---|---|
| `ui/damage.html` | Existing Damage overlay entry; Start/Stop, Reset, gear, hide, options, resize grip |
| `ui/damage.js` | `EpochSession` transport; `damage_session_read` and `damage_session` start/stop/reset; one-second polling with busy guard; current options key `epochpact.damage-window.v1` |
| `ui/damage-meter.js` | Shared `EpochDamage` helpers (`rows`, `sources`, `status`, `table`, `csv`, `duration`, `fmt`, `elements`); also used by Summons, so preserve existing consumers and CSV behavior |
| `ui/overlay.js` | `EpochOverlay.normalise`, `variables`, `install`; shared options, native window resize/hide and minimum size |
| `ui/style.css` | Shared `.overlay-*` styles; scope new meter styles to avoid changing other windows |
| `ui/storage-sync.js`, `ui/i18n.js`, `ui/session.js` | Existing storage, translation and transport integration |
| `tools/test_damage_meter.cjs` | Existing helper/CSV/type-filter tests to preserve |

The real window is a frameless, always-on-top WebView2 overlay. It does not require an Alt-Tab into the main application. A game screenshot in the preview is only a way to judge legibility; it is not a real game overlay test.

Keep `EpochOverlay.install(window, {options, save, minimum:[260,120], ...})`, the native `.pywebview-drag-region` and the real hide/resize bridge. The preview's drag code only moves an article inside a screenshot; **do not use it to move the real desktop window**. Keep one real control bar rather than duplicating it with the preview header.

Preserve background colour/opacity, text colour, font size 10–22 px, row spacing, top bar always/hover, hidden source rows and damage-type selection. Use the existing `--overlay-bg`, `--overlay-ink`, `--overlay-font`, `--overlay-gap`, `--overlay-shadow` variables instead of hardcoding away those preferences. Maintain old stored preferences when adding the style option.

## Data contract and calculation rules

Expose a rendering boundary such as `render(state, viewOptions)` and call it from the **existing** polling path. Rendering must not start/reset sessions, add polling loops or send gameplay commands.

1. `state.types` already contains You and every recorded minion kind. `state.allies` is an aggregate of those same minions. **Do not add it again.**
2. Overall DPS = sum of the chosen metric from `state.types`. Ally DPS = `state.allies[metric]`, or a correctly computed sum of non-You sources if the aggregate is absent. `live` is a count for `×4`, **not a damage multiplier**.
3. Canonical array order is **Physical, Fire, Cold, Lightning, Necrotic, Void, Poison**. Sorting rows visually must not reorder array indices. “Elemental” and ailments are not additional damage types.
4. Map 10s → `dps10`, 60s → `dps60`, 10min → `dps600`, 1h → `dps3600`, Session → `average`, Combat → `combatAverage`. Preserve the existing metrics even though the design demo shows only three menu entries.
5. Element DPS uses `source.elements[metric][index]` for the four rolling windows. For Session/Combat element averages, use element cumulative `total[index]` divided by the corresponding elapsed/combat time, with zero/invalid guards. Do not estimate element DPS by applying current percentages to an unrelated average.
6. Use source-level metrics for the source and overall totals. `unsplitHits > 0` means type attribution may be incomplete; show a compact disclosure. Do not force the type strip to equal the overall value by inventing splits.
7. A mixed-type hit can increment more than one element hit count. Do not sum element hit counts to create session `hits`.
8. Clicking, expanding or hiding a detail row must not change what “TOTAL DPS” means. Keep the global total and the selected source distinct. If retaining the existing damage-type filter, label any filtered total explicitly so it cannot be mistaken for global DPS.
9. Escape arbitrary game source names using DOM `textContent` or the existing escaping helpers; the fixture's fixed names are not a safe general template.
10. Handle missing/malformed fields without throwing or displaying `NaN`/`Infinity`. Distinguish known zero from missing data. Live count zero does not erase damage already recorded.

Sanity check with [backend-state.json](examples/backend-state.json): You `702.4` + Skeleton `260.0` = **962.4 DPS (10s)**. Skeleton `live:4` does not turn 260 into 1,040. Do not add `allies.dps10` again.

The design fixture intentionally uses larger, different numbers: You **33,600**, Allies **20,640**, total **54,240**. Those are visual sample DPS values, not measurements from the game or values calculated from the brief's example.

### Mock behavior to replace, not ship

- `sources` / `types` inside the preview script are fixed visual fixtures.
- The 60s and Combat options multiply those fixture values by **0.84** and **0.77**. That is demonstration logic, **not valid DPS calculation**.
- Stop only changes a local icon; Reset only zeros the visual fixture; the clock is fixed at `01:35`.
- `.collapse` locally collapses the article; it is not the real desktop hide operation. Use the existing host hide behavior and provide any compact mode separately.
- Preview source IDs and icon mappings cover three sample minions only. Production needs arbitrary sources, stable identity across refreshes, and a fallback icon.
- Original fragment `window.openai`, `Tweak` and widget events belong to Codex's preview host. The standalone preview removes those dependencies; neither implementation is a completed production renderer.

The current state does **not** expose per-skill, per-ailment, Hit-vs-DoT or Melee/Spell/Throw/Bow attribution. Do not advertise or synthesize those breakdowns.

## Icons and references

The atlas has **6 columns × 4 rows**. Indices are zero-based: `column = index % 6`, `row = floor(index / 6)`. CSS sprite positions: `x = column * 20%`, `y = row * 100/3%`, `background-size:600% 400%`.

| Type | Index in damage array | Atlas tile | Suggested colour from brief |
|---|---:|---:|---|
| Physical | 0 | 13 | `#CFC6B8` |
| Fire | 1 | 2 | `#FF7A45` |
| Cold | 2 | 4 | `#73C7F2` |
| Lightning | 3 | 3 | `#F2D348` |
| Necrotic | 4 | 23 | `#7FE0C4` |
| Void | 5 | 22 | `#B48CFF` |
| Poison | 6 | 1 | `#9AD94A` |

Sample source tiles: player 13, skeleton 11, golem 7, archer 10. These are project-generated **AI-painted placeholder icons**, not verified official Last Epoch skill icons. The game capture is context-only. Do not describe either as an official icon pack or include the capture in the player runtime.

Research references: [Details! author gallery](https://www.curseforge.com/wow/addons/details/gallery) for compact source bars; [ArcDPS author site](https://www.deltaconnected.com/arcdps/) for low-chrome overlay presentation. No code or art from those projects is included in this package.

## Verification and acceptance

Run the packaged checks as described in [verification/README.md](verification/README.md). The JSON report is the authority for the measured preview checks, dimensions and browser version. The checker also writes the three supplied screenshots. They are browser design previews, not screenshots of a newly integrated runtime.

Before calling the application integration finished:

- Test all three styles with actual `render(state)` input: no session, no damage, stopped with retained data, player-only, several minions, removed/reappearing source, long source names, missing arrays, very large numbers, `unsplitHits`, `ok:false` and `hooked:false`.
- Assert the 962.4 DPS example, source selection invariance, no double counting of `allies`, and no multiplication by `live`. Test every metric, including average denominators of zero.
- Verify preserved source/type filters, option migration and persistence, shared CSV/helper tests, localization and the Summons page.
- Test actual **260 × 120** window size, 10–22 px fonts, spacing preferences, lots of rows, Windows display scaling and game UI scales. Default preview sizes are not proof that all of these work. Keep the overall total accessible, use a scrollable detail area and a compact fallback instead of clipping rows or controls.
- Verify native drag, resize, gear, hide, Start/Stop/Reset and main-window synchronization. Avoid destroying focused controls on one-second refreshes; create stable DOM and update values.
- Keep resource use bounded: reuse the one-second read, no scene scan, no extra per-source polling, no per-frame re-render.
- Record actual WebView2 and real game checks separately, including window close/reopen and normal application/game shutdown. Do not launch the game or alter saves just to review this package.

**Not performed for this handoff:** real data integration, new EXE/DLL build, game launch, player save changes, live damage accuracy tests or GitHub release upload. This folder is a complete design handoff; it does not claim those implementation tasks are complete.
