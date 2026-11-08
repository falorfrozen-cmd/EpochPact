// Personal layouts are parsed into temporary data for previews, never applied.
std::vector<std::string> SavedNames(){
    std::vector<std::string> names;std::error_code error;
    for(const auto& e:std::filesystem::directory_iterator(TemplatesFolder(),error)){
        if(!e.is_regular_file()||e.is_symlink()||e.path().extension()!=L".txt")continue;
        const auto name=e.path().stem().string();if(rules::ValidName(name.c_str()))names.push_back(name);
    }
    std::sort(names.begin(),names.end());return names;
}
std::string SavedBytes(const std::string& name){
    auto path=TemplatePath(name);std::error_code error;if(std::filesystem::is_symlink(path,error)||std::filesystem::file_size(path,error)>1024*1024||error)throw std::runtime_error("Template is missing or invalid.");
    std::ifstream input(path,std::ios::binary);return {(std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>()};
}
std::string SavedKey(const std::string& name){
    const auto text=name+SavedBytes(name);uint64_t hash=1469598103934665603ull;for(unsigned char c:text)hash=(hash^c)*1099511628211ull;
    std::ostringstream key;key<<"s:"<<std::hex<<hash<<':';for(unsigned char c:name)key<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(c);return key.str();
}
std::string SavedKeyName(const std::string& key){
    const auto split=key.find(':',2);if(key.rfind("s:",0)!=0||split==std::string::npos||split>18||(key.size()-split-1)%2||key.size()-split-1>80)throw std::runtime_error("Invalid saved preview key.");
    std::string name;for(size_t k=split+1;k<key.size();k+=2){const auto pair=key.substr(k,2);if(!std::all_of(pair.begin(),pair.end(),[](unsigned char c){return std::isxdigit(c);}))throw std::runtime_error("Invalid saved preview key.");name+=static_cast<char>(std::stoi(pair,nullptr,16));}
    if(!rules::ValidName(name.c_str())||SavedKey(name)!=key)throw std::runtime_error("Template changed. Refresh its preview.");return name;
}
ingame::json::Value SavedDesign(const std::string& key){
    std::istringstream input(SavedBytes(SavedKeyName(key)));std::vector<Item> read;int shape=0,texture=0;float size=38;
    if(!ReadLayout(input,false,&read,&shape,&size,&texture))throw std::runtime_error("That file is not a hideout template.");
    const char* shapes[]{"round","square","hall","cross","islands"};std::ostringstream out;out<<"{\"shape\":"<<Json(shapes[shape])<<",\"size\":"<<size<<",\"texture\":"<<Json(kTextures[texture].label)<<",\"items\":[";bool first=true;
    for(const auto& i:read)if(!i.off){if(!first)out<<',';first=false;out<<"{\"kind\":"<<i.kind<<",\"name\":"<<Json(i.name)<<",\"guid\":"<<Json(i.guid)<<",\"right\":"<<i.right<<",\"up\":"<<i.up<<",\"lift\":"<<i.lift<<",\"yaw\":"<<i.yaw<<",\"size\":"<<i.size<<",\"maxSpan\":0}";}
    ingame::json::Value result;if(!ingame::json::Parse(out.str()+"]}",result))throw std::runtime_error("Template preview could not be read.");return result;
}
