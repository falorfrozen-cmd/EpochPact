// The command channel: <game>\EpochPact\ipc\cmd.txt in (one command per line; the file is
// taken and deleted), <game>\EpochPact\ipc\out.txt out (each command echoed as "> cmd",
// then its reply). The same file contract as ForgePact's bp_ipc.
#pragma once

#include "il2cpp_api.hpp"

#include <string>

namespace ep::commands {

std::string Execute(const std::string& line);

// Polls cmd.txt every 100 ms forever. Call from the core's worker thread while it is
// detached from IL2CPP; each batch of commands attaches to `domain` while it runs.
void Loop(il2cpp::Domain* domain);

}  // namespace ep::commands
