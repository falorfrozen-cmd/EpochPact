#pragma once
#include <string>
namespace ep::mapview {
bool Init();
std::string Read();
std::string Set(double enabled);
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Capture();
#endif
}
