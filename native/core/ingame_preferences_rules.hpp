// Only replay settings. A preference file is never a source of arbitrary commands or save actions.
#pragma once
#include "ingame_browser_rules.hpp"
#include <sstream>

namespace ep::ingame::model {
inline void Erase(J& j, const std::string& key) {
    j.members.erase(std::remove_if(j.members.begin(), j.members.end(), [&](const auto& p) { return p.first == key; }), j.members.end());
}
inline bool ProfileSetting(const J& c, const J& value) {
    const auto life = c["lifetime"].Str(), widget = c["widget"].Str();
    if ((life != "session" && life != "actor") || (widget != "number" && widget != "toggle" && widget != "choice")) return false;
    if (value.IsNull()) return true; // reset
    if (!value.IsNumber() && !value.IsBool()) return false;
    const double n = value.IsBool() ? (value.boolean ? 1 : 0) : value.number;
    if (!std::isfinite(n)) return false;
    if (widget == "toggle") return n == 0 || n == 1;
    if (c["min"].IsNumber() && n < c["min"].number) return false;
    if (c["max"].IsNumber() && n > c["max"].number) return false;
    if (On(c["integer"]) && n != std::floor(n)) return false;
    if (!c["choices"].items.empty() && std::none_of(c["choices"].items.begin(), c["choices"].items.end(), [n](const J& v) { return v.Num() == n; })) return false;
    return true;
}
// Return only the exact commands supported by the desktop's last-settings replay whitelist.
// No quoting, whitespace, newlines, expected-owner fields or extra arguments can be injected.
inline std::string ReplayCommand(const J& entry) {
    const auto id = entry["id"].Str(); const auto& a = entry["args"];
    if (!a.IsObject()) return "";
    const auto token = [](const J& v) { return v.IsString() ? v.text : v.IsNumber() && std::isfinite(v.number) ? Id(v) : ""; };
    if (id == "loot_mode") {
        const auto v = a["mode"].Str();
        return (v == "all" || v == "filter" || v == "quality" || v == "materials") ? "lootmode " + v : "";
    }
    if (id == "loot_category") {
        static const std::set<std::string> categories{"materials", "gold", "potions", "xp", "favor", "bones", "amber"};
        const auto c = a["category"].Str(), v = token(a["value"]);
        return categories.count(c) && (v == "0" || v == "1") ? "lootcategory " + c + " " + v : "";
    }
    if (id == "loot_affixes" || id == "weaver_affixes") {
        auto ids = token(a["ids"]);
        if (ids.empty() || ids == "none") ids = "none";
        else {
            std::set<unsigned long> unique; std::stringstream stream(ids); std::string part;
            while (std::getline(stream, part, ',')) {
                if (part.empty() || part.size() > 10 || part.find_first_not_of("0123456789") != std::string::npos) return "";
                try { auto v = std::stoul(part); if (v > 2147483647 || !unique.insert(v).second || unique.size() > 64) return ""; } catch (...) { return ""; }
            }
            if (ids.back() == ',' || unique.empty()) return "";
        }
        return std::string(id == "loot_affixes" ? "lootaffixes " : "weaveraffixes ") + ids;
    }
    if (id == "font_family") {
        const auto family = a["family"].Str();
        if (family.empty() || family.size() > 256) return "";
        std::string v;
        for (unsigned char ch : family) {
            if (ch < 32 || ch == 127 || ch == '"' || ch == '\\' || ch == '%' || ch == '{' || ch == '}') return "";
            v += ch == ' ' ? "%20" : ch == '|' ? "%7C" : std::string(1, static_cast<char>(ch));
        }
        return "fontset family " + v;
    }
    return "";
}
inline J ChoiceEntry(const std::string& command, std::string* key) {
    std::istringstream stream(command); std::vector<std::string> words; std::string word;
    while (stream >> word) words.push_back(word);
    J e = Object(), args = Object(); std::string id;
    if (words.size() == 2 && words[0] == "lootmode") { id = "loot_mode"; Put(args, "mode", Text(words[1])); }
    else if (words.size() == 2 && (words[0] == "lootaffixes" || words[0] == "weaveraffixes")) {
        id = words[0] == "lootaffixes" ? "loot_affixes" : "weaver_affixes"; Put(args, "ids", Text(words[1]));
    } else if (words.size() == 3 && words[0] == "lootcategory") {
        id = "loot_category"; Put(args, "category", Text(words[1])); Put(args, "value", Text(words[2]));
    } else if (words.size() == 3 && words[0] == "fontset" && words[1] == "family") {
        id = "font_family"; std::string family = words[2];
        for (const auto& pair : {std::pair<std::string, std::string>{"%20", " "}, {"%7C", "|"}})
            for (size_t at = family.find(pair.first); at != std::string::npos; at = family.find(pair.first, at + pair.second.size())) family.replace(at, pair.first.size(), pair.second);
        Put(args, "family", Text(family));
    } else return {};
    Put(e, "id", Text(id)); Put(e, "args", args);
    if (ReplayCommand(e) != command) return {};
    *key = id + (id == "loot_category" ? ":" + words[1] : "");
    return e;
}
} // namespace ep::ingame::model
