#pragma once
#include <string>
namespace ep::effects {
bool Init();
void Frame();
std::string Read();
std::string Set(const std::string& key,double value);
void ReleaseRoots();
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Test(const std::string& expectedId,const std::string& action);
#endif
}
