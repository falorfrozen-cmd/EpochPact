// Character bonuses remembered per character (%LOCALAPPDATA%\EpochPact\character-bonuses.json), so a
// game restart or a character load can apply them again. Pure C++: the file's text and its changes;
// stat_editor.cpp reads and writes the file. Covered by native/tests/stat_key_test.cpp.
//
// {"version":1,"characters":{"<save id>|<name>":[{"key":[sp,tags,special,extra],"added":a,"increased":i,"more":m}]}}
#pragma once
#include "ingame_json.hpp"
#include "stat_key.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ep::statedit::memory {
struct Bonus {
    Key key;
    float added = 0, increased = 0, more = 0;
    bool None() const { return !added && !increased && !more; }
    bool operator==(const Bonus&) const = default;
};
using Characters = std::map<std::string, std::vector<Bonus>>;
constexpr size_t kCharacters = 200, kBonuses = 300;

// Movement speed (SP 9, untagged) belongs to the Speed setting, which profiles already keep.
inline bool Kept(Key key) { return !(key.sp == 9 && !key.tags && !key.special && !key.extra); }
// The save id alone could name a new character made in a deleted one's slot.
inline std::string Character(const std::string& id, const std::string& name) { return id.empty() ? id : id + "|" + name; }

inline bool Whole(const ingame::json::Value& v, double low, double high, double* out) {
    if (!v.IsNumber() || v.number != std::floor(v.number) || v.number < low || v.number > high) return false;
    *out = v.number;
    return true;
}
inline bool Contribution(const ingame::json::Value& v, Mode mode, float* out) {
    if (!v.IsNumber() || !ValidValue(mode, v.number)) return false;
    *out = static_cast<float>(v.number);
    return true;
}
// A damaged, unknown or oversized entry is left out; the others still count.
inline Characters Parse(std::string_view text) {
    Characters out;
    ingame::json::Value root;
    if (!ingame::json::Parse(text, root) || root["version"].Num() != 1 || !root["characters"].IsObject()) return out;
    for (const auto& [character, list] : root["characters"].members) {
        if (out.size() >= kCharacters || character.empty() || !list.IsArray()) continue;
        std::vector<Bonus> bonuses;
        for (const auto& item : list.items) {
            if (bonuses.size() >= kBonuses) break;
            const auto& k = item["key"];
            double sp = 0, tags = 0, special = 0, extra = 0;
            Bonus b;
            if (!k.IsArray() || k.items.size() != 4 || !Whole(k.items[0], 0, 255, &sp) || !Whole(k.items[1], INT32_MIN, INT32_MAX, &tags) ||
                !Whole(k.items[2], 0, 255, &special) || !Whole(k.items[3], INT32_MIN, INT32_MAX, &extra))
                continue;
            b.key = {static_cast<uint8_t>(sp), static_cast<int32_t>(tags), static_cast<uint8_t>(special), static_cast<int32_t>(extra)};
            if (!Contribution(item["added"], Mode::Added, &b.added) || !Contribution(item["increased"], Mode::Increased, &b.increased) ||
                !Contribution(item["more"], Mode::More, &b.more) || b.None() || !Kept(b.key))
                continue;
            if (std::none_of(bonuses.begin(), bonuses.end(), [&](const Bonus& x) { return x.key == b.key; })) bonuses.push_back(b);
        }
        if (!bonuses.empty()) out[character] = std::move(bonuses);
    }
    return out;
}
inline std::string Number(float v) {
    std::ostringstream out;
    out.precision(9);
    out << v;
    return out.str();
}
inline std::string Quote(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 0x20) { char buffer[8]; std::snprintf(buffer, sizeof buffer, "\\u%04x", c); out += buffer; }
        else out += static_cast<char>(c);
    }
    return out + "\"";
}
inline std::string Text(const Characters& all) {
    std::string out = "{\"version\":1,\"characters\":{";
    bool first = true;
    for (const auto& [character, bonuses] : all) {
        if (bonuses.empty()) continue;
        out += (first ? "\n" : ",\n") + Quote(character) + ":[";
        first = false;
        for (size_t i = 0; i < bonuses.size(); ++i) {
            const auto& b = bonuses[i];
            out += (i ? "," : "") + std::string("{\"key\":[") + std::to_string(b.key.sp) + "," + std::to_string(b.key.tags) + "," +
                std::to_string(b.key.special) + "," + std::to_string(b.key.extra) + "],\"added\":" + Number(b.added) +
                ",\"increased\":" + Number(b.increased) + ",\"more\":" + Number(b.more) + "}";
        }
        out += "]";
    }
    return out + "\n}}\n";
}
// The character's bonus for a key after a change: all zero (a reset) removes it.
inline void Put(std::vector<Bonus>& list, const Bonus& bonus) {
    if (!Kept(bonus.key)) return;
    const auto it = std::find_if(list.begin(), list.end(), [&](const Bonus& b) { return b.key == bonus.key; });
    if (it != list.end()) {
        if (bonus.None()) list.erase(it);
        else *it = bonus;
    } else if (!bonus.None() && list.size() < kBonuses) list.push_back(bonus);
}
}  // namespace ep::statedit::memory
