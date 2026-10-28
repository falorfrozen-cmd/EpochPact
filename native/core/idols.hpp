// Idols with more affixes. The game gives a non-unique idol one prefix and one suffix; with these settings each
// idol the game makes outside shops and gamblers (drops, rewards, Nemesis, Spawn Items) rolls 1..n of each.
//
//   idolprefixes <1-3>   each idol rolls 1 to n prefixes (1: the game's own)
//   idolsuffixes <1-3>   each idol rolls 1 to n suffixes (1: the game's own)
//
// After GenerateItems.GenerateAffixes, the extra affixes are picked as the game picks them: among the affixes its
// own IsValidAffix allows on that idol (level, idol type, class, no repeats), weighted by AffixWeighting, with a
// tier from RollTier and a random roll; then the item's saved form is rebuilt (RebuildID) and read back. Weaver and
// Heretical idols keep the game's rules. Offline only.
#pragma once

#include <string>

namespace ep::idols {

bool Init();
std::string SetPrefixes(double n);
std::string SetSuffixes(double n);
std::string Status();

}  // namespace ep::idols
