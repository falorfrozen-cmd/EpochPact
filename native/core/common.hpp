// EpochPact core: shared paths and the log.
#pragma once

#include <windows.h>
#include <string>

namespace ep {

// <game>\EpochPact\ with a trailing backslash (wide), set once by InitPaths().
const std::wstring& PluginDir();
bool InitPaths();

// Appends one timestamped line to <game>\EpochPact\logs\core.log (printf-style).
void Log(const char* fmt, ...);

// Creates every missing directory on the way to `dir`.
bool EnsureDir(const std::wstring& dir);

}  // namespace ep
