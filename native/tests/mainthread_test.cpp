// Actual mainthread.cpp and hook engine against a synthetic Unity Update.
// No game process, saves, UI or game metadata are accessed.
#include "../core/mainthread.hpp"
#include "../core/game.hpp"
#include "../core/common.hpp"
#include <atomic>
#include <future>
#include <iostream>

std::atomic<unsigned> updates{0};
#pragma optimize("", off)
__declspec(noinline) void Update(void*, const ep::il2cpp::Method*) { updates.fetch_add(1); }
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
    check(ep::mainthread::Init(), "synthetic frame metadata");
    ep::mainthread::KeepTicking(); Update(nullptr, nullptr);
    check(ep::mainthread::HookInstalled(), "bootstrap installs hook");
    auto drain = [&](auto& future) {
        const auto until = GetTickCount64() + 2000;
        while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready && GetTickCount64() < until) {
            Update(nullptr, nullptr); Sleep(1);
        }
    };
    int steps = 0;
    auto work = std::async(std::launch::async, [&] { return ep::mainthread::RunSteps([&] { return ++steps == 3; }, 500, nullptr); });
    drain(work); check(work.get() && steps == 3, "one queued step per frame completes");
    auto cancelled = std::async(std::launch::async, [&] { return ep::mainthread::Run([&] { steps = 99; }, 30, nullptr); });
    check(!cancelled.get(), "unpicked queued job times out");
    Update(nullptr, nullptr); check(steps == 3, "cancelled mutation never executes later");
    Sleep(2100); ep::mainthread::Housekeep();
    check(ep::mainthread::HookInstalled(), "idle housekeeping leaves hook installed");
    auto again = std::async(std::launch::async, [&] { return ep::mainthread::Run([&] { ++steps; }, 500, nullptr); });
    drain(again); check(again.get() && steps == 4, "new job after idle uses existing hook");
    const auto telemetry = ep::mainthread::Telemetry();
    check(telemetry.find("\"hookInstalls\":1") != std::string::npos, "hook installed exactly once");
    check(telemetry.find("\"pendingJobs\":0") != std::string::npos, "completion and cancellation balance pending count");
    const auto before = updates.load();
    for (int i = 0; i < 1000; ++i) Update(nullptr, nullptr);
    check(updates.load() == before + 1000, "idle frames still call original Update");
    std::cout << "mainthread: " << tests - failed << '/' << tests << " passed\n";
    return failed ? 1 : 0;
}
