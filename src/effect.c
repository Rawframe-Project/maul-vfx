// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Effect defs checked and compiled: every field against its range, then
// each emitter's attribute plan laid out.

#include "effect.h"

#include "allocator.h"

mvfxEmitterDef mvfxDefaultEmitterDef(void)
{
    return (mvfxEmitterDef){
        .cookie = MVFX_EMITTER_DEF_COOKIE,
        .capacity = 1024,
        .rate = 64.0f,
        .lifetime = {1.0f, 1.0f},
    };
}

mvfxEffectDef mvfxDefaultEffectDef(void)
{
    return (mvfxEffectDef){.cookie = MVFX_EFFECT_DEF_COOKIE};
}

// Whether low <= x <= high; false for NaN.
static bool IsWithin(float x, float low, float high)
{
    return x >= low && x <= high;
}

static bool IsEmitterDefValid(const mvfxEmitterDef* def)
{
    return def->cookie == MVFX_EMITTER_DEF_COOKIE && def->capacity >= 1 &&
           def->capacity <= MVFX_MAX_EMITTER_CAPACITY &&
           IsWithin(def->rate, 0.0f, MVFX_MAX_SPAWN_RATE) &&
           IsWithin(def->lifetime.max, 0.0f, MVFX_MAX_LIFETIME) && def->lifetime.min > 0.0f &&
           def->lifetime.min <= def->lifetime.max;
}

bool mvfxIsEffectDefValid(const mvfxEffectDef* def)
{
    if (def == nullptr || def->cookie != MVFX_EFFECT_DEF_COOKIE || def->emitters == nullptr ||
        def->emitterCount < 1 || def->emitterCount > MVFX_MAX_EMITTERS)
    {
        return false;
    }
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        if (!IsEmitterDefValid(&def->emitters[i]))
        {
            return false;
        }
    }
    return true;
}

// The plan of the attributes every emitter has: position, age, inverse
// life and serial, each padded and aligned.
static mvfxAttributePlan PlanAttributes(int32_t paddedCapacity)
{
    mvfxAttributePlan plan = {0};
    mvfxLayout layout = {0};
    for (mvfxAttribute a = 0; a < MVFX_ATTRIBUTE_COUNT; ++a)
    {
        plan.present |= 1u << a;
        plan.offsets[a] =
            mvfxLayoutAdd(&layout, (size_t)paddedCapacity, sizeof(float), MVFX_ATTRIBUTE_ALIGNMENT);
    }
    plan.blockBytes = layout.size;
    return plan;
}

mvfxEffect* mvfxCompileEffect(const mvfxEffectDef* def, const mvfxAllocator* allocator)
{
    mvfxLayout layout = {0};
    (void)mvfxLayoutAdd(&layout, 1, sizeof(mvfxEffect), alignof(mvfxEffect));
    size_t emittersAt = mvfxLayoutAdd(&layout, (size_t)def->emitterCount,
                                      sizeof(mvfxCompiledEmitter), alignof(mvfxCompiledEmitter));
    if (layout.overflow)
    {
        return nullptr;
    }
    unsigned char* block = mvfxAllocate(allocator, layout.size, layout.alignment);
    if (block == nullptr)
    {
        return nullptr;
    }
    mvfxEffect* effect = (mvfxEffect*)block;
    *effect = (mvfxEffect){
        .emitters = (mvfxCompiledEmitter*)(block + emittersAt),
        .emitterCount = def->emitterCount,
        .blockBytes = layout.size,
        .blockAlignment = layout.alignment,
    };
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        const mvfxEmitterDef* source = &def->emitters[i];
        int32_t padded = (source->capacity + MVFX_ATTRIBUTE_PADDING - 1) / MVFX_ATTRIBUTE_PADDING *
                         MVFX_ATTRIBUTE_PADDING;
        effect->emitters[i] = (mvfxCompiledEmitter){
            .capacity = source->capacity,
            .paddedCapacity = padded,
            .rate = source->rate,
            .lifetime = source->lifetime,
            .plan = PlanAttributes(padded),
        };
    }
    return effect;
}

void mvfxReleaseEffect(mvfxEffect* effect, const mvfxAllocator* allocator)
{
    mvfxRelease(allocator, effect, effect->blockBytes, effect->blockAlignment);
}
