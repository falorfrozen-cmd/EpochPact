// Item quality and looting on top of loot.cpp's drop multiplier.
//
//   rarity <1-10>      every RollRarity result has a (mult-1)/mult chance to come out one
//                      tier better (normal -> magic -> rare -> exalted -> unique/set)
//   weaverswill <1-10> the Weaver's Will a Unique rolls (ItemData.RollWeaversWill) times
//                      mult, up to the game's maximum (28); every source rolls it there
//                      (RollBaseItem, RevealCocoonedUnique, ... via RollLegendaryProperties)
//   lprolls <1-1000>   the Legendary Potential a Unique rolls (ItemData.RollLegendaryPotential) is rolled
//                      up to this many times and the highest kept (never above the game's maximum)
//   autopickup <0|1>   once per ~0.75 s, ask the game to pick up every ground item label
//                      and every active gold pile, potion, xp tome, favor tome and ancient
//                      bone in the zone (the same entry points a click uses)
#pragma once

#include <string>

namespace ep::items {

bool Init();
std::string SetRarity(double m);
std::string RarityStatus();
std::string SetWeaversWill(double m);
std::string WeaversWillStatus();
std::string SetLegendaryRolls(double n);
std::string LegendaryRollsStatus();
std::string SetAuto(double on);
std::string AutoPickupStatus();
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string PickupDiagnostics();  // test only: why collection paused or did nothing
#endif
std::string SetRadius(double radius);
std::string RadiusStatus();
std::string SetInterval(double seconds);
// Explicit one-shot collection of every loaded drop (filter and capacity apply).
std::string PickupNow();
// Recent item and gold pickups (by EpochPact or the player), newest last.
std::string LootLog(unsigned long long since);
std::string ClearLootLog();
// Includes pending V2 tooltips. The normal mouse pickup is never intercepted.
bool IsInspectingItem();
std::string Status();

}  // namespace ep::items
