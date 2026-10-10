// Gold and item drops, each a multiplier like the experience one:
// offline play only, and x1 takes every hook back out.
//
//   gold <1-100>     gold picked up from the ground (GroundItemManager.pickupGold ->
//                    GoldTracker.modifyGold); spending, quest rewards and vendors are untouched
//   drops <1-25>     ItemDrop.DropItem's itemMultiplier, the item count every loot source
//                    (enemy deaths, objectives, arenas, bosses) passes to the game's drop roll
#pragma once

#include <string>

namespace ep::loot {
bool SetCoFDropDependency(bool enabled, std::string* why);

bool Init();
std::string SetGold(double m);
std::string SetDrops(double m);
std::string Status();

}  // namespace ep::loot
