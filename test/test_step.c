// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a step spawns, keeps and refuses, and that a span of ticks reaches
// the same counts however it is split.

#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <stdlib.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
} Scene;

static Scene NewScene(int32_t capacity, float rate, mvfxRange lifetime)
{
    Scene scene = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = capacity;
    emitter.rate = rate;
    emitter.lifetime = mvfxRangeSource(lifetime.min, lifetime.max);
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.seed = 1234;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    return scene;
}

static mvfxEmitterStats Stats(const Scene* scene)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(scene->context, scene->instance, 0, &stats) == mvfx_success,
          "stats read");
    return stats;
}

static void TestSpawnAndDeathAtExactTicks(void)
{
    // 64 a second, each living exactly one second: particle k is born at
    // tick 16,384 (k + 1), when the rate's integral reaches k + 1. At one
    // second 64 are owed and the first has lived 63/64 of its life.
    Scene scene = NewScene(1024, 64.0f, (mvfxRange){1.0f, 1.0f});
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    mvfxEmitterStats stats = Stats(&scene);
    CHECK(stats.owed == 64, "owed at one second");
    CHECK(stats.live == 64 && stats.refused == 0, "all alive");
    CHECK(mvfxContext_StepTicks(scene.context, 16383) == mvfx_success, "a tick short");
    stats = Stats(&scene);
    CHECK(stats.owed == 64 && stats.live == 64, "nothing born or ended a tick early");
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "the next birth's tick");
    stats = Stats(&scene);
    CHECK(stats.owed == 65 && stats.live == 64, "one born as the first ends");
    mvfxDestroyContext(scene.context);
}

static void TestCapacityRefuses(void)
{
    Scene scene = NewScene(10, 100.0f, (mvfxRange){10.0f, 10.0f});
    CHECK(mvfxContext_Step(scene.context, 1.0) == mvfx_success, "step");
    mvfxEmitterStats stats = Stats(&scene);
    CHECK(stats.owed == 100, "owed");
    CHECK(stats.live == 10 && stats.refused == 90, "the capacity refuses the rest, counted");
    mvfxDestroyContext(scene.context);
}

static void TestBornAndEndedWithinAStep(void)
{
    // Lives of 1 ms against a 0.5 s step: every particle but the newest is
    // born and gone within the step, owed and never seen.
    Scene scene = NewScene(1000, 1000.0f, (mvfxRange){0.001f, 0.001f});
    CHECK(mvfxContext_Step(scene.context, 0.5) == mvfx_success, "step");
    mvfxEmitterStats stats = Stats(&scene);
    CHECK(stats.owed == 500, "owed");
    CHECK(stats.live <= 2 && stats.refused == 0, "only the newest live; none refused");
    mvfxDestroyContext(scene.context);
}

static void TestZeroRateSpawnsNothing(void)
{
    Scene scene = NewScene(16, 0.0f, (mvfxRange){1.0f, 1.0f});
    CHECK(mvfxContext_Step(scene.context, 1.0) == mvfx_success, "step");
    mvfxEmitterStats stats = Stats(&scene);
    CHECK(stats.owed == 0 && stats.live == 0, "nothing at rate 0");
    mvfxDestroyContext(scene.context);
}

static void TestSplitsReachTheSameCounts(void)
{
    // Five seconds of ticks, in one-second steps and in random steps that sum
    // to the same ticks: the counts after every whole second agree.
    Scene whole = NewScene(100, 77.7f, (mvfxRange){0.5f, 3.0f});
    Scene split = NewScene(100, 77.7f, (mvfxRange){0.5f, 3.0f});
    srand(99);
    for (int32_t second = 0; second < 5; ++second)
    {
        CHECK(mvfxContext_StepTicks(whole.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
        int64_t left = MVFX_TICKS_PER_SECOND;
        while (left > 0)
        {
            int64_t ticks = 1 + rand() % 40000;
            ticks = ticks > left ? left : ticks;
            CHECK(mvfxContext_StepTicks(split.context, ticks) == mvfx_success, "a part");
            left -= ticks;
        }
        mvfxEmitterStats a = Stats(&whole);
        mvfxEmitterStats b = Stats(&split);
        CHECK(a.owed == b.owed && a.live == b.live && a.refused == b.refused,
              "the same counts after the same ticks");
    }
    CHECK(Stats(&whole).refused > 0, "the capacity was reached");
    mvfxDestroyContext(whole.context);
    mvfxDestroyContext(split.context);
}

static void TestOneStepEqualsTickByTick(void)
{
    // A full emitter with varied lives, where births find it full and deaths
    // free it within a step: one step of a quarter second, and the same span
    // stepped one tick at a time, end with the same counts.
    Scene whole = NewScene(40, 500.0f, (mvfxRange){0.02f, 0.2f});
    Scene ticks = NewScene(40, 500.0f, (mvfxRange){0.02f, 0.2f});
    CHECK(mvfxContext_StepTicks(whole.context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success, "step");
    for (int32_t i = 0; i < MVFX_TICKS_PER_SECOND / 4; ++i)
    {
        if (mvfxContext_StepTicks(ticks.context, 1) != mvfx_success)
        {
            CHECK(false, "a tick");
            break;
        }
    }
    mvfxEmitterStats a = Stats(&whole);
    mvfxEmitterStats b = Stats(&ticks);
    CHECK(a.refused > 0, "births were refused");
    CHECK(a.owed == b.owed && a.live == b.live && a.refused == b.refused,
          "one step reaches what single ticks reach");
    mvfxDestroyContext(whole.context);
    mvfxDestroyContext(ticks.context);
}

static void TestDeathFreesItsTickForABirth(void)
{
    // 64 a second, each living half a second, room for 32: each death falls
    // on a birth's tick and frees its room first, so a full emitter refuses
    // nothing, and the particle born within the step that ends on its last
    // tick is gone.
    Scene scene = NewScene(32, 64.0f, (mvfxRange){0.5f, 0.5f});
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    mvfxEmitterStats stats = Stats(&scene);
    CHECK(stats.owed == 128, "owed at two seconds");
    CHECK(stats.refused == 0, "deaths come before births on a tick");
    CHECK(stats.live == 32, "the one ending now is gone");
    mvfxDestroyContext(scene.context);
}

static void TestSecondsRoundToTheNearestTick(void)
{
    // 16,383.7 ticks round to 16,384, the first birth's tick.
    Scene scene = NewScene(16, 64.0f, (mvfxRange){1.0f, 1.0f});
    CHECK(mvfxContext_Step(scene.context, 16383.7 / MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(Stats(&scene).owed == 1, "rounded up to the birth's tick");
    CHECK(mvfxContext_Step(scene.context, 16384.3 / MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(Stats(&scene).owed == 2, "rounded down to the next birth's tick");
    mvfxDestroyContext(scene.context);
}

int main(void)
{
    TestSpawnAndDeathAtExactTicks();
    TestCapacityRefuses();
    TestBornAndEndedWithinAStep();
    TestZeroRateSpawnsNothing();
    TestSplitsReachTheSameCounts();
    TestOneStepEqualsTickByTick();
    TestDeathFreesItsTickForABirth();
    TestSecondsRoundToTheNearestTick();
    return s_failures == 0 ? 0 : 1;
}
