// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The task hooks, with each range wrapped in the library's floating-point
// mode.

#include "tasks.h"

#include "float_mode.h"

// What a wrapped range needs: the library's task and its context.
typedef struct WrappedTask
{
    mvfxTaskFn* task;
    void* taskContext;
} WrappedTask;

static void RunInFloatMode(int32_t startIndex, int32_t endIndex, void* taskContext)
{
    const WrappedTask* wrapped = taskContext;
    mvfxFloatMode mode = mvfxEnterFloatMode();
    wrapped->task(startIndex, endIndex, wrapped->taskContext);
    mvfxLeaveFloatMode(mode);
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
        RunInFloatMode(0, itemCount, &wrapped);
        return;
    }
    void* handle = tasks->enqueueTask(RunInFloatMode, itemCount, minRange < 1 ? 1 : minRange,
                                      &wrapped, tasks->userContext);
    if (handle != nullptr)
    {
        tasks->finishTask(handle, tasks->userContext);
    }
}
