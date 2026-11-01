#pragma once
#include <string>

namespace ep::progression {
bool Init();
std::string Read();
std::string Identity();
// The campaign with its normal rewards; keepLevel: no quest experience and no level-55 top-up.
std::string Complete(const std::string& expectedId, bool keepLevel = false);
std::string UnlockWaypoints(const std::string& expectedId);
// Raise the character to a level (2-100) after a recovery backup; a lower level is refused.
std::string SetLevel(const std::string& expectedId, int target);
std::string OpenMastery(const std::string& expectedId);
// Capture touches game objects and must run on the main thread. Writing the
// copied strings touches only the filesystem and belongs on the IPC worker.
struct SnapshotData {
    std::string character, stash, global, stashId, name, id, operation, rewards;
};
SnapshotData CaptureSnapshotCurrent(const std::string& expectedId, const char* operation);
std::string WriteCapturedSnapshot(const SnapshotData& snapshot);
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string TestMastery(const std::string& expectedId, const std::string& action);
// Isolated crafting fixture setup only; excluded from every player build.
std::string SnapshotFixtureCurrent(const std::string& expectedId, const char* operation);
#endif
void SaveCurrent(const std::string& expectedId);
}
