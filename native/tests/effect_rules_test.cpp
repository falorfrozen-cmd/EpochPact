#include "../core/effect_rules.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace ep::effects::rules;
// Explicit checks: the native tests are built with NDEBUG, which removes assert().
void Check(bool value,const char* what){if(!value){std::printf("FAILED effect rules: %s\n",what);std::exit(1);}}
int main(){
    Check(Plain("<color=red>Bleed</color>\nDeals <b>physical</b> damage.")=="Bleed Deals physical damage.","tags and line breaks");
    Check(Plain("<sprite name=fire>Burning\r\n\t effect")=="Burning effect","sprites and whitespace");
    Check(Plain("Жёлтый эффект",5)=="Жё","UTF-8 boundary");
    Check(Timed(true,false,2,10)&&!Timed(false,false,2,10)&&!Timed(true,true,2,10),"timed flags");
    Check(!Timed(true,false,-1,10)&&!Timed(true,false,2,0)&&!Timed(true,false,std::numeric_limits<float>::quiet_NaN(),10),"invalid timers");
    Check(Fraction(3,10)==.3f&&Fraction(15,10)==1&&Fraction(-1,10)==0,"timer fraction");
    std::vector<Effect> effects(30);for(size_t i=0;i<effects.size();++i){auto& e=effects[i];e.id=static_cast<int>(1000+i);e.positive=i%2;e.timed=i%3==0;e.remaining=static_cast<float>(30-i);}
    Order(effects);Check(effects.size()==30&&effects.front().positive==false&&effects.front().timed,"harmful timed first");
    for(size_t i=1;i<effects.size();++i)Check(!effects[i-1].positive||effects[i].positive,"harmful before helpful");
    Check(LineRatio(1.3f)==1.3f&&LineRatio(1.05f)==1.2f&&LineRatio(0.2f)==1.45f&&LineRatio(std::numeric_limits<float>::infinity())==1.45f,"line ratio bounds and fallback");
    size_t cases=0;
    for(float w:{640.f,1024.f,1280.f,1920.f,3440.f})for(float h:{480.f,820.f,1024.f,1080.f,1440.f})for(double scale:{.7,.85,1.,1.25,1.5})
    for(bool description:{false,true})for(float line:{1.15f,1.45f,1.9f}){
        const auto self=Place(w,h,scale,12,false,description,line),target=Place(w,h,scale,12,true,description,line);
        for(const auto* p:{&self,&target}){
            if(!p->visible)continue;
            // Every text box holds at least one full line of its own font size.
            Check(p->titleHeight>=p->titleSize*line-.01f&&p->nameHeight>=p->nameSize*line-.01f&&p->timeHeight>=p->timeSize*line-.01f,"text boxes hold a line");
            Check(p->stackHeight>=p->stackSize*line-.01f&&p->footerHeight>=p->footerSize*line-.01f,"stack and footer boxes hold a line");
            Check(!description||p->descriptionHeight>=2*p->descriptionSize*line-.01f,"two description lines");
            Check(p->titleSize>=12&&p->nameSize>=12&&p->descriptionSize>=11&&p->timeSize>=11&&p->stackSize>=10&&p->footerSize>=11,"readable minimum sizes");
            // Name, description and timer track fit in a row; so do the icon and the stack count on it.
            Check(p->nameHeight+p->descriptionHeight+7*p->scale<=p->row,"row contents fit");
            Check(p->icon+8*p->scale<=p->row+.01f&&p->stackHeight<=p->icon,"icon and stack fit");
            Check(p->timeHeight<=p->nameHeight,"timer beside the name");
            Check(p->height==p->chrome+p->row*static_cast<float>(p->rows),"panel height");
            Check(-p->y+p->height<=h*.78f+.01f&&p->x+p->width<=w&&p->rows>=1&&p->rows<=12,"panel on screen");
        }
        if(self.visible&&target.visible)Check(self.x+self.width<=target.x||-self.y>=-target.y+target.height,"panels do not overlap");
        ++cases;
    }
    Check(!Place(320,800,1,5,false,true).visible&&!Place(1920,1080,2,5,false,true).visible,"unsupported sizes hidden");
    // The live failure: at scale 0.7 the boxes shrank below the readable text size.
    const auto small=Place(1280,1024,.7,3,false,false,1.45f);
    Check(small.visible&&small.nameHeight>=12*1.45f-.01f&&small.titleHeight>=12*1.45f-.01f,"scale 0.7 keeps names and title");
    std::printf("effect rules: text, timer, ordering, dynamic definitions and %zu layout cases passed\n",cases);
}
