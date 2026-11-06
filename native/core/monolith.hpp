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
// Skip quest echoes before the boss once a Normal run's Stability is full.
std::string SetQuestSkip(bool on);
// Boss doors in the Monolith Hub and Boss Rush. BossFrame runs on the main thread each frame.
std::string BossDoors(bool on);
std::string SetNoBlessing(bool on);
std::string BossStatus();
void BossFrame();
std::string Status();
#ifdef EPOCHPACT_TESTING
}
#include <vector>
namespace ep::monolith {
std::string BossTest(const std::string& id, const std::vector<std::string>& args);
#endif
#ifdef EPOCHPACT_RESEARCH
std::string TestGain(const std::string& id, int timeline, int difficulty, int amount);
#endif
}
