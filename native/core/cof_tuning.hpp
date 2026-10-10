#pragma once
#include <functional>
#include <string>
namespace ep::cof::tuning {
bool Init();
std::string Set(const std::string& setting, double value);
std::string Read();
std::string Status();
void WithEnemyLoot(bool enemyDeath, const std::function<void()>& original);
}
