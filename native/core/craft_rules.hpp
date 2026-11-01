// Custom item affix choices (Craft from EpochPact). Each comma-separated
// token is an affix ID, which keeps the earlier behaviour (T7, the item-wide
// maximum/midpoint roll), or "ID:tier:roll": tier 1-8 and roll 1-100 (% of the
// tier's range) or "r" for a random roll. T8 is the game's eighth tier: the
// first T8 becomes the item's sealed primordial affix, any further T8 stays a
// regular affix at that tier (the game itself rolls one T8 per item).
#pragma once
#include <charconv>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace ep::crafting::rules {
constexpr int kItemRoll = 0;     // the item-wide maximum or midpoint roll
constexpr int kRandomRoll = -1;  // a random roll, like a crafted affix
constexpr int kPrimordialTier = 8;

struct AffixChoice {
    int id = 0;
    int tier = 7;
    int roll = kItemRoll;
};

inline int Whole(std::string_view text, int low, int high) {
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < low || value > high)
        throw std::invalid_argument("out of range");
    return value;
}

inline AffixChoice ParseChoice(std::string_view token) {
    AffixChoice choice;
    const auto first = token.find(':');
    if (first == std::string_view::npos) {
        choice.id = Whole(token, 0, 65535);
        return choice;
    }
    const auto second = token.find(':', first + 1);
    if (second == std::string_view::npos) throw std::invalid_argument("missing roll");
    choice.id = Whole(token.substr(0, first), 0, 65535);
    choice.tier = Whole(token.substr(first + 1, second - first - 1), 1, kPrimordialTier);
    const auto roll = token.substr(second + 1);
    choice.roll = roll == "r" ? kRandomRoll : Whole(roll, 1, 100);
    return choice;
}

// New tiers for affixes already on an item: "ID:tier,..." with tier 1-8, each affix once.
struct TierChoice {
    int id = 0;
    int tier = 7;
};
inline std::vector<TierChoice> ParseTiers(std::string_view input, size_t maximum = 16) {
    std::vector<TierChoice> out;
    if (input.empty() || input == "none") throw std::runtime_error("choose a new tier for an affix");
    for (size_t start = 0;;) {
        const auto end = input.find(',', start);
        const auto token = input.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        const auto colon = token.find(':');
        TierChoice choice;
        try {
            if (colon == std::string_view::npos) throw std::invalid_argument("missing tier");
            choice.id = Whole(token.substr(0, colon), 0, 65535);
            choice.tier = Whole(token.substr(colon + 1), 1, kPrimordialTier);
        } catch (const std::invalid_argument&) {
            throw std::runtime_error("each change is an affix ID and a tier 1-8, as ID:tier");
        }
        for (const auto& known : out)
            if (known.id == choice.id) throw std::runtime_error("change each affix once");
        out.push_back(choice);
        if (out.size() > maximum) throw std::runtime_error("too many tier changes");
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return out;
}

// "keep" means no new affixes. Throws std::runtime_error with a player-facing message.
inline std::vector<AffixChoice> ParseAffixes(std::string_view input, size_t maximum = 7) {
    std::vector<AffixChoice> out;
    if (input == "keep") return out;
    if (input.empty()) throw std::runtime_error("select affixes or keep the current affixes");
    for (size_t start = 0;;) {
        const auto end = input.find(',', start);
        const auto token = input.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        AffixChoice choice;
        try {
            choice = ParseChoice(token);
        } catch (const std::invalid_argument&) {
            throw std::runtime_error("each affix is an ID or ID:tier 1-8:roll 1-100 or r");
        }
        for (const auto& known : out)
            if (known.id == choice.id) throw std::runtime_error("choose different affixes");
        out.push_back(choice);
        if (out.size() > maximum) throw std::runtime_error("choose up to seven different affixes");
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return out;
}

// The affix roll byte: 0-255 across the tier's range.
inline uint8_t RollByte(int roll, bool perfect, uint8_t random) {
    if (roll == kRandomRoll) return random;
    if (roll == kItemRoll) return perfect ? 255 : 128;
    if (roll < 1 || roll > 100) throw std::runtime_error("roll must be 1-100%");
    return static_cast<uint8_t>(std::lround(roll * 255.0 / 100.0));
}
}  // namespace ep::crafting::rules
