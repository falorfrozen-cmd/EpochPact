#pragma once

#include <string>
#include <vector>

namespace ep::eternity {

// The game's own Eternity Cache panel anywhere (offline play only): see eternity_cache.cpp.
std::string Open(const std::string& expectedId);
std::string Status();
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
// eternitytest <save id> state | put <x> <y> | seal <affix id|none> | uiseal <dropdown index>
std::string Test(const std::string& expectedId, const std::vector<std::string>& args);
#endif

}  // namespace ep::eternity
