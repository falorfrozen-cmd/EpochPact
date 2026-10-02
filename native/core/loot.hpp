// Gold, item drops and monster density, each a multiplier like the experience one:
// offline play only, and x1 takes every hook back out.
//
//   gold <1-100>     gold picked up from the ground (GroundItemManager.pickupGold ->
//                    GoldTracker.modifyGold); spending, quest rewards and vendors are untouched
//   drops <1-25>     ItemDrop.DropItem's itemMultiplier, the item count every loot source
//                    (enemy deaths, objectives, arenas, bosses) passes to the game's drop roll
//   density <1-5>    monsters per pack: Spawner.numberToSpawn scaled while
//                    Spawner.GenerateEntitiesInternal rolls the pack; single spawns (bosses,
//                    unique enemies) stay single
#pragma once

#include <string>

namespace ep::loot {

bool Init();
std::string SetGold(double m);
std::string SetDrops(double m);
std::string SetDensity(double m);
std::string Status();

}  // namespace ep::loot
