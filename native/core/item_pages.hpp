#pragma once
#include <string>

// Extra pages for the inventory and for the idols with their altar. One page is in the game's own containers; the others wait in
// a file per character. A switch writes the file first, then swaps, then saves the game, so a crash at any point loses nothing.
namespace ep::itempages {
constexpr int kPages = 4;
enum Kind { kInventory = 0, kIdols = 1 };
std::string Read(bool full);
// page 1..kPages. Refused in a transition, or while no offline character is loaded.
std::string Switch(Kind kind, int page);
// Every waiting item goes to the stash, for a player who wants to stop using the pages or remove the mod.
std::string ToStash();
// The page in the game's container now; 0 without a loaded offline character. Main thread.
int Current(Kind kind);
// What each page holds, for the buttons. Main thread.
int Count(Kind kind, int page);
void Frame();
}  // namespace ep::itempages
