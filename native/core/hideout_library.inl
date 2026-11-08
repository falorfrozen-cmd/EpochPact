// Library preferences are independent of layout history and character saves.
std::set<std::string> favoriteModels;
std::deque<std::string> recentModels;
void LoadLibrary(){
    auto path=FilePath();if(path.empty())return;path+=L".library";std::ifstream in(path);char type=0;std::string guid;
    while(in>>type>>guid){if(!byGuid.count(guid))continue;if(type=='F')favoriteModels.insert(guid);else if(type=='R'&&recentModels.size()<32&&std::find(recentModels.begin(),recentModels.end(),guid)==recentModels.end())recentModels.push_back(guid);}
}
void SaveLibrary(){
    auto path=FilePath();if(path.empty())throw std::runtime_error("No preferences folder is available.");path+=L".library";std::ostringstream out;
    for(const auto& guid:favoriteModels)out<<"F "<<guid<<'\n';for(const auto& guid:recentModels)out<<"R "<<guid<<'\n';const auto data=out.str();
    if(!WriteFile(path,&data))throw std::runtime_error("Could not save library preferences.");
}
void RememberModel(const std::string& guid){
    recentModels.erase(std::remove(recentModels.begin(),recentModels.end(),guid),recentModels.end());recentModels.push_front(guid);if(recentModels.size()>32)recentModels.pop_back();
    try{SaveLibrary();}catch(const std::exception& e){Log("hideout library: %s",e.what());}
}
bool LibraryCategory(const Entry& entry,const std::string& category){
    if(category=="all")return true;
    if(category=="favorites")return favoriteModels.count(entry.guid)!=0;
    if(category=="recent")return std::find(recentModels.begin(),recentModels.end(),entry.guid)!=recentModels.end();
    if(category.rfind("tag:",0)==0)return rules::CatalogTag(entry.lower,category.substr(4));
    return category.size()==1&&entry.category==category[0];
}
std::optional<std::string> LibraryCommand(const std::vector<std::string>& args){
    if(args[0]!="favorite")return std::nullopt;const auto index=std::stoul(args.at(1));if(index>=catalog.size())throw std::runtime_error("Unknown library model.");
    const auto old=favoriteModels;const auto guid=catalog[index].guid;const bool on=args.size()>2?args[2]=="1":!favoriteModels.count(guid);
    if(on)favoriteModels.insert(guid);else favoriteModels.erase(guid);
    try{SaveLibrary();}catch(...){favoriteModels=old;throw;}
    return State();
}
