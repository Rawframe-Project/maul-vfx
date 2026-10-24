// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The appearance module's own frame arithmetic: the frames moved are the
// floor of an exact product, at a normalized age and at an age in ticks
// that a test cannot reach by stepping (its inverse life and birth tick
// set in the emitter's arrays). The values over life as the output writes
// them are in test_over_life.c.

#include "appearance.h"
#include "context.h"
#include "instance.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    mvfxInstance* live;
} Scene;

static Scene NewScene(const mvfxEmitterDef* emitter)
{
    Scene scene = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    scene.live = scene.context->instances.slots[scene.instance.index1 - 1].object;
    return scene;
}

// An emitter of what the host spawns, living two seconds.
static mvfxEmitterDef Held(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.hostSpawnCapacity = 4;
    emitter.lifetime = mvfxRangeSource(2.0f, 2.0f);
    return emitter;
}

// One particle at rest at the origin (or moving at speed along X), then
// the given seconds.
static Scene Run(const mvfxEmitterDef* emitter, float speed, int64_t ticks)
{
    Scene scene = NewScene(emitter);
    static const double origin[3] = {0, 0, 0};
    const float velocity[3] = {speed, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = origin, .velocities = velocity};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_success, "placed");
    // At most a second a step.
    for (int64_t left = ticks; left > 0; left -= MVFX_TICKS_PER_SECOND)
    {
        int64_t step = left < MVFX_TICKS_PER_SECOND ? left : MVFX_TICKS_PER_SECOND;
        CHECK(mvfxContext_StepTicks(scene.context, step) == mvfx_success, "step");
    }
    return scene;
}

static void TestExactFrames(void)
{
    mvfxEmitterDef emitter = Held();
    // The frames moved are the floor of the exact product, where binary64
    // rounds up to the next integer: an age of 0.6232155561447144 (one
    // second of a life whose inverse it is) times 64.00150299072266 cycles
    // of 65,535 frames is just below 2,613,977, frame 58,111 looping; a
    // particle 977,351,125,672 ticks old at 1.0000003576278687 frames a
    // second is just below 932,075, frame 14,584.
    emitter.overLife.frame = (mvfxFrameOverLife){.animation = mvfx_frameOverLife,
                                                 .frameCount = 65535,
                                                 .cycles = 64.00150299072266f,
                                                 .loop = true};
    Scene scene = Run(&emitter, 0.0f, MVFX_TICKS_PER_SECOND);
    float* inverse = mvfxAttributeArray(scene.live, 0, mvfx_attributeInverseLife);
    inverse[0] = 0.6232155561447144f;
    CHECK(mvfxFrameOf(scene.live, 0, 0) == 58111u, "over life, exactly");
    mvfxDestroyContext(scene.context);
    emitter.overLife.frame = (mvfxFrameOverLife){.animation = mvfx_frameAtRate,
                                                 .frameCount = 65535,
                                                 .frameRate = 1.0000003576278687f,
                                                 .loop = true};
    scene = Run(&emitter, 0.0f, MVFX_TICKS_PER_SECOND);
    uint64_t birth = (uint64_t)(scene.live->clock - 977351125672);
    ((uint32_t*)mvfxAttributeArray(scene.live, 0, mvfx_attributeBirthLow))[0] = (uint32_t)birth;
    ((uint32_t*)mvfxAttributeArray(scene.live, 0, mvfx_attributeBirthHigh))[0] =
        (uint32_t)(birth >> 32u);
    CHECK(mvfxFrameOf(scene.live, 0, 0) == 14584u, "at a rate, exactly");
    mvfxDestroyContext(scene.context);
}

int main(void)
{
    TestExactFrames();
    return s_failures == 0 ? 0 : 1;
}
