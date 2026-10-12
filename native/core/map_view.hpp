#pragma once
#include <string>
namespace ep::mapview {
bool Init();
std::string Read();
std::string Set(double enabled);
// Session-only minimap/overlay display: mapset <key> <value> | mapset reset.
std::string SettingsRead();
std::string SetSetting(const std::string& key, double value);
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Capture();
#endif
}
