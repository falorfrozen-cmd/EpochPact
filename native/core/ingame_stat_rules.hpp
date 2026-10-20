// Pure rules shared by the native stat dialog and its fixture tests. No Unity/game access.
#pragma once
#include "ingame_json.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <map>
#include <regex>
#include <sstream>
#include <string>

namespace ep::ingame::stats {
inline constexpr std::array<const char*, 3> modes{"added", "increased", "more"};
struct Location { std::string tab; int index = -1; bool extra = false; };
// A view address only: no generic click/action execution, parameters or stat writes.
inline bool ViewLocation(std::string_view text, Location* out) {
    const size_t colon = text.find(':');
    if (colon == std::string_view::npos) return false;
    Location next; next.tab = text.substr(0, colon);
    if (next.tab != "sheet" && next.tab != "aliases" && next.tab != "raw") return false;
    text.remove_prefix(colon + 1);
    if (text.ends_with(":extra")) { next.extra = true; text.remove_suffix(6); }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), next.index);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || next.index < 0) return false;
    *out = std::move(next);
    return true;
}
inline bool Number(const std::string& text, double* value) {
    if (text.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (!end || end == text.c_str() || *end || !std::isfinite(v)) return false;
    *value = v;
    return true;
}
inline std::string Precise(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << value;
    return out.str();
}
inline std::string Key(const json::Value& editor) {
    const auto& key = editor["key"];
    std::string out = "statraw";
    for (const char* field : {"sp", "tags", "special", "extra"}) out += " " + Precise(key[field].Num());
    return out;
}
inline bool Command(const json::Value& editor, const std::string& mode, const std::string& input, std::string* command) {
    bool allowed = false;
    for (const auto& m : editor["modes"].items) allowed |= m.Str() == mode;
    const auto& unit = editor["units"][mode];
    double value = 0;
    const double scale = unit["scale"].Num();
    if (!allowed || scale <= 0 || !Number(input, &value) || value < unit["minimum"].Num() || value > unit["maximum"].Num()) return false;
    value /= scale;
    if (!std::isfinite(value)) return false;
    *command = Key(editor) + " " + mode + " " + Precise(value);
    return true;
}
struct Current { bool known = false, attached = false; std::array<double, 3> values{}; std::string game; };
// Each calculation mode keeps its own valid typed draft, as app.js activeStat.drafts does.
// Reading the actor must not replace an unsent edit; switching modes falls back to the live
// contribution in that mode, in its displayed units (for example 0.5 becomes 50%).
struct Drafts {
    std::map<std::string, std::string> values;
    void Edit(const std::string& mode, const std::string& text) {
        double value = 0;
        if (Number(text, &value)) values[mode] = text;
    }
    std::string Value(const json::Value& config, const Current& current, const std::string& mode) const {
        if (const auto at = values.find(mode); at != values.end()) return at->second;
        for (size_t i = 0; i < modes.size(); ++i) if (mode == modes[i]) return Precise(current.values[i] * config["units"][mode]["scale"].Num(1));
        return "0";
    }
};
// ST.signed uses en-US grouping and at most six fraction digits, regardless of Windows locale.
inline std::string Display(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(6) << value;
    std::string text = out.str();
    while (text.back() == '0') text.pop_back();
    if (text.back() == '.') text.pop_back();
    const size_t start = text[0] == '-' ? 1 : 0;
    const size_t point = text.find('.');
    size_t end = point == std::string::npos ? text.size() : point;
    while (end > start + 3) { end -= 3; text.insert(end, ","); }
    return text;
}
inline Current Contributions(const std::string& text) {
    Current out;
    static const std::regex owned(R"(EpochPact added=([\d.eE+-]+) increased=([\d.eE+-]+) more=([\d.eE+-]+) attached=([01]))");
    static const std::regex empty(R"((SP=\d+ \([^\n]*\) tags=-?\d+ special=\d+ extra=-?\d+: exact-key sum|\bstatraw\b|\bstat\b)[^\n]*applicableMore=)");
    static const std::regex game(R"(\bgameValue=(-?\d+))");
    std::smatch match;
    if (std::regex_search(text, match, owned)) {
        for (size_t i = 0; i < 3; ++i) if (!Number(match[i + 1].str(), &out.values[i])) return Current{};
        out.known = true;
        out.attached = match[4] == "1";
    } else if (text.find("EpochPact added=") == std::string::npos && std::regex_search(text, empty)) {
        out.known = out.attached = true;
    }
    if (std::regex_search(text, match, game)) out.game = match[1];
    return out;
}
} // namespace ep::ingame::stats
