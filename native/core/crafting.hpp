#pragma once
#include <string>
namespace ep::crafting {
bool Init();
std::string Read();
std::string Set(const std::string& key, double value);
std::string Forge(const std::string& id);
std::string Custom(const std::string& id, int templateId, const std::string& affixes, bool perfect,const std::string& modifiers="");
// The forge item's affixes at new tiers ("ID:tier,..."), after a recovery backup.
std::string Tiers(const std::string& id, const std::string& tiers);
// The inventory's Uniques that can have Legendary Potential; "x.y.uniqueID:lp,..." changes theirs (0-4), after a recovery backup.
std::string LegendaryItems();
std::string SetLegendary(const std::string& id, const std::string& changes);
// An item's affixes replaced by "ID:tier:roll,..." (Custom Items' format), built for its type, the first T8 sealed
// as its primordial affix, its rarity from the regular affixes. Main thread; the caller rebuilds its saved form.
void SetAffixes(void* item, const std::string& affixes, bool perfect);
std::string MaterialsRead();
// Equipment the active loot filter hides on the ground is shattered into shards while on (every frame's short check).
std::string SetAutoShatter(bool on);
std::string SetShatterQuiet(bool on);
void AutoShatterFrame();
std::string MaximizeMaterials(const std::string& id, int target);
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
std::string Test(const std::string& id, const std::string& action);
#endif
}
