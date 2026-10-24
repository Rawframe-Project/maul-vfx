// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Planned instances' bookkeeping that only the CPU path holds: a planned
// beam's strike counter kept as the stepped beam's, through steps of a
// tick, frames and steps longer than a life, and a stop. The strikes
// themselves (their ticks, serials, lives and deaths) are compared with
// the stepped beam's in test_plan.c.

#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include "maul-vfx/plan.h"

#define STEP_TICKS (MVFX_TICKS_PER_SECOND / 60)

static void TestBeamStrikes(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 6;
    emitter.lifetime = mvfxRangeSource(0.05f, 0.2f);
    emitter.ribbon.ids = mvfx_ribbonOne;
    emitter.beam = (mvfxBeamDef){.enabled = true,
                                 .segments = 5,
                                 .start = {.point = {0, 0.5f, 0}},
                                 .end = {.point = {4, 0, -1}},
                                 .noiseSpeed = 2.5f};
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.seed = 7;
    mvfxInstanceId cpu = {0};
    mvfxInstanceId planned = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &cpu) == mvfx_success, "a CPU instance");
    instanceDef.planned = true;
    CHECK(mvfxCreateInstance(context, &instanceDef, &planned) == mvfx_success,
          "a planned instance");
    bool same = true;
    int32_t strikes = 0;
    for (int32_t s = 0; same && s < 64; ++s)
    {
        if (s == 50)
        {
            CHECK(mvfxInstance_Stop(context, cpu, mvfx_stopEmitting) == mvfx_success &&
                      mvfxInstance_Stop(context, planned, mvfx_stopEmitting) == mvfx_success,
                  "stopped");
        }
        // Ticks one at a time, frames, and steps longer than a life.
        int64_t ticks = s < 8 ? 1 : (s % 5 == 0 ? MVFX_TICKS_PER_SECOND / 2 : STEP_TICKS);
        CHECK(mvfxContext_StepTicks(context, ticks) == mvfx_success, "stepped");
        const mvfxEmitterState* a = &mvfxContextInstance(context, cpu)->emitters[0];
        const mvfxEmitterState* b = &mvfxContextInstance(context, planned)->emitters[0];
        same = a->beamStrike == b->beamStrike;
        strikes = (int32_t)a->beamStrike;
    }
    CHECK(same && strikes > 20, "a planned beam's strike counter is the stepped one's");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestBeamStrikes();
    return s_failures == 0 ? 0 : 1;
}
