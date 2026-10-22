// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Uses the installed library as a consumer would, in C17: it checks that
// the linked library is the version it was compiled against, makes a
// fountain, steps it for a second and writes its particles' places.

#include "maul-vfx/base.h"
#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    mvfxVersion version = mvfxGetVersion();
    if (version.major != MVFX_VERSION_MAJOR || version.minor != MVFX_VERSION_MINOR)
    {
        printf("FAIL: linked %u.%u, compiled against %d.%d\n", version.major, version.minor,
               MVFX_VERSION_MAJOR, MVFX_VERSION_MINOR);
        return 1;
    }
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        printf("FAIL: no context\n");
        return 1;
    }
    // 50 particles a second thrown up under gravity.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 128;
    emitter.rate = 50.0f;
    emitter.lifetime = mvfxRangeSource(2.0f, 2.0f);
    emitter.velocity.mode = mvfx_directionCone;
    emitter.velocity.direction[0] = 0.0f;
    emitter.velocity.direction[1] = 1.0f;
    emitter.velocity.direction[2] = 0.0f;
    emitter.velocity.coneAngle = 0.2f;
    emitter.velocity.speed.min = 3.0f;
    emitter.velocity.speed.max = 4.0f;
    emitter.motion.gravity[1] = -9.8f;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    mvfxInstanceId instance = {0};
    bool ok = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    instanceDef.effect = effect;
    ok = ok && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;
    for (int32_t step = 0; ok && step < 60; ++step)
    {
        ok = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
    }
    mvfxLayoutField place = {0};
    place.field = mvfx_recordPosition;
    place.format = mvfx_formatFloat32;
    place.components = 3;
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &place;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 12;
    mvfxLayoutId layout = {0};
    static float places[128][3];
    mvfxView view = {0};
    view.forward[2] = -1.0f;
    view.up[1] = 1.0f;
    int64_t count = 0;
    ok = ok && mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success &&
         mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0, places, 128,
                                     &count) == mvfx_success;
    mvfxDestroyContext(context);
    // About 50 live after a second at 50 a second.
    if (!ok || count < 45 || count > 55)
    {
        printf("FAIL: %lld particles\n", (long long)count);
        return 1;
    }
    printf("maul-vfx %u.%u.%u: %lld particles after a second\n", version.major, version.minor,
           version.patch, (long long)count);
    return 0;
}
