// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Parallel work through the host's task hooks, or serially when it gave
// none. Every range runs in the library's floating-point mode, on
// whichever thread the host chose.

#ifndef MAUL_VFX_SRC_TASKS_H
#define MAUL_VFX_SRC_TASKS_H

#include "maul-vfx/context.h"

// A context's task system: the host's hooks, or none.
typedef struct mvfxTasks
{
    mvfxEnqueueTaskFunction* enqueueTask;
    mvfxFinishTaskFunction* finishTask;
    void* userContext;
} mvfxTasks;

// Runs task over [0, itemCount) in ranges of at least minRange items and
// returns when every range has run. The ranges may run on several threads
// at once: task must write only what its own items own. NULL tasks run
// it on this thread.
void mvfxRunTasks(const mvfxTasks* tasks, mvfxTaskFunction* task, int32_t itemCount,
                  int32_t minRange, void* taskContext);

#endif
