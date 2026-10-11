#pragma once
#include <string>
namespace ep::workshop {
void Load();
void Ensure();
std::string Catalog();
int Recipe(const std::string& selections,bool perfect,float baseEffect);
bool Owns(int id);
}
