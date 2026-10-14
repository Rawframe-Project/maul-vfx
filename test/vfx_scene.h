// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The scene the determinism tests step: an effect of three endless
// emitters over four instances, with full and churning emitters among
// them, and an effect of emitter lives (a rate curve, looping bursts,
// delays) over two; a fixed sequence of steps, and a hash of every
// instance's state.

#ifndef MAUL_VFX_TEST_VFX_SCENE_H
#define MAUL_VFX_TEST_VFX_SCENE_H

#include "context.h"
#include "instance.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"

// The steps of the scene, in ticks: uneven, as a host's frames are.
static const int64_t s_sceneSteps[] = {17476, 17477, 16000, 1,     40000, 17476, 3,
                                       99999, 17476, 17476, 8192,  65536, 17477, 1048576,
                                       5,     17476, 30000, 17476, 17476, 2};

#define SCENE_STEP_COUNT ((int32_t)(sizeof s_sceneSteps / sizeof s_sceneSteps[0]))

// Creates the scene's effects and instances on a context.
static inline void BuildScene(mvfxContext* context)
{
    mvfxEmitterDef emitters[3] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef(),
                                  mvfxDefaultEmitterDef()};
    emitters[0].capacity = 100;
    emitters[0].rate = 37.5f;
    emitters[0].lifetime = (mvfxRange){0.25f, 1.5f};
    emitters[1].capacity = 64;
    emitters[1].rate = 2000.0f;
    emitters[1].lifetime = (mvfxRange){0.01f, 0.05f};
    emitters[0].shape.kind = mvfx_shapeSphere;
    emitters[0].shape.radius = 1.5f;
    emitters[0].shape.thickness = 0.3f;
    emitters[0].velocity.speed = (mvfxRange){1.0f, 4.0f};
    emitters[1].shape.kind = mvfx_shapeCone;
    emitters[1].shape.radius = 0.5f;
    emitters[1].shape.length = 2.0f;
    emitters[1].shape.angle = 0.4f;
    emitters[1].shape.coneVolume = true;
    emitters[1].shape.rotation[0] = 0.38268343f;
    emitters[1].shape.rotation[3] = 0.92387953f;
    emitters[1].velocity.mode = mvfx_directionCone;
    emitters[1].velocity.coneAngle = 0.25f;
    emitters[1].velocity.randomize = 0.2f;
    emitters[1].velocity.speed = (mvfxRange){5.0f, 6.0f};
    emitters[2].shape.kind = mvfx_shapeBox;
    emitters[2].shape.halfExtents[0] = 1.0f;
    emitters[2].shape.halfExtents[1] = 0.5f;
    emitters[2].shape.halfExtents[2] = 2.0f;
    emitters[2].shape.thickness = 0.0f;
    emitters[2].shape.offset[1] = 3.0f;
    emitters[2].velocity.mode = mvfx_directionFromCenter;
    emitters[2].velocity.speed = (mvfxRange){0.5f, 0.5f};
    emitters[2].capacity = 7;
    emitters[2].rate = 0.7f;
    emitters[2].lifetime = (mvfxRange){5.0f, 9.0f};
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 3;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "scene effect");
    // An effect of emitter lives: a rate curve over loops, delayed and
    // looping bursts of random counts and chances, and a late constant rate.
    static const mvfxCurveKey rise[3] = {
        {0.0f, 0.0f, 0.0f, 0.0f, mvfx_interpolationCubic},
        {0.4f, 1.0f, 0.0f, 0.0f, mvfx_interpolationLinear},
        {1.0f, 0.2f, 0.0f, 0.0f, mvfx_interpolationLinear},
    };
    static const mvfxBurst bursts[2] = {
        {.time = 0.0f,
         .minCount = 5,
         .maxCount = 20,
         .cycles = 3,
         .interval = 0.1f,
         .probability = 0.8f},
        {.time = 0.25f, .minCount = 30, .maxCount = 30, .cycles = 1, .probability = 1.0f},
    };
    mvfxEmitterDef lives[3] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef(),
                               mvfxDefaultEmitterDef()};
    lives[0].capacity = 50;
    lives[0].rate = 400.0f;
    lives[0].rateCurve = (mvfxCurve){rise, 3, mvfx_tangentsMonotone};
    lives[0].duration = 0.75f;
    lives[0].lifetime = (mvfxRange){0.05f, 0.3f};
    lives[1].capacity = 40;
    lives[1].rate = 0.0f;
    lives[1].bursts = bursts;
    lives[1].burstCount = 2;
    lives[1].startDelay = 0.2f;
    lives[1].duration = 0.5f;
    lives[1].loops = 3;
    lives[1].lifetime = (mvfxRange){0.1f, 0.6f};
    lives[0].shape.kind = mvfx_shapeCircle;
    lives[0].shape.radius = 2.0f;
    lives[0].shape.thickness = 0.5f;
    lives[0].shape.arc = 4.0f;
    lives[0].shape.arcMode = mvfx_arcPingPong;
    lives[0].shape.arcSpeed = 1.5f;
    lives[0].velocity.speed = (mvfxRange){2.0f, 2.0f};
    lives[1].shape.kind = mvfx_shapeHemisphere;
    lives[1].shape.radius = 1.0f;
    lives[1].shape.thickness = 0.0f;
    lives[1].velocity.mode = mvfx_directionFixed;
    lives[1].velocity.direction[1] = 1.0f;
    lives[1].velocity.speed = (mvfxRange){0.0f, 3.0f};
    lives[2].shape.kind = mvfx_shapeLine;
    lives[2].shape.length = 4.0f;
    lives[2].shape.arcMode = mvfx_arcLoop;
    lives[2].shape.arcSpeed = 0.5f;
    lives[2].velocity.speed = (mvfxRange){1.0f, 1.0f};
    lives[2].capacity = 30;
    lives[2].rate = 50.0f;
    lives[2].startDelay = 1.0f;
    lives[2].lifetime = (mvfxRange){0.2f, 0.9f};
    mvfxEffectDef livesDef = mvfxDefaultEffectDef();
    livesDef.emitters = lives;
    livesDef.emitterCount = 3;
    mvfxEffectId livesEffect = {0};
    CHECK(mvfxCreateEffect(context, &livesDef, &livesEffect) == mvfx_success, "lives effect");
    for (int32_t i = 0; i < 2; ++i)
    {
        mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
        instanceDef.effect = livesEffect;
        instanceDef.seed = 0x5EEDu + (uint32_t)i;
        mvfxInstanceId instance = {0};
        CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success,
              "lives instance");
    }
    for (int32_t i = 0; i < 4; ++i)
    {
        mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
        instanceDef.effect = effect;
        instanceDef.origin[0] = 1000.0 * (double)i;
        instanceDef.seed = i == 3 ? 0 : 0xABCDu + (uint32_t)i;
        mvfxInstanceId instance = {0};
        CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success,
              "scene instance");
    }
}

// A hash of every live instance of the context, in slot order.
static inline uint64_t HashScene(const mvfxContext* context)
{
    uint64_t hash = MVFX_HASH_INIT;
    for (uint32_t i = 0; i < context->instances.capacity; ++i)
    {
        const mvfxInstance* instance = context->instances.slots[i].object;
        if (instance != NULL)
        {
            hash = mvfxHashInstance(instance, hash);
        }
    }
    return hash;
}

#endif
