# Monolith Apply readiness — 2026-10-09

The UI retained a Monolith snapshot when the same character changed areas. A denied
Echo snapshot could therefore keep Apply disabled after returning to a hub. The
previous tooltip also reported only “Refresh live data”, without explaining the
area restriction. These are UI readiness defects; the original screenshot alone
does not identify which cached condition was false at that exact moment.

Monolith snapshots now require a matching loaded character **and area**. Changing
areas invalidates the Monolith and Echo caches. Refresh live reads the connection
first and the section second, through the existing serial job queue. It remains
clickable when old connection state has disabled save actions. Disabled action
cards display their reason. Typed Corruption / Stability values remain local
drafts for their character, timeline and difficulty, survive refreshing, and are
cleared after successful application or a character change.

## Follow-up: focus and draft retention

The initial fix still cleared drafts when a focus-triggered connection read lost
the player identity temporarily. Minimisation, a transition, an IPC timeout or
interface-session renewal could therefore replace typed amounts with saved values.

Draft ownership now survives those temporary states. Only a settled offline
identity with a different save slot or a different known character name clears
the draft and resets the selected target. Numeric/string slot zero is the same
owner. Corruption and Stability retain empty, zero and invalid pending amounts;
unknown live bounds no longer become a false `max=0`. Save actions stay disabled
until current character and area data have been verified again.

Focus refreshes unchanged identity/settings without replacing the form DOM.
Explicit Refresh live still reads and redraws. An Apply captures its arguments
before confirmation, validates current identity again, and clears only its own
unchanged submitted draft after success. Newer edits and other timeline drafts
survive; failed actions retain their drafts for an explicit retry.

The shipping focus handler was tested against transient missing identity, timeout,
server invalidation, repeated focus, character switches, target changes and failed
Apply. Four regression cases failed against the before-change source and pass
with this fix. Current live browser and test evidence is recorded in
`research/live/monolith-draft-fix-20261009/verification.json`; that report explicitly
separates browser refresh/navigation from simulated focus and physical Alt-Tab.

Validation:

- 81 Python backend / HTTP tests passed.
- 24 JavaScript tests passed: 9 stat presentation, 8 session and 7 shipping-handler
  readiness tests. Fixtures reproduce Echo → hub, hub → Echo, one-click refresh,
  disconnected / transitioning states and draft isolation.
- Live `identityread` reported Falor, slot `0`, scene `M_Rest`; `monolithread`
  reported `editable=true`. The live browser showed all four Apply action buttons
  enabled. Entering `300` and clicking Refresh live retained `300` and re-enabled
  Apply. No console errors were captured.
- The actual `EpochPact.cmd` desktop window opened and displayed the connected
  Monolith page. The UI checks used reads and navigation; no Apply action was
  submitted and no character save was edited for this verification.

This fix changes UI files only. The native DLL was not rebuilt or replaced.
Native save-id, offline and safe-area guards still validate every actual action.
Live Monolith mutations were not repeated on the user's character in this check;
their earlier separate coverage remains in `all-mods-current-verification.md`.

Screenshot: `ui-evidence/monolith-apply-enabled-20261009.png`.
Before-change source copies and launcher logs:
`research/live/apply-eligibility-20261009/`.

Commands:

```text
node --test tools/test_stat_model.cjs tools/test_ui_session.cjs tools/test_ui_readiness.cjs
py -3 -m unittest tools.test_le_session tools.test_progression_backend tools.test_monolith_backend tools.test_cof_backend tools.test_collection_backend tools.test_loot_crafting_backend tools.test_ui_bridge
```
