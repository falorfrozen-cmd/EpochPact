// Item categories: which of EpochPact's eight inventory categories an item belongs to, for the category
// column next to the inventory (its stash tab, its filter and its automatic pickup routing). The first
// category that fits wins, so a Unique bow is a Unique, an exalted ring is Exalted.
#pragma once

#include <string>

namespace ep::categories::rules {

enum Category : int { Unique = 0, Exalted, Idol, Special, Weapon, Armour, Jewellery, Other, kCount };

constexpr const char* kKeys[kCount]{"unique", "exalted", "idol", "special", "weapon", "armour", "jewellery", "other"};
constexpr const char* kNames[kCount]{"Unique & Set", "Exalted", "Idols", "Keys & special", "Weapons", "Armour", "Jewellery", "Other"};
// The stash tab of a category: this prefix and the category's name ("EP Unique & Set").
constexpr const char* kTabPrefix = "EP ";

// itemType: EquipmentType (0-41) or a non-equipment base type (100 and up); rarity: ItemData.rarity
// (7 Unique, 8 Set, 9 Legendary); exalted: the game's own ItemData.isExaltedItem.
inline Category Classify(int itemType, int rarity, bool exalted) {
    if (rarity >= 7 && rarity <= 9) return Unique;
    if (exalted) return Exalted;
    if ((itemType >= 25 && itemType <= 33) || itemType == 41) return Idol;           // idols, idol altars
    if ((itemType >= 34 && itemType <= 39) || itemType >= 100) return Special;       // blessings, lenses, keys, ...
    if (itemType >= 0 && itemType <= 4) return Armour;                               // helmet, body, belt, boots, gloves
    if ((itemType >= 5 && itemType <= 19) || itemType == 23 || itemType == 24) return Weapon;  // and off-hands
    if (itemType >= 20 && itemType <= 22) return Jewellery;                          // amulet, ring, relic
    return Other;
}

// A category's key ("unique", ...) to its index; -1 if unknown.
inline int Index(const std::string& key) {
    for (int i = 0; i < kCount; ++i)
        if (key == kKeys[i]) return i;
    return -1;
}

inline std::string TabName(int category) { return category >= 0 && category < kCount ? std::string(kTabPrefix) + kNames[category] : std::string(); }

}  // namespace ep::categories::rules
