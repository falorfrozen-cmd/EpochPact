#include "../core/convenience_rules.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace ep::convenience::rules;
void Check(bool value){if(!value){std::puts("FAILED convenience bounds/budget");std::exit(1);}}
int main(){
    unsigned cases=0;
    for(float scale:{.75f,1.f,1.25f,1.5f,2.f})for(float width:{1280.f,1920.f,2560.f})for(float leftWidth:{185.66f,232.419f})for(float currencyWidth:{175.8f,200.f,416.71f,497.46f}){
        Rect screen{-width/scale,-540, width/scale,1080/scale};
        Rect menu{-768,-540,768,1080},footer{-708.8f,-514.2f,664,27.6f};
        Rect left{footer.x,footer.y,leftWidth,30.36f};
        const float f=CurrencyScale(footer,left,currencyWidth);
        Rect currency{footer.x+footer.w-currencyWidth*f,footer.y+footer.h*(1-f)/2,currencyWidth*f,footer.h*f};
        Check(!Overlaps(currency,left));
        Rect grid{-705.6f,-475.8f,657.6f,376.8f},neighbour{-1536,-540,768,1080};
        auto p=PlaceToolbar(menu,screen,grid,footer,left,currency);
        Check(!p.visible||(Inside(p.rect,menu)&&Inside(p.rect,screen)));
        if(Inside(menu,screen))Check(p.visible);
        if(p.visible){
            Check(Inside(p.grid,menu)&&p.rect.y>=left.y+left.h+4&&p.rect.h==32);
            Check(std::abs(p.grid.y+p.grid.h-grid.y-grid.h)<.001f);
            Check(std::abs(p.grid.w/grid.w-p.grid.h/grid.h)<.001f&&p.grid.w/grid.w>=.9f);
            for(size_t i=0;i<3;++i){auto b=p.buttons[i];Check(Inside(b,p.rect)&&!Overlaps(b,left)&&!Overlaps(b,currency)&&!Overlaps(b,p.grid)&&!Overlaps(b,neighbour));for(size_t j=0;j<i;++j)Check(!Overlaps(b,p.buttons[j]));}
        }
        ++cases;
    }
    Rect menu{-768,-540,768,1080},footer{-708.8f,-514.2f,664,27.6f},left{footer.x,footer.y,185.66f,footer.h},currency{-220.6f,footer.y,175.8f,footer.h};
    Rect screen{-1920,-540,1920,1080},grid{-705.6f,-475.8f,657.6f,376.8f};
    auto standard=PlaceToolbar(menu,screen,grid,footer,left,currency);
    Check(standard.visible&&standard.buttons[0].w==100&&standard.buttons[1].w==118&&standard.rect.y>footer.y+footer.h);
    Rect maximumCurrency{-461.51f,footer.y,416.71f,footer.h};
    auto maximum=PlaceToolbar(menu,screen,grid,footer,left,maximumCurrency);
    Check(maximum.visible&&maximum.rect.x==standard.rect.x&&maximum.rect.y==standard.rect.y);
    Check(CurrencyScale(footer,{left.x,left.y,232.419f,30.36f},497.46f)<1);
    Check(!PlaceToolbar(menu,screen,{grid.x,-530,grid.w,70},footer,left,currency).visible);
    Check(!PlaceToolbar({0,0,std::numeric_limits<float>::quiet_NaN(),1},screen,grid,footer,left,currency).visible);
    Check(!PlaceToolbar(menu,screen,grid,footer,left,{0,0,0,0}).visible);
    Check(!PlaceToolbar(menu,screen,{grid.x,grid.y,0,grid.h},footer,left,currency).visible);
    Budget count{0,100};for(int i=0;i<8;++i)Check(count.Next(101));Check(!count.Next(101));
    Budget time{0,100};Check(time.Next(1599));Check(!time.Next(1600));
    std::printf("PASS %u scale/window/currency/translation combinations, dedicated lower row, square cells, maximum balances, invalid bounds and budgets\n",cases);
}
