#pragma once
#include <string>
// Plays the Rogue with Zerrick's body (the campaign's male rogue), display only: in play and on the character screens.
namespace ep::bodyswap {
bool Init();
void Frame();  // main thread, from the convenience frame callback
void Want(bool on);  // the player's choice (Wardrobe), remembered in a file
bool Wanted();
std::string Command(const std::string& action);  // on | off | read
void ReleaseRoots();
}  // namespace ep::bodyswap
