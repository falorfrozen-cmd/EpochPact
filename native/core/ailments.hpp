#pragma once
#include <string>
// Ailment stack cap and duration multipliers for the ailments the player's side applies (offline only, nothing saved).
namespace ep::ailments {
bool Init();
void Frame();  // main thread, from the convenience frame callback
std::string Read();
std::string Set(const std::string& key, double value);
void ReleaseRoots();
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Test(int id, int stacks, bool foe);  // foe: applied with an enemy's alignment
#endif
}  // namespace ep::ailments
