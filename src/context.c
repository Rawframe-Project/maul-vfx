// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating and destroying contexts.

#include "context.h"

#include "allocator.h"
#include "effect.h"
#include "grid.h"
#include "instance.h"
#include "invariant.h"
#include "layout.h"
#include "mesh.h"

#include <stdatomic.h>

// The context's allocator: the host's, each call counted as host code.
static void* HostAlloc(size_t size, size_t alignment, void* opaque)
{
    mvfxContext* context = opaque;
    mvfxEnterHost(context);
    void* memory = mvfxAllocate(&context->hostAllocator, size, alignment);
    mvfxLeaveHost(context);
    return memory;
}

static void HostFree(void* memory, size_t size, size_t alignment, void* opaque)
{
    mvfxContext* context = opaque;
    mvfxEnterHost(context);
    mvfxRelease(&context->hostAllocator, memory, size, alignment);
    mvfxLeaveHost(context);
}

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
    if (def->cookie != MVFX_CONTEXT_DEF_COOKIE)
    {
        return mvfx_errorInvalid;
    }
    if (def->version != MVFX_ABI_VERSION)
    {
        return mvfx_errorVersion;
    }
    if (!mvfxIsAllocatorValid(&def->allocator) ||
        (def->enqueueTask == nullptr) != (def->finishTask == nullptr) || def->limits.effects < 1 ||
        def->limits.effects > 65536 || def->limits.instances < 1 || def->limits.instances > 65536 ||
        def->limits.meshes < 1 || def->limits.meshes > 65536 || def->limits.grids < 1 ||
        def->limits.grids > 65536 || def->limits.layouts < 1 || def->limits.layouts > 65536 ||
        def->limits.particles < 0 || def->limits.particles > ((int64_t)1 << 40))
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
        .hostAllocator = def->allocator,
        .tasks = {def->enqueueTask, def->finishTask, def->userTaskContext},
        .particleBudget = def->limits.particles,
    };
    if (def->allocator.alloc != nullptr)
    {
        // The host's allocator behind the context's own, which counts its
        // calls.
        context->allocator = (mvfxAllocator){HostAlloc, HostFree, context};
    }
    mvfxSlots* tables[5] = {&context->effects, &context->instances, &context->meshes,
                            &context->grids, &context->layouts};
    const int32_t limits[5] = {def->limits.effects, def->limits.instances, def->limits.meshes,
                               def->limits.grids, def->limits.layouts};
    for (int32_t t = 0; t < 5; ++t)
    {
        if (!mvfxInitSlots(tables[t], &context->allocator, (uint32_t)limits[t]))
        {
            for (int32_t u = t - 1; u >= 0; --u)
            {
                mvfxReleaseSlots(tables[u], &context->allocator);
            }
            mvfxRelease(&def->allocator, context, sizeof(mvfxContext), alignof(mvfxContext));
            return mvfx_errorCapacity;
        }
    }
    *contextOut = context;
    return mvfx_success;
}

void mvfxDestroyContext(mvfxContext* context)
{
    if (context == nullptr || mvfxRefusesHostCall(context))
    {
        return;
    }
    mvfxAllocator allocator = context->allocator;
    mvfxAllocator host = context->hostAllocator;
    for (uint32_t i = 0; i < context->instances.capacity; ++i)
    {
        mvfxInstance* instance = context->instances.slots[i].object;
        if (instance != nullptr)
        {
            mvfxReleaseInstance(instance, &allocator);
        }
    }
    mvfxReleaseSlots(&context->instances, &allocator);
    for (uint32_t i = 0; i < context->effects.capacity; ++i)
    {
        mvfxEffect* effect = context->effects.slots[i].object;
        if (effect != nullptr)
        {
            mvfxReleaseEffect(effect, &allocator);
        }
    }
    mvfxReleaseSlots(&context->effects, &allocator);
    for (uint32_t i = 0; i < context->meshes.capacity; ++i)
    {
        mvfxMesh* mesh = context->meshes.slots[i].object;
        if (mesh != nullptr)
        {
            mvfxReleaseMesh(mesh, &allocator);
        }
    }
    mvfxReleaseSlots(&context->meshes, &allocator);
    for (uint32_t i = 0; i < context->grids.capacity; ++i)
    {
        mvfxGrid* grid = context->grids.slots[i].object;
        if (grid != nullptr)
        {
            mvfxReleaseGrid(grid, &allocator);
        }
    }
    mvfxReleaseSlots(&context->grids, &allocator);
    for (uint32_t i = 0; i < context->layouts.capacity; ++i)
    {
        mvfxOutputLayout* layout = context->layouts.slots[i].object;
        if (layout != nullptr)
        {
            mvfxReleaseLayout(layout, &allocator);
        }
    }
    mvfxReleaseSlots(&context->layouts, &allocator);
    // The context's own memory through the host's allocator: the count of
    // its calls lives in it.
    mvfxRelease(&host, context, sizeof(mvfxContext), alignof(mvfxContext));
}

int64_t mvfxContext_GetParticles(const mvfxContext* context)
{
    return context->particlesHeld;
}

uint64_t mvfxGetContextMisuse(const mvfxContext* context)
{
    return atomic_load_explicit(&context->misuse, memory_order_relaxed);
}

void mvfxCountMisuse(mvfxContext* context)
{
    atomic_fetch_add_explicit(&context->misuse, 1, memory_order_relaxed);
}

void mvfxEnterHost(mvfxContext* context)
{
    atomic_fetch_add_explicit(&context->hostCalls, 1, memory_order_relaxed);
}

void mvfxLeaveHost(mvfxContext* context)
{
    // This thread's own entry is still counted, whatever other threads do.
    MVFX_ASSERT(atomic_load_explicit(&context->hostCalls, memory_order_relaxed) > 0);
    atomic_fetch_sub_explicit(&context->hostCalls, 1, memory_order_relaxed);
}

bool mvfxRefusesHostCall(mvfxContext* context)
{
    if (context == nullptr || atomic_load_explicit(&context->hostCalls, memory_order_relaxed) == 0)
    {
        return false;
    }
    mvfxCountMisuse(context);
    return true;
}
