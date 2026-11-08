#include "../core/monolith_rules.hpp"
#include <cstdio>
#include <limits>
int main() {
    using namespace ep::monolith::rules;
    int count = 0, failures = 0;
    const auto check = [&](bool pass, const char* name) { ++count; if (!pass) { ++failures; std::printf("FAIL: %s\n", name); } };
    HallMoveRequest move;move.Set(1,"6","MonolithHub",42,1000);
    check(move.Valid("6","MonolithHub",42,10999),"warmup keeps the same offline actor's movement request for at most ten seconds");
    check(!move.Valid("6","MonolithHub",42,11000),"a timed-out request cannot warp later");
    check(!move.Valid("7","MonolithHub",42,1001)&&!move.Valid("6","EoT",42,1001),"character changes and scene departures cancel pending movement");
    check(!move.Valid("6","MonolithHub",43,1001),"reloading the same save with a new actor cannot replay a pending warp");
    move.Clear();check(!move.Valid("6","MonolithHub",42,1002),"a consumed or cancelled movement request stays cleared");
    const DoorPosition saved{18,1,-7},oldObstacleEdge{21,2,-4};
    const auto fixed=ResolveDoorPosition(true,false,saved,oldObstacleEdge,true,.5f);
    check(fixed.x==18&&fixed.y==1.5f&&fixed.z==-7,"hideout doors keep exact authored X/Z despite stale NavMesh obstacles");
    const auto station=ResolveDoorPosition(true,true,saved,oldObstacleEdge,true,3);
    check(station.x==18&&station.y==1&&station.z==-7,"station proxy stays on ground while visible model owns lift");
    const auto hub=ResolveDoorPosition(false,false,saved,oldObstacleEdge,true,3);
    check(hub.x==21&&hub.y==2&&hub.z==-4,"native Hub doors still use normal walkable snapping");
    const auto unsampled=ResolveDoorPosition(false,false,saved,oldObstacleEdge,false,3);
    check(unsampled.x==18&&unsampled.y==1&&unsampled.z==-7,"failed Hub sample retains its original fallback");
    for(unsigned flags=0;flags<64;++flags)
        check(DoorInput(flags&1,flags&2,flags&4,flags&8,flags&16,flags&32)==(flags==15),"custom doors need an explicit local offline foreground interaction outside menus/editing");
    check(Multiplier(1), "off"); check(Multiplier(100), "maximum");
    check(!Multiplier(0), "zero rejected"); check(!Multiplier(101), "oversize rejected");
    check(!Multiplier(std::numeric_limits<double>::quiet_NaN()), "NaN rejected");
    check(!Multiplier(std::numeric_limits<double>::infinity()), "infinity rejected");
    check(Corruption(0, 0, true, 50), "normal baseline");
    check(Corruption(50, 0, true, 50), "normal maximum");
    check(!Corruption(51, 0, true, 50), "normal cap enforced");
    check(!Corruption(-1, 0, true, 50), "negative rejected");
    check(!Corruption(99, 100, false, 0), "empowered floor enforced");
    check(Corruption(100, 100, false, 0), "empowered baseline");
    check(Corruption(65535, 100, false, 0), "serialization maximum");
    check(!Corruption(65536, 100, false, 0), "UInt16 wrap prevented");
    check(ScaleGain(25, 5, 0) == 125, "gain multiplication");
    check(ScaleGain(-25, 5, 100) == -25, "loss not multiplied");
    check(ScaleGain(0, 5, 100) == 0, "zero unchanged");
    check(ScaleGain(25, 1, 0) == 25, "x1 unchanged");
    check(ScaleGain(1, 1.5, 0) == 2, "fraction rounded");
    check(ScaleGain(1000000000, 100, 100) == 2147483547, "addition cannot overflow");
    check(ScaleGain(50, 100, 2147483647) == 0, "full integer cap");
    check(ScaleGain(25, std::numeric_limits<double>::quiet_NaN(), 0) == 25, "invalid gain multiplier unchanged");
    std::printf("monolith: %d/%d passed\n", count-failures, count);
    return failures ? 1 : 0;
}
