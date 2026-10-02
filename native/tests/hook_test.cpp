// Tests for the x64 decoder and the hook engine, on functions with known first
// instructions (test_targets.asm). Prints one line per check and, last,
// "hook_test: <passed>/<total> passed"; the exit code is the number of failures.

#include "../core/hook.hpp"
#include "../core/x64_decode.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

extern "C" int t_plain(int);
extern "C" int t_riprel(int);
extern "C" int t_cmp_flag(int);
extern "C" int t_short_jcc(int);
extern "C" int t_call(int);
extern "C" int t_tiny(int);
extern "C" int t_vex(int);
extern "C" uint32_t g_value;
extern "C" uint8_t g_flag;

namespace {

int g_total = 0, g_passed = 0;

void Check(bool ok, const char* name, const std::string& detail = {}) {
    ++g_total;
    if (ok) ++g_passed;
    std::printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", name, detail.empty() ? "" : ": ", detail.c_str());
}

using Fn = int (*)(int);
Fn o_plain, o_riprel, o_cmp, o_jcc, o_call;
int d_plain_x10(int x) { return o_plain(x) * 10; }
int d_plain_add(int x) { return o_plain(x) + 1000; }
int d_riprel(int x) { return o_riprel(x) + 1; }
int d_cmp(int x) { return o_cmp(x); }
int d_jcc(int x) { return o_jcc(x) + 1; }
int d_call(int x) { return o_call(x) * 2; }
int d_none(int x) { return x; }

struct Case {
    std::vector<uint8_t> bytes;
    uint8_t len;
    ep::x64::Kind kind;
    bool rip;
    const char* what;
};

void DecoderTable() {
    using K = ep::x64::Kind;
    const Case cases[] = {
        {{0x48, 0x89, 0x5C, 0x24, 0x08}, 5, K::Plain, false, "mov [rsp+8], rbx"},
        {{0x57}, 1, K::Plain, false, "push rdi"},
        {{0x41, 0x56}, 2, K::Plain, false, "push r14"},
        {{0x48, 0x83, 0xEC, 0x20}, 4, K::Plain, false, "sub rsp, 20h"},
        {{0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00}, 7, K::Plain, false, "sub rsp, 100h"},
        {{0x80, 0x3D, 0x11, 0x22, 0x33, 0x44, 0x00}, 7, K::Plain, true, "cmp byte [rip+x], 0"},
        {{0x48, 0x8B, 0x05, 0x11, 0x22, 0x33, 0x44}, 7, K::Plain, true, "mov rax, [rip+x]"},
        {{0x48, 0x8D, 0x0D, 0x11, 0x22, 0x33, 0x44}, 7, K::Plain, true, "lea rcx, [rip+x]"},
        {{0xF6, 0x05, 0x11, 0x22, 0x33, 0x44, 0x01}, 7, K::Plain, true, "test byte [rip+x], 1"},
        {{0xF3, 0x0F, 0x10, 0x05, 0x11, 0x22, 0x33, 0x44}, 8, K::Plain, true, "movss xmm0, [rip+x]"},
        {{0x48, 0x8B, 0xDA}, 3, K::Plain, false, "mov rbx, rdx"},
        {{0x48, 0xB8, 1, 2, 3, 4, 5, 6, 7, 8}, 10, K::Plain, false, "mov rax, imm64"},
        {{0xB8, 1, 2, 3, 4}, 5, K::Plain, false, "mov eax, imm32"},
        {{0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00}, 6, K::Plain, false, "nop word [rax+rax]"},
        {{0x0F, 0x1F, 0x80, 0, 0, 0, 0}, 7, K::Plain, false, "nop dword [rax+0]"},
        {{0x0F, 0x29, 0x74, 0x24, 0x20}, 5, K::Plain, false, "movaps [rsp+20h], xmm6"},
        {{0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0, 0, 0}, 9, K::Plain, false, "mov rax, gs:[58h]"},
        {{0xC7, 0x44, 0x24, 0x20, 1, 0, 0, 0}, 8, K::Plain, false, "mov dword [rsp+20h], 1"},
        {{0x66, 0xC7, 0x44, 0x24, 0x20, 1, 0}, 7, K::Plain, false, "mov word [rsp+20h], 1"},
        {{0xF7, 0xC1, 1, 0, 0, 0}, 6, K::Plain, false, "test ecx, 1"},
        {{0x75, 0x10}, 2, K::JccRel8, false, "jne rel8"},
        {{0x0F, 0x84, 1, 0, 0, 0}, 6, K::JccRel32, false, "je rel32"},
        {{0xE8, 1, 0, 0, 0}, 5, K::CallRel32, false, "call rel32"},
        {{0xE9, 1, 0, 0, 0}, 5, K::JmpRel32, false, "jmp rel32"},
        {{0xEB, 0x05}, 2, K::JmpRel8, false, "jmp rel8"},
        {{0xC3}, 1, K::Ret, false, "ret"},
        {{0xCC}, 1, K::Plain, false, "int3"},
    };
    for (const Case& c : cases) {
        std::vector<uint8_t> buf(c.bytes);
        buf.resize(16, 0x90);
        ep::x64::Insn in;
        const bool ok = ep::x64::Decode(buf.data(), &in);
        char detail[96];
        std::snprintf(detail, sizeof detail, "len %u kind %d rip %d", in.len, static_cast<int>(in.kind), in.ripRelative);
        Check(ok && in.len == c.len && in.kind == c.kind && in.ripRelative == c.rip, c.what, detail);
    }
    const std::vector<std::vector<uint8_t>> refused = {{0xC5, 0xF8, 0x77}, {0xE3, 0x02}, {0xE2, 0x02}, {0x62, 0xF1}};
    for (const auto& r : refused) {
        std::vector<uint8_t> buf(r);
        buf.resize(16, 0x90);
        ep::x64::Insn in;
        char name[48];
        std::snprintf(name, sizeof name, "refuses %02X %02X", r[0], r[1]);
        Check(!ep::x64::Decode(buf.data(), &in), name);
    }
}

std::string Hook(void* target, void* detour, Fn* original) {
    std::string why;
    return ep::hook::Install(target, detour, reinterpret_cast<void**>(original), &why) ? std::string() : why;
}

void Hooks() {
    std::string why;
    Check(t_plain(5) == 6, "t_plain before");
    why = Hook(reinterpret_cast<void*>(&t_plain), reinterpret_cast<void*>(&d_plain_x10), &o_plain);
    Check(why.empty(), "install t_plain", why);
    Check(t_plain(5) == 60, "t_plain hooked (x10)", std::to_string(t_plain(5)));
    Check(ep::hook::Remove(reinterpret_cast<void*>(&t_plain), &why), "remove t_plain", why);
    Check(t_plain(5) == 6, "t_plain after remove", std::to_string(t_plain(5)));
    why = Hook(reinterpret_cast<void*>(&t_plain), reinterpret_cast<void*>(&d_plain_add), &o_plain);
    Check(why.empty() && t_plain(5) == 1006, "t_plain re-hooked with another detour", std::to_string(t_plain(5)));
    Check(!ep::hook::Install(reinterpret_cast<void*>(&t_plain), reinterpret_cast<void*>(&d_none), nullptr, &why),
          "a second install is refused", why);
    ep::hook::Remove(reinterpret_cast<void*>(&t_plain), nullptr);

    why = Hook(reinterpret_cast<void*>(&t_riprel), reinterpret_cast<void*>(&d_riprel), &o_riprel);
    Check(why.empty() && t_riprel(2) == 43, "RIP-relative load moved to the trampoline", why.empty() ? std::to_string(t_riprel(2)) : why);
    g_value = 50;
    Check(t_riprel(2) == 53, "the moved load still reads the same variable", std::to_string(t_riprel(2)));
    ep::hook::Remove(reinterpret_cast<void*>(&t_riprel), nullptr);

    why = Hook(reinterpret_cast<void*>(&t_cmp_flag), reinterpret_cast<void*>(&d_cmp), &o_cmp);
    g_flag = 0;
    const int off = t_cmp_flag(5);
    g_flag = 1;
    const int on = t_cmp_flag(5);
    Check(why.empty() && off == 5 && on == 105, "IL2CPP-style flag compare moved", why.empty() ? std::to_string(off) + "/" + std::to_string(on) : why);
    ep::hook::Remove(reinterpret_cast<void*>(&t_cmp_flag), nullptr);

    why = Hook(reinterpret_cast<void*>(&t_short_jcc), reinterpret_cast<void*>(&d_jcc), &o_jcc);
    Check(why.empty() && t_short_jcc(0) == 4 && t_short_jcc(5) == 8, "short jcc widened, both branches",
          why.empty() ? std::to_string(t_short_jcc(0)) + "/" + std::to_string(t_short_jcc(5)) : why);
    ep::hook::Remove(reinterpret_cast<void*>(&t_short_jcc), nullptr);

    why = Hook(reinterpret_cast<void*>(&t_call), reinterpret_cast<void*>(&d_call), &o_call);
    Check(why.empty() && t_call(1) == 2002, "relative call moved", why.empty() ? std::to_string(t_call(1)) : why);
    ep::hook::Remove(reinterpret_cast<void*>(&t_call), nullptr);

    why = Hook(reinterpret_cast<void*>(&t_tiny), reinterpret_cast<void*>(&d_none), nullptr);
    Check(!why.empty() && why.find("returns") != std::string::npos, "refuses a function shorter than the jump", why);
    Check(t_tiny(3) == 0, "the refused function is untouched");
    why = Hook(reinterpret_cast<void*>(&t_vex), reinterpret_cast<void*>(&d_none), nullptr);
    Check(!why.empty() && why.find("decode") != std::string::npos, "refuses an encoding it does not know", why);
    Check(t_vex(3) == 0, "the refused function is untouched");
}

// A thread calls t_plain nonstop while the hook goes in and out 300 times: every result
// must be either the plain one (2) or the hooked one (20).
void Race() {
    std::atomic<bool> stop{false};
    std::atomic<long long> calls{0}, wrong{0};
    std::thread caller([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            const int r = t_plain(1);
            if (r != 2 && r != 20) wrong.fetch_add(1);
            calls.fetch_add(1, std::memory_order_relaxed);
        }
    });
    int failures = 0;
    for (int i = 0; i < 300; ++i) {
        if (!Hook(reinterpret_cast<void*>(&t_plain), reinterpret_cast<void*>(&d_plain_x10), &o_plain).empty()) ++failures;
        if (!ep::hook::Remove(reinterpret_cast<void*>(&t_plain), nullptr)) ++failures;
    }
    stop = true;
    caller.join();
    char detail[96];
    std::snprintf(detail, sizeof detail, "%lld calls, %lld wrong results, %d failed installs/removes", calls.load(), wrong.load(), failures);
    Check(wrong == 0 && failures == 0 && calls > 0, "install/remove under a running caller", detail);
}

}  // namespace

int main() {
    DecoderTable();
    Hooks();
    Race();
    std::printf("hook_test: %d/%d passed\n", g_passed, g_total);
    return g_total - g_passed;
}
