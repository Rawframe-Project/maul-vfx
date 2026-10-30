// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The root of Maul VFX: a context holds the host's allocator and task
// hooks, and owns the effects and instances made through it.

#ifndef MAUL_VFX_CONTEXT_H
#define MAUL_VFX_CONTEXT_H

#include "maul-vfx/base.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A context. Create it with mvfxCreateContext.
    typedef struct mvfxContext mvfxContext;

    // A range of work items, [startIndex, endIndex), run by the host on any
    // thread. The library passes taskContext through unchanged.
    typedef void mvfxTaskFunction(int32_t startIndex, int32_t endIndex, void* taskContext);

    // Hands the host itemCount items of work: the host splits [0, itemCount)
    // into ranges of at least minRange items (the last may be shorter),
    // runs task once on each range, on any thread and in any order, and
    // returns a handle that finishTask waits on. A host that ran every range
    // before returning may return NULL, and finishTask is then not called.
    // Results never depend on the split or the order: each item writes only
    // its own slots.
    typedef void* mvfxEnqueueTaskFunction(mvfxTaskFunction* task, int32_t itemCount,
                                          int32_t minRange, void* taskContext, void* userContext);

    // Waits until every range of the work behind userTask has run.
    //
    // The task hooks, and the tasks they run, call none of the context's
    // functions that take it mutable (it is stepping): such a call is
    // refused with mvfx_errorBusy and counted as misuse.
    typedef void mvfxFinishTaskFunction(void* userTask, void* userContext);

    // The named limits of a context. A creation past one is refused.
    typedef struct mvfxContextLimits
    {
        // Effects that exist at once, from 1 to 65,536.
        int32_t effects;
        // Instances that exist at once, from 1 to 65,536.
        int32_t instances;
        // Meshes that exist at once, from 1 to 65,536.
        int32_t meshes;
        // Grids that exist at once, from 1 to 65,536.
        int32_t grids;
        // Output layouts that exist at once, from 1 to 65,536.
        int32_t layouts;
        // The particles all its instances' emitters may hold at once (the
        // sum of their capacities at their quality levels), from 1 to 2^40;
        // 0 for no budget.
        int64_t particles;
    } mvfxContextLimits;

    // How a context is made. Build it with mvfxDefaultContextDef.
    typedef struct mvfxContextDef
    {
        uint32_t cookie;
        // MVFX_ABI_VERSION of the headers the program was built with; the
        // cookie and this stay the first two fields in every version, so
        // that mvfxCreateContext can refuse a def of another layout.
        uint32_t version;
        mvfxAllocator allocator;
        // The host's task system; both NULL, the default, runs every item
        // serially on the calling thread. One without the other is
        // invalid.
        mvfxEnqueueTaskFunction* enqueueTask;
        mvfxFinishTaskFunction* finishTask;
        // Passed to enqueueTask and finishTask as their userContext.
        void* userTaskContext;
        mvfxContextLimits limits;
    } mvfxContextDef;

// The cookie of a context def, which its default sets and
// mvfxCreateContext checks.
#define MVFX_CONTEXT_DEF_COOKIE 0x6D766378u

    /// Returns the default context def: the C library's allocator, serial
    /// work, 256 effects, 1,024 instances, 64 meshes, 64 grids and 64
    /// layouts.
    ///
    /// The default is built in the program from the headers it includes,
    /// so the version it stamps is the program's.
    ///
    /// @return The def, with a valid cookie and version.
    /// @par Thread safety
    /// Safe from any thread.
    static inline mvfxContextDef mvfxDefaultContextDef(void)
    {
#ifdef __cplusplus
        mvfxContextDef def = {};
#else
    mvfxContextDef def = {0};
#endif
        def.cookie = MVFX_CONTEXT_DEF_COOKIE;
        def.version = MVFX_ABI_VERSION;
        def.limits.effects = 256;
        def.limits.instances = 1024;
        def.limits.meshes = 64;
        def.limits.grids = 64;
        def.limits.layouts = 64;
        return def;
    }

    /// Creates a context.
    ///
    /// @param def         The def, from mvfxDefaultContextDef.
    /// @param contextOut  Receives the context; set to NULL on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument or a
    ///         def without its cookie; then `mvfx_errorVersion` for a def
    ///         stamped by headers of another ABI version than the library's,
    ///         before anything else of it is read; `mvfx_errorInvalid` for an
    ///         allocator with one function, a task system with one hook or a
    ///         limit out of its range; `mvfx_errorCapacity` when the
    ///         allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateContext(const mvfxContextDef* def,
                                                         mvfxContext** contextOut);

    /// Destroys a context and everything it owns.
    ///
    /// @param context  The context. NULL does nothing.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_API void mvfxDestroyContext(mvfxContext* context);

    /// Returns how many calls the context refused as misuse: invalid
    /// arguments against it. Stale ids are not misuse.
    ///
    /// @param context  The context.
    /// @return The count since the context was created.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_API uint64_t mvfxContext_GetMisuse(const mvfxContext* context);

    /// Returns the particles the context's instances hold against its
    /// budget: the sum of their emitters' capacities at their quality
    /// levels.
    ///
    /// @param context  The context.
    /// @return The particles held now.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_API int64_t mvfxContext_GetParticles(const mvfxContext* context);

#ifdef __cplusplus
}
#endif

#endif
