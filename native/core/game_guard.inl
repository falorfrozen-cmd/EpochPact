// Guarded(): the try/catch half (managed exceptions are C++ exceptions in IL2CPP) and
// the SEH half (an access violation in game code), kept apart because a function with
// __try may not hold objects that need unwinding.
#pragma once

#include <windows.h>

#include <cstdio>
#include <string>

namespace ep::game::detail {

template <typename F>
int SehCall(F& fn) {
    __try {
        fn();
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return static_cast<int>(GetExceptionCode());
    }
}

}  // namespace ep::game::detail

namespace ep::game {

template <typename F>
bool Guarded(F&& fn, std::string* why) {
    try {
        const int code = detail::SehCall(fn);
        if (code == static_cast<int>(0xE06D7363)) {  // a C++ throw, which is how IL2CPP raises managed exceptions
            if (why) *why = "managed exception inside game code";
            return false;
        }
        if (code != 0) {
            if (why) {
                char buf[64];
                std::snprintf(buf, sizeof buf, "fault 0x%08X inside game code", static_cast<unsigned>(code));
                *why = buf;
            }
            return false;
        }
        return true;
    } catch (...) {
        if (why) *why = "managed exception inside game code";
        return false;
    }
}

}  // namespace ep::game
