// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rigid_wake.h"

#include "appearance.h"
#include "effect.h"
#include "rigid_shape.h"

#include <math.h>

float mvfxReachOf(const mvfxInstance* instance, int32_t emitter, int32_t i)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    float size[3];
    mvfxBodyMass mass;
    mvfxSizeOf(instance, emitter, i, size);
    mvfxBodyShape shape = mvfxBodyShapeOf(&compiled->rigid, compiled->hull, size, &mass);
    return mvfxBodyReach(&shape);
}

void mvfxWakeWithin(mvfxInstance* instance, int32_t emitter, const float center[3], float radius)
{
    if (!mvfxHasAttribute(&instance->effect->emitters[emitter].plan, mvfx_attributeSleep))
    {
        return;
    }
    float* sleep = mvfxAttributeArray(instance, emitter, mvfx_attributeSleep);
    const float* x[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        x[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
    }
    for (int32_t i = 0; i < instance->emitters[emitter].count; ++i)
    {
        if (!(sleep[i] < 0.0f))
        {
            continue;
        }
        float d = 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            float g = x[k][i] - center[k];
            d += g * g;
        }
        float reach = radius + mvfxReachOf(instance, emitter, i);
        sleep[i] = d <= reach * reach ? 0.0f : sleep[i];
    }
}

void mvfxWakeNear(mvfxInstance* instance, int32_t emitter, const mvfxResolvedCollider* before,
                  const mvfxResolvedCollider* after)
{
    if (!mvfxHasAttribute(&instance->effect->emitters[emitter].plan, mvfx_attributeSleep))
    {
        return;
    }
    float* sleep = mvfxAttributeArray(instance, emitter, mvfx_attributeSleep);
    bool changed = mvfxOnlyPointsChanged(before, after);
    const float* x[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        x[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
    }
    for (int32_t i = 0; i < instance->emitters[emitter].count; ++i)
    {
        if (!(sleep[i] < 0.0f))
        {
            continue;
        }
        const float p[3] = {x[0][i], x[1][i], x[2][i]};
        float gap = INFINITY;
        if (changed)
        {
            gap = mvfxChangedGap(after, p);
        }
        else
        {
            // The collider as it was by its bounds: its grid may be gone.
            gap = before != nullptr ? fminf(gap, mvfxColliderGap(before, p, true)) : gap;
            gap = after != nullptr ? fminf(gap, mvfxColliderGap(after, p, false)) : gap;
        }
        float reach = (1.0f + MVFX_CONTACT_MARGIN) * mvfxReachOf(instance, emitter, i);
        sleep[i] = gap <= reach ? 0.0f : sleep[i];
    }
}

int32_t mvfxCountAsleep(const mvfxInstance* instance, int32_t emitter)
{
    if (!mvfxHasAttribute(&instance->effect->emitters[emitter].plan, mvfx_attributeSleep))
    {
        return 0;
    }
    const float* sleep = mvfxAttributeArray(instance, emitter, mvfx_attributeSleep);
    int32_t asleep = 0;
    for (int32_t i = 0; i < instance->emitters[emitter].count; ++i)
    {
        asleep += sleep[i] < 0.0f ? 1 : 0;
    }
    return asleep;
}
