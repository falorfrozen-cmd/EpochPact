#pragma once
#include <string>

namespace ep::progression {
bool Init();
std::string Read();
std::string Identity();
std::string Complete(const std::string& expectedId);
std::string UnlockWaypoints(const std::string& expectedId);
// Capture touches game objects and must run on the main thread. Writing the
// copied strings touches only the filesystem and belongs on the IPC worker.
struct SnapshotData {
    std::string character, stash, global, stashId, name, id, operation, rewards;
};
SnapshotData CaptureSnapshotCurrent(const std::string& expectedId, const char* operation);
std::string WriteCapturedSnapshot(const SnapshotData& snapshot);
// Called on the game main thread by other progression features.
std::string SnapshotCurrent(const std::string& expectedId, const char* operation);
void SaveCurrent(const std::string& expectedId);
}
