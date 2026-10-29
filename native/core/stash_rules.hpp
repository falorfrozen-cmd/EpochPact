// Stash tabs from loot filter rules (offline only).
//   stashrules <id>                                   active filter rules, stash folders/tabs and rule-tab links
//   stashrulescreate <id> <hidden 0|1> <disabled 0|1>  one free tab per rule (in a folder named after the filter)
//   stashruleassign <id> <tabId> <rule number|0>       write a rule's conditions into a tab's priority; 0 clears
//   stashrulesort <id>                                 move inventory items into their rule's tab (exact filter match)
// Every change writes a recovery backup of the saves first.
#pragma once
#include <string>

namespace ep::stashrules {
std::string Read(const std::string& id);
std::string Create(const std::string& id, bool hidden, bool disabled);
std::string Assign(const std::string& id, int tabId, int ruleNumber);
std::string Sort(const std::string& id);

// Item categories (category_rules.hpp; offline only):
//   catread              the categories: ticked or not, their tab, their items in the inventory and in the tab
//   catauto <key> <0|1>  a category's picked-up items go straight to its tab
//   catshow <key|none>   the inventory shows one category, the others dimmed
//   catsetup <id>        creates the missing "EP <name>" tabs in a stash folder "EpochPact" (recovery backup first)
//   catsort <id>         moves the inventory's items of the ticked categories into their tabs (recovery backup first)
//   catopen <key>        opens the stash on a category's tab
//   catmaterials         opens the game's crafting materials panel (closes it when open)
std::string CategoryRead();
std::string CategoryAuto(const std::string& key, bool on);
std::string CategoryShow(const std::string& key);
std::string CategorySetup(const std::string& id);
std::string CategorySort(const std::string& id);
std::string CategoryOpen(const std::string& key);
std::string CategoryMaterials();
void CategoryPicked(void* item);  // the pickup hook (main thread), after the item went into the inventory
int CategoryFilter();             // the category the inventory shows, -1 all
int CategoryOf(void* item);       // an ItemData's category
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Test(const std::string& id, const std::string& action);
#endif
}
