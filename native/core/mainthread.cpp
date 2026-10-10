#include "mainthread.hpp"

#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>

namespace ep::mainthread {

namespace {

using UpdateFn = void (*)(void* self, const il2cpp::Method* method);

struct Job {
    std::function<bool()> fn;
    std::shared_ptr<std::atomic<int>> state;  // 0 queued, 1 running, 2 done
    std::shared_ptr<std::atomic<DWORD>> progress;
    LARGE_INTEGER previousStart{};
};

game::MethodRef g_update;  // UnityEngine.EventSystems.EventSystem.Update
UpdateFn g_orig = nullptr;
std::mutex g_queueLock;    // the queue
std::mutex g_hookLock;     // installing and removing the frame hook
std::deque<Job> g_jobs;
std::atomic<unsigned> g_pending{0};
std::atomic<unsigned> g_installs{0};
std::atomic<bool> g_installed{false};
std::atomic<bool> g_stopping{false};
game::MethodRef g_background;
bool g_backgroundSet = false; // main thread only
std::atomic<unsigned long long> g_steps{0}, g_maxMicros{0}, g_slowSteps{0};
std::atomic<unsigned long long> g_gapMax{0}, g_gapTotal{0}, g_gapCount{0};
LARGE_INTEGER g_frequency{};

void Detour(void* self, const il2cpp::Method* method) {
    if (!g_pending.load(std::memory_order_acquire)) { g_orig(self, method); return; }
    // Work queued by a step runs on a later frame, never in the same drain loop.
    {
        Job job;
        bool found = false;
        {
            std::lock_guard<std::mutex> hold(g_queueLock);
            if (!g_jobs.empty()) { job = std::move(g_jobs.front()); g_jobs.pop_front(); --g_pending; found = true; }
        }
        if (!found) { g_orig(self, method); return; }
        job.state->store(1);
        std::string why;
        bool done = true;
        LARGE_INTEGER start{}, finish{}; QueryPerformanceCounter(&start);
        if (job.previousStart.QuadPart) {
            const auto gap = static_cast<unsigned long long>((start.QuadPart - job.previousStart.QuadPart) * 1000000 / g_frequency.QuadPart);
            ++g_gapCount; g_gapTotal += gap;
            auto oldGap = g_gapMax.load(); while (oldGap < gap && !g_gapMax.compare_exchange_weak(oldGap, gap)) {}
        }
        job.previousStart = start;
        const bool succeeded = game::Guarded([&] {
            if (!g_backgroundSet && g_background) {
                bool enabled = true; void* args[]{&enabled}; void* exception = nullptr;
                il2cpp::api().runtime_invoke(g_background.info, nullptr, args, &exception);
                if (!exception) { g_backgroundSet = true; Log("main thread: background updates enabled"); }
            }
            done = job.fn();
        }, &why);
        QueryPerformanceCounter(&finish);
        const auto micros = static_cast<unsigned long long>((finish.QuadPart - start.QuadPart) * 1000000 / g_frequency.QuadPart);
        ++g_steps; if (micros >= 16000) ++g_slowSteps;
        auto previous = g_maxMicros.load(); while (previous < micros && !g_maxMicros.compare_exchange_weak(previous, micros)) {}
        job.progress->store(GetTickCount());
        if (!succeeded) { Log("main thread: a job failed: %s", why.c_str()); job.state->store(3); }
        else if (done) job.state->store(2);
        else { std::lock_guard<std::mutex> hold(g_queueLock); g_jobs.push_back(std::move(job)); ++g_pending; }

    }
    g_orig(self, method);
}

bool EnsureHook(std::string* why) {
    std::lock_guard<std::mutex> hold(g_hookLock);
    if (g_stopping) { if (why) *why = "the game is shutting down"; return false; }
    if (g_installed) return true;
    if (!hook::Install(g_update.code, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&g_orig), why)) return false;
    ++g_installs;
    g_installed = true;
    return true;
}

}  // namespace

bool Init() {
    QueryPerformanceFrequency(&g_frequency);
    g_update = game::FindMethod("UnityEngine.UI.dll", "UnityEngine.EventSystems", "EventSystem", "Update", 0);
    g_background = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Application", "set_runInBackground", 1);
    Log("main thread: EventSystem.Update %s", g_update ? "found" : "MISSING");
    return static_cast<bool>(g_update);
}

bool Run(std::function<void()> job, unsigned timeoutMs, std::string* why) {
    return RunSteps([job = std::move(job)] { job(); return true; }, timeoutMs, why);
}

void KeepTicking() {
    // Bootstrap once Unity begins updating, without delaying DLL readiness while
    // the game is still loading. This job owns no caller stack references.
    auto state = std::make_shared<std::atomic<int>>(0);
    auto progress = std::make_shared<std::atomic<DWORD>>(GetTickCount());
    { std::lock_guard<std::mutex> hold(g_queueLock); if (g_stopping) return; g_jobs.push_back({[] { return true; }, state, progress}); ++g_pending; }

    std::string why;
    if (!EnsureHook(&why)) {
        std::lock_guard<std::mutex> hold(g_queueLock);
        for (const auto& pending : g_jobs) pending.state->store(3);
        g_jobs.clear(); g_pending = 0;
        Log("main thread: background bootstrap refused: %s", why.c_str());
    }
}

bool RunSteps(std::function<bool()> job, unsigned timeoutMs, std::string* why) {
    if (!g_update) {
        if (why) *why = "EventSystem.Update was not found";
        return false;
    }
    auto state = std::make_shared<std::atomic<int>>(0);
    auto progress = std::make_shared<std::atomic<DWORD>>(GetTickCount());
    {
        std::lock_guard<std::mutex> hold(g_queueLock);
        if (g_stopping) { if (why) *why = "the game is shutting down; no job was submitted"; return false; }
        g_jobs.push_back({std::move(job), state, progress}); ++g_pending;
    }

    std::string hookWhy;
    if (!EnsureHook(&hookWhy)) {
        std::lock_guard<std::mutex> hold(g_queueLock);
        for (const auto& pending : g_jobs) pending.state->store(3);
        g_jobs.clear(); g_pending = 0;
        if (why) *why = "frame hook refused: " + hookWhy;
        return false;
    }
    while (state->load() < 2) {
        if (GetTickCount() - progress->load() > timeoutMs) {
            std::lock_guard<std::mutex> hold(g_queueLock);
            for (auto it = g_jobs.begin(); it != g_jobs.end(); ++it) {
                if (it->state == state) {
                    g_jobs.erase(it); --g_pending;
                    if (why) *why = state->load() == 0 ? "the main thread did not pick the job up (is the game paused or minimised?)" :
                        "game updates stopped between steps; the remaining action was cancelled and was not repeated";
                    return false;
                }
            }
        }
        Sleep(5);  // a job that started is waited for: it may refer to the caller's stack
    }
    if (state->load() == 3) { if (why) *why = "main-thread operation failed; see core.log; it was not repeated"; return false; }
    return true;
}

void Housekeep() {
    // The frame hook stays installed; its idle path takes no queue mutex.
}

void Stop() {
    std::lock_guard<std::mutex> hold(g_queueLock);
    g_stopping = true;
    for (const auto& job : g_jobs) job.state->store(3);
    g_jobs.clear(); g_pending = 0;
}

bool HookInstalled() { return g_installed; }
std::string Telemetry(bool reset) {
    const auto steps = reset ? g_steps.exchange(0) : g_steps.load();
    const auto max = reset ? g_maxMicros.exchange(0) : g_maxMicros.load();
    const auto slow = reset ? g_slowSteps.exchange(0) : g_slowSteps.load();
    const auto gap = reset ? g_gapMax.exchange(0) : g_gapMax.load();
    const auto total = reset ? g_gapTotal.exchange(0) : g_gapTotal.load();
    const auto count = reset ? g_gapCount.exchange(0) : g_gapCount.load();
    return "{\"ok\":true,\"hookInstalls\":" + std::to_string(g_installs.load()) +
        ",\"pendingJobs\":" + std::to_string(g_pending.load()) + ",\"steps\":" + std::to_string(steps) + ",\"maxStepMs\":" +
        std::to_string(max / 1000.0) + ",\"stepsOver16ms\":" + std::to_string(slow) +
        ",\"maxFrameGapMs\":" + std::to_string(gap / 1000.0) + ",\"meanFrameGapMs\":" +
        std::to_string(count ? total / (1000.0 * count) : 0.0) + "}";
}

}  // namespace ep::mainthread
