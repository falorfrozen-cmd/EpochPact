#include "hook.hpp"

#include "x64_decode.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdarg>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace ep::hook {

namespace {

constexpr size_t kPatch = 5;            // E9 rel32
constexpr size_t kTrampolineMax = 128;
constexpr intptr_t kReach = 0x7FF00000; // stay a little inside rel32's +-2 GB

struct Block {
    uint8_t* base;
    size_t used;
    size_t size;
};

struct Record {
    uint8_t* target = nullptr;
    uint8_t* trampoline = nullptr;
    uint8_t* relay = nullptr;
    void* detour = nullptr;
    uint8_t saved[32] = {};
    uint8_t patch[32] = {};
    size_t stolen = 0;
    bool installed = false;
};

std::mutex g_lock;
std::vector<Block> g_blocks;
std::vector<Record> g_records;

bool Near(const void* a, const void* b) {
    const intptr_t d = reinterpret_cast<intptr_t>(a) - reinterpret_cast<intptr_t>(b);
    return d > -kReach && d < kReach;
}

void Fail(std::string* why, const char* fmt, ...) {
    if (!why) return;
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    *why = buf;
}

// Each trampoline owns a separate page: writing a new hook must never change the
// protection of a page another thread can already execute. Reserve nearby address
// space, commit only the page being built as RW, then seal it RX before publishing.
uint8_t* AllocNear(const uint8_t* target) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const size_t page = si.dwPageSize;
    for (Block& b : g_blocks) {
        if (b.used + page <= b.size && Near(b.base, target) && Near(b.base + b.size, target)) {
            uint8_t* p = b.base + b.used;
            if (!VirtualAlloc(p, page, MEM_COMMIT, PAGE_READWRITE)) continue;
            b.used += page;
            return p;
        }
    }
    const uintptr_t gran = si.dwAllocationGranularity;
    const uintptr_t lo = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const uintptr_t hi = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
    const uintptr_t origin = reinterpret_cast<uintptr_t>(target) & ~(gran - 1);
    for (uintptr_t delta = gran; delta < static_cast<uintptr_t>(kReach) - gran; delta += gran) {
        for (int dir = 0; dir < 2; ++dir) {
            const uintptr_t addr = dir ? origin + delta : origin - delta;
            if ((dir == 0 && origin < delta + lo) || addr < lo || addr + gran > hi) continue;
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof mbi) || mbi.State != MEM_FREE) continue;
            void* p = VirtualAlloc(reinterpret_cast<void*>(addr), gran, MEM_RESERVE, PAGE_NOACCESS);
            if (!p) continue;
            if (!VirtualAlloc(p, page, MEM_COMMIT, PAGE_READWRITE)) {
                VirtualFree(p, 0, MEM_RELEASE);
                continue;
            }
            g_blocks.push_back({static_cast<uint8_t*>(p), page, gran});
            return static_cast<uint8_t*>(p);
        }
    }
    return nullptr;
}

// Only unpublished pages can be reclaimed. Published trampolines remain callable
// for the process lifetime, including after a hook is removed.
void DiscardPage(uint8_t* page) {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    for (auto it = g_blocks.begin(); it != g_blocks.end(); ++it) {
        if (page >= it->base && page < it->base + it->size) {
            if (VirtualFree(page, si.dwPageSize, MEM_DECOMMIT) &&
                page + si.dwPageSize == it->base + it->used) {
                it->used -= si.dwPageSize;
                if (it->used == 0 && VirtualFree(it->base, 0, MEM_RELEASE)) g_blocks.erase(it);
            }
            return;
        }
    }
}

bool SealCode(uint8_t* page, std::string* why) {
    DWORD old = 0;
    if (!VirtualProtect(page, kTrampolineMax + 14, PAGE_EXECUTE_READ, &old)) {
        Fail(why, "cannot make the trampoline read/execute (error %lu)", GetLastError());
        return false;
    }
    if (!FlushInstructionCache(GetCurrentProcess(), page, kTrampolineMax + 14)) {
        Fail(why, "cannot flush the trampoline instruction cache (error %lu)", GetLastError());
        return false;
    }
    return true;
}

void EmitAbsJmp(uint8_t*& out, const void* dest) {  // FF 25 00000000 <abs64>
    *out++ = 0xFF;
    *out++ = 0x25;
    std::memset(out, 0, 4);
    out += 4;
    std::memcpy(out, &dest, 8);
    out += 8;
}

bool Rel32(const uint8_t* next, const uint8_t* dest, int32_t* rel) {
    const intptr_t d = dest - next;
    if (d < INT32_MIN || d > INT32_MAX) return false;
    *rel = static_cast<int32_t>(d);
    return true;
}

// Copies the instructions that cover the first kPatch bytes of `target` into `tramp`,
// moving every relative reference, and ends it with a jump back.
bool BuildTrampoline(uint8_t* target, uint8_t* tramp, size_t* stolenOut, std::string* why) {
    // Pass 1: how many bytes the patch takes over (needed to spot jumps into them).
    size_t stolen = 0;
    while (stolen < kPatch) {
        x64::Insn in;
        if (!x64::Decode(target + stolen, &in)) {
            Fail(why, "cannot decode the instruction at +%zu (first byte 0x%02X)", stolen, target[stolen]);
            return false;
        }
        if (in.kind == x64::Kind::Ret) {
            Fail(why, "the function returns at +%zu, before the %zu bytes a jump needs", stolen, kPatch);
            return false;
        }
        if ((in.kind == x64::Kind::JmpRel8 || in.kind == x64::Kind::JmpRel32) && stolen + in.len < kPatch) {
            Fail(why, "an unconditional jump at +%zu ends the code before %zu bytes", stolen, kPatch);
            return false;
        }
        stolen += in.len;
    }
    if (stolen > sizeof(Record::saved)) {
        Fail(why, "%zu bytes to move is more than the engine keeps", stolen);
        return false;
    }

    // Pass 2: rewrite into the trampoline.
    uint8_t* out = tramp;
    for (size_t at = 0; at < stolen;) {
        x64::Insn in;
        x64::Decode(target + at, &in);
        const uint8_t* src = target + at;
        const uint8_t* srcNext = src + in.len;
        const uint8_t* dest = srcNext + in.rel;
        const bool branch = in.kind == x64::Kind::JmpRel8 || in.kind == x64::Kind::JmpRel32 || in.kind == x64::Kind::JccRel8 ||
                            in.kind == x64::Kind::JccRel32 || in.kind == x64::Kind::CallRel32;
        if (branch && in.kind != x64::Kind::CallRel32 && dest >= target && dest < target + stolen) {
            Fail(why, "a jump at +%zu lands inside the bytes the patch replaces", at);
            return false;
        }
        int32_t rel = 0;
        switch (in.kind) {
            case x64::Kind::Plain: {
                std::memcpy(out, src, in.len);
                if (in.ripRelative) {
                    int32_t disp;
                    std::memcpy(&disp, src + in.dispOffset, 4);
                    const uint8_t* abs = srcNext + disp;
                    if (!Rel32(out + in.len, abs, &rel)) {
                        Fail(why, "a RIP-relative operand at +%zu is out of reach from the trampoline", at);
                        return false;
                    }
                    std::memcpy(out + in.dispOffset, &rel, 4);
                }
                out += in.len;
                break;
            }
            case x64::Kind::JmpRel8:
            case x64::Kind::JmpRel32:
                if (Rel32(out + 5, dest, &rel)) {
                    *out++ = 0xE9;
                    std::memcpy(out, &rel, 4);
                    out += 4;
                } else {
                    EmitAbsJmp(out, dest);
                }
                break;
            case x64::Kind::JccRel8:
            case x64::Kind::JccRel32:
                if (Rel32(out + 6, dest, &rel)) {
                    *out++ = 0x0F;
                    *out++ = static_cast<uint8_t>(0x80 | in.condition);
                    std::memcpy(out, &rel, 4);
                    out += 4;
                } else {  // inverted short jcc over an absolute jump
                    *out++ = static_cast<uint8_t>(0x70 | (in.condition ^ 1));
                    *out++ = 14;
                    EmitAbsJmp(out, dest);
                }
                break;
            case x64::Kind::CallRel32:
                if (Rel32(out + 5, dest, &rel)) {
                    *out++ = 0xE8;
                    std::memcpy(out, &rel, 4);
                    out += 4;
                } else {  // call [rip+2]; jmp +8; dq dest
                    const uint8_t seq[] = {0xFF, 0x15, 0x02, 0x00, 0x00, 0x00, 0xEB, 0x08};
                    std::memcpy(out, seq, sizeof seq);
                    out += sizeof seq;
                    std::memcpy(out, &dest, 8);
                    out += 8;
                }
                break;
            default:
                Fail(why, "unexpected instruction kind at +%zu", at);
                return false;
        }
        at += in.len;
        if (static_cast<size_t>(out - tramp) > kTrampolineMax - 14) {
            Fail(why, "the trampoline would not fit");
            return false;
        }
    }
    int32_t back = 0;
    if (Rel32(out + 5, target + stolen, &back)) {
        *out++ = 0xE9;
        std::memcpy(out, &back, 4);
        out += 4;
    } else {
        EmitAbsJmp(out, target + stolen);
    }
    *stolenOut = stolen;
    return true;
}

// Suspends every other thread of the process. Returns false (with all threads running
// again) when one of them stands inside [lo, hi). Nothing here allocates once the first
// thread is suspended: it might hold the heap lock.
bool ThreadIds(std::vector<DWORD>& ids, DWORD* error) {
    ids.clear();
    ids.reserve(256);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) { *error = GetLastError(); return false; }
    THREADENTRY32 te{sizeof te};
    const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
    SetLastError(ERROR_SUCCESS);
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
        if (te.th32OwnerProcessID == pid && te.th32ThreadID != self) ids.push_back(te.th32ThreadID);
    *error = GetLastError();
    CloseHandle(snap);
    if (*error != ERROR_NO_MORE_FILES) return false;
    std::sort(ids.begin(), ids.end());
    *error = ERROR_SUCCESS;
    return true;
}

bool Freeze(std::vector<HANDLE>& held, std::vector<DWORD>& ids, std::vector<DWORD>& second,
            const uint8_t* lo, const uint8_t* hi, DWORD* error) {
    // Snapshot allocation happens before suspension: a stopped thread may hold
    // the process heap lock. Stable preflight detects churn, but cannot prevent
    // a new thread being created after the second snapshot.
    if (!ThreadIds(ids, error) || !ThreadIds(second, error)) return false;
    if (ids != second) { *error = ERROR_RETRY; return false; }
    held.clear();
    held.reserve(ids.size());
    bool inside = false;
    for (DWORD id : ids) {
        HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
        if (!t) { *error = GetLastError(); inside = true; break; }
        if (SuspendThread(t) == static_cast<DWORD>(-1)) {
            *error = GetLastError(); CloseHandle(t);
            inside = true; break;
        }
        held.push_back(t);
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(t, &ctx)) {
            const auto rip = reinterpret_cast<const uint8_t*>(ctx.Rip);
            if (rip >= lo && rip < hi) { *error = ERROR_RETRY; inside = true; }
        } else { *error = GetLastError(); inside = true; break; }
    }
    if (!inside) return true;
    for (HANDLE t : held) {
        ResumeThread(t);
        CloseHandle(t);
    }
    held.clear();
    return false;
}

void Thaw(std::vector<HANDLE>& held) {
    for (HANDLE t : held) {
        ResumeThread(t);
        CloseHandle(t);
    }
    held.clear();
}

// Writes `bytes` over `at` with the other threads held; retried while one of them is inside.
bool RestoreProtection(uint8_t* at, size_t n, DWORD protection) {
    DWORD ignored = 0;
    // Preserve the exact original flags. Do not silently replace them with RX.
    for (int i = 0; i < 3; ++i)
        if (VirtualProtect(at, n, protection, &ignored)) return true;
    return false;
}

bool WriteCode(uint8_t* at, const uint8_t* expected, const uint8_t* bytes, size_t n, std::string* why) {
    std::vector<HANDLE> held;
    // Keep these buffers alive until after Thaw: freeing a local vector in
    // Freeze's success return can deadlock on a suspended thread's heap lock.
    std::vector<DWORD> ids, second;
    DWORD freezeError = ERROR_SUCCESS;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (Freeze(held, ids, second, at, at + n, &freezeError)) {
            // A different patcher may have changed the target since preflight.
            if (std::memcmp(at, expected, n) != 0) {
                Thaw(held);
                Fail(why, "the target changed; refusing to overwrite another patch");
                return false;
            }
            DWORD old = 0;
            bool ok = VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old) != 0;
            DWORD error = ok ? ERROR_SUCCESS : GetLastError();
            bool rollbackFlush = true, protectionRestored = true;
            if (ok) {
                std::memcpy(at, bytes, n);
                ok = FlushInstructionCache(GetCurrentProcess(), at, n) != 0;
                if (!ok) error = GetLastError();
                if (ok) {
                    protectionRestored = RestoreProtection(at, n, old);
                    if (!protectionRestored) { error = GetLastError(); ok = false; }
                }
                if (!ok) {
                    // No thread is running the patch yet. Undo it before resuming.
                    std::memcpy(at, expected, n);
                    rollbackFlush = FlushInstructionCache(GetCurrentProcess(), at, n) != 0;
                    protectionRestored = RestoreProtection(at, n, old);
                }
            }
            Thaw(held);
            if (!ok) Fail(why, "code patch failed (error %lu); rollback cache=%s, protection=%s", error,
                          rollbackFlush ? "ok" : "FAILED", protectionRestored ? "ok" : "FAILED");
            return ok;
        }
        // Permission/context failures cannot be assumed safe or spun away.
        if (freezeError != ERROR_RETRY && freezeError != ERROR_INVALID_PARAMETER) {
            Fail(why, "cannot safely suspend/inspect process threads (error %lu)", freezeError);
            return false;
        }
        Sleep(1);
    }
    Fail(why, "thread preflight did not become safe after 100 attempts (error %lu)", freezeError);
    return false;
}

Record* Find(void* target) {
    for (Record& r : g_records)
        if (r.target == target) return &r;
    return nullptr;
}

}  // namespace

bool Install(void* targetPtr, void* detour, void** original, std::string* why) {
    std::lock_guard<std::mutex> hold(g_lock);
    auto* target = static_cast<uint8_t*>(targetPtr);
    if (!target || !detour) {
        Fail(why, "null target or detour");
        return false;
    }
    Record* rec = Find(target);
    if (rec && rec->installed) {
        Fail(why, "already installed");
        return false;
    }
    if (rec && std::memcmp(target, rec->saved, rec->stolen) != 0) {
        Fail(why, "the target changed after this hook was removed");
        return false;
    }
    if (!rec || rec->detour != detour) {
        uint8_t* slot = AllocNear(target);
        if (!slot) {
            Fail(why, "no free memory within 2 GB of the target");
            return false;
        }
        Record fresh;
        fresh.target = target;
        fresh.trampoline = slot;
        fresh.relay = slot + kTrampolineMax;
        fresh.detour = detour;
        if (!BuildTrampoline(target, fresh.trampoline, &fresh.stolen, why)) {
            DiscardPage(slot);
            return false;
        }
        std::memcpy(fresh.saved, target, fresh.stolen);
        uint8_t* relay = fresh.relay;
        EmitAbsJmp(relay, detour);
        if (!SealCode(slot, why)) {
            DiscardPage(slot);
            return false;
        }
        // Retain old RX pages: a caller may still be returning through an earlier
        // trampoline. A re-install with the same detour can reuse its immutable page.
        if (rec) *rec = fresh;
        else {
            g_records.push_back(fresh);
            rec = &g_records.back();
        }
    }

    uint8_t patch[sizeof(Record::saved)];
    std::memset(patch, 0xCC, rec->stolen);  // a jump into the middle of the old bytes should crash loudly
    int32_t rel = 0;
    if (!Rel32(target + kPatch, rec->relay, &rel)) {
        Fail(why, "the relay is out of reach");
        return false;
    }
    patch[0] = 0xE9;
    std::memcpy(patch + 1, &rel, 4);
    if (original) *original = rec->trampoline;
    if (!WriteCode(target, rec->saved, patch, rec->stolen, why)) return false;
    std::memcpy(rec->patch, patch, rec->stolen);
    rec->installed = true;
    return true;
}

bool Remove(void* target, std::string* why) {
    std::lock_guard<std::mutex> hold(g_lock);
    Record* rec = Find(target);
    if (!rec || !rec->installed) {
        Fail(why, "not installed");
        return false;
    }
    if (!WriteCode(rec->target, rec->patch, rec->saved, rec->stolen, why)) return false;
    rec->installed = false;
    return true;
}

bool IsInstalled(void* target) {
    std::lock_guard<std::mutex> hold(g_lock);
    const Record* rec = Find(target);
    return rec && rec->installed;
}

bool RemoveAll(std::string* why) {
    std::lock_guard<std::mutex> hold(g_lock);
    bool ok = true;
    std::string detail;
    for (Record& rec : g_records) {
        if (!rec.installed) continue;
        if (WriteCode(rec.target, rec.patch, rec.saved, rec.stolen, &detail)) rec.installed = false;
        else { ok = false; Fail(why, "hook at %p could not be restored: %s", rec.target, detail.c_str()); }
    }
    return ok;
}

}  // namespace ep::hook
