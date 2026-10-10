#pragma once
#include <algorithm>
#include <cmath>
#include <utility>

namespace ep::density::rules {
constexpr double MaxMultiplier = 5;
constexpr float MaxRolledCount = 128;
enum class Reason { Eligible, Single, Special, Unknown, Invalid, Summoned, Offline, Limit };
struct Pack {
    float mean = 0, variance = 0;
    bool known = false, normal = false, special = false, friendly = false, summoned = false;
};
struct Plan { Reason reason; float mean; bool limited = false; };

inline Plan Evaluate(const Pack& pack, double multiplier, bool offline) {
    if (!offline) return {Reason::Offline, pack.mean};
    if (!std::isfinite(multiplier) || multiplier <= 1 || multiplier > MaxMultiplier ||
        !std::isfinite(pack.mean) || pack.mean < 0 || !std::isfinite(pack.variance) || pack.variance < 0 || pack.variance > 1)
        return {Reason::Invalid, pack.mean};
    if (!pack.known) return {Reason::Unknown, pack.mean};
    if (!pack.normal || pack.special || pack.friendly) return {Reason::Special, pack.mean};
    if (pack.summoned) return {Reason::Summoned, pack.mean};
    if (pack.mean <= 1.5f) return {Reason::Single, pack.mean};
    // Bound the pre-rarity roll, including its variance. Never reduce an existing pack.
    const double ceiling = MaxRolledCount / (1.0 + pack.variance);
    if (pack.mean >= ceiling) return {Reason::Limit, pack.mean};
    const double wanted = pack.mean * multiplier;
    return {Reason::Eligible, static_cast<float>((std::min)(wanted, ceiling)), wanted > ceiling};
}

// Restore on normal return and on C++/managed exception unwind (/EHa in the core).
template<class F> void WithCount(float& field, float temporary, F&& original) {
    struct Restore { float& field; float saved; ~Restore() { field = saved; } } restore{field, field};
    field = temporary;
    std::forward<F>(original)();
}

// A reentrant callback for the same spawner must not multiply the temporary value again.
struct Scope;
inline thread_local const Scope* current = nullptr;
struct Scope {
    const void* self;
    const Scope* previous;
    explicit Scope(const void* p) : self(p), previous(current) { current = this; }
    ~Scope() { current = previous; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    static bool Contains(const void* p) {
        for (auto* s = current; s; s = s->previous) if (s->self == p) return true;
        return false;
    }
};
} // namespace ep::density::rules
