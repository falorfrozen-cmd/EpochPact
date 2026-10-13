# Blessing choices and tree connections — validation (2026-10-13)

Steam Last Epoch 1.5.2.1, offline, on fresh isolated copies of the first
character (EpFeedbackTest, Mage). Every session restored and byte-verified the
54 save files and the installed runtime; the real character was never loaded.

## Blessing choices

Player report: "Blessings are not spawning 5 at the end of a bossfight."

After a Monolith boss, `MonolithZoneManager.OnQuestCompleted` calls
`ItemContainersManager.populateBlessingOptions(timeline, difficulty,
numberOfStandardBlessings, extraBlessings)` — extras are +1 at 50 and +2 at 200
corruption — and then opens the reward panel through
`UIBase.OpenBlessingOptions(timeline, difficulty, numberOfBlessings)`.

- `populateBlessingOptions` always asks the generator
  (`GenerateItems.rollBlessingsForTimelineDifficulty`) for **3** + extra and
  fills only standard + extra option slots.
- The reward panel (`BlessingRewardPanelManager.OnOptionsPopulated`) hides
  option slots 4 and 5 unless its count reaches them.

The setting only changed the generator, so five blessings were rolled and three
were shown. It also replaced the game's extras with 0, so with a lower setting
than the corruption total the slots asked for blessings that were not rolled.

Fix: the same rule (`rules::BlessingStandard`/`BlessingShown`, never below the
game's own total) is applied to the generator, the option slots and the panel
count. Unit tests check that the filled and shown counts agree for every
setting and extra.

Option slots filled by the game's own `populateBlessingOptions` (all nine slots
cleared first):

| Setting \ corruption extras | +0 | +1 | +2 |
|---|---|---|---|
| native (0) | 3 | 4 | 5 |
| 3 | 3 | 4 | 5 |
| 4 | 4 | 4 | 5 |
| 5 | 5 | 5 | 5 |

Maximum blessing rolls applied to all five. With the setting at 5, the game's
`OpenBlessingOptions` asked for 3 and the "Timeline stabilized — Activate
Blessing" panel showed five active options (screenshot in the local evidence).

## Tree connections

Each tree was filled through the game's own spend command, deepest nodes first.

| Tree | Result |
|---|---|
| Passive, Runemaster (not chosen, Sorcerer chosen) | 30 of 30 nodes to their maximum, 0 faults |
| Passive, Spellblade (not chosen) | 32 of 32 nodes, 0 faults |
| Skill tree without "Ignore skill node connections" (control) | 9 of 26 nodes — the game's connections apply |
| Skill trees with the switch | 31 of 31 and 26 of 26 nodes, 0 faults |
| Weaver tree with the switch (after joining The Woven) | 79 of 79 nodes, 0 faults |

Persistence: passive 417, skills 101 + 70 and Weaver 173 points were identical
node for node after a zone change (Z32 → Z52) and after leaving to character
selection and loading the character again.

## Automated (0.1.2-dev.25)

Native 19/19 programs, Python 292, JavaScript 161. Package: player checks 24/24
(including Install update), connection checks 4/4; Windows Defender found no
threats in the ZIP, EXE, core and loader. VirusTotal was not run; hashes in
`docs/security/virustotal-20261013-dev25.json`. Live checks used the test flavour
of the core; the player core is built from the same sources.
