// Work that must run on the game's main thread (anything that touches Unity objects).
//
// Run() queues a job and waits for it. The queue is drained from a hook on
// UnityEngine.EventSystems.EventSystem.Update, which runs every frame in the menus and in
// the world. The hook is installed only while jobs are waiting and removed again after
// two idle seconds, so an idle EpochPact adds nothing to a frame.
#pragma once

#include <functional>
#include <string>

namespace ep::mainthread {

bool Init();

// Runs `job` on the main thread; false (with the reason) if it did not run within `timeoutMs`.
bool Run(std::function<void()> job, unsigned timeoutMs, std::string* why);

// Called by the command loop on each poll: removes the frame hook after two idle seconds.
void Housekeep();

bool HookInstalled();

}  // namespace ep::mainthread
