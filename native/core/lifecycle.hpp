#pragma once
#include <windows.h>

namespace ep::lifecycle {
void SetCleanup(void (*cleanup)());
// Install before attaching the worker to IL2CPP. Shutdown waits for its exit.
bool Init(void* runtimeShutdown);
bool Stopping();
void WorkerFinished();
}
