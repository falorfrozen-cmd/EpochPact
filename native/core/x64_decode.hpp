// A small x64 instruction decoder for the hook engine: the length of one instruction and
// what moving it to another address needs (a RIP-relative operand, a relative jump or call).
// It covers the general-purpose and SSE encodings compiled code starts functions with, and
// says "unsupported" for everything else (VEX/EVEX, loop/jrcxz, far forms), so the engine
// refuses a hook rather than guessing.
#pragma once

#include <cstdint>

namespace ep::x64 {

enum class Kind : uint8_t {
    Plain,       // copy as is (a RIP-relative operand still needs its displacement moved)
    JmpRel8,     // EB cb
    JmpRel32,    // E9 cd
    JccRel8,     // 7x cb
    JccRel32,    // 0F 8x cd
    CallRel32,   // E8 cd
    Ret,         // C3 / C2 iw: the function may end here
    Unsupported,
};

struct Insn {
    uint8_t len = 0;
    Kind kind = Kind::Unsupported;
    bool ripRelative = false;  // ModRM with mod=00, rm=101: a disp32 relative to the next instruction
    uint8_t dispOffset = 0;    // where that disp32 sits inside the instruction
    uint8_t condition = 0;     // for jcc: the low nibble of the opcode (0x0-0xF)
    int32_t rel = 0;           // for relative jumps and calls: the displacement
};

// Decodes the instruction at `code` (at most 15 bytes are read). Returns false and sets
// kind = Unsupported when the encoding is outside what the decoder knows.
bool Decode(const uint8_t* code, Insn* out);

}  // namespace ep::x64
