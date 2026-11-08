#pragma once
#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>
#include <string_view>

namespace ep::hideout::rules {
inline std::string CatalogLower(std::string name){std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return name;}
inline bool CatalogHas(const std::string& name,std::initializer_list<std::string_view> words){for(auto word:words)if(name.find(word)!=std::string::npos)return true;return false;}
inline bool CatalogTag(const std::string& lower,const std::string& tag){
    if(tag=="wall")return CatalogHas(lower,{"wall","fence","railing"});
    if(tag=="column")return CatalogHas(lower,{"pillar","column","obelisk"});
    if(tag=="floor")return CatalogHas(lower,{"floor","ground","road","path_","tile","terrain","bridge","platform"})&&!CatalogHas(lower,{"pillar","column"});
    if(tag=="statue")return CatalogHas(lower,{"statue","sculpt","bust_"});
    if(tag=="banner")return CatalogHas(lower,{"banner","flag","standard_"});
    if(tag=="light")return CatalogHas(lower,{"torch","lamp","lantern","brazier","candle"});
    if(tag=="altar")return CatalogHas(lower,{"altar","shrine","fountain"});
    if(tag=="crystal")return CatalogHas(lower,{"crystal","gemstone","geode"});
    if(tag=="rock")return CatalogHas(lower,{"rock","cliff","boulder","rubble"});
    if(tag=="tree")return CatalogHas(lower,{"tree","trunk","log_","stump"});
    if(tag=="plant")return CatalogHas(lower,{"fern","bush","flower","grass","plant","vine","coral","shroom"});
    if(tag=="furniture")return CatalogHas(lower,{"chair","table","bench","chest","cache","barrel","crate","shelf","bookcase","bed_"});
    return false;
}
}
