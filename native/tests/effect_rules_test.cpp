#include "../core/effect_rules.hpp"
#include <cassert>
#include <limits>
#include <cstdio>
using namespace ep::effects::rules;
int main(){
    assert(Plain("<color=red>Bleed</color>\nDeals <b>physical</b> damage.")=="Bleed Deals physical damage.");
    assert(Plain("<sprite name=fire>Burning\r\n\t effect")=="Burning effect");
    assert(Plain("Жёлтый эффект",5)=="Жё");
    assert(Timed(true,false,2,10)&&!Timed(false,false,2,10)&&!Timed(true,true,2,10));
    assert(!Timed(true,false,-1,10)&&!Timed(true,false,2,0)&&!Timed(true,false,std::numeric_limits<float>::quiet_NaN(),10));
    assert(Fraction(3,10)==.3f&&Fraction(15,10)==1&&Fraction(-1,10)==0);
    std::vector<Effect> effects(30);for(size_t i=0;i<effects.size();++i){auto& e=effects[i];e.id=static_cast<int>(1000+i);e.positive=i%2;e.timed=i%3==0;e.remaining=static_cast<float>(30-i);}
    Order(effects);assert(effects.size()==30&&effects.front().positive==false&&effects.front().timed);
    for(size_t i=1;i<effects.size();++i)assert(!effects[i-1].positive||effects[i].positive);
    size_t cases=0;for(float w:{640.f,1024.f,1920.f,3440.f})for(float h:{480.f,820.f,1080.f,1440.f})for(double scale:{.7,1.,1.5})for(bool description:{false,true}){
        const auto self=Place(w,h,scale,12,false,description),target=Place(w,h,scale,12,true,description);
        if(self.visible&&target.visible){assert(self.x+self.width<=target.x||-self.y>=-target.y+target.height);assert(self.x+self.width<=w&&target.x+target.width<=w);assert(-self.y+self.height<=h*.78f+.01f);assert(-target.y+target.height<=h*.78f+.01f);assert(self.rows<=12&&target.rows<=12);}++cases;
    }
    assert(!Place(320,800,1,5,false,true).visible&&!Place(1920,1080,2,5,false,true).visible);
    std::printf("effect rules: text, timer, ordering, dynamic definitions and %zu layout cases passed\n",cases);
}
