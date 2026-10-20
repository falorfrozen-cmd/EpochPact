// Pure rules shared by the native stat dialog and its fixture tests. No Unity/game access.
#pragma once
#include "ingame_json.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <regex>
#include <sstream>
#include <string>

namespace ep::ingame::stats {
inline constexpr std::array<const char*, 3> modes{"added", "increased", "more"};
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
