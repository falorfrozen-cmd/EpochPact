// EpochPact's own x64 inline hook engine.
//
// Install() overwrites the first instructions of a function with a 5-byte jump to a relay
// placed within 2 GB of it; the relay jumps on to the detour. The instructions it replaced
// are copied to a trampoline (RIP-relative operands, relative jumps and calls moved to their
// new address), followed by a jump back, and `original` points at that trampoline, so the
// detour can call the real function. Every other thread of the process is suspended while
// the bytes change, and an install is retried if one of them stands inside the bytes.
// Anything the decoder does not understand makes Install() refuse, with the reason.
#pragma once

#include <string>

namespace ep::hook {

bool Install(void* target, void* detour, void** original, std::string* why);

// Puts the original bytes back. The trampoline and relay stay allocated, since a thread may
// still be running them; they are reused if the same target is hooked again.
bool Remove(void* target, std::string* why);

bool IsInstalled(void* target);

}  // namespace ep::hook
