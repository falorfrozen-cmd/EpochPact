#pragma once
#include "stat_key.hpp"
#include <string>
#include <vector>

namespace ep::statedit {
bool Init();
std::string Read(Key key);
std::string Set(Key key, Mode mode, double value);
std::string RawCommand(const std::vector<std::string>& args);
std::string SheetCommand(const std::vector<std::string>& args);
std::string Catalog();
std::string ResetAll();
std::string PlayerRead();
std::string PlayOffline();
std::string SheetOpen(bool open);
// Background character loading follows the game's tile click flow, without desktop input.
std::string Characters(const std::string& name = {}, bool load = false, int level = -1);
// Research travel uses an already unlocked waypoint and its ordinary UI load method.
std::string Waypoints(const std::string& target = {}, const std::string& expectedId = {});
std::string Exits(const std::string& target = {});
}  // namespace ep::statedit
