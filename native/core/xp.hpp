// Experience multiplier: scales the experience a kill or an experience mote gives, at
// ExperienceTracker.GainExpFromEnemyOrMote(long), before the game applies its own level
// difference and under-level rules. Offline play only; x1 removes the hook entirely.
#pragma once

#include <string>

namespace ep::xp {

constexpr double kMax = 100.0;

bool Init();
std::string Set(double multiplier);  // the reply line for the command
std::string Status();
void* Target();                      // the hooked function's entry (for research calls)

}  // namespace ep::xp
