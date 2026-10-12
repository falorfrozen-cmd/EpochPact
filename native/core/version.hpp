#pragma once
#include "../version_info.h"

namespace ep {

#ifdef EPOCHPACT_RESEARCH
constexpr const char* kVersion = EP_VERSION_STR "-research";
#elif defined(EPOCHPACT_TESTING)
constexpr const char* kVersion = EP_VERSION_STR "-test";
#else
constexpr const char* kVersion = EP_VERSION_STR;
#endif

}  // namespace ep
