#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace ep::hideout::rules {
struct ObjectIdentity {int id=0,kind=0,door=0;std::string guid,name;};
inline bool SameObjectIdentity(const ObjectIdentity& a,const ObjectIdentity& b){return a.kind==b.kind&&a.door==b.door&&a.guid==b.guid&&a.name==b.name;}
// Prefer stable IDs for every survivor before matching remaining duplicates by
// catalog identity. This preserves unaffected native instances on edit Cancel.
inline std::vector<int> MatchRestoredObjects(const std::vector<ObjectIdentity>& wanted,const std::vector<ObjectIdentity>& current){
    std::vector<int> matches(wanted.size(),-1);std::vector<bool> used(current.size());
    for(size_t n=0;n<wanted.size();++n)for(size_t k=0;k<current.size();++k)
        if(!used[k]&&wanted[n].id==current[k].id&&SameObjectIdentity(wanted[n],current[k])){matches[n]=static_cast<int>(k);used[k]=true;break;}
    for(size_t n=0;n<wanted.size();++n)if(matches[n]<0)for(size_t k=0;k<current.size();++k)
        if(!used[k]&&SameObjectIdentity(wanted[n],current[k])){matches[n]=static_cast<int>(k);used[k]=true;break;}
    return matches;
}
// Completed layouts only: a moving ghost never becomes a history entry.
class LayoutHistory {
    std::vector<std::string> states;
    std::vector<std::vector<int>> objectIds;
    size_t cursor=0;
public:
    static constexpr size_t limit=50;
    void Reset(const std::string& state,const std::vector<int>& ids={}){states={state};objectIds={ids};cursor=0;}
    bool Empty()const{return states.empty();}
    bool CanUndo()const{return !states.empty()&&cursor>0;}
    bool CanRedo()const{return !states.empty()&&cursor+1<states.size();}
    size_t UndoCount()const{return cursor;}
    size_t RedoCount()const{return states.empty()?0:states.size()-cursor-1;}
    const std::string& Current()const{return states.at(cursor);}
    const std::string& UndoTarget()const{return states.at(cursor-1);}
    const std::string& RedoTarget()const{return states.at(cursor+1);}
    const std::vector<int>& CurrentIds()const{return objectIds.at(cursor);}
    const std::vector<int>& UndoIds()const{return objectIds.at(cursor-1);}
    const std::vector<int>& RedoIds()const{return objectIds.at(cursor+1);}
    void CommitUndo(){if(CanUndo())--cursor;}
    void CommitRedo(){if(CanRedo())++cursor;}
    void ReplaceCurrent(const std::string& state,const std::vector<int>& ids={}){if(Empty())Reset(state,ids);else{states[cursor]=state;objectIds[cursor]=ids;}}
    bool Record(const std::string& state,const std::vector<int>& ids={}){
        if(Empty()){Reset(state,ids);return false;}
        if(Current()==state){if(!ids.empty())objectIds[cursor]=ids;return false;}
        states.erase(states.begin()+cursor+1,states.end());objectIds.erase(objectIds.begin()+cursor+1,objectIds.end());
        states.push_back(state);objectIds.push_back(ids);cursor=states.size()-1;
        if(states.size()>limit+1){states.erase(states.begin());objectIds.erase(objectIds.begin());--cursor;}
        return true;
    }
};
}
