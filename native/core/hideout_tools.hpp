#pragma once
#include <array>
#include <cmath>

namespace ep::hideout::rules {
struct Pose {float x=0,z=0,lift=0,yaw=0,size=1;};
inline float Snap(float value,float step){return step>0?std::round(value/step)*step:value;}
inline Pose Follow(const Pose& item,const Pose& before,const Pose& after){
    const float ratio=after.size/before.size,angle=(after.yaw-before.yaw)*3.14159265358979323846f/180;
    const float x=(item.x-before.x)*ratio,z=(item.z-before.z)*ratio,c=std::cos(angle),s=std::sin(angle);
    return {after.x+c*x+s*z,after.z-s*x+c*z,after.lift+(item.lift-before.lift)*ratio,item.yaw+after.yaw-before.yaw,item.size*ratio};
}
inline std::array<std::array<float,2>,4> Rectangle(float cx,float cz,float ex,float ez){return {{{cx-ex,cz-ez},{cx+ex,cz-ez},{cx+ex,cz+ez},{cx-ex,cz+ez}}};}
}
