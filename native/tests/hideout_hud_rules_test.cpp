#include "../core/hideout_hud_rules.hpp"
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

namespace {
int checks=0,failures=0;
void Check(bool pass,const char* text){++checks;if(!pass){++failures;std::printf("FAIL %s\n",text);}}
using ep::hideout::hudrules::Rect;
using ep::hideout::hudrules::TravelIconLayout;
using ep::hideout::hudrules::CanTravelButton;
void LayoutChecks(float width,float height,float scale,Rect map){
    const auto result=TravelIconLayout(width,height,scale,map,true);
    Check(result.visible,"usable minimap has a shortcut");if(!result.visible)return;
    const auto& r=result.icon;
    const float iconPixels=std::clamp(map.w*.08f,10.f,40.f),gapPixels=std::clamp(map.w*.03f,3.f,10.f);
    const float reserve=std::min(map.w,std::max(map.w*.23f,2*iconPixels+gapPixels));
    Check(std::abs(r.w*scale-iconPixels)<.001f&&r.w==r.h,"physical icon size follows native minimap scale");
    Check(r.x>=8&&r.y>=8&&r.x+r.w<=width/scale-8+.01f&&r.y+r.h<=height/scale-8+.01f,"inside viewport at independent HUD scale");
    Check(r.y>=(map.y+map.h+gapPixels)/scale-.01f,"below the complete native minimap");
    Check(r.x+r.w<=(map.x+map.w-reserve-gapPixels)/scale+.01f,"left of reserved native map controls");
}
}

int main(){
    const float screens[][2]{{1280,720},{1920,1080},{2560,1440}};
    for(const auto& screen:screens)for(float scale:{.5f,.75f,1.f,1.25f,1.5f,2.f})
        for(float width:{100.f,180.f,320.f,450.f,600.f})
            LayoutChecks(screen[0],screen[1],scale,{screen[0]-width-14,50,width,width*.82f});

    LayoutChecks(1920,1080,1,{1600,0,320,300}); // exact right/top screen edges
    LayoutChecks(1280,720,2,{680,20,600,400});
    const auto small=TravelIconLayout(1920,1080,1,{1800,40,100,100},true);
    Check(small.visible&&std::abs(small.icon.x-(1900-23-3-10))<.001f,"small minimap reservation does not keep a fixed 76px minimum");
    const auto large=TravelIconLayout(1920,1080,2,{1300,40,600,400},true);
    Check(large.visible&&std::abs(large.icon.x-((1900-138-10-40)/2.f))<.001f,"large native reservation converted after physical sizing");
    // Actual polish-54 screenshot: the old 38 logical units produced a
    // 20.27px icon ending at177.85px, across the quest title starting~174px.
    const Rect screenshotMap{496.813f,.0000305176f,136.533f,153.317f};
    for(float scale:{.5f,480.f/900.f,.75f,1.f,1.5f,2.f}){
        const auto shot=TravelIconLayout(640,480,scale,screenshotMap,true);
        Check(shot.visible,"actual 640x480 screenshot shortcut stays visible");
        Check(std::abs(shot.icon.w*scale-10.92264f)<.01f,"screenshot icon matches native10-11px controls");
        Check((shot.icon.y+shot.icon.h)*scale<171.f,"screenshot shortcut ends above quest title");
        const float left=shot.icon.x*scale,right=(shot.icon.x+shot.icon.w)*scale;
        Check(left>580.f&&right<607.f&&607.f-right<12.f,"screenshot shortcut sits beside and left of native M/minimize row");
        Check(std::abs(left-586.924f)<.02f&&std::abs(shot.icon.y*scale-157.413f)<.02f,"physical screenshot placement is independent of HUD scale");
    }

    for(Rect map: {Rect{-1,40,300,300},Rect{1600,-1,300,300},Rect{1800,40,300,300},
                   Rect{1500,900,300,300},Rect{1500,40,0,300},Rect{1500,40,-1,300},
                   Rect{1500,40,300,0},Rect{1500,40,300,-1},Rect{1500,40,63,300},Rect{1500,40,300,47}})
        Check(!TravelIconLayout(1920,1080,1,map,true).visible,"invalid/offscreen/collapsed minimap hidden");
    Check(!TravelIconLayout(1920,1080,1,{1500,40,300,300},false).visible,"invalid binding never guesses position");
    Check(!TravelIconLayout(1920,1080,1,{1600,780,300,300},true).visible,"map at bottom edge has no overlapping shortcut");
    Check(!TravelIconLayout(1920,1080,4,{0,40,64,100},true).visible,"left clamping cannot overlap native controls");
    for(float value:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        Check(!TravelIconLayout(value,1080,1,{100,40,300,300},true).visible,"invalid screen width hidden");
        Check(!TravelIconLayout(1920,value,1,{100,40,300,300},true).visible,"invalid screen height hidden");
        Check(!TravelIconLayout(1920,1080,value,{100,40,300,300},true).visible,"invalid HUD scale hidden");
    }
    for(float bad:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        for(int field=0;field<4;++field){Rect r{1500,40,300,300};
            if(field==0)r.x=bad;else if(field==1)r.y=bad;else if(field==2)r.w=bad;else r.h=bad;
            Check(!TravelIconLayout(1920,1080,1,r,true).visible,"nonfinite map coordinate hidden");}
    }
    Check(!TravelIconLayout(1920,1080,std::numeric_limits<float>::denorm_min(),{1500,40,300,300},true).visible,"overflowing logical viewport hidden");
    Check(CanTravelButton(true,true,false,false,false,false,true,false),"ready offline player can use travel shortcut");
    Check(!CanTravelButton(false,true,false,false,false,false,true,false),"online mode refused");
    Check(!CanTravelButton(true,false,false,false,false,false,true,false),"dead/unloaded player refused");
    Check(!CanTravelButton(true,true,true,false,false,false,true,false),"transition refused");
    Check(!CanTravelButton(true,true,false,true,false,false,true,false),"F6 window blocks travel shortcut");
    Check(!CanTravelButton(true,true,false,false,true,false,true,false),"editing refuses accidental travel");
    Check(!CanTravelButton(true,true,false,false,false,true,true,false),"pending travel blocks repeat click");
    Check(!CanTravelButton(true,true,false,false,false,false,false,false),"hidden game UI blocks shortcut");
    Check(!CanTravelButton(true,true,false,false,false,false,true,true),"overlay map blocks shortcut");
    std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
