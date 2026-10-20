// Interface font and text size (display only, session).
//   fontread                      current choice, Windows font families and counters
//   fontset family <name|game>    a Windows font for all interface text ("%20" for spaces); game restores
//   fontset scale <0.7-1.5>       text size multiplier; 1 restores
#pragma once
#include <string>

namespace ep::fonts {
bool Init();
std::string Read();
std::string Set(const std::string& key, const std::string& value);
// The in-game window's root Transform (nullptr: none): its texts are the desktop app's and keep
// their own font and size. Main thread.
void SetWindowRoot(void* transform);
void ReleaseRoots();  // before IL2CPP shutdown while GC handles are valid
}
