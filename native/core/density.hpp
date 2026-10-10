// Offline pack-size multiplier. Reads runtime metadata by name; x1 removes all hooks.
// Changes future generation only. densityread is an on-demand scene/queue snapshot.
#pragma once
#include <string>
namespace ep::density {
bool Init();
std::string Set(double multiplier);
std::string Status();
std::string Read();
}
