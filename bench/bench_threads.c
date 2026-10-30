// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The library's scaling over a host's threads: 64 instances of the typical
// sprite emitter, about a million particles, stepped at 60 steps a second
// with no task system (one thread), then through a host pool of 1, 2, 4
// and 8 threads (the calling thread one of them) that splits each step's
// instances into ranges; then the same with one octave of turbulence,
// whose work is arithmetic rather than memory. Each prints the best of
// three runs of 30 steps after the emitters have filled, and its speed
// over one thread; the target is 8 threads at least 6 times one.

#include "bench_sprite.h"

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

enum
{
    RUNS = 3,
    STEPS = 30,
    // Steps before timing: the sprite's particles live 1.8 to 2.2 s.
    WARM = 150,
    INSTANCES = 64,
    MOST_THREADS = 8
};

static const int64_t s_step = MVFX_TICKS_PER_SECOND / 60;

static double Now(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + 1e-9 * (double)now.tv_nsec;
}

// A host pool: workers that wait for a job, then take its ranges in turn
// with the calling thread until none is left.
typedef struct Pool
{
    pthread_t workers[MOST_THREADS];
    int32_t workerCount;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    pthread_cond_t done;
    // The job: its task, items and range, the next range's start, the
    // workers still in it, and its number (0 before the first).
    mvfxTaskFunction* task;
    void* taskContext;
    int32_t itemCount;
    int32_t range;
    atomic_int next;
    int32_t busy;
    uint64_t job;
    bool quit;
} Pool;

static void TakeRanges(Pool* pool)
{
    for (;;)
    {
        int32_t start = atomic_fetch_add(&pool->next, pool->range);
        if (start >= pool->itemCount)
        {
            return;
        }
        int32_t end = start + pool->range < pool->itemCount ? start + pool->range : pool->itemCount;
        pool->task(start, end, pool->taskContext);
    }
}

static void* Work(void* argument)
{
    Pool* pool = argument;
    uint64_t seen = 0;
    pthread_mutex_lock(&pool->lock);
    for (;;)
    {
        while (!pool->quit && pool->job == seen)
        {
            pthread_cond_wait(&pool->wake, &pool->lock);
        }
        if (pool->quit)
        {
            pthread_mutex_unlock(&pool->lock);
            return NULL;
        }
        seen = pool->job;
        pthread_mutex_unlock(&pool->lock);
        TakeRanges(pool);
        pthread_mutex_lock(&pool->lock);
        pool->busy -= 1;
        if (pool->busy == 0)
        {
            pthread_cond_signal(&pool->done);
        }
    }
}

static void* Enqueue(mvfxTaskFunction* task, int32_t itemCount, int32_t minRange, void* taskContext,
                     void* userContext)
{
    Pool* pool = userContext;
    // About four ranges a thread, none below the library's least.
    int32_t threads = pool->workerCount + 1;
    int32_t range = (itemCount + 4 * threads - 1) / (4 * threads);
    pthread_mutex_lock(&pool->lock);
    pool->task = task;
    pool->taskContext = taskContext;
    pool->itemCount = itemCount;
    pool->range = range > minRange ? range : minRange;
    atomic_store(&pool->next, 0);
    pool->busy = pool->workerCount;
    pool->job += 1;
    pthread_cond_broadcast(&pool->wake);
    pthread_mutex_unlock(&pool->lock);
    TakeRanges(pool);
    return pool;
}

static void Finish(void* userTask, void* userContext)
{
    (void)userContext;
    Pool* pool = userTask;
    pthread_mutex_lock(&pool->lock);
    while (pool->busy > 0)
    {
        pthread_cond_wait(&pool->done, &pool->lock);
    }
    pthread_mutex_unlock(&pool->lock);
}

static bool OpenPool(Pool* pool, int32_t threads)
{
    *pool = (Pool){.workerCount = threads - 1};
    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->wake, NULL);
    pthread_cond_init(&pool->done, NULL);
    for (int32_t t = 0; t < pool->workerCount; ++t)
    {
        if (pthread_create(&pool->workers[t], NULL, Work, pool) != 0)
        {
            pool->workerCount = t;
            return false;
        }
    }
    return true;
}

static void ClosePool(Pool* pool)
{
    pthread_mutex_lock(&pool->lock);
    pool->quit = true;
    pthread_cond_broadcast(&pool->wake);
    pthread_mutex_unlock(&pool->lock);
    for (int32_t t = 0; t < pool->workerCount; ++t)
    {
        pthread_join(pool->workers[t], NULL);
    }
    pthread_cond_destroy(&pool->done);
    pthread_cond_destroy(&pool->wake);
    pthread_mutex_destroy(&pool->lock);
}

// A step's best time over the runs for a context with the pool (NULL for
// none), with turbulence or not, and the particles alive at the end.
static double TimeStep(Pool* pool, bool turbulent, int64_t* live)
{
    double best = -1.0;
    for (int32_t r = 0; r < RUNS; ++r)
    {
        mvfxContextDef def = mvfxDefaultContextDef();
        def.limits.instances = INSTANCES;
        if (pool != NULL)
        {
            def.enqueueTask = Enqueue;
            def.finishTask = Finish;
            def.userTaskContext = pool;
        }
        // About 15,600 particles an instance.
        mvfxEmitterDef sprite = Sprite(17000, 7800.0f);
        static const mvfxForce turbulence = {
            .kind = mvfx_forceTurbulence,
            .strength = {.kind = mvfx_sourceRange, .range = {2.0f, 2.0f}},
            .noise = {.frequency = 1.0f, .octaves = 1, .lacunarity = 2.0f, .gain = 0.5f}};
        sprite.forces = turbulent ? &turbulence : NULL;
        sprite.forceCount = turbulent ? 1 : 0;
        mvfxEffectDef effectDef = mvfxDefaultEffectDef();
        effectDef.emitters = &sprite;
        effectDef.emitterCount = 1;
        mvfxContext* context = NULL;
        mvfxEffectId effect = {0};
        mvfxInstanceId instances[INSTANCES];
        bool opened = mvfxCreateContext(&def, &context) == mvfx_success &&
                      mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success;
        for (int32_t i = 0; opened && i < INSTANCES; ++i)
        {
            mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
            instanceDef.effect = effect;
            instanceDef.seed = (uint32_t)i + 1u;
            opened = mvfxCreateInstance(context, &instanceDef, &instances[i]) == mvfx_success;
        }
        for (int32_t s = 0; opened && s < WARM; ++s)
        {
            opened = mvfxContext_StepTicks(context, s_step) == mvfx_success;
        }
        double start = Now();
        for (int32_t s = 0; opened && s < STEPS; ++s)
        {
            opened = mvfxContext_StepTicks(context, s_step) == mvfx_success;
        }
        double seconds = (Now() - start) / STEPS;
        *live = 0;
        for (int32_t i = 0; opened && i < INSTANCES; ++i)
        {
            mvfxEmitterStats stats = {0};
            opened = mvfxInstance_GetEmitterStats(context, instances[i], 0, &stats) == mvfx_success;
            *live += stats.live;
        }
        mvfxDestroyContext(context);
        if (!opened)
        {
            return -1.0;
        }
        best = best < 0.0 || seconds < best ? seconds : best;
    }
    return best;
}

// One emitter's scaling: one thread, then the pool's counts.
static int Scale(bool turbulent)
{
    int64_t live = 0;
    double one = TimeStep(NULL, turbulent, &live);
    if (one < 0.0 || live == 0)
    {
        return 1;
    }
    printf("# %d instances, %lld particles alive%s\n", INSTANCES, (long long)live,
           turbulent ? ", one octave of turbulence" : "");
    printf("%-26s %10.2f ms a step\n", "one thread, no tasks", one * 1e3);
    static const int32_t counts[4] = {1, 2, 4, MOST_THREADS};
    int failed = 0;
    for (int32_t c = 0; c < 4; ++c)
    {
        Pool pool;
        bool opened = OpenPool(&pool, counts[c]);
        double seconds = opened ? TimeStep(&pool, turbulent, &live) : -1.0;
        ClosePool(&pool);
        if (seconds < 0.0)
        {
            failed = 1;
            continue;
        }
        char label[32];
        snprintf(label, sizeof label, "%d thread%s through tasks", counts[c],
                 counts[c] == 1 ? "" : "s");
        printf("%-26s %10.2f ms a step %8.2f times one%s\n", label, seconds * 1e3, one / seconds,
               counts[c] == MOST_THREADS ? " (target 6)" : "");
    }
    return failed;
}

int main(void)
{
    int failed = Scale(false);
    failed |= Scale(true);
    if (failed)
    {
        printf("a case failed\n");
    }
    return failed;
}
