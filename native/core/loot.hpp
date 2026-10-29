// Gold and item drops, each a multiplier like the experience one:
// offline play only, and x1 takes every hook back out.
//
//   gold <1-100>     gold picked up from the ground (GroundItemManager.pickupGold ->
//                    GoldTracker.modifyGold); spending, quest rewards and vendors are untouched
//   drops <1-25>     ItemDrop.DropItem's itemMultiplier, the item count every loot source
//                    (enemy deaths, objectives, arenas, bosses) passes to the game's drop roll
//   bones <1-100>    Ancient Bones dropped for the player (GroundItemManager.dropAncientBoneForPlayer),
//                    so the pile on the ground already shows the larger amount
#pragma once

#include <string>

namespace ep::loot {
// Gold the game adds while one of these is open on this thread counts as picked up. The loot log's
// pickupGold hook (items.cpp) opens one around every pickup; a second hook on that method would be refused.
inline thread_local int goldPickupDepth = 0;
struct GoldPickupScope {
    GoldPickupScope() { ++goldPickupDepth; }
    ~GoldPickupScope() { --goldPickupDepth; }
    GoldPickupScope(const GoldPickupScope&) = delete;
    GoldPickupScope& operator=(const GoldPickupScope&) = delete;
};
bool SetCoFDropDependency(bool enabled, std::string* why);

bool Init();
std::string SetGold(double m);
std::string SetDrops(double m);
std::string SetBones(double m);
std::string Status();

}  // namespace ep::loot
