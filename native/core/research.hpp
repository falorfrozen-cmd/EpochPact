// Research-build helpers for live checks driven by code (no clicking in the game):
// objects captured as the game creates them, and commands that
// call the game's own functions on the main thread. Not part of a player build.
#pragma once

#include <string>
#include <vector>

namespace ep::research {

// Installs the capture hooks.
void Init();

// Sets Application.runInBackground so the game keeps ticking (and commands keep running)
// while its window is not focused. Call after mainthread::Init(); it retries in the
// background until the game gets one focused moment to run the job.
void KeepTicking();

// Called from the command loop; retries KeepTicking's job now and then until it lands.
void Housekeep();

// Handles `charsel`, `load <i>`, `xpread`, `xpgain <n>`, `xpkill`, `enemies`.
// Returns false when `cmd` is not a research command.
bool Handle(const std::string& cmd, const std::vector<std::string>& args, std::string* reply);

std::string Status();

}  // namespace ep::research
