// Inventory and stash helpers (offline only), with the game's own Sort:
//   invread                                   the settings below and counters
//   invset autosort 0|1                        sort the inventory each time it opens
//   invset autosortall 0|1                     sort every stash tab each time it is shown
//   invset sellkeep 0|1|2                      Sell all: 0 sells everything, 1 keeps Unique/Set/Legendary,
//                                              2 also keeps Exalted items
//   invsorttab <tab ID> 0|1                    sort this stash tab each time it is shown
// The settings are kept in <game>\EpochPact\auto-sort.txt.
#pragma once
#include <string>

namespace ep::invtools {
bool Init();
void Frame();  // from the convenience frame callback
std::string Read();
std::string Set(const std::string& key, int value);
std::string SetTab(int tabId, bool on);
// Sell all keeps this item (the sellkeep setting).
bool KeepWhenSelling(void* itemData);
// Main thread, once per frame of a RunSteps job that moves inventory items: true once every open
// inventory window has finished drawing its items for ten frames in a row (an item moved while
// a window draws can leave its picture behind). Counts in the two ints; throws after 600 frames.
bool InventorySettled(int& drawnFrames, int& waits);
// Main thread, after EpochPact moved items: redraws the open inventory and stash windows whose
// pictures no longer match their items. Returns how many were redrawn.
int RedrawStale();
}
