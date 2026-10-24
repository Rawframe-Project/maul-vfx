// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The tests' switches (test_hooks.h).

#include "test_hooks.h"

#include "effect.h"
#include "instance.h"
#include "instance_api.h"
#include "water.h"

bool mvfxTestSetOwed(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, uint64_t owed)
{
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr || emitter < 0 || emitter >= live->effect->emitterCount)
    {
        return false;
    }
    live->emitters[emitter].owed = owed;
    return true;
}

bool mvfxTestTearEdges(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                       int32_t particle, uint32_t bits)
{
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr || emitter < 0 || emitter >= live->effect->emitterCount ||
        !mvfxHasAttribute(&live->effect->emitters[emitter].plan, mvfx_attributeTorn) ||
        particle < 0 || particle >= live->emitters[emitter].count)
    {
        return false;
    }
    uint32_t* torn = mvfxAttributeArray(live, emitter, mvfx_attributeTorn);
    torn[particle] = bits;
    return true;
}

int32_t mvfxTestRigidPairs(const mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                           int32_t* a, int32_t* b, int32_t capacity)
{
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr || emitter < 0 || emitter >= live->effect->emitterCount)
    {
        return -1;
    }
    const mvfxEmitterState* state = &live->emitters[emitter];
    for (int32_t c = 0; c < state->rigidPairCount && c < capacity; ++c)
    {
        a[c] = state->rigidPairs[c].a;
        b[c] = state->rigidPairs[c].b;
    }
    return state->rigidPairCount;
}

bool mvfxTestWriteWaterDepth(mvfxContext* context, mvfxInstanceId instance, int32_t water,
                             const float* values, int64_t count)
{
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr || water < 0 || water >= live->effect->waterCount)
    {
        return false;
    }
    const mvfxCompiledWater* compiled = &live->effect->waters[water];
    mvfxWaterState* state = &live->waters[water];
    if (compiled->def.model != mvfx_waterShallow || count != compiled->cells)
    {
        return false;
    }
    for (int64_t i = 0; i < count; ++i)
    {
        state->heights[state->current][i] = values[i];
    }
    return true;
}
