#pragma once
#include <string>
// Rage of Morditas (Season 5) settings: the game's own encounter data, changed in
// memory only while offline play runs (never in the save) and put back on reset.
namespace ep::rage {
bool Init();
void Frame();  // main thread, from the convenience frame callback
std::string Read();
std::string Set(const std::string& key, double value);
void ReleaseRoots();
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Test(const std::string& expectedId, const std::string& action);
#endif
}  // namespace ep::rage
