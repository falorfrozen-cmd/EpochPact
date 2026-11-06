// In-game support report: a hotkey (F8 by default) saves a folder under
// <game>/EpochPact/reports with a screenshot, game/EpochPact state, recent logs
// (Windows user and computer names masked) and a description file to fill in.
// Nothing is sent anywhere.
//   reportread                    hotkey and last report
//   reportkey F5-F12|off          choose or turn off the hotkey (remembered)
//   reportnow                     the same report without the hotkey
#pragma once
#include <string>

namespace ep::report {
bool Init();
std::string Read();
std::string SetKey(const std::string& key);
void Notice(const std::string& title, const std::string& text);  // the game's own notice at the top of the screen
std::string Create(const char* trigger);
}
