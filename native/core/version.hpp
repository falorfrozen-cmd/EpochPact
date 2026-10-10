#pragma once

namespace ep {

#ifdef EPOCHPACT_RESEARCH
constexpr const char* kVersion = "0.1.0-research";
#else
constexpr const char* kVersion = "0.1.0";
#endif

}  // namespace ep
