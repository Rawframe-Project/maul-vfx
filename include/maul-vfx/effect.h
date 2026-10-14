// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Effects: what an effect's emitters spawn and how their particles live,
// stated once in a def and compiled when the effect is created. An effect
// never changes after; instances of it play it.

#ifndef MAUL_VFX_EFFECT_H
#define MAUL_VFX_EFFECT_H

#include "maul-vfx/context.h"
#include "maul-vfx/curve.h"
#include "maul-vfx/shape.h"
#include "maul-vfx/value.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The ticks in a second: every clock counts ticks of 2^-20 seconds, so
// that spans of time add exactly however they were split into steps.
#define MVFX_TICKS_PER_SECOND 1048576

// The most emitters an effect holds.
#define MVFX_MAX_EMITTERS 64

// The most particles an emitter holds alive at once.
#define MVFX_MAX_EMITTER_CAPACITY (1 << 24)

// The highest spawn rate an emitter may state, in particles a second.
#define MVFX_MAX_SPAWN_RATE 1e7f

// The longest life a particle may be given, in seconds; also the longest
// delay, duration, burst time and burst interval.
#define MVFX_MAX_LIFETIME 1e6f

// The shortest loop an emitter may have, and the shortest interval of a
// repeating burst, in seconds (1,024 ticks).
#define MVFX_MIN_DURATION 0x1p-10f

// The most bursts an emitter holds.
#define MVFX_MAX_BURSTS 16

    // Names an effect of a context: a 1-based slot, 0 for the null id, and
    // a generation that tells a live effect from earlier occupants of its
    // slot.
    typedef struct mvfxEffectId
    {
        uint32_t index1;
        uint32_t generation;
    } mvfxEffectId;

    // A burst: particles born at once at a time of each loop, repeated a
    // number of times at an interval.
    typedef struct mvfxBurst
    {
        // When it first fires, in seconds from the loop's start, at most
        // MVFX_MAX_LIFETIME; a time at or past the loop's end never fires.
        float time;
        // The particles each firing spawns, drawn uniformly from [minCount,
        // maxCount] per firing: 0 <= minCount <= maxCount <=
        // MVFX_MAX_EMITTER_CAPACITY.
        int32_t minCount;
        int32_t maxCount;
        // How many times it fires each loop, 0 for until the loop ends (or
        // forever without a duration).
        int32_t cycles;
        // The seconds between firings when cycles is not 1: from
        // MVFX_MIN_DURATION to MVFX_MAX_LIFETIME.
        float interval;
        // The chance each firing spawns at all, from 0 to 1.
        float probability;
    } mvfxBurst;

    // Where an emitter's particles live.
    typedef uint8_t mvfxSimulationSpace;
    enum
    {
        // In the world: they stay where they were born as the instance moves.
        mvfx_spaceWorld = 0,
        // In the instance's space: they move, turn and scale with it.
        mvfx_spaceLocal = 1,
    };

// The most particles an emitter may spawn a unit of distance.
#define MVFX_MAX_DISTANCE_RATE 1e4f

    // How an emitter is made. Build it with mvfxDefaultEmitterDef.
    //
    // Times are rounded once to ticks of 2^-20 seconds when the effect is
    // created, so a repeating burst never drifts. The particles an emitter's
    // rate and its repeating bursts spawn a second, at most, may not exceed
    // MVFX_MAX_SPAWN_RATE each.
    typedef struct mvfxEmitterDef
    {
        uint32_t cookie;
        // The particles it holds alive at once, from 1 to
        // MVFX_MAX_EMITTER_CAPACITY; a spawn past them is refused and
        // counted.
        int32_t capacity;
        // The particles it spawns a second, from 0 to MVFX_MAX_SPAWN_RATE.
        float rate;
        // The rate's shape over each loop: no keys keeps it constant;
        // otherwise its value at a loop's normalized age, from 0 to 1, times
        // rate. Its keys' times lie in [0, 1], its baked values are never
        // below 0, and rate times its highest is at most
        // MVFX_MAX_SPAWN_RATE. It needs a duration. The effect bakes it: its
        // keys may be released once the effect is created.
        mvfxCurve rateCurve;
        // Its bursts, at most MVFX_MAX_BURSTS; NULL when burstCount is 0.
        const mvfxBurst* bursts;
        int32_t burstCount;
        // Seconds from the instance's start before it spawns, from 0 to
        // MVFX_MAX_LIFETIME.
        float startDelay;
        // The seconds of one loop, from MVFX_MIN_DURATION to
        // MVFX_MAX_LIFETIME; 0 spawns without end and never loops.
        float duration;
        // With a duration, how many loops it spawns before it stops, 0 for
        // forever; at least 0.
        int32_t loops;
        // Where its particles live.
        mvfxSimulationSpace space;
        // The particles it spawns a unit of distance the instance travels,
        // from 0 to MVFX_MAX_DISTANCE_RATE, along the way while it spawns at
        // all; a step owing more than its capacity (a long jump) refuses the
        // rest.
        float ratePerDistance;
        // The share of the instance's velocity its particles set off with,
        // from -10 to 10; only in world space.
        float inheritVelocity;
        // How many particles the host may queue for it between steps
        // (mvfxInstance_Spawn), from 0 to its capacity, reserved in each
        // instance.
        int32_t hostSpawnCapacity;
        // Each particle's life in seconds, required: a range above 0 and at
        // most MVFX_MAX_LIFETIME; a curve's or a parameter's lives are held
        // to one tick at least and MVFX_MAX_LIFETIME at most.
        mvfxScalarSource lifetime;
        // Where its particles are born.
        mvfxShape shape;
        // How they set off.
        mvfxVelocity velocity;
        // The optional initial values, each stored only when its source is
        // set: size per axis (X, Y, Z), linear color (red, green, blue,
        // alpha), a sprite's angle in radians and spin in radians a second
        // (either sets both, the other 0).
        mvfxVectorSource size;
        mvfxVectorSource color;
        mvfxScalarSource angle;
        mvfxScalarSource spin;
        // A mesh particle's orientation (a quaternion) and angular velocity
        // in radians a second (either sets both, the other identity or 0);
        // orientation reads Euler angles in radians in the euler mode.
        mvfxOrientationMode orientationMode;
        mvfxVectorSource orientation;
        mvfxVectorSource angularVelocity;
        // The flipbook frame it starts at.
        mvfxFrameStart frame;
        // Its custom attributes, at most MVFX_MAX_CUSTOM_ATTRIBUTES, each of
        // one to four components; NULL when customCount is 0.
        const mvfxCustomAttribute* custom;
        int32_t customCount;
    } mvfxEmitterDef;

// The cookie of an emitter def, which its default sets and the effect's
// creation checks.
#define MVFX_EMITTER_DEF_COOKIE 0x6D766565u

    /// Returns the default emitter def: 1,024 particles in world space, 64 a
    /// second without end from the instance's start, none by distance, no
    /// bursts, each living one second, born at the emitter's origin at rest (a point shape,
    /// unturned, whole arcs, along the shape's normal at a speed of 0).
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
        // The defaults of its instances' parameters, four floats each, finite;
        // at most MVFX_MAX_PARAMETERS, NULL when parameterCount is 0.
        const float* parameters;
        int32_t parameterCount;
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
    ///         or an emitter def without its cookie, a field out of its
    ///         range or an invalid curve, which counts as misuse when the
    ///         context is given;
    ///         `mvfx_errorStale` for a mesh shape's mesh that names no live
    ///         mesh, `mvfx_errorInvalid` too for a mesh without the elements
    ///         its mode needs; `mvfx_errorLimit` when the context holds its
    ///         limit of effects; `mvfx_errorCapacity` when the allocator
    ///         fails.
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
    ///         effect (the null id included); `mvfx_errorBusy` while an
    ///         instance plays it; `mvfx_errorInvalid` for a NULL context.
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
