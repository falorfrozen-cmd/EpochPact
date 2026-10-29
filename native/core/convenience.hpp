#pragma once
#include <string>
namespace ep::convenience {
bool Init();
std::string Read();
std::string Set(const std::string& key,double value);
std::string Summons();
// Damage session for summons analysis: damagesession [start|stop|reset].
std::string DamageSession(const std::string& action);
// The damage window's icon for a source: damageicon a:<skill ID>|c:<class>:<mastery>
// (a PNG of at most 64x64, base64; png is null when the source has no icon).
std::string DamageIcon(const std::string& key);
// The death recap: the last deaths with the hits before each; deathrecap 0|1 turns recording off/on, clear empties it.
std::string DeathRecap(const std::string& action);
// Interaction area: qolarea radius <1-30> | shape <0 circle|1 square> | show <0|1> | color <RRGGBB>.
std::string SetArea(const std::string& key, const std::string& value);
std::string InventoryRead();
std::string Inventory(const std::string& expectedId,const std::string& action);
// The stash opens next on this tab (one EpochPact opened): the stash tab memory remembers it. Main thread.
void PreferStashTab(int tab);
// The open inventory panel's frame as fractions of the screen, from its top-left corner; false while the
// inventory is closed. Main thread.
bool InventoryBounds(float* left,float* top,float* right,float* bottom);
void ReleaseRoots();
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Test(const std::string& id,const std::string& action);
#endif
}
