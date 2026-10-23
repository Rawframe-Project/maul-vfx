// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Worker twins: the scene stepped serially, and through a host task system
// that cuts every step into ranges of random sizes and runs them in a
// shuffled order on four threads, must hold the same state after every
// step (docs/internals/determinism.md).
// ThreadSanitizer runs it in CI.

#include "vfx_scene.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

#define WORKERS    4
#define MAX_RANGES 4096

// One step's work as the host cuts it.
typedef struct HostWork
{
    mvfxTaskFn* task;
    void* taskContext;
    int32_t starts[MAX_RANGES];
    int32_t ends[MAX_RANGES];
    int32_t rangeCount;
    atomic_int next;
    pthread_t threads[WORKERS];
} HostWork;

// The host: its random state, used only on the stepping thread.
typedef struct Host
{
    uint32_t random;
    HostWork work;
} Host;

static uint32_t NextRandom(Host* host)
{
    host->random = host->random * 1664525u + 1013904223u;
    return host->random >> 8u;
}

static void* Worker(void* argument)
{
    HostWork* work = argument;
    for (;;)
    {
        int32_t i = atomic_fetch_add(&work->next, 1);
        if (i >= work->rangeCount)
        {
            return NULL;
        }
        work->task(work->starts[i], work->ends[i], work->taskContext);
    }
}

static void* Enqueue(mvfxTaskFn* task, int32_t itemCount, int32_t minRange, void* taskContext,
                     void* userContext)
{
    Host* host = userContext;
    HostWork* work = &host->work;
    work->task = task;
    work->taskContext = taskContext;
    work->rangeCount = 0;
    int32_t start = 0;
    while (start < itemCount && work->rangeCount < MAX_RANGES)
    {
        int32_t length = minRange + (int32_t)(NextRandom(host) % 7u) * minRange;
        int32_t end = start + length < itemCount ? start + length : itemCount;
        work->starts[work->rangeCount] = start;
        work->ends[work->rangeCount] = end;
        work->rangeCount += 1;
        start = end;
    }
    // A shuffle, so ranges run in no particular order.
    for (int32_t i = work->rangeCount - 1; i > 0; --i)
    {
        int32_t j = (int32_t)(NextRandom(host) % (uint32_t)(i + 1));
        int32_t s = work->starts[i];
        int32_t e = work->ends[i];
        work->starts[i] = work->starts[j];
        work->ends[i] = work->ends[j];
        work->starts[j] = s;
        work->ends[j] = e;
    }
    atomic_store(&work->next, 0);
    for (int32_t t = 0; t < WORKERS; ++t)
    {
        CHECK(pthread_create(&work->threads[t], NULL, Worker, work) == 0, "a worker started");
    }
    return work;
}

static void Finish(void* userTask, void* userContext)
{
    (void)userContext;
    HostWork* work = userTask;
    for (int32_t t = 0; t < WORKERS; ++t)
    {
        pthread_join(work->threads[t], NULL);
    }
}

static mvfxContext* NewContext(Host* host, int32_t instances)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    def.limits.instances = instances;
    if (host != NULL)
    {
        def.enqueueTask = Enqueue;
        def.finishTask = Finish;
        def.userTaskContext = host;
    }
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context");
    return context;
}

static void TestTwinsAgree(void)
{
    static Host host = {.random = 12345u};
    // Two copies of the scene give the host enough instances to cut into
    // ranges; more slots than instances, so ranges hold empty slots too.
    mvfxContext* serial = NewContext(NULL, 40);
    mvfxContext* parallel = NewContext(&host, 40);
    for (int32_t copy = 0; copy < 2; ++copy)
    {
        BuildScene(serial);
        BuildScene(parallel);
    }
    for (int32_t step = 0; step < SCENE_STEP_COUNT; ++step)
    {
        MoveScene(serial, step);
        MoveScene(parallel, step);
        CHECK(mvfxContext_StepTicks(serial, s_sceneSteps[step]) == mvfx_success, "serial step");
        CHECK(mvfxContext_StepTicks(parallel, s_sceneSteps[step]) == mvfx_success, "parallel step");
        CHECK(HashScene(serial) == HashScene(parallel), "the twins hold the same state");
    }
    mvfxDestroyContext(serial);
    mvfxDestroyContext(parallel);
}

int main(void)
{
    TestTwinsAgree();
    return s_failures == 0 ? 0 : 1;
}
