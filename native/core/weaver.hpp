// Weaver's Will on top of the game's own rolls (offline play only):
//
//   weaversdrop <0-100>      percent of the Uniques rolled without a required legendary type that
//                            UniqueList.rollUnique is asked for as a Weaver's Will Unique first
//                            (the same level, item type and random-drop rules; 0 = off)
//   weaversupgrades <1-10>   every Weaver's Will upgrade the game grants after a kill
//                            (CharacterMutator.UpgradeRandomWeaversWillItem) runs that many times
//   weaveraffixes <ids|none> during ItemData.SpendWeaversWillToUpgrade a wanted affix below T7 is the
//                            one upgraded, and a new affix comes from the wanted ones that fit the item;
//                            when none fits, the game's own random choice stands
//   weaverread               the three settings and their counters
#pragma once

#include <string>

namespace ep::weaver {

bool Init();
std::string SetShare(double percent);
std::string SetSpeed(double multiplier);
std::string SetAffixes(const std::string& ids);
std::string Read();
std::string Status();

}  // namespace ep::weaver
