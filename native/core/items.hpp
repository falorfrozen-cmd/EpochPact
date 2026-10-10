// Item quality and looting on top of loot.cpp's drop multiplier.
//
//   rarity <1-10>      every RollRarity result has a (mult-1)/mult chance to come out one
//                      tier better (normal -> magic -> rare -> exalted -> unique/set)
//   autopickup <0|1>   once per ~0.75 s, ask the game to pick up every ground item label
//                      and every active gold pile, potion, xp tome, favor tome and ancient
//                      bone in the zone (the same entry points a click uses)
#pragma once

#include <string>

namespace ep::items {

bool Init();
std::string SetRarity(double m);
std::string RarityStatus();
std::string SetAuto(double on);
std::string AutoPickupStatus();
std::string Status();

}  // namespace ep::items
