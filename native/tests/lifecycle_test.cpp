// Real shutdown detour and frame queue with a synthetic IL2CPP shutdown target.
#include "../core/lifecycle.hpp"
#include "../core/mainthread.hpp"
#include "../core/game.hpp"
#include "../core/common.hpp"
#include "../core/hook.hpp"
#include <atomic>
#include <future>
#include <iostream>

std::atomic<bool> exited{false}, originalCalled{false}, ordered{false}, restored{false};
void Shutdown();
void Update(void*, const ep::il2cpp::Method*);
#pragma optimize("", off)
__declspec(noinline) void Shutdown() {
    originalCalled = true; ordered = exited.load();
    restored = !ep::hook::IsInstalled(reinterpret_cast<void*>(&Shutdown)) &&
               !ep::hook::IsInstalled(reinterpret_cast<void*>(&Update));
}
__declspec(noinline) void Update(void*, const ep::il2cpp::Method*) {}
#pragma optimize("", on)
namespace ep {
void Log(const char*, ...) {}
namespace il2cpp { const Api& api() { static Api value{}; return value; } }
namespace game {
MethodRef FindMethod(const char*, const char*, const char* cls, const char*, int) {
    return std::string(cls) == "EventSystem" ? MethodRef{nullptr, reinterpret_cast<void*>(&Update)} : MethodRef{};
}
}
}
int main() {
    unsigned tests = 0, failed = 0;
    auto check = [&](bool ok, const char* label) { ++tests; if (!ok) { ++failed; std::cerr << "FAIL " << label << '\n'; } };
    std::promise<bool> initialized;
    int mutations = 0;
    auto worker = std::async(std::launch::async, [&] {
        const bool ok = ep::lifecycle::Init(reinterpret_cast<void*>(&Shutdown)) && ep::mainthread::Init();
        initialized.set_value(ok);
        if (ok) ep::mainthread::Run([&] { ++mutations; }, 5000, nullptr);
        exited = true; ep::lifecycle::WorkerFinished();
    });
    check(initialized.get_future().get(), "shutdown detour initialized on worker");
    const auto deadline = GetTickCount64() + 1000;
    while (ep::mainthread::Telemetry().find("\"pendingJobs\":1") == std::string::npos && GetTickCount64() < deadline) Sleep(1);
    check(ep::mainthread::Telemetry().find("\"pendingJobs\":1") != std::string::npos, "worker has a queued mutation before shutdown");
    Shutdown(); worker.get();
    check(ep::lifecycle::Stopping(), "shutdown stop signal published");
    check(originalCalled && ordered, "original runtime shutdown waits for worker exit");
    check(restored, "shutdown and frame hooks restored before original runtime destruction");
    check(mutations == 0, "pending mutation cancelled before runtime destruction");
    check(!ep::mainthread::Run([] {}, 5000, nullptr), "no job accepted after runtime shutdown");
    std::cout << "lifecycle: " << tests - failed << '/' << tests << " passed\n";
    return failed ? 1 : 0;
}
