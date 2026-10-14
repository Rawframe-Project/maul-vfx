// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating, destroying and reading instances, and the step's refusals.

#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>

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
    CHECK(mvfxContext_GetMisuse(context) == 0, "no misuse so far");
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
    CHECK(instance.index1 == 0 && mvfxContext_GetMisuse(context) == ++misuse, "counted");
    CHECK(mvfxCreateInstance(context, &def, NULL) == mvfx_errorInvalid, "no out-parameter");
    CHECK(mvfxContext_GetMisuse(context) == ++misuse, "counted");
    CHECK(mvfxCreateInstance(NULL, &def, &instance) == mvfx_errorInvalid, "no context");
    mvfxInstanceDef bad = def;
    bad.cookie = 0;
    CHECK(mvfxCreateInstance(context, &bad, &instance) == mvfx_errorInvalid, "no cookie");
    CHECK(mvfxContext_GetMisuse(context) == ++misuse, "counted");
    static const double origins[] = {(double)NAN, (double)INFINITY, 0x1p53};
    for (int32_t i = 0; i < 3; ++i)
    {
        bad = def;
        bad.origin[i] = origins[i];
        CHECK(mvfxCreateInstance(context, &bad, &instance) == mvfx_errorInvalid, "bad origin");
        CHECK(mvfxContext_GetMisuse(context) == ++misuse, "counted");
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
    CHECK(mvfxContext_GetMisuse(context) == misuse, "each counted");
    CHECK(mvfxContext_GetMisuse(context) == misuse && misuse == 9, "nine refusals");

    CHECK(mvfxContext_Step(context, -0.001) == mvfx_errorInvalid, "a negative step");
    CHECK(mvfxContext_Step(context, (double)NAN) == mvfx_errorInvalid, "a NaN step");
    CHECK(mvfxContext_Step(context, MVFX_MAX_STEP_SECONDS * 1.01) == mvfx_errorInvalid,
          "too long a step");
    CHECK(mvfxContext_StepTicks(context, -1) == mvfx_errorInvalid, "negative ticks");
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND + 1) == mvfx_errorInvalid,
          "too many ticks");
    CHECK(mvfxContext_GetMisuse(context) == misuse + 5, "each counted");
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

int main(void)
{
    TestLifecycle();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
