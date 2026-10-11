# EpochPact 0.1.2-dev.3 — mastery recovery validation

This candidate retains the dev.2 feature controls and Auto Pickup fix and adds
**Campaign → Choose Mastery**. Compatibility was checked against Steam build
25816319 (displayed 1.5.2.1, internal 1.5.2.2). Tests used isolated, backed-up
solo offline characters, not the user's original character.

## Problem and behavior

Campaign completion uses the game's quest completion/reward path. It does not
run the NPC dialogue that opens mastery selection. A completed character can
therefore still have no chosen mastery. The new explicit button opens the
game's normal first-choice panel. The player chooses and confirms in the game.
No mastery is assigned by the mod, and opening the panel does not repeat rewards.
Characters completed with an earlier EpochPact release can use the same button.

Native guards require a loaded offline character, the matching save ID, a
completed **The Power of Mastery** quest and no existing chosen mastery. Missing
metadata, loading transitions and unknown states are rejected. Existing mastery
respec remains the game's NPC action. Startup, navigation, theme changes, reads
and campaign completion do not automatically open the selection screen.

## Measured checks

| Layer | Result | What it establishes |
| --- | --- | --- |
| Python suite | 193 passed | Backend, installer and bridge contracts, including mastery identity validation |
| JavaScript suite | 84 passed | UI/session contracts, mastery availability and refresh after returning from the game |
| Native fixtures | 14 executables passed | Existing native component fixtures; these are not complete game coverage |
| Live native mastery checks | 19 passed | Incomplete quest and wrong-ID refusal, campaign rewards, normal first-choice panel, three enabled options, dismissal, normal confirmation and refusal after choosing |
| Real game restart | Passed | Chosen mastery persisted and recovery remained unavailable |
| Live browser against research core | 4 passed | Both themes, live reads, selected-mastery restriction and no implicit actions |
| Final compiled player EXE | 20 package/installer checks passed | Inventory, bundled Python, English assets, exact DLLs, read-only source handling, update backups, custom Unicode path and API/session checks |
| Final EXE + player DLL + actual DOM button | 4 UI checks plus native readback passed | Exactly one explicit mastery request opened the native panel; mastery remained unassigned and rewards unchanged; research command unavailable |

The hook fixture's concurrent install/remove stress test initially exceeded a
30-second limit. A progress-instrumented diagnostic copy completed 50/50 checks
in 81.66 seconds. The unchanged standard executable then completed 50/50 in
105.54 seconds, with zero incorrect caller results and zero failed installs or
removals. The earlier timeouts remain recorded; they were not counted as passes.

The two research mastery sessions and two final-player sessions closed normally
with exit code **0 (0x00000000)**. All **54 original save files** were restored
and verified byte-for-byte after each test series; only test clone files were
archived. The original installed mod runtime was also restored.

## Scope and limits

The selection/confirmation helper exists only in research builds and was used
on the isolated test character to invoke the game's own buttons. The distributed
player DLL excludes that helper. The final EXE test clicked the actual web UI
button headlessly and verified the real native panel, without mouse/keyboard
takeover. This run did not repeat every dev.2 skill, minion or boss-reward test.
See [feature-validation-2026-10-11.md](feature-validation-2026-10-11.md) for those
results and limitations.

Earlier dev.2 and vanilla comparisons returned **0xC0000005** at full game exit.
The successful mastery session exits do not establish that the earlier cause
has been resolved for all users. Interactive file-picker and UAC approval were
not repeated in this run. The new binaries have not received a new VirusTotal
scan; an older archive's clean report does not apply to this candidate.

The requested one-click crafting-material quantity feature is deferred and is
not included in this candidate. No new files were uploaded to GitHub or Nexus
as part of this verification.

Evidence is retained locally under `research/live/mastery-20261011/` and
`research/live/mastery-player-20261011/`; private saves, logs, research helpers
and SDK files are excluded from the player archive.
