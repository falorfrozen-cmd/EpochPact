#pragma once
#include <cmath>

namespace ep::player::rules {
inline float CooldownDelta(float delta, double multiplier, bool localOwner) {
    if (!localOwner || !std::isfinite(delta) || delta <= 0 ||
        !std::isfinite(multiplier) || multiplier < 1 || multiplier > 10) return delta;
    return static_cast<float>(delta * multiplier);
}
}
