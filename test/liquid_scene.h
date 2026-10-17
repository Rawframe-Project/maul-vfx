// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the liquid tests share: a planar liquid's emitter, an instance of
// it opened with particles spawned still, its places and count, and steps
// of 1/60 s.

#ifndef MAUL_VFX_TEST_LIQUID_SCENE_H
#define MAUL_VFX_TEST_LIQUID_SCENE_H

#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#define MOST 1024

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

// A planar liquid's emitter: host spawns only, long lives, no gravity.
static inline mvfxEmitterDef Emitter(float spacing)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = MOST;
    emitter.hostSpawnCapacity = MOST;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.liquid.enabled = true;
    emitter.liquid.planar = true;
    emitter.liquid.spacing = spacing;
    return emitter;
}

// Opens an instance of the emitter and spawns particles at places, still.
static inline void Open(const mvfxEmitterDef* emitter, const double* places, int32_t count)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    mvfxSpawnRequest request = {.count = count, .positions = places};
    CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "spawned");
    // The spawns are born in the first step, and join the liquid from the
    // next.
    CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success, "born");
}

// The emitter's places, by axis.
static inline float* Places(int32_t k)
{
    const mvfxInstance* live = mvfxContextInstance(s_context, s_instance);
    return mvfxAttributeArray(live, 0, (mvfxAttribute)(mvfx_attributePositionX + k));
}

static inline int32_t Count(void)
{
    return mvfxContextInstance(s_context, s_instance)->emitters[0].count;
}

// Steps of 1/60 s.
static inline void Steps(int32_t n)
{
    for (int32_t s = 0; s < n; ++s)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success, "step");
    }
}

#endif
