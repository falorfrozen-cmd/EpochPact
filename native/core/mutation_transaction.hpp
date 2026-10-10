#pragma once
#include <stdexcept>
#include <string>

namespace ep::transaction {
// Run only brackets the two managed phases. Write must finish on the caller's
// worker before Apply can be queued. A refused/no-op prepare creates no backup.
template<class Run, class Prepare, class Write, class Apply, class Quote>
std::string Execute(Run run, Prepare prepare, Write write, Apply apply, Quote quote) {
    bool prepared = false;
    auto reply = run([&] {
        auto early = prepare();
        if (!early.empty()) return early;
        prepared = true;
        return std::string("{\"ok\":true}");
    });
    // Run can fail after Prepare, e.g. an executor/guard failure. Never write or
    // mutate unless it reports that the whole preparation completed.
    if (!prepared || reply != "{\"ok\":true}") return reply;
    std::string backup;
    try { backup = write(); }
    catch (const std::exception& e) { return "{\"ok\":false,\"error\":" + quote(e.what()) + "}"; }
    if (backup.empty()) return "{\"ok\":false,\"error\":\"backup path missing; action cancelled\"}";
    reply = run(apply);
    // Also retain the recovery path on timeout, identity change or guarded
    // failure after the backup was committed. Never retry a save mutation.
    if (reply.empty() || reply.front() != '{' || reply.back() != '}')
        reply = "{\"ok\":false,\"error\":\"invalid mutation response\"}";
    reply.pop_back();
    return reply + ",\"backup\":" + quote(backup) + "}";
}
}
