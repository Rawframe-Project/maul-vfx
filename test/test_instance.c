// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating, destroying and reading instances, and the step's refusals;
// the bytes an instance and its effect hold, sized by what the effect has.

#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static mvfxContext* NewContext(int32_t instances)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    def.limits.instances = instances;
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context created");
    return context;
}

static mvfxEffectId NewEffect(mvfxContext* context)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "effect created");
    return effect;
}

static void TestLifecycle(void)
{
    mvfxContext* context = NewContext(4);
    mvfxEffectId effect = NewEffect(context);
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    def.origin[0] = 1e9;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &def, &instance) == mvfx_success, "created");
    CHECK(mvfxInstance_IsValid(context, instance), "valid");
    CHECK(mvfxDestroyEffect(context, effect) == mvfx_errorBusy, "its effect is busy");
    mvfxEmitterStats stats = {.status = 9, .live = 1, .owed = 1, .refused = 1};
    CHECK(mvfxInstance_GetEmitterStats(context, instance, 0, &stats) == mvfx_success, "stats");
    CHECK(stats.live == 0 && stats.owed == 0 && stats.refused == 0, "nothing before a step");
    CHECK(stats.status == mvfx_emitterActive, "active from the start");
    CHECK(mvfxDestroyInstance(context, instance) == mvfx_success, "destroyed");
    CHECK(!mvfxInstance_IsValid(context, instance), "stale after");
    CHECK(mvfxDestroyInstance(context, instance) == mvfx_errorStale, "twice is stale");
    CHECK(mvfxInstance_GetEmitterStats(context, instance, 0, &stats) == mvfx_errorStale,
          "stats of a stale instance");
    CHECK(stats.live == 0 && stats.owed == 0 && stats.status == 0, "zeroed on failure");
    CHECK(mvfxDestroyEffect(context, effect) == mvfx_success, "free once its instance is gone");
    CHECK(mvfxGetContextMisuse(context) == 0, "no misuse so far");
    mvfxDestroyContext(context);
}

static void TestRefusals(void)
{
    mvfxContext* context = NewContext(2);
    mvfxEffectId effect = NewEffect(context);
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    mvfxInstanceId instance = {5, 5};
    uint64_t misuse = 0;
    CHECK(mvfxCreateInstance(context, NULL, &instance) == mvfx_errorInvalid, "no def");
    CHECK(instance.index1 == 0 && mvfxGetContextMisuse(context) == ++misuse, "counted");
    CHECK(mvfxCreateInstance(context, &def, NULL) == mvfx_errorInvalid, "no out-parameter");
    CHECK(mvfxGetContextMisuse(context) == ++misuse, "counted");
    CHECK(mvfxCreateInstance(NULL, &def, &instance) == mvfx_errorInvalid, "no context");
    mvfxInstanceDef bad = def;
    bad.cookie = 0;
    CHECK(mvfxCreateInstance(context, &bad, &instance) == mvfx_errorInvalid, "no cookie");
    CHECK(mvfxGetContextMisuse(context) == ++misuse, "counted");
    static const double origins[] = {(double)NAN, (double)INFINITY, 0x1p53};
    for (int32_t i = 0; i < 3; ++i)
    {
        bad = def;
        bad.origin[i] = origins[i];
        CHECK(mvfxCreateInstance(context, &bad, &instance) == mvfx_errorInvalid, "bad origin");
        CHECK(mvfxGetContextMisuse(context) == ++misuse, "counted");
    }
    bad = def;
    bad.effect = (mvfxEffectId){effect.index1, effect.generation + 1};
    CHECK(mvfxCreateInstance(context, &bad, &instance) == mvfx_errorStale, "a stale effect");
    mvfxInstanceId a = {0};
    mvfxInstanceId b = {0};
    CHECK(mvfxCreateInstance(context, &def, &a) == mvfx_success, "first");
    CHECK(mvfxCreateInstance(context, &def, &b) == mvfx_success, "second");
    CHECK(mvfxCreateInstance(context, &def, &instance) == mvfx_errorLimit, "past the limit");
    mvfxEmitterStats stats;
    CHECK(mvfxInstance_GetEmitterStats(context, a, 1, &stats) == mvfx_errorInvalid, "no emitter 1");
    CHECK(mvfxInstance_GetEmitterStats(context, a, -1, &stats) == mvfx_errorInvalid,
          "no emitter -1");
    CHECK(mvfxInstance_GetEmitterStats(context, a, 0, NULL) == mvfx_errorInvalid, "no out");
    misuse += 3;
    CHECK(mvfxGetContextMisuse(context) == misuse, "each counted");
    CHECK(mvfxGetContextMisuse(context) == misuse && misuse == 9, "nine refusals");

    CHECK(mvfxContext_Step(context, -0.001) == mvfx_errorInvalid, "a negative step");
    CHECK(mvfxContext_Step(context, (double)NAN) == mvfx_errorInvalid, "a NaN step");
    CHECK(mvfxContext_Step(context, MVFX_MAX_STEP_SECONDS * 1.01) == mvfx_errorInvalid,
          "too long a step");
    CHECK(mvfxContext_StepTicks(context, -1) == mvfx_errorInvalid, "negative ticks");
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND + 1) == mvfx_errorInvalid,
          "too many ticks");
    CHECK(mvfxGetContextMisuse(context) == misuse + 5, "each counted");
    CHECK(mvfxContext_Step(NULL, 0.1) == mvfx_errorInvalid, "no context");
    CHECK(mvfxContext_StepTicks(NULL, 1) == mvfx_errorInvalid, "no context, ticks");
    CHECK(mvfxContext_Step(context, 0.0) == mvfx_success, "a zero step");
    CHECK(mvfxContext_Step(context, MVFX_MAX_STEP_SECONDS) == mvfx_success, "the longest step");
    mvfxDestroyContext(context);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    contextDef.limits.instances = 0;
    mvfxContext* none = NULL;
    CHECK(mvfxCreateContext(&contextDef, &none) == mvfx_errorInvalid, "no instances allowed");
    contextDef.limits.instances = 65537;
    CHECK(mvfxCreateContext(&contextDef, &none) == mvfx_errorInvalid, "too many instances");
}

// The bytes a counting allocator holds, each block's start kept before it.
static int64_t s_held;

static void* Allocate(size_t size, size_t alignment, void* context)
{
    (void)context;
    size_t front = alignment > 16 ? alignment : 16;
    unsigned char* block = malloc(size + front + alignment);
    if (block == NULL)
    {
        return NULL;
    }
    uintptr_t start = ((uintptr_t)block + front + alignment - 1) & ~(uintptr_t)(alignment - 1);
    ((void**)start)[-1] = block;
    s_held += (int64_t)size;
    return (void*)start;
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    if (memory != NULL)
    {
        free(((void**)memory)[-1]);
        s_held -= (int64_t)size;
    }
}

// An effect of one plain emitter holds no gas or water room: its compiled
// form and an instance of it are a few KiB, not the tens they were with
// every gas's state inline.
static void TestMemory(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    contextDef.allocator = (mvfxAllocator){.alloc = Allocate, .free = Free};
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context created");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 16;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    int64_t before = s_held;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect created");
    int64_t effectBytes = s_held - before;
    CHECK(effectBytes > 0 && effectBytes <= 16384, "a plain effect within 16 KiB");
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    mvfxInstanceId instance = {0};
    before = s_held;
    CHECK(mvfxCreateInstance(context, &def, &instance) == mvfx_success, "created");
    int64_t instanceBytes = s_held - before;
    CHECK(instanceBytes > 0 && instanceBytes <= 6144, "a plain instance of 16 within 6 KiB");
    mvfxDestroyContext(context);
    CHECK(s_held == 0, "everything given back");
}

int main(void)
{
    TestLifecycle();
    TestRefusals();
    TestMemory();
    return s_failures == 0 ? 0 : 1;
}
