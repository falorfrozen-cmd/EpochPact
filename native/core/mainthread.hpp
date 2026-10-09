// Work that must run on the game's main thread (anything that touches Unity objects).
//
// Run() queues a job and waits for it. The queue is drained from a hook on
// UnityEngine.EventSystems.EventSystem.Update, which runs every frame in the menus and in
// the world. The hook stays installed once enabled; an idle frame takes only an atomic
// pending-count check before calling the original Update (no queue mutex).
#pragma once

#include <functional>
#include <string>

namespace ep::mainthread {

bool Init();

// Runs `job` on the main thread; false (with the reason) if it did not run within `timeoutMs`.
bool Run(std::function<void()> job, unsigned timeoutMs, std::string* why);
// Executes one bounded step per Unity frame; return true from the final step.
// A started operation is never abandoned while it still references caller state.
bool RunSteps(std::function<bool()> step, unsigned timeoutMs, std::string* why);
void KeepTicking();

// Called by the command loop on each poll: retained for command-loop compatibility; does not remove the frame hook.
void Housekeep();

bool HookInstalled();
std::string Telemetry(bool reset = false);

}  // namespace ep::mainthread
