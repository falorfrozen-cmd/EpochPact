#pragma once
#include <string>
namespace ep::collection {
std::string Catalog();
std::string Items(int offset);
std::string RollsRead();  // test only: inventory Unique, Set and Legendary roll bytes (rollsread)
}
