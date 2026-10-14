// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating and destroying contexts.

#include "context.h"

#include "allocator.h"
#include "effect.h"

mvfxResult mvfxCreateContext(const mvfxContextDef* def, mvfxContext** contextOut)
{
    if (contextOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    *contextOut = nullptr;
    if (def == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (def->version != MVFX_ABI_VERSION)
    {
        return mvfx_errorVersion;
    }
    if (def->cookie != MVFX_CONTEXT_DEF_COOKIE || !mvfxIsAllocatorValid(&def->allocator) ||
        (def->enqueueTask == nullptr) != (def->finishTask == nullptr) || def->limits.effects < 1 ||
        def->limits.effects > 65536)
    {
        return mvfx_errorInvalid;
    }
    mvfxContext* context = mvfxAllocate(&def->allocator, sizeof(mvfxContext), alignof(mvfxContext));
    if (context == nullptr)
    {
        return mvfx_errorCapacity;
    }
    *context = (mvfxContext){
        .allocator = def->allocator,
        .tasks = {def->enqueueTask, def->finishTask, def->userTaskContext},
    };
    if (!mvfxInitSlots(&context->effects, &context->allocator, (uint32_t)def->limits.effects))
    {
        mvfxRelease(&def->allocator, context, sizeof(mvfxContext), alignof(mvfxContext));
        return mvfx_errorCapacity;
    }
    *contextOut = context;
    return mvfx_success;
}

void mvfxDestroyContext(mvfxContext* context)
{
    if (context == nullptr)
    {
        return;
    }
    mvfxAllocator allocator = context->allocator;
    for (uint32_t i = 0; i < context->effects.capacity; ++i)
    {
        mvfxEffect* effect = context->effects.slots[i].object;
        if (effect != nullptr)
        {
            mvfxReleaseEffect(effect, &allocator);
        }
    }
    mvfxReleaseSlots(&context->effects, &allocator);
    mvfxRelease(&allocator, context, sizeof(mvfxContext), alignof(mvfxContext));
}

uint64_t mvfxContext_GetMisuse(const mvfxContext* context)
{
    return context->misuse;
}

void mvfxCountMisuse(mvfxContext* context)
{
    ++context->misuse;
}
