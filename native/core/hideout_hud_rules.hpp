#pragma once
// Pure placement/safety policy for the minimap's Hideout shortcut. All input
// map/screen values are physical pixels; only the returned icon is HUD units.
#include <algorithm>
#include <cmath>

namespace ep::hideout::hudrules {
struct Rect { float x=0,y=0,w=0,h=0; };
struct IconLayout { Rect icon; bool visible=false; };

inline IconLayout TravelIconLayout(float screenPixelWidth,float screenPixelHeight,
                                  float hudScale,Rect minimapPixels,bool valid) {
    const auto finite=[](float n){return std::isfinite(n);};
    if(!valid||!finite(screenPixelWidth)||!finite(screenPixelHeight)||!finite(hudScale)||
       !finite(minimapPixels.x)||!finite(minimapPixels.y)||!finite(minimapPixels.w)||!finite(minimapPixels.h)||
       screenPixelWidth<=0||screenPixelHeight<=0||hudScale<=0||
       minimapPixels.x<0||minimapPixels.y<0||minimapPixels.w<64||minimapPixels.h<48)
        return {};
    const float right=minimapPixels.x+minimapPixels.w,bottom=minimapPixels.y+minimapPixels.h;
    if(!finite(right)||!finite(bottom)||right>screenPixelWidth||bottom>screenPixelHeight)return {};
    const float width=screenPixelWidth/hudScale,height=screenPixelHeight/hudScale;
    if(!finite(width)||!finite(height))return {};
    // Match the native minimap controls' physical scale. A fixed logical size
    // grew twice as tall as the native row at 640x480 and covered quest text.
    const float sizePixels=std::clamp(minimapPixels.w*.08f,10.f,40.f);
    const float gapPixels=std::clamp(minimapPixels.w*.03f,3.f,10.f);
    constexpr float edge=8;
    const float size=sizePixels/hudScale,gap=gapPixels/hudScale;
    if(!finite(size)||!finite(gap)||width<size+2*edge||height<size+2*edge)return {};
    // The M/minimize reservation scales with native UI too. A fixed 76px
    // minimum put the shortcut far over the quest title on small minimaps.
    const float reservePixels=std::min(minimapPixels.w,
        std::max(minimapPixels.w*.23f,2*sizePixels+gapPixels));
    const float controlsLeft=(right-reservePixels)/hudScale,mapBottom=bottom/hudScale;
    const float x=std::clamp(controlsLeft-gap-size,edge,width-edge-size);
    const float y=std::clamp(mapBottom+gap,edge,height-edge-size);
    // Clamping must never push the shortcut onto the map or the native row.
    // A minimap at an unusable edge simply has no shortcut, rather than a
    // guessed corner position which could cover another gameplay control.
    if(x+size>controlsLeft-gap+.01f||y<mapBottom+gap-.01f)return {};
    return {{x,y,size,size},true};
}

inline bool CanTravelButton(bool offline,bool alive,bool transitioning,bool mainWindowOpen,
                            bool editing,bool busy,bool uiVisible,bool overlayMapOpen) {
    return offline&&alive&&!transitioning&&!mainWindowOpen&&!editing&&!busy&&uiVisible&&!overlayMapOpen;
}
} // namespace ep::hideout::hudrules
