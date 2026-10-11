#pragma once
#include <string>
namespace ep::convenience {
bool Init();
std::string Read();
std::string Set(const std::string& key,double value);
std::string Summons();
std::string InventoryRead();
std::string Inventory(const std::string& expectedId,const std::string& action);
void ReleaseRoots();
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Test(const std::string& id,const std::string& action);
#endif
}
