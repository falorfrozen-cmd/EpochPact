#pragma once
#include <cctype>
#include <string>

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
}  // namespace ep::tiers::text
