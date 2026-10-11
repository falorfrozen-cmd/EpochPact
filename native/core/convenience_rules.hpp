#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ep::convenience::rules {
// All coordinates are in the inventory's canvas units. The toolbar is outside
// the complete menu rectangle, including the resource footer and inventory cells.
struct Rect { float x,y,w,h; };
struct Toolbar { bool visible=false; Rect rect{}; };
inline bool Overlaps(Rect a,Rect b) {
    return a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y;
}
inline bool Inside(Rect a,Rect screen) {
    return a.x>=screen.x&&a.y>=screen.y&&a.x+a.w<=screen.x+screen.w&&a.y+a.h<=screen.y+screen.h;
}
inline Toolbar PlaceToolbar(Rect menu,Rect available,const Rect* exclusions,size_t count) {
    if(!std::isfinite(menu.x+menu.y+menu.w+menu.h+available.w+available.h)||menu.w<=0||menu.h<=0)return {};
    constexpr float width=108,height=108,gap=8;
    // Prefer the space beside the menu. Try different heights to avoid another
    // open panel. If no safe space exists, the app's Inventory actions remain.
    for(float y:{menu.y+menu.h/2-height/2,menu.y+menu.h-height-gap,menu.y+gap}) {
        Rect r{menu.x-width-gap,y,width,height};bool blocked=!Inside(r,available)||Overlaps(r,menu);
        for(size_t i=0;i<count&&!blocked;++i)blocked=Overlaps(r,exclusions[i]);
        if(!blocked)return {true,r};
    }
    return {};
}
// Time and item limits both apply: a large scene never receives an unbounded
// transform/readiness sweep in a frame. Scene discovery is a separate one-shot.
struct Budget {
    static constexpr size_t MaximumChecks=8;
    static constexpr uint64_t MaximumMicros=1500;
    size_t checked=0;uint64_t start;
    bool Next(uint64_t now){if(checked>=MaximumChecks||now-start>=MaximumMicros)return false;++checked;return true;}
};
}
