// In-game panel (experiment): a hotkey (F6 by default) opens a small EpochPact window drawn
// with the game's own Unity UI on a canvas of its own. Its buttons send the ordinary commands
// (xp, density, autopickup) through commands::SubmitUi, so the commands' own limits and the
// offline-only gate apply unchanged.
#pragma once
#include <string>
namespace ep::ingame {
bool Init();
// From the convenience frame callback (the game's UI update, main thread).
void Frame();
// panel read | panel 0 | panel 1
std::string Panel(const std::string& action);
// panelkey F5-F12 | off
std::string SetKey(const std::string& key);
void ReleaseRoots();
}  // namespace ep::ingame
