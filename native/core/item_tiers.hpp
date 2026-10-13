// Affix tiers on item icons and tier colours in item tooltips (display only).
//   tierread                       state, palette and counters
//   tierset icons|tooltips|lines|rules 0|1  tier digits on icons; T1-T8 tags / coloured affix lines / loot filter rule line in tooltips
//   tierset rulepos 0|1|2          rule line under the item type / before the affixes / at the end of the tooltip
//   tierset ruleplain 0|1          rule line shows only the rule name
//   tiercolor <1-8> <RRGGBB>       palette entry; tiercolor reset restores the defaults
#pragma once
#include <string>

namespace ep::tiers {
bool Init();
std::string Read();
std::string Set(const std::string& key, int value);
std::string SetColor(const std::string& tier, const std::string& hex);
void ReleaseRoots();  // before IL2CPP shutdown while GC handles are valid
}
