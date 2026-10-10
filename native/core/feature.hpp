// The shape every multiplier feature shares: a value, one or more hooks, and the same
// rules (offline play only, x1 takes the hooks back out, a partial install is rolled
// back). xp.cpp predates this header and keeps its own copy of the counters.
#pragma once

#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace ep::feature {

struct Hook {
    const char* name;
    game::MethodRef ref;
    void* detour;
    void** original;
};

struct Feature {
    const char* cmd = "";
    double max = 1;
    std::atomic<double> value{1.0};
    std::atomic<bool> firstPending{false};
    std::atomic<uint64_t> boosted{0}, refused{0};
    std::vector<Hook*> hooks;
};

// Arms or disarms a feature: x1 removes its hooks, anything else needs offline play and
// every hook in (a partial install is rolled back).
inline std::string Set(Feature& f, double m) {
    char buf[256];
    for (Hook* h : f.hooks)
        if (!h->ref) {
            std::snprintf(buf, sizeof buf, "%s: refused: %s was not found in this game build", f.cmd, h->name);
            return buf;
        }
    if (!(m >= 1.0 && m <= f.max)) {
        std::snprintf(buf, sizeof buf, "%s: refused: the multiplier must be between 1 and %g", f.cmd, f.max);
        return buf;
    }
    if (m == 1.0) {
        f.value = 1.0;
        std::string why, failed;
        for (Hook* h : f.hooks)
            if (hook::IsInstalled(h->ref.code) && !hook::Remove(h->ref.code, &why)) failed += std::string(" ") + h->name + ": " + why;
        if (!failed.empty()) return std::string(f.cmd) + ": x1 set, but a hook stays:" + failed;
        std::snprintf(buf, sizeof buf, "%s -> x1 (off; hooks removed)", f.cmd);
        return buf;
    }
    if (!game::IsOfflinePlay()) return std::string(f.cmd) + ": refused: " + game::GateText();
    f.value = m;
    f.firstPending = true;
    std::vector<Hook*> fresh;
    for (Hook* h : f.hooks) {
        if (hook::IsInstalled(h->ref.code)) continue;
        std::string why;
        if (!hook::Install(h->ref.code, h->detour, h->original, &why)) {
            for (Hook* back : fresh) hook::Remove(back->ref.code, nullptr);
            f.value = 1.0;
            return std::string(f.cmd) + ": refused: the hook on " + h->name + " did not go in: " + why;
        }
        fresh.push_back(h);
    }
    std::snprintf(buf, sizeof buf, "%s -> x%g", f.cmd, m);
    return buf;
}

inline std::string Line(const Feature& f) {
    bool in = !f.hooks.empty();
    for (const Hook* h : f.hooks) in = in && h->ref && hook::IsInstalled(h->ref.code);
    char buf[200];
    std::snprintf(buf, sizeof buf, "%s: x%g, hooks %s, boosted %llu, refused online %llu", f.cmd, f.value.load(), in ? "in" : "out",
                  static_cast<unsigned long long>(f.boosted.load()), static_cast<unsigned long long>(f.refused.load()));
    return buf;
}

// True when a detour should change something now; counts and logs the first time.
inline bool Active(Feature& f, double* m) {
    *m = f.value.load(std::memory_order_relaxed);
    if (*m == 1.0) return false;
    if (!game::IsOfflinePlay()) {
        f.refused.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    f.boosted.fetch_add(1, std::memory_order_relaxed);
    return true;
}

}  // namespace ep::feature
