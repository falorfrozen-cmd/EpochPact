#pragma once
#include <string>
namespace ep::crafting {
bool Init();
std::string Read();
std::string Set(const std::string& key, double value);
std::string Forge(const std::string& id);
std::string Custom(const std::string& id, int templateId, const std::string& affixes, bool perfect,const std::string& modifiers="");
std::string MaterialsRead();
std::string MaximizeMaterials(const std::string& id, int target);
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Test(const std::string& id, const std::string& action);
#endif
}
