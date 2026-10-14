#pragma once
#include <string>
namespace ep::convenience {
bool Init();
std::string Read();
std::string Set(const std::string& key,double value);
std::string Summons();
// Damage session for summons analysis: damagesession [start|stop|reset].
std::string DamageSession(const std::string& action);
// Interaction area: qolarea radius <1-30> | shape <0 circle|1 square> | show <0|1> | color <RRGGBB>.
std::string SetArea(const std::string& key, const std::string& value);
std::string InventoryRead();
std::string Inventory(const std::string& expectedId,const std::string& action);
void ReleaseRoots();
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Test(const std::string& id,const std::string& action);
#endif
}
