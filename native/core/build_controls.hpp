#pragma once
#include <string>
namespace ep::builds {
bool Init();
void ReleaseRoots(); // Called before IL2CPP shutdown while GC handles are valid.
std::string Read();
std::string Set(const std::string& key, double value);
std::string Respec(const std::string& expectedId, const std::string& kind);
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Test(const std::string& id, const std::string& action);
#endif
}
