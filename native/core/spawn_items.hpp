// Spawn items (Crafting → Spawn Items): offline play only, the loaded character's own item generator.
//
//   spawnlist                                          the base items, Uniques and Sets that can be spawned
//   spawnitem <save_id> <item> <rarity> <lp> <level> <count>
//       item    b<type>.<subtype> (a base item) or u<unique id> (a Unique or Set item)
//       rarity  base items: 0 Normal, 1 Magic, 2 Rare, 3 Exalted (ignored for Uniques and Sets)
//       lp      Uniques: -1 the game's roll, 0-4 that Legendary Potential (Weaver's Will Uniques and Sets roll their own)
//       level   item level 1-100; count 1-20, dropped around the character
#pragma once

#include <string>

namespace ep::spawn {
constexpr int kMaximumCount = 20;
std::string List();
std::string Spawn(const std::string& saveId, const std::string& item, int rarity, int lp, int level, int count);
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
// Test builds: every listed item from the generator at item level 100, compared with the request; nothing dropped.
std::string Check(int rarity, int lp, int from, int count);
// Test builds: count idols of every idol base from the generator at the item level; each one's affixes (kind, ID,
// tier, roll) and whether its saved form reads back the same; nothing dropped.
std::string IdolCheck(int count, int level);
std::string ForgingCheck(int count, int level);  // Forging Potential of new items (fpcheck)
#endif
}  // namespace ep::spawn
