#pragma once
#include <string>
namespace ep::mapview {
bool Init();
std::string Read();
std::string Set(double enabled);
// Session-only minimap/overlay display: mapset <key> <value> | mapset reset.
std::string SettingsRead();
std::string SetSetting(const std::string& key, double value);
// mapset hideoutbutton: false hides the in-game UI's Hideout button under the minimap.
bool HideoutButton();
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Capture();
#endif
}
