// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The task hooks, with each range run in the library's floating-point
// environment, and the hooks themselves, host code that computes nothing
// of the library's, in the host's own (src/fpenv.h).

#include "tasks.h"

#include "fpenv.h"

// What a wrapped range needs: the library's task and its context.
typedef struct WrappedTask
{
    mvfxTaskFn* task;
    void* taskContext;
} WrappedTask;

static void RunInFloatEnv(int32_t startIndex, int32_t endIndex, void* taskContext)
{
    const WrappedTask* wrapped = taskContext;
    uint64_t env = mvfxFloatEnvEnter();
    wrapped->task(startIndex, endIndex, wrapped->taskContext);
    mvfxFloatEnvLeave(env);
}

// The host's hooks, each the one call between the switches.
static MVFX_NOINLINE void* Enqueue(const mvfxTasks* tasks, WrappedTask* wrapped, int32_t itemCount,
                                   int32_t minRange)
{
    mvfxFloatHost host = mvfxFloatHostEnter();
    void* handle =
        tasks->enqueueTask(RunInFloatEnv, itemCount, minRange, wrapped, tasks->userContext);
    mvfxFloatHostLeave(host);
    return handle;
}

static MVFX_NOINLINE void Finish(const mvfxTasks* tasks, void* handle)
{
    mvfxFloatHost host = mvfxFloatHostEnter();
    tasks->finishTask(handle, tasks->userContext);
    mvfxFloatHostLeave(host);
}

void mvfxRunTasks(const mvfxTasks* tasks, mvfxTaskFn* task, int32_t itemCount, int32_t minRange,
                  void* taskContext)
{
    if (itemCount <= 0)
    {
        return;
    }
    WrappedTask wrapped = {task, taskContext};
    if (tasks == nullptr || tasks->enqueueTask == nullptr || itemCount <= minRange)
    {
        RunInFloatEnv(0, itemCount, &wrapped);
        return;
    }
    void* handle = Enqueue(tasks, &wrapped, itemCount, minRange < 1 ? 1 : minRange);
    if (handle != nullptr)
    {
        Finish(tasks, handle);
    }
}
