// Research-build helpers for live checks driven by code (no clicking in the game):
// objects captured as the game creates them, and commands that
// call the game's own functions on the main thread. Not part of a player build.
#pragma once

#include <string>
#include <vector>

namespace ep::research {

// Installs the capture hooks.
void Init();

// Handles `charsel`, `load <i>`, `xpread`, `xpgain <n>`, `xpkill`, `enemies`.
// Returns false when `cmd` is not a research command.
bool Handle(const std::string& cmd, const std::vector<std::string>& args, std::string* reply);

std::string Status();

}  // namespace ep::research
