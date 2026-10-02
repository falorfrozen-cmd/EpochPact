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
    std::function<void()> fn;
    std::shared_ptr<std::atomic<int>> state;  // 0 queued, 1 running, 2 done
};

game::MethodRef g_update;  // UnityEngine.EventSystems.EventSystem.Update
UpdateFn g_orig = nullptr;
std::mutex g_queueLock;    // the queue
std::mutex g_hookLock;     // installing and removing the frame hook
std::deque<Job> g_jobs;
std::atomic<DWORD> g_lastWork{0};
std::atomic<bool> g_installed{false};

void Detour(void* self, const il2cpp::Method* method) {
    for (;;) {
        Job job;
        {
            std::lock_guard<std::mutex> hold(g_queueLock);
            if (g_jobs.empty()) break;
            job = std::move(g_jobs.front());
            g_jobs.pop_front();
        }
        job.state->store(1);
        std::string why;
        if (!game::Guarded([&] { job.fn(); }, &why)) Log("main thread: a job failed: %s", why.c_str());
        job.state->store(2);
        g_lastWork = GetTickCount();
    }
    g_orig(self, method);
}

bool EnsureHook(std::string* why) {
    std::lock_guard<std::mutex> hold(g_hookLock);
    if (g_installed) return true;
    if (!hook::Install(g_update.code, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&g_orig), why)) return false;
    g_installed = true;
    return true;
}

}  // namespace

bool Init() {
    g_update = game::FindMethod("UnityEngine.UI.dll", "UnityEngine.EventSystems", "EventSystem", "Update", 0);
    Log("main thread: EventSystem.Update %s", g_update ? "found" : "MISSING");
    return static_cast<bool>(g_update);
}

bool Run(std::function<void()> job, unsigned timeoutMs, std::string* why) {
    if (!g_update) {
        if (why) *why = "EventSystem.Update was not found";
        return false;
    }
    auto state = std::make_shared<std::atomic<int>>(0);
    {
        std::lock_guard<std::mutex> hold(g_queueLock);
        g_jobs.push_back({std::move(job), state});
    }
    g_lastWork = GetTickCount();
    std::string hookWhy;
    if (!EnsureHook(&hookWhy)) {
        std::lock_guard<std::mutex> hold(g_queueLock);
        g_jobs.clear();
        if (why) *why = "frame hook refused: " + hookWhy;
        return false;
    }
    const DWORD start = GetTickCount();
    while (state->load() != 2) {
        if (state->load() == 0 && GetTickCount() - start > timeoutMs) {
            std::lock_guard<std::mutex> hold(g_queueLock);
            for (auto it = g_jobs.begin(); it != g_jobs.end(); ++it) {
                if (it->state == state) {
                    g_jobs.erase(it);
                    if (why) *why = "the main thread did not pick the job up (is the game paused or minimised?)";
                    return false;
                }
            }
        }
        Sleep(5);  // a job that started is waited for: it may refer to the caller's stack
    }
    return true;
}

void Housekeep() {
    std::lock_guard<std::mutex> hold(g_hookLock);
    if (!g_installed) return;
    {
        std::lock_guard<std::mutex> q(g_queueLock);
        if (!g_jobs.empty()) return;
    }
    if (GetTickCount() - g_lastWork < 2000) return;
    std::string why;
    if (hook::Remove(g_update.code, &why)) g_installed = false;
    else Log("main thread: frame hook removal failed: %s", why.c_str());
}

bool HookInstalled() { return g_installed; }

}  // namespace ep::mainthread
