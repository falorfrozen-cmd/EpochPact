#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>
namespace ep::effects::rules {
// Presentation limits are not a six/24-effect catalog. Every live definition
// can be shown; the finite HUD bounds how many rows are on screen together.
constexpr size_t kMaxEffects=256,kMaxRows=12;
inline std::string Plain(const std::string& source,size_t limit=220){
    std::string out;bool tag=false,space=false;
    for(size_t i=0;i<source.size();++i){const unsigned char c=source[i];
        if(c=='<'){tag=true;continue;}if(tag){if(c=='>')tag=false;continue;}
        if(c=='\n'||c=='\r'||c=='\t'||c==' '){space=!out.empty();continue;}
        if(c<32)continue;
        // Cut only at UTF-8 character boundaries, never inside a glyph.
        const size_t count=c<128?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:(c&0xf8)==0xf0?4:1;
        if(out.size()+count+(space?1:0)>limit||i+count>source.size())break;
        if(space){out+=' ';space=false;}out.append(source,i,count);i+=count-1;
    }return out;
}
inline bool Timed(bool fixed,bool ignore,float remaining,float duration){
    return fixed&&!ignore&&std::isfinite(remaining)&&std::isfinite(duration)&&remaining>=0&&duration>0;
}
inline float Fraction(float remaining,float duration){return Timed(true,false,remaining,duration)?std::clamp(remaining/duration,0.f,1.f):0.f;}
struct Effect{int id=0;bool ailment=true,positive=false,timed=false;float remaining=0,duration=0;int stacks=0;std::string name,description,stackLabel;void* sprite=nullptr;};
inline void Order(std::vector<Effect>& values){
    std::stable_sort(values.begin(),values.end(),[](const Effect& a,const Effect& b){
        if(a.positive!=b.positive)return !a.positive; // harmful first, no guessed danger score
        if(a.timed!=b.timed)return a.timed;
        if(a.timed&&a.remaining!=b.remaining)return a.remaining<b.remaining;
        if(a.ailment!=b.ailment)return a.ailment;
        return a.id<b.id;
    });
}
struct Layout{float width=0,row=0,height=0,x=0,y=0,scale=1;size_t rows=0;bool visible=false;};
inline Layout Place(float w,float h,double scale,size_t requested,bool target,bool descriptions){
    Layout r;if(!std::isfinite(w)||!std::isfinite(h)||w<480||h<320||!std::isfinite(scale)||scale<.7||scale>1.5)return r;
    r.width=std::min(300.f*static_cast<float>(scale),w*(target?.32f:.29f));
    const float s=std::min(static_cast<float>(scale),r.width/300.f),top=target?std::max(65.f,h*.09f):std::max(100.f,h*.23f);
    if(s<.7f)return r;r.scale=s;r.row=std::max(descriptions?52.f:35.f,(descriptions?64.f:42.f)*s);
    const float bottom=h*.78f,available=bottom-top-60*s;
    if(available<r.row||r.width<150)return r;
    r.rows=std::min({std::clamp(requested,size_t(1),kMaxRows),static_cast<size_t>(available/r.row),kMaxRows});
    r.height=60*s+r.row*static_cast<float>(r.rows);r.x=target?w*.33f:18.f;r.y=-top;r.visible=r.rows>0;
    return r;
}
}
