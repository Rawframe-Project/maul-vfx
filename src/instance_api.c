// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating, destroying and reading instances on a context.

#include "instance_api.h"

#include "context.h"
#include "effect_api.h"
#include "instance.h"
#include "slots.h"

#include <math.h>

mvfxInstanceDef mvfxDefaultInstanceDef(void)
{
    return (mvfxInstanceDef){.cookie = MVFX_INSTANCE_DEF_COOKIE};
}

// Whether every coordinate of an origin is finite and at most 2^52 in
// size, where a double still holds a millimeter's fraction.
static bool IsOriginValid(const double origin[3])
{
    for (int32_t axis = 0; axis < 3; ++axis)
    {
        if (!(fabs(origin[axis]) <= 0x1p52))
        {
            return false;
        }
    }
    return true;
}

// The seed an instance takes when its def gives none: its ordinal among
// the context's instances, hashed, never 0.
static uint32_t SeedFromOrdinal(uint64_t ordinal)
{
    uint64_t hash = mvfxHash64(MVFX_HASH_INIT, &ordinal, (int32_t)sizeof ordinal);
    uint32_t seed = (uint32_t)(hash ^ (hash >> 32u));
    return seed != 0 ? seed : 1;
}

mvfxResult mvfxCreateInstance(mvfxContext* context, const mvfxInstanceDef* def,
                              mvfxInstanceId* instanceOut)
{
    if (context == nullptr || instanceOut == nullptr || def == nullptr ||
        def->cookie != MVFX_INSTANCE_DEF_COOKIE || !IsOriginValid(def->origin))
    {
        if (instanceOut != nullptr)
        {
            *instanceOut = (mvfxInstanceId){0};
        }
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    *instanceOut = (mvfxInstanceId){0};
    mvfxEffect* effect = mvfxContextEffect(context, def->effect);
    if (effect == nullptr)
    {
        return mvfx_errorStale;
    }
    if (context->instances.firstFree == 0)
    {
        return mvfx_errorLimit;
    }
    uint32_t seed = def->seed != 0 ? def->seed : SeedFromOrdinal(context->instancesCreated);
    mvfxInstance* instance =
        mvfxCreateInstanceBlock(effect, def->origin, seed, &context->allocator);
    if (instance == nullptr)
    {
        return mvfx_errorCapacity;
    }
    context->instancesCreated += 1;
    effect->users += 1;
    uint32_t generation = 0;
    uint32_t index1 = mvfxTakeSlot(&context->instances, instance, &generation);
    *instanceOut = (mvfxInstanceId){index1, generation};
    return mvfx_success;
}

mvfxResult mvfxDestroyInstance(mvfxContext* context, mvfxInstanceId instance)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    live->effect->users -= 1;
    mvfxReleaseInstance(live, &context->allocator);
    mvfxFreeSlot(&context->instances, instance.index1);
    return mvfx_success;
}

bool mvfxInstance_IsValid(const mvfxContext* context, mvfxInstanceId instance)
{
    return context != nullptr && mvfxContextInstance(context, instance) != nullptr;
}

mvfxResult mvfxInstance_GetEmitterStats(mvfxContext* context, mvfxInstanceId instance,
                                        int32_t emitter, mvfxEmitterStats* statsOut)
{
    if (statsOut != nullptr)
    {
        *statsOut = (mvfxEmitterStats){0};
    }
    if (context == nullptr || statsOut == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (emitter < 0 || emitter >= live->effect->emitterCount)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    const mvfxEmitterState* state = &live->emitters[emitter];
    *statsOut = (mvfxEmitterStats){state->count, state->owed, state->refused};
    return mvfx_success;
}

mvfxInstance* mvfxContextInstance(const mvfxContext* context, mvfxInstanceId instance)
{
    return mvfxSlotObject(&context->instances, instance.index1, instance.generation);
}
