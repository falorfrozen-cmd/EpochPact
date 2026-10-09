#pragma once
#include <string>
namespace ep::monolith {
bool Init();
std::string Read();
std::string Echoes(const std::string& id, int timeline, int difficulty);
std::string FocusEcho(const std::string& id, int timeline, int difficulty, int index);
std::string PanelReady(const std::string& id, int timeline, int difficulty);
std::string Unlock(const std::string& id);
std::string Select(const std::string& id, int timeline, int difficulty);
std::string Corruption(const std::string& id, int timeline, int difficulty, int value);
std::string Stability(const std::string& id, int timeline, int difficulty, int value);
std::string SetMultiplier(double value);
std::string Status();
#ifdef EPOCHPACT_RESEARCH
std::string TestGain(const std::string& id, int timeline, int difficulty, int amount);
#endif
}
