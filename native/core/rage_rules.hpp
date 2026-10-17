#pragma once
// Rage of Morditas settings (Season 5), pure rules: how the game's own values
// are scaled. The game data itself is only changed in memory (rage.cpp).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ep::rage::rules {
struct Range{double low,high,neutral;};
// spawn: encounter chance × (the game rolls its 30 % times each zone's own factor); hunttime: hunt duration ×;
// charges and buffs: share of the kills needed; blood: percent of the encounter's items with Blood
// rather than Ice forging potential (-1: the game's own share).
constexpr Range kSpawn{1,5,1},kHuntTime{1,5,1},kCharges{0.2,1,1},kBuffs{0.2,1,1},kBlood{0,100,-1};
inline bool InRange(const Range& r,double v){return std::isfinite(v)&&(v==r.neutral||(v>=r.low&&v<=r.high));}
// The Blood share as the game's chance: the game's own, or the chosen percent.
inline float BloodChance(float original,double percent){return percent<0?original:static_cast<float>(std::min(100.0,percent)/100.0);}
// A rate times a multiplier (the game multiplies it again by the zone's factor, so no cap here).
inline float ScaleRate(float original,double multiplier){
    if(!std::isfinite(original)||original<=0)return original;
    return static_cast<float>(static_cast<double>(original)*multiplier);
}
// A chance (0..1) times a multiplier, never above 100 %.
inline float ScaleChance(float original,double multiplier){
    if(!std::isfinite(original)||original<=0)return original;
    const double v=static_cast<double>(original)*multiplier;
    return static_cast<float>(std::min(1.0,std::max(0.0,v)));
}
// A duration in seconds times a multiplier.
inline float ScaleTime(float original,double multiplier){
    if(!std::isfinite(original)||original<=0)return original;
    return static_cast<float>(static_cast<double>(original)*multiplier);
}
// Kill thresholds times a share (0.2..1): rounded, at least 1, and still rising
// one after another so every charge or buff keeps its own threshold.
inline std::vector<int> ScaleThresholds(const std::vector<int>& original,double share){
    std::vector<int> out;out.reserve(original.size());
    int previous=0;
    for(int v:original){
        int scaled=v<=0?v:static_cast<int>(std::lround(static_cast<double>(v)*share));
        if(v>0)scaled=std::max({scaled,1,previous+1});
        out.push_back(scaled);if(v>0)previous=scaled;
    }
    return out;
}
}  // namespace ep::rage::rules
