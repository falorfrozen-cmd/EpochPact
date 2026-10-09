#include "lifecycle.hpp"
#include "hook.hpp"
#include "mainthread.hpp"
#include "common.hpp"
#include <atomic>

namespace ep::lifecycle {
namespace {
using ShutdownFn = void (*)();
ShutdownFn original = nullptr;
std::atomic<bool> stopping{false};
HANDLE finished = nullptr; // Process lifetime: no CRT destructor may close it early.
DWORD workerId = 0;
void Shutdown() {
    Log("runtime shutdown: stopping command worker and cancelling queued frame jobs");
    stopping.store(true, std::memory_order_release);
    mainthread::Stop();
    // Cancelled frame jobs release the IPC worker's stack and let it detach
    // while the domain is still valid. Never tear down IL2CPP under that worker.
    if (finished && GetCurrentThreadId() != workerId) WaitForSingleObject(finished, INFINITE);
    Log("runtime shutdown: worker stopped; continuing IL2CPP shutdown");
    original();
}
}
bool Init(void* target) {
    workerId = GetCurrentThreadId();
    finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!finished || !target) return false;
    std::string why;
    if (!hook::Install(target, reinterpret_cast<void*>(&Shutdown), reinterpret_cast<void**>(&original), &why)) {
        Log("runtime shutdown guard refused: %s", why.c_str());
        return false;
    }
    Log("runtime shutdown guard installed");
    return true;
}
bool Stopping() { return stopping.load(std::memory_order_acquire); }
void WorkerFinished() { if (finished) SetEvent(finished); }
}
