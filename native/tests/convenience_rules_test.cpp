#include "../core/convenience_rules.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace ep::convenience::rules;
void Check(bool value){if(!value){std::puts("FAILED convenience bounds/budget");std::exit(1);}}
int main(){
    unsigned cases=0;
    // Measured real game geometry; include longer translated Transfer/Sort
    // labels and wider currency values. Every visible shortcut must remain
    // wholly inside the footer, clear of cells and neighbouring game panels.
    for(float scale:{.75f,1.f,1.25f,1.5f,2.f})for(float width:{1280.f,1920.f,2560.f})for(float leftWidth:{185.66f,210.f})for(float currencyWidth:{175.8f,200.f,300.f,460.f}){
        Rect screen{-width/scale,-540, width/scale,1080/scale};
        Rect menu{-768,-540,768,1080},footer{-708.8f,-514.2f,664,27.6f};
        Rect left{footer.x,footer.y,leftWidth,footer.h},currency{footer.x+footer.w-currencyWidth,footer.y,currencyWidth,footer.h};
        Rect grid{-705.6f,-475.8f,657.6f,376.8f},neighbour{-1536,-540,768,1080};
        auto p=PlaceToolbar(menu,screen,footer,left,currency);
        Check(!p.visible||(Inside(p.rect,menu)&&Inside(p.rect,screen)&&Inside(p.rect,footer)));
        if(p.visible)for(size_t i=0;i<3;++i){auto b=p.buttons[i];Check(Inside(b,p.rect)&&!Overlaps(b,left)&&!Overlaps(b,currency)&&!Overlaps(b,grid)&&!Overlaps(b,neighbour));for(size_t j=0;j<i;++j)Check(!Overlaps(b,p.buttons[j]));}
        if(currencyWidth>=300)Check(!p.visible);
        ++cases;
    }
    Rect menu{-768,-540,768,1080},footer{-708.8f,-514.2f,664,27.6f},left{footer.x,footer.y,185.66f,footer.h},currency{-220.6f,footer.y,175.8f,footer.h};
    Rect screen{-1920,-540,1920,1080};auto standard=PlaceToolbar(menu,screen,footer,left,currency);Check(standard.visible);Check(standard.buttons[1].w>standard.buttons[0].w);
    currency.x=-275;currency.w=230.2f;auto compact=PlaceToolbar(menu,screen,footer,left,currency);Check(compact.visible&&compact.rect.w<standard.rect.w);
    Check(!PlaceToolbar({0,0,std::numeric_limits<float>::quiet_NaN(),1},screen,footer,left,currency).visible);
    Check(!PlaceToolbar(menu,screen,footer,left,{0,0,0,0}).visible);
    Check(!PlaceToolbar(menu,screen,{footer.x,footer.y,footer.w,20},left,currency).visible);
    Budget count{0,100};for(int i=0;i<8;++i)Check(count.Next(101));Check(!count.Next(101));
    Budget time{0,100};Check(time.Next(1599));Check(!time.Next(1600));
    std::printf("PASS %u scale/window/currency/translation combinations, measured footer placement, compact layout, invalid bounds and time/count limits\n",cases);
}
