# Passive tree allocation fix — validation (2026-10-13)

Steam Last Epoch 1.5.2.1, offline, on fresh isolated copies of the first
character (EpFeedbackTest, Mage, 545 passive points with Passive points ×10).
Afterwards the 54 save files were restored and byte-verified, and the installed
`version.dll`, core and `installed-build.json` were put back and hash-verified.
The real character was never loaded.

## Player report

With **Tree connections → Passives** on: in a mastery tree the character did
not choose (a Necromancer in the Warlock tree), some nodes took all their points,
some stopped at one point and some took none; a Sentinel could not place some
points either.

## Cause

Tree connections lets the game's own spend command check a copy of each passive
node with its two mastery thresholds removed. The copy was made field by field,
and `recalculatedRequirementReferences` — a `bool` — was copied as if it were a
reference. The game sets that flag once it has resolved a node's requirements,
which the passive panel does for the nodes it shows. From then on the copy read
address `0x1`, faulted, and the fault handler fell back to the game's unmodified
node: that node was checked against its real thresholds again. A node's first
point often went through (flag not yet set) and the next were refused, which is
the "only one point" pattern. Nothing else in the spend path changes the result:
the game's checks are point cap, available points, a chosen mastery for mastery
nodes, that same mastery for nodes needing 25 or more points in it, the mastery
threshold, 20 base-class points before mastery nodes, and connections (constants
read in game: 20, 25, 45, 22).

Fix: fields are copied by their declared IL2CPP type (`rules::CopyKind`, unit
tested). Threshold fields must be bytes or the copy is refused.

## Live results

The passive panel the game shows is the banner panel. A mouse click on its nodes
runs `SkillTreeNode.Clicked` → `TreeUIManager.characterTreeNodeClicked` →
`receiveSpendPassivePointCommand`. Tests spent through that path and through the
spend command directly, highest thresholds first, with Sorcerer chosen.

| Run | Runemaster (not chosen), 30 nodes | Spellblade (not chosen), 32 nodes | Mage base, 15 nodes | Faults |
|---|---|---|---|---|
| Previous behaviour, spend command | 0 points in all 30 | 0 points in all 32 | — | 62 |
| Previous behaviour, panel clicks | 0 points in all 30 | 0 points in all 32 | 6 nodes stopped at 1 point | 125 |
| Fixed, spend command | all to cap (212) | all to cap (205) | — | 0 |
| Fixed, panel clicks | all to cap | all to cap | all to cap | 0 |

An earlier run without a chosen mastery also filled every node of all four
trees to its cap until the points ran out.

## Automated

Native 19/19 programs (including the field copy rules), Python 292, JavaScript
161. Package: player checks 24/24 (including Install update), connection checks
4/4; Windows Defender found no threats in the ZIP, EXE, core and loader.
VirusTotal was not run; hashes in `docs/security/virustotal-20261013-dev24.json`.
Live checks used the test flavour of the core; the player core is built from the
same sources. Test actions used: `passive-map-N`, `passive-jump-N`,
`choose-mastery-N`, `passive-ui-open|show-N|press-N|state|close` (test core
only, isolated clone names only).

## Also fixed

A research-build test action (`smart_loot` tooltip close) wrote the address of a
local null into two reference fields; it now writes null. It is not part of the
player build.
