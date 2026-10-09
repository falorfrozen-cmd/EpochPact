#pragma once
#include <string>
namespace ep::smartloot {
bool Init();
bool CanCollect();
bool Accept(void* label);
bool Category(int index);
std::string Read();
std::string Set(const std::string& key, const std::string& value);
#ifdef EPOCHPACT_RESEARCH
std::string Test(const std::string& id, const std::string& action);
#endif
}
