#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace ep::monolith::rules {
constexpr int kMaxStoredCorruption = 65535; // ProgressManager serializes UInt16.
constexpr double kMaxMultiplier = 100.0;
struct HallMoveRequest {
    int direction=0;uint64_t expires=0,owner=0;std::string id,scene;
    void Clear(){direction=0;expires=owner=0;id.clear();scene.clear();}
    void Set(int value,const std::string& player,const std::string& area,uint64_t actor,uint64_t now){direction=value;id=player;scene=area;owner=actor;expires=now+10000;}
    bool Valid(const std::string& player,const std::string& area,uint64_t actor,uint64_t now)const{return direction&&now<expires&&id==player&&scene==area&&owner==actor;}
};
struct DoorPosition {float x,y,z;};
// A hideout door is authored geometry. Snapping it to a NavMesh baked with its
// previous collider moves it away from its own saved position on every edit.
inline DoorPosition ResolveDoorPosition(bool inHideout,bool hasLook,DoorPosition authored,DoorPosition sampled,bool found,float lift){
    if(inHideout){authored.y+=hasLook?0:lift;return authored;}
    return found?sampled:authored;
}
inline bool DoorInput(bool click,bool localPlayer,bool offline,bool foreground,bool editing,bool menuOpen) {
    return click&&localPlayer&&offline&&foreground&&!editing&&!menuOpen;
}
inline bool Multiplier(double value) { return std::isfinite(value) && value >= 1 && value <= kMaxMultiplier; }
inline bool Corruption(int value, int minimum, bool capped, int maximum) {
    return minimum >= 0 && value >= minimum && value <= kMaxStoredCorruption && (!capped || value <= maximum);
}
inline int ScaleGain(int amount, double multiplier, int current) {
    if (amount <= 0 || !Multiplier(multiplier) || current < 0) return amount;
    const auto room = static_cast<int64_t>(std::numeric_limits<int>::max()) - current;
    return static_cast<int>(std::min(room, static_cast<int64_t>(std::llround(static_cast<double>(amount) * multiplier))));
}
}
