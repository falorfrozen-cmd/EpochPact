// The Woven Enchanter's uses (offline play only):
//
//   enchanteruses <1-40>   the uses a Woven Enchanter allows each time you meet one ("1/1 uses remain"),
//                          times the number: its ZoneRestrictedCraftingManager (CraftingType IdolEnchantment)
//                          gets maxUses x n after the game's own CalculateMaxUses. 1 = the game's own.
//                          Memory Amber costs and the craft itself stay the game's.
#pragma once

#include <string>

namespace ep::enchanter {

bool Init();
std::string SetUses(double n);
std::string Status();
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
// An isolated ZoneRestrictedCraftingManager on a new hidden object (limit type 0 Constant or 1
// IdolEnchantment): its uses before and after one use and a change of the setting, then it is destroyed.
std::string Check(int limitType);
#endif

}  // namespace ep::enchanter
