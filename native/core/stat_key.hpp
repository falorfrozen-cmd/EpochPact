#pragma once
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

namespace ep::statedit {
struct Key {
    uint8_t sp = 0;
    int32_t tags = 0;
    uint8_t special = 0;
    int32_t extra = 0;
    bool operator==(const Key&) const = default;
};
enum class Mode { Added, Increased, More };

inline bool Integer(std::string_view s, int64_t low, int64_t high, int64_t* value) {
    if (s.empty()) return false;
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s.remove_prefix(2); }
    int64_t n = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n, base);
    if (ec != std::errc{} || end != s.data() + s.size() || n < low || n > high) return false;
    *value = n;
    return true;
}
inline bool Number(const std::string& s, double* value) {
    char* end = nullptr;
    errno = 0;
    const double n = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end || errno == ERANGE || !std::isfinite(n) || std::fabs(n) > (std::numeric_limits<float>::max)()) return false;
    // Reject conversion underflow as well as overflow: a nonzero modifier must not silently become zero.
    if (n != 0 && static_cast<float>(n) == 0) return false;
    *value = n;
    return true;
}
inline bool ParseMode(const std::string& s, Mode* mode) {
    if (s == "added") *mode = Mode::Added;
    else if (s == "increased") *mode = Mode::Increased;
    else if (s == "more") *mode = Mode::More;
    else return false;
    return true;
}
inline const char* ModeName(Mode mode) {
    return mode == Mode::Added ? "added" : mode == Mode::Increased ? "increased" : "more";
}
inline bool ValidValue(Mode mode, double value) {
    return std::isfinite(value) && std::fabs(value) <= (std::numeric_limits<float>::max)() &&
        (value == 0 || static_cast<float>(value) != 0) &&
        (mode == Mode::Added || value >= -1);
}
inline std::string NormalName(std::string_view input) {
    std::string s;
    for (unsigned char c : input) {
        if (c >= 'A' && c <= 'Z') s += static_cast<char>(c + ('a' - 'A'));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) s += static_cast<char>(c);
    }
    return s;
}
inline double AttributeContribution(double base, double flat, double increased, double more) {
    const double total = (base + flat) * (1 + increased) * (1 + more);
    if (!std::isfinite(total) || std::abs(total) > 1000000)
        return std::numeric_limits<double>::quiet_NaN();
    return total - base;
}
}  // namespace ep::statedit
