// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Effects: what an effect's emitters spawn and how their particles live,
// stated once in a def and compiled when the effect is created. An effect
// never changes after; instances of it play it.

#ifndef MAUL_VFX_EFFECT_H
#define MAUL_VFX_EFFECT_H

#include "maul-vfx/context.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The most emitters an effect holds.
#define MVFX_MAX_EMITTERS 64

// The most particles an emitter holds alive at once.
#define MVFX_MAX_EMITTER_CAPACITY (1 << 24)

// The highest spawn rate an emitter may state, in particles a second.
#define MVFX_MAX_SPAWN_RATE 1e7f

// The longest life a particle may be given, in seconds.
#define MVFX_MAX_LIFETIME 1e6f

    // Names an effect of a context: a 1-based slot, 0 for the null id, and
    // a generation that tells a live effect from earlier occupants of its
    // slot.
    typedef struct mvfxEffectId
    {
        uint32_t index1;
        uint32_t generation;
    } mvfxEffectId;

    // A value drawn uniformly from [min, max] for each particle; min equal to
    // max gives that value.
    typedef struct mvfxRange
    {
        float min;
        float max;
    } mvfxRange;

    // How an emitter is made. Build it with mvfxDefaultEmitterDef.
    typedef struct mvfxEmitterDef
    {
        uint32_t cookie;
        // The particles it holds alive at once, from 1 to
        // MVFX_MAX_EMITTER_CAPACITY; a spawn past them is refused and
        // counted.
        int32_t capacity;
        // The particles it spawns a second, from 0 to MVFX_MAX_SPAWN_RATE.
        float rate;
        // Each particle's life in seconds: above 0, at most
        // MVFX_MAX_LIFETIME, min at most max.
        mvfxRange lifetime;
    } mvfxEmitterDef;

// The cookie of an emitter def, which its default sets and the effect's
// creation checks.
#define MVFX_EMITTER_DEF_COOKIE 0x6D766565u

    /// Returns the default emitter def: 1,024 particles, 64 a second, each
    /// living one second, born at the emitter's origin.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_API mvfxEmitterDef mvfxDefaultEmitterDef(void);

    // How an effect is made. Build it with mvfxDefaultEffectDef.
    typedef struct mvfxEffectDef
    {
        uint32_t cookie;
        // Its emitters, from 1 to MVFX_MAX_EMITTERS, each from
        // mvfxDefaultEmitterDef. The effect copies what it needs: the array
        // may be released once the effect is created.
        const mvfxEmitterDef* emitters;
        int32_t emitterCount;
    } mvfxEffectDef;

// The cookie of an effect def, which its default sets and
// mvfxCreateEffect checks.
#define MVFX_EFFECT_DEF_COOKIE 0x6D766566u

    /// Returns the default effect def, with no emitters yet.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_API mvfxEffectDef mvfxDefaultEffectDef(void);

    /// Creates an effect from a def: checks every field of it and of its
    /// emitters, then compiles it, allocating its compiled form in one block
    /// from the context's allocator.
    ///
    /// @param context    The context that owns the effect.
    /// @param def        The def, from mvfxDefaultEffectDef.
    /// @param effectOut  Receives the effect's id; the null id on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def
    ///         or an emitter def without its cookie, or a field out of its
    ///         range, which counts as misuse when the context is given;
    ///         `mvfx_errorLimit` when the context holds its limit of
    ///         effects; `mvfx_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateEffect(mvfxContext* context,
                                                        const mvfxEffectDef* def,
                                                        mvfxEffectId* effectOut);

    /// Destroys an effect and returns its memory to the context's allocator.
    ///
    /// @param context  The context that owns the effect.
    /// @param effect   The effect.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         effect (the null id included); `mvfx_errorInvalid` for a NULL
    ///         context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxDestroyEffect(mvfxContext* context, mvfxEffectId effect);

    /// Returns whether an id names a live effect of the context. A stale id
    /// is not misuse.
    ///
    /// @param context  The context.
    /// @param effect   Any id.
    /// @return Whether the effect is alive.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_API bool mvfxEffect_IsValid(const mvfxContext* context, mvfxEffectId effect);

#ifdef __cplusplus
}
#endif

#endif
