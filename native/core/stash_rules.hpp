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
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Test(const std::string& id, const std::string& action);
#endif
}
