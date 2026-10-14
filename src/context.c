// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating and destroying contexts.

#include "context.h"

#include "allocator.h"

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
        (def->enqueueTask == nullptr) != (def->finishTask == nullptr))
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
