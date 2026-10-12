// Isolated protocol transport fixture using the production parser/owner/reply.
// Never loads a game DLL; only operates inside the supplied temporary folder.
#include "../core/ipc_protocol.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
std::string Json(const std::string& text) {
    std::string out="\"";
    for(char c:text){if(c=='"'||c=='\\')out+='\\';if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else out+=c;}
    return out+'"';
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 2;
    const std::filesystem::path dir=argv[1];
    const DWORD delay=std::stoul(argv[2]);
    const auto owner=ep::ipc::CurrentOwner();
    std::ofstream(dir/"owner.json")<<ep::ipc::Ping(owner);
    int mutations=0;
    while(!std::filesystem::exists(dir/"stop")) {
        std::ifstream input(dir/"cmd.txt");
        if(!input){Sleep(2);continue;}
        std::string line;std::getline(input,line);input.close();
        std::filesystem::remove(dir/"cmd.txt");
        const auto packet=ep::ipc::Parse(line,owner.Key());
        if(!packet.accepted && packet.error.empty())continue;
        std::string result;
        if(!packet.accepted)result="{\"ok\":false,\"error\":"+Json(packet.error)+"}";
        else if(packet.command=="connectionping")result=ep::ipc::Ping(owner);
        else if(packet.command=="status")result="EpochPact; gate: offline play [client only]\nxp: x1\nspeed: x1";
        else if(packet.command=="sessionread"){Sleep(delay);result="{\"ok\":true,\"state\":\"InGame\",\"transitioning\":false}";}
        else if(packet.command=="identityread"){Sleep(delay);result="{\"ok\":true,\"offline\":true,\"player\":{\"id\":\"42\",\"name\":\"Protocol fixture\",\"scene\":\"M_Rest\"}}";}
        else if(packet.command.rfind("statraw ",0)==0)result="statraw: EpochPact added=0 increased=0 more=0 attached=1";
        else if(packet.command=="mutation"){++mutations;std::ofstream(dir/"mutations")<<mutations;result="applied";}
        else result="{\"ok\":false,\"error\":\"Unknown fixture command\"}";
        std::ofstream(dir/"seen",std::ios::app)<<packet.command<<'\n';
        {std::ofstream output(dir/"reply.tmp");output<<ep::ipc::Reply(owner,packet.nonce,Json(result));}
        // Match the production publisher's bounded retry of an atomic rename.
        // A client may briefly hold reply.json open; never execute the command again.
        bool published=false;
        for(unsigned attempt=0;attempt<250;++attempt){
            if(MoveFileExW((dir/"reply.tmp").c_str(),(dir/"reply.json").c_str(),MOVEFILE_REPLACE_EXISTING)){published=true;break;}
            const auto error=GetLastError();
            if(error!=ERROR_SHARING_VIOLATION&&error!=ERROR_ACCESS_DENIED)break;
            Sleep(2);
        }
        if(!published)return 3;
    }
    return 0;
}
