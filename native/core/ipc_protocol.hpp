#pragma once
#include <windows.h>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace ep::ipc {
struct Owner {
    DWORD pid;
    uint64_t started;
    std::string Key() const { return std::to_string(pid) + ":" + std::to_string(started); }
};
inline Owner CurrentOwner() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        throw std::runtime_error("could not identify the game process");
    return {GetCurrentProcessId(), (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime};
}
struct Packet { std::string nonce, command, error; bool accepted = false; };
inline Packet Parse(const std::string& line, const std::string& owner) {
    Packet packet;
    if (line.empty()) return packet;
    packet.command = line;
    if (line.front() != '@') { packet.accepted = true; return packet; }
    const auto space = line.find(' ');
    if (space == std::string::npos || space < 2 || space > 65) return {};
    packet.nonce = line.substr(1, space - 1);
    if (packet.nonce.find_first_not_of("0123456789abcdef") != std::string::npos) return {};
    packet.command = line.substr(space + 1);
    if (!packet.command.empty() && packet.command.front() == '!') {
        const auto end = packet.command.find(' ');
        if (end == std::string::npos || packet.command.substr(1, end - 1) != owner) {
            packet.error = "The game restarted or a different game session received the request. The action was not applied.";
            return packet;
        }
        packet.command.erase(0, end + 1);
    }
    packet.accepted = !packet.command.empty();
    return packet;
}
inline std::string Ping(const Owner& owner) {
    return "{\"ok\":true,\"protocol\":3,\"processId\":" + std::to_string(owner.pid) +
        ",\"sessionKey\":\"" + owner.Key() + "\"}";
}
inline std::string Reply(const Owner& owner, const std::string& nonce, const std::string& encodedReply) {
    return "{\"nonce\":\"" + nonce + "\",\"processId\":" + std::to_string(owner.pid) +
        ",\"sessionKey\":\"" + owner.Key() + "\",\"reply\":" + encodedReply + "}";
}
}
