#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ep::convenience::rules {
// A dedicated action row below the item grid, above the native footer.
// Transfer/Sort and currencies never share the EpochPact button row.
struct Rect { float x,y,w,h; };
struct Toolbar { bool visible=false; Rect rect{}; std::array<Rect,3> buttons{}; Rect grid{}; };
inline bool Overlaps(Rect a,Rect b) {
    return a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y;
}
inline bool Inside(Rect a,Rect screen) {
    return a.x>=screen.x&&a.y>=screen.y&&a.x+a.w<=screen.x+screen.w&&a.y+a.h<=screen.y+screen.h;
}
inline bool Valid(Rect r) {
    return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.w)&&std::isfinite(r.h)&&r.w>0&&r.h>0;
}
inline float CurrencyScale(Rect footer,Rect left,float fullWidth) {
    if(!Valid(footer)||!Valid(left)||!std::isfinite(fullWidth)||fullWidth<=0)return 1;
    return std::clamp((footer.x+footer.w-left.x-left.w-6)/fullWidth,.65f,1.f);
}
inline Toolbar PlaceToolbar(Rect menu,Rect available,Rect grid,Rect footer,Rect left,Rect currencies) {
    if(!Valid(menu)||!Valid(available)||!Valid(grid)||!Valid(footer)||!Valid(left)||!Valid(currencies))return {};
    constexpr float gap=4,height=32,width=330;
    const float bottom=std::max({footer.y+footer.h,left.y+left.h,currencies.y+currencies.h})+gap;
    Rect row{grid.x+(grid.w-width)/2,bottom,width,height};
    // Uniform scaling preserves square cells, item hit targets and the matching
    // Item Slerping transform. Keep the grid's top edge and horizontal centre.
    const float factor=std::min(1.f,(grid.y+grid.h-row.y-row.h-gap)/grid.h);
    if(factor<.90f)return {};
    Rect fitted{grid.x+grid.w*(1-factor)/2,grid.y+grid.h-grid.h*factor,grid.w*factor,grid.h*factor};
    if(!Inside(row,menu)||!Inside(row,available)||!Inside(fitted,menu)||!Inside(fitted,available)||
       Overlaps(row,left)||Overlaps(row,currencies)||Overlaps(row,fitted))return {};
    Toolbar result{true,row,{},fitted};float next=row.x;
    for(size_t i=0;i<result.buttons.size();++i){float w=i==1?118.f:100.f;result.buttons[i]={next,row.y,w,height};next+=w+6;}
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
// A zone load queues a few hundred condition handlers. Classify them in
// batches so nearby objects are usable within a second of loading, without
// a long frame: at most 32 handlers or 1 ms per work tick.
struct ClassifyBudget {
    static constexpr size_t MaximumHandlers=32;
    static constexpr uint64_t MaximumMicros=1000;
    size_t classified=0;uint64_t start;
    bool Next(uint64_t now){if(classified>=MaximumHandlers||now-start>=MaximumMicros)return false;++classified;return true;}
};
// After the n-th trigger of an object that is still usable: wait 2, 4 and 8 s,
// then never again. A chest or shrine the game will not consume must not pull
// at the character every few seconds.
constexpr int MaximumTriggers=4;
inline uint64_t TriggerRetryMs(int attempts){
    if(attempts<=0)return 0;
    if(attempts>=MaximumTriggers)return UINT64_MAX;
    return 2000ull<<(attempts-1);
}
// How near a chest, shrine or portal must be: a circle or a square of `radius`
// game units on the ground around it. The height may differ by up to
// max(3, radius), so an object on another floor stays out.
constexpr double MinimumAreaRadius=1,MaximumAreaRadius=30;
inline bool InArea(double dx,double dy,double dz,double radius,int shape){
    if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(dz)||!std::isfinite(radius)||radius<=0)return false;
    if(std::abs(dy)>std::max(3.0,radius))return false;
    if(shape==1)return std::abs(dx)<=radius&&std::abs(dz)<=radius;
    return dx*dx+dz*dz<=radius*radius;
}
// The drawn outline: 48 points on the circle, or the square's 4 corners.
inline int AreaPoints(int shape){return shape==1?4:48;}
inline void AreaPoint(int shape,double radius,int index,double& x,double& z){
    if(shape==1){static constexpr int sx[4]{-1,1,1,-1},sz[4]{-1,-1,1,1};x=radius*sx[index&3];z=radius*sz[index&3];return;}
    const double angle=6.283185307179586*index/AreaPoints(0);x=radius*std::cos(angle);z=radius*std::sin(angle);
}
// The damage meter splits a hit's applied damage by damage type (the game's
// DamageType order: physical, fire, cold, lightning, necrotic, void, poison)
// in proportion to the hit's own damage per type. Parts that are not finite
// and positive count as zero; false when no part is (nothing to split).
constexpr size_t DamageTypes=7;
using DamageParts=std::array<float,DamageTypes>;
inline bool SplitDamage(float damage,const DamageParts& parts,DamageParts& out){
    out={};
    if(!std::isfinite(damage)||damage<=0)return false;
    double sum=0;
    for(float p:parts)if(std::isfinite(p)&&p>0)sum+=p;
    if(!(sum>0)||!std::isfinite(sum))return false;
    for(size_t i=0;i<DamageTypes;++i)if(std::isfinite(parts[i])&&parts[i]>0)out[i]=static_cast<float>(damage*(parts[i]/sum));
    return true;
}
}
