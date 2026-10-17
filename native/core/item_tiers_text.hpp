#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace ep::tiers::text {
// The game's own colour tags in a tooltip line: <color=...>, </color> and the
// short <#RRGGBB> form (TMP tag names are case-insensitive). Other tags are kept.
inline std::string StripColorTags(const std::string& line) {
    auto starts = [&](size_t at, const char* tag) {
        for (size_t i = 0; tag[i]; ++i)
            if (at + i >= line.size() || std::tolower(static_cast<unsigned char>(line[at + i])) != tag[i]) return false;
        return true;
    };
    std::string out;
    out.reserve(line.size());
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '<' && (starts(i, "<color=") || starts(i, "</color>") || starts(i, "<#"))) {
            const auto end = line.find('>', i);
            if (end != std::string::npos) { i = end; continue; }
        }
        out += line[i];
    }
    return out;
}
// A description the game generated for an unnamed loot filter rule starts with what the rule does
// ("Show All Uniques", "Hide All Normal Items"); without those words the rule's own words remain.
inline std::string RuleWords(const std::string& description) {
    static const char* actions[]{"show all ", "hide all ", "highlight all ", "recolor all ", "show ", "hide ", "highlight ", "recolor "};
    for (const char* action : actions) {
        const size_t n = std::strlen(action);
        if (description.size() <= n) continue;
        bool same = true;
        for (size_t i = 0; i < n && same; ++i) same = std::tolower(static_cast<unsigned char>(description[i])) == action[i];
        if (same) return description.substr(description.find_first_not_of(' ', n) == std::string::npos ? description.size() : description.find_first_not_of(' ', n));
    }
    return description;
}
// What the rule line shows, as bits: "Loot filter:" label, the rule number, Show/Hide/Highlight, the name.
constexpr int kRuleLabel = 1, kRuleNumber = 2, kRuleKind = 4, kRuleName = 8, kRuleAll = 15;
inline bool ValidRuleParts(int parts) { return parts >= 1 && parts <= kRuleAll && (parts & (kRuleNumber | kRuleKind | kRuleName)); }
// The rule line's words (open/close: the colour tags around them; labelTag: the label in its own colour).
// number <= 0: no rule matched. Missing parts are left out; a rule without a name shows its number.
inline std::string RuleLine(int parts, int number, int outcome, const std::string& name, const std::string& open, const std::string& labelTag) {
    static const char* kinds[]{"Show", "Hide", "Highlight"};
    const std::string label = parts & kRuleLabel ? labelTag + " " : "";
    if (number <= 0) return label + open + (parts & kRuleLabel ? "no rule matched (shown by default)" : "No matching rule") + "</color>";
    std::vector<std::string> words;
    if (parts & kRuleNumber) words.push_back("Rule " + std::to_string(number));
    if (parts & kRuleKind) words.push_back(kinds[outcome < 0 ? 0 : outcome > 2 ? 2 : outcome]);
    if ((parts & kRuleName) && !name.empty()) words.push_back(name);
    if (words.empty()) words.push_back("Rule " + std::to_string(number));
    std::string text;
    for (size_t i = 0; i < words.size(); ++i) text += (i ? " · " : "") + words[i];
    return label + open + text + "</color>";
}
// The rich-text tags a player may write in a loot filter rule name to colour or mark words:
// <color=#RGB|#RRGGBB|#RRGGBBAA|name>, <#RRGGBB>, </color>, <b> <i> <u> <s> and their closing tags.
inline bool NameTag(std::string_view tag) {
    std::string t;
    for (char c : tag) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* simple : {"<b>", "</b>", "<i>", "</i>", "<u>", "</u>", "<s>", "</s>", "</color>"})
        if (t == simple) return true;
    auto hex = [](std::string_view v) {
        if (v.size() != 3 && v.size() != 4 && v.size() != 6 && v.size() != 8) return false;
        for (char c : v) if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        return true;
    };
    if (t.size() > 3 && t.front() == '<' && t.back() == '>' && t[1] == '#') return hex(std::string_view(t).substr(2, t.size() - 3));
    if (t.rfind("<color=", 0) != 0 || t.back() != '>') return false;
    std::string_view value = std::string_view(t).substr(7, t.size() - 8);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
    if (!value.empty() && value.front() == '#') return hex(value.substr(1));
    for (const char* name : {"black", "blue", "green", "orange", "purple", "red", "white", "yellow"})
        if (value == name) return true;
    return false;
}
// A rule name in pieces: the tags NameTag allows, and the text between them (other tags stay text).
struct NamePiece { bool tag; std::string text; };
inline std::vector<NamePiece> NameParts(const std::string& name) {
    std::vector<NamePiece> parts;
    size_t from = 0;
    for (size_t i = name.find('<'); i != std::string::npos; i = name.find('<', i + 1)) {
        const auto end = name.find('>', i);
        if (end == std::string::npos) break;
        if (!NameTag(std::string_view(name).substr(i, end - i + 1))) continue;
        if (i > from) parts.push_back({false, name.substr(from, i - from)});
        parts.push_back({true, name.substr(i, end - i + 1)});
        from = end + 1;
        i = end;
    }
    if (from < name.size()) parts.push_back({false, name.substr(from)});
    return parts;
}
// A rule name for the tooltip rule line: its allowed tags kept, every other tag left out.
inline std::string KeepNameTags(const std::string& name) {
    std::string out;
    for (const auto& part : NameParts(name)) {
        if (part.tag) { out += part.text; continue; }
        for (size_t i = 0; i < part.text.size(); ++i) {
            if (part.text[i] == '<') { const auto end = part.text.find('>', i); if (end != std::string::npos) { i = end; continue; } }
            out += part.text[i];
        }
    }
    return out;
}
// Extra loot filter colours: each comes from one of the game's own colours, every shade of it
// (label, button, border, shine, tooltip, beam) moved to the new hue with the same relative
// saturation and brightness.
struct Rgbf { float r, g, b; };
struct Hsv { float h, s, v; };
inline Hsv ToHsv(Rgbf c) {
    const float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), d = mx - mn;
    float h = 0;
    if (d > 1e-6f) {
        if (mx == c.r) h = std::fmod((c.g - c.b) / d, 6.f);
        else if (mx == c.g) h = (c.b - c.r) / d + 2;
        else h = (c.r - c.g) / d + 4;
        h *= 60; if (h < 0) h += 360;
    }
    return {h, mx > 1e-6f ? d / mx : 0, mx};
}
inline Rgbf FromHsv(Hsv c) {
    const float h = std::fmod(std::fmod(c.h, 360.f) + 360.f, 360.f) / 60, s = std::clamp(c.s, 0.f, 1.f), v = std::clamp(c.v, 0.f, 1.f);
    const float x = v * s * (1 - std::fabs(std::fmod(h, 2.f) - 1)), m = v - v * s, k = v * s;
    const int i = static_cast<int>(h) % 6;
    const Rgbf parts[]{{k, x, 0}, {x, k, 0}, {0, k, x}, {0, x, k}, {x, 0, k}, {k, 0, x}};
    return {parts[i].r + m, parts[i].g + m, parts[i].b + m};
}
inline Rgbf FromRgb(uint32_t rgb) { return {((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f}; }
// One shade of the template colour, moved to the target colour.
inline Rgbf Recolor(Rgbf shade, Rgbf templ, Rgbf target) {
    const Hsv f = ToHsv(shade), t = ToHsv(templ), n = ToHsv(target);
    const float s = t.s > 0.05f ? f.s * n.s / t.s : n.s, v = t.v > 0.05f ? f.v * n.v / t.v : f.v;
    return FromHsv({n.h, s, v});
}
// The extra colours: distinct hues and a few greys and browns.
constexpr uint32_t kExtraFilterColors[]{0xFF1493, 0xFF00FF, 0x8A2BE2, 0x5B2BC4, 0x4169E1, 0x87CEEB, 0x00CED1, 0x008B8B, 0x40E0D0, 0x98FB98, 0x32CD32, 0x7FFF00,
    0x9ACD32, 0x808000, 0xFFD700, 0xFFBF00, 0xFF8C00, 0xFF7F50, 0xFA8072, 0xDC143C, 0x800000, 0x8B4513, 0xD2B48C, 0xC0C0C0, 0x708090, 0xF5F5DC};
}  // namespace ep::tiers::text
