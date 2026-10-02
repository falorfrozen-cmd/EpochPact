#include "x64_decode.hpp"

#include <cstring>

namespace ep::x64 {

namespace {

// One-byte opcodes: ModRM present? (x64; the invalid-in-64-bit ones are handled below)
bool OneByteHasModRm(uint8_t op) {
    if (op < 0x40) return (op & 0x07) < 0x04;  // ALU r/m forms: 00-03, 08-0B, ... 38-3B
    switch (op) {
        case 0x63: case 0x69: case 0x6B:
        case 0x80: case 0x81: case 0x83:
        case 0x84: case 0x85: case 0x86: case 0x87: case 0x88: case 0x89: case 0x8A: case 0x8B:
        case 0x8C: case 0x8D: case 0x8E: case 0x8F:
        case 0xC0: case 0xC1: case 0xC6: case 0xC7:
        case 0xD0: case 0xD1: case 0xD2: case 0xD3:
        case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        case 0xF6: case 0xF7: case 0xFE: case 0xFF:
            return true;
        default:
            return false;
    }
}

// Two-byte opcodes (0F xx) without a ModRM byte.
bool TwoByteNoModRm(uint8_t op) {
    switch (op) {
        case 0x05: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0B: case 0x0E:  // syscall, clts, sysret, invd, wbinvd, ud2, femms
        case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x37:  // wrmsr, rdtsc, rdmsr, rdpmc, sysenter, sysexit, getsec
        case 0x77:                                                                    // emms
        case 0xA0: case 0xA1: case 0xA2: case 0xA8: case 0xA9: case 0xAA:             // push/pop fs/gs, cpuid, rsm
            return true;
        default:
            return (op >= 0x80 && op <= 0x8F) || (op >= 0xC8 && op <= 0xCF);        // jcc rel32, bswap
    }
}

// Two-byte opcodes with an imm8 after the ModRM operand.
bool TwoByteImm8(uint8_t op) {
    switch (op) {
        case 0x70: case 0x71: case 0x72: case 0x73: case 0xA4: case 0xAC: case 0xBA:
        case 0xC2: case 0xC4: case 0xC5: case 0xC6:
            return true;
        default:
            return false;
    }
}

}  // namespace

bool Decode(const uint8_t* code, Insn* out) {
    Insn in{};
    const uint8_t* p = code;
    bool opsize16 = false, addr32 = false, rexW = false;

    // Legacy prefixes, at most four in practice; 15 bytes is the architectural limit.
    for (int i = 0; i < 14; ++i) {
        const uint8_t b = *p;
        if (b == 0x66) opsize16 = true;
        else if (b == 0x67) addr32 = true;
        else if (b == 0xF0 || b == 0xF2 || b == 0xF3 || b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65) {}
        else break;
        ++p;
    }
    if ((*p & 0xF0) == 0x40) {
        rexW = (*p & 0x08) != 0;
        ++p;
    }

    const uint8_t op = *p++;
    bool modrm = false;
    int imm = 0;  // immediate bytes after the ModRM operand (or after the opcode)
    const int iz = opsize16 ? 2 : 4;

    if (op == 0x0F) {
        const uint8_t op2 = *p++;
        if (op2 == 0x38) {
            ++p;
            modrm = true;
        } else if (op2 == 0x3A) {
            ++p;
            modrm = true;
            imm = 1;
        } else if (op2 >= 0x80 && op2 <= 0x8F) {
            std::memcpy(&in.rel, p, 4);
            p += 4;
            in.kind = Kind::JccRel32;
            in.condition = op2 & 0x0F;
            in.len = static_cast<uint8_t>(p - code);
            *out = in;
            return true;
        } else if (op2 == 0x0F || op2 == 0x24 || op2 == 0x25 || op2 == 0x26 || op2 == 0x27 || op2 == 0x36 || op2 == 0x39 || op2 == 0x3B ||
                   op2 == 0x3C || op2 == 0x3D || op2 == 0x3E || op2 == 0x3F || op2 == 0x04 || op2 == 0x0A || op2 == 0x0C || op2 == 0x7A ||
                   op2 == 0x7B || op2 == 0xA6 || op2 == 0xA7 || op2 == 0xB9 || op2 == 0xFF) {
            *out = in;  // 3DNow!, reserved and undefined forms
            return false;
        } else {
            modrm = !TwoByteNoModRm(op2);
            if (TwoByteImm8(op2)) imm = 1;
        }
    } else {
        switch (op) {
            // Invalid in 64-bit mode, VEX/EVEX, and branches that cannot be widened.
            case 0x06: case 0x07: case 0x0E: case 0x16: case 0x17: case 0x1E: case 0x1F: case 0x27: case 0x2F: case 0x37: case 0x3F:
            case 0x60: case 0x61: case 0x62: case 0x82: case 0x9A: case 0xC4: case 0xC5: case 0xCE: case 0xD4: case 0xD5: case 0xD6:
            case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xEA:
                *out = in;
                return false;
            default:
                break;
        }
        if (op >= 0x70 && op <= 0x7F) {
            in.rel = static_cast<int8_t>(*p++);
            in.kind = Kind::JccRel8;
            in.condition = op & 0x0F;
            in.len = static_cast<uint8_t>(p - code);
            *out = in;
            return true;
        }
        if (op == 0xEB || op == 0xE9 || op == 0xE8) {
            if (op == 0xEB) {
                in.rel = static_cast<int8_t>(*p++);
                in.kind = Kind::JmpRel8;
            } else {
                std::memcpy(&in.rel, p, 4);
                p += 4;
                in.kind = op == 0xE9 ? Kind::JmpRel32 : Kind::CallRel32;
            }
            in.len = static_cast<uint8_t>(p - code);
            *out = in;
            return true;
        }
        modrm = OneByteHasModRm(op);
        if (op < 0x40) {
            if ((op & 0x07) == 0x04) imm = 1;       // AL, ib
            else if ((op & 0x07) == 0x05) imm = iz;  // eAX, iz
        } else if (op == 0x68 || op == 0x69 || op == 0x81 || op == 0xA9 || op == 0xC7) {
            imm = iz;
        } else if (op == 0x6A || op == 0x6B || op == 0x80 || op == 0x83 || op == 0xA8 || op == 0xC0 || op == 0xC1 || op == 0xC6 ||
                   op == 0xCD || (op >= 0xB0 && op <= 0xB7) || (op >= 0xE4 && op <= 0xE7)) {
            imm = 1;
        } else if (op >= 0xB8 && op <= 0xBF) {
            imm = rexW ? 8 : iz;                     // mov r64, imm64 with REX.W
        } else if (op >= 0xA0 && op <= 0xA3) {
            imm = addr32 ? 4 : 8;                    // mov moffs
        } else if (op == 0xC2 || op == 0xCA) {
            imm = 2;
        } else if (op == 0xC8) {
            imm = 3;                                 // enter iw, ib
        }
        if (op == 0xC3 || op == 0xC2) in.kind = Kind::Ret;
    }

    if (modrm) {
        const uint8_t m = *p++;
        const uint8_t mod = m >> 6, reg = (m >> 3) & 7, rm = m & 7;
        if (op == 0xF6 && reg < 2) imm = 1;   // test r/m8, ib
        if (op == 0xF7 && reg < 2) imm = iz;  // test r/m, iz
        if (mod != 3) {
            if (rm == 4) {
                const uint8_t sib = *p++;
                if (mod == 0 && (sib & 7) == 5) p += 4;  // no base: disp32
            } else if (mod == 0 && rm == 5) {
                in.ripRelative = true;
                in.dispOffset = static_cast<uint8_t>(p - code);
                p += 4;
            }
            if (mod == 1) p += 1;
            else if (mod == 2) p += 4;
        }
    }
    p += imm;
    in.len = static_cast<uint8_t>(p - code);
    if (in.len > 15) {
        in.kind = Kind::Unsupported;
        *out = in;
        return false;
    }
    if (in.kind != Kind::Ret) in.kind = Kind::Plain;
    *out = in;
    return true;
}

}  // namespace ep::x64
