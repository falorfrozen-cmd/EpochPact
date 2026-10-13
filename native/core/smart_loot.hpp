#pragma once
#include <string>
namespace ep::smartloot {
bool Init();
bool CanCollect();
bool Accept(void* label);
// Shards, runes and glyphs go to material storage, not the inventory grid.
bool IsMaterial(void* label);
// The active loot filter's verdict for an unpacked item: the rule number the
// game itself shows ("Item rule numbering") and that rule's name (custom name,
// else the game's rule description). Number 0 means no rule matched.
// color is the rule's label colour preset; -1 when the rule keeps the item's own colour.
struct RuleMatch { int number = 0; int outcome = -1; std::string name; int orderMin = 0, orderMax = 0; int color = -1; };
bool RuleFor(void* unpackedItem, RuleMatch* result);
bool Category(int index);
std::string Read();
std::string Set(const std::string& key, const std::string& value);
#ifdef EPOCHPACT_RESEARCH
std::string Test(const std::string& id, const std::string& action);
#endif
}
