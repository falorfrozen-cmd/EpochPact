#include "../core/convenience_rules.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace ep::convenience::rules;
void Check(bool value){if(!value){std::puts("FAILED convenience bounds/budget");std::exit(1);}}
int main(){
    unsigned cases=0;
    // The same menu in canvas units at window sizes and UI scales. Reserve the
    // complete menu; individual gold counters cannot accidentally be omitted.
    for(float scale:{.75f,1.f,1.25f,1.5f,2.f})for(float width:{1280.f,1920.f,2560.f}){
        Rect screen{0,0,width/scale,1080/scale},menu{screen.w-760,0,760,screen.h};
        auto p=PlaceToolbar(menu,screen,nullptr,0);
        Check(!p.visible||(Inside(p.rect,screen)&&!Overlaps(p.rect,menu)));
        if(p.visible){Rect stash{0,0,menu.x-4,screen.h};auto occupied=PlaceToolbar(menu,screen,&stash,1);Check(!occupied.visible);}
        ++cases;
    }
    Rect screen{0,0,1920,1080},menu{1100,0,820,1080},obstacle{0,0,1000,1080};
    Check(!PlaceToolbar(menu,screen,&obstacle,1).visible);
    Check(!PlaceToolbar({0,0,1920,1080},screen,nullptr,0).visible);
    Check(!PlaceToolbar({0,0,std::numeric_limits<float>::quiet_NaN(),1},screen,nullptr,0).visible);
    Budget count{0,100};for(int i=0;i<8;++i)Check(count.Next(101));Check(!count.Next(101));
    Budget time{0,100};Check(time.Next(1599));Check(!time.Next(1600));
    std::printf("PASS %u scale/window combinations, occupied space, invalid bounds and time/count limits\n",cases);
}
