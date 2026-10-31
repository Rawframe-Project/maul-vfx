// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The library's own sine, cosine and exponential reach a host's outputs
// bit for bit: an emitter's cone of directions carries the cosine of its
// angle in its view, and a particle under drag alone keeps e^(-k t) of
// its velocity over a step. Each against its known float, the correctly
// rounded value, a fifth of a unit or more from halfway between two
// floats. test_elementary checks the functions themselves, on strides of
// their domains, with test_elementary_sweep over every float; they stay
// internal, as no output reaches every argument.

#include "particles.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"
#include "maul-vfx/plan.h"

#include <math.h>
#include <string.h>

static uint32_t Bits(float x)
{
    uint32_t bits = 0;
    memcpy(&bits, &x, sizeof bits);
    return bits;
}

static void TestConeCosine(void)
{
    // cos(0.7f) = 0x1.8799656bf1a0dp-1, the float 0x1.879966p-1.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.velocity.coneAngle = 0.7f;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxEmitterView view;
    CHECK(mvfxEffect_GetEmitterView(context, effect, 0, &view) == mvfx_success, "view");
    CHECK(Bits(view.velocity.coneCosine) == 0x3F43CCB3u &&
              view.velocity.coneCosine == (float)cos((double)0.7f),
          "the cone's cosine");
    mvfxDestroyContext(context);
}

static void TestDragDecay(void)
{
    // e^-4 = 0x1.2c155b8213cf4p-6, the float 0x1.2c155cp-6: the velocity
    // left after a second under drag 4, from 1.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.hostSpawnCapacity = 1;
    emitter.lifetime = mvfxMakeRangeSource(10.0f, 10.0f);
    emitter.motion.gravity[0] = 0.0f;
    emitter.motion.gravity[1] = 0.0f;
    emitter.motion.gravity[2] = 0.0f;
    emitter.motion.drag = 4.0f;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    static const double origin[3] = {0, 0, 0};
    static const float up[3] = {0, 1, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = origin, .velocities = up};
    CHECK(mvfxInstance_Spawn(context, instance, 0, &request) == mvfx_success, "placed");
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND) == mvfx_success, "a second");
    TestParticle particle;
    CHECK(ReadParticles(context, instance, 0, &particle, 1) == 1, "one particle");
    CHECK(Bits(particle.velocity[1]) == 0x3C960AAEu && particle.velocity[1] == (float)exp(-4.0),
          "e^-4 of its velocity");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestConeCosine();
    TestDragDecay();
    return s_failures == 0 ? 0 : 1;
}
