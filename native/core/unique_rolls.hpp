// Unique minimum roll: new Uniques, Sets and Legendaries roll their unique modifiers and implicits in the best
// part of each range.
//
//   uniqueroll <0-100>  once GenerateItems.RollBaseItem has made an item (or ItemData.RevealCocoonedUnique has opened a
//                       Weaver cocoon), each roll (0-255) keeps its place but within
//                       the best (100-n)% of its range, toward the end the game itself calls best
//                       (ItemData.MaxRollIsBestForUniqueRollID; implicits: the higher, as the game keeps it when it
//                       rerolls them). 100: every value at its best; 0: off, hooks removed. Gamblers
//                       (GenerateItems.rollItemForGambling, ...SoulGambling, ...FactionGambling) and rolls made by
//                       crafting or similar-item rewards keep the game's own.
#pragma once

#include <string>

namespace ep::uniquerolls {

bool Init();
std::string Set(double percent);
std::string Status();

}  // namespace ep::uniquerolls
