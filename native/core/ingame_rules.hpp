// Pure rules of the in-game panel (no Unity, no IL2CPP): value steps, the status lines the
// panel reads, and the command a button sends. native/tests/ingame_rules_test.cpp covers them.
#pragma once
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ep::ingame::rules {

inline constexpr std::array<double, 8> kXpSteps{1, 2, 3, 5, 10, 20, 50, 100};
inline constexpr std::array<double, 6> kDensitySteps{1, 1.5, 2, 3, 4, 5};

// The next step above (up) or below the current value. A value between two steps moves to
// the nearest step in that direction; the ends stay where they are.
template <size_t N> double Step(const std::array<double, N>& steps, double current, bool up) {
    if (!std::isfinite(current)) return steps[0];
    if (up) {
        for (double s : steps) if (s > current + 1e-9) return s;
        return steps[N - 1];
    }
    for (size_t i = N; i-- > 0;) if (steps[i] < current - 1e-9) return steps[i];
    return steps[0];
}

// "2", "1.5", "100": the shortest form the command parser and the status line both use.
inline std::string Number(double value) {
    char text[32];
    std::snprintf(text, sizeof text, "%g", value);
    return text;
}

struct Values {
    double xp = -1, density = -1;  // -1: not read yet
    int autopickup = -1;           // -1 unknown, 0 off, 1 on
    bool offline = false;          // the gate line says "offline play"
};

// The value after "<name>: x" at the start of a line of the status command's reply.
inline double LineValue(const std::string& status, const std::string& name) {
    const std::string key = name + ": x";
    for (size_t pos = status.find(key); pos != std::string::npos; pos = status.find(key, pos + key.size())) {
        if (pos != 0 && status[pos - 1] != '\n') continue;
        const char* start = status.c_str() + pos + key.size();
        char* end = nullptr;
        const double value = std::strtod(start, &end);
        if (end != start && std::isfinite(value)) return value;
    }
    return -1;
}

inline Values Parse(const std::string& status) {
    Values v;
    v.xp = LineValue(status, "xp");
    v.density = LineValue(status, "density");
    for (const char* state : {"on", "off"}) {
        const std::string key = std::string("autopickup: ") + state;
        const size_t pos = status.find(key);
        if (pos != std::string::npos && (pos == 0 || status[pos - 1] == '\n')) v.autopickup = state[1] == 'n' ? 1 : 0;
    }
    v.offline = status.find("; gate: offline play") != std::string::npos;
    return v;
}

enum class Action { XpDown, XpUp, DensityDown, DensityUp, AutoPickup };

// The ordinary command a button sends; the command itself checks offline play and limits.
inline std::string Command(Action action, const Values& v) {
    const double xp = v.xp > 0 ? v.xp : 1, density = v.density > 0 ? v.density : 1;
    switch (action) {
    case Action::XpDown: return "xp " + Number(Step(kXpSteps, xp, false));
    case Action::XpUp: return "xp " + Number(Step(kXpSteps, xp, true));
    case Action::DensityDown: return "density " + Number(Step(kDensitySteps, density, false));
    case Action::DensityUp: return "density " + Number(Step(kDensitySteps, density, true));
    case Action::AutoPickup: return v.autopickup == 1 ? "autopickup 0" : "autopickup 1";
    }
    return {};
}

// What the panel shows for a value it has not read yet.
inline std::string Shown(double value) { return value > 0 ? "x" + Number(value) : "..."; }

// The first line of a command reply, shortened for the panel's message line.
inline std::string Brief(const std::string& reply, size_t limit = 90) {
    std::string line = reply.substr(0, reply.find('\n'));
    if (line.size() > limit) line = line.substr(0, limit - 3) + "...";
    return line;
}

}  // namespace ep::ingame::rules
