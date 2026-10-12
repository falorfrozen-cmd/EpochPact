#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ep::convenience::rules {
// All coordinates are in the inventory's canvas units. Reserve the actual
// Transfer/Sort controls and visible currencies, not the currency layout's
// otherwise empty container. Never fall back to a neighbouring game panel.
struct Rect { float x,y,w,h; };
struct Toolbar { bool visible=false; Rect rect{}; std::array<Rect,3> buttons{}; };
inline bool Overlaps(Rect a,Rect b) {
    return a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y;
}
inline bool Inside(Rect a,Rect screen) {
    return a.x>=screen.x&&a.y>=screen.y&&a.x+a.w<=screen.x+screen.w&&a.y+a.h<=screen.y+screen.h;
}
inline bool Valid(Rect r) {
    return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.w)&&std::isfinite(r.h)&&r.w>0&&r.h>0;
}
inline Toolbar PlaceToolbar(Rect menu,Rect available,Rect footer,Rect left,Rect currencies) {
    if(!Valid(menu)||!Valid(available)||!Valid(footer)||!Valid(left)||!Valid(currencies))return {};
    constexpr float gap=6,minimumWidth=236,preferredWidth=266;
    const float x=std::max(footer.x+gap,left.x+left.w+gap);
    const float end=std::min(footer.x+footer.w-gap,currencies.x-gap);
    const float width=std::min(preferredWidth,end-x);
    const float height=std::min(26.4f,footer.h);
    Rect row{x,footer.y+(footer.h-height)/2,width,height};
    if(width<minimumWidth||height<22||!Inside(row,menu)||!Inside(row,available)||!Inside(row,footer)||Overlaps(row,left)||Overlaps(row,currencies))return {};
    // Stash all needs more room than the two shorter labels. Compress only
    // within measured readable limits; an impossible footer hides shortcuts.
    const float shortWidth=(width-12-14)/3;
    Toolbar result{true,row};float next=x;
    for(size_t i=0;i<result.buttons.size();++i){float w=shortWidth+(i==1?14:0);result.buttons[i]={next,row.y,w,height};next+=w+gap;}
    return result;
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
