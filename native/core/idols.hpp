// Idols and idol altars with more affixes. The game gives a non-unique idol one prefix and one suffix and a Rare
// idol altar two of each; with these settings each one the game makes outside shops and gamblers (drops, rewards,
// Nemesis, Spawn Items) rolls up to n - 1 more of each kind.
//
//   idolprefixes <1-3>   each idol rolls 1 to n prefixes, each Rare idol altar 2 to n + 1 (1: the game's own)
//   idolsuffixes <1-3>   the same for suffixes
//
// After GenerateItems.GenerateAffixes, the extra affixes are picked as the game picks them: among the affixes its
// own IsValidAffix allows on that item (level, type, class, no repeats; a Weaver idol's Weaver affixes too),
// weighted by AffixWeighting, with a tier from RollTier and a random roll; then the item's saved form is rebuilt
// (RebuildID) and read back. Heretical idols keep the game's rules. Offline only.
#pragma once

#include <string>

namespace ep::idols {

bool Init();
std::string SetPrefixes(double n);
std::string SetSuffixes(double n);
std::string Status();

}  // namespace ep::idols
