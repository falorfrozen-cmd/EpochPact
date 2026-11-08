#pragma once
#include <string>
// Quad stash tabs: a chosen stash tab holds 24 x 34 cells instead of the game's 12 x 17, four times the items.
// The choice is kept in %LOCALAPPDATA%\EpochPact\quad-stash.txt by the tab's own id; the tab's container is
// made larger before the game loads its items, and the stash window draws its cells at half size.
// Offline only. Items keep their places in the game's save; without EpochPact the game cannot show
// items outside its 12 x 17, so a tab is made normal again only when everything fits.
namespace ep::quadstash {
bool Init();
void Frame();  // main thread
std::string Read();
// index: the tab's place in the stash, -1 for the tab shown now. Making a tab normal is refused while
// anything lies outside 12 x 17.
std::string Set(int index, bool on);
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Look();  // the stash window's objects around its grid
std::string TestMove(int x, int y, int number = -1);  // the first inventory item into the tab shown (or tab number), at x, y
std::string ConfigureOpen();
std::string ConfigureLook();
std::string ConfigureCheck(int value);
std::string Shot(const std::string& path);  // a screenshot by the game, no notice  // 0|1 the check, 2 Confirm, 3 Cancel  // the first inventory item into the tab shown, at x, y
#endif
}  // namespace ep::quadstash
