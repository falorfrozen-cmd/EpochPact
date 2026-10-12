// Exercise the production packet parser. No game process or game files used.
#include "../core/ipc_protocol.hpp"
#include <cstdio>
int main() {
    using namespace ep::ipc;
    int checks=0, failures=0;
    auto check=[&](bool ok,const char* name) { ++checks; if(!ok)++failures; std::printf("%s %s\n",ok?"ok":"FAIL",name); };
    const auto owner=CurrentOwner();
    check(owner.pid==GetCurrentProcessId() && owner.started>0,"real process creation identity");
    const auto key=owner.Key();
    auto packet=Parse("@abcdef !"+key+" xp 2",key);
    check(packet.accepted && packet.command=="xp 2" && packet.nonce=="abcdef","bound command strips verified session token");
    bool mutated=false;
    for(const auto& wrong : {std::string("1:1"),std::to_string(owner.pid)+":"+std::to_string(owner.started+1)}) {
        const auto stale=Parse("@abcdef !"+wrong+" questscomplete 42",key);
        if(stale.accepted)mutated=true;
        check(!stale.accepted && !stale.error.empty() && stale.nonce=="abcdef","stale or reused PID rejected before execution");
    }
    check(!mutated,"wrong-session mutation never executes");
    for(const auto& line : {"", "@ !x status", "@ABCD status", "@a !", "@a !1:1", "@a !1:1 "})
        check(!Parse(line,key).accepted,"malformed packet rejected");
    check(Parse("@abc status",key).accepted && Parse("status",key).accepted,"legacy read/CLI compatibility");
    const auto ping=Ping(owner), reply=Reply(owner,"abc","\"status\"");
    check(ping.find("\"protocol\":3")!=std::string::npos && ping.find(key)!=std::string::npos,"worker ping identifies protocol and session");
    check(reply.find("\"nonce\":\"abc\"")!=std::string::npos && reply.find(key)!=std::string::npos && reply.find("\"reply\":\"status\"")!=std::string::npos,"reply preserves nonce and authenticated owner");
    std::printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
