// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Instances: one playing of an effect, at a place, with a seed. A
// context's step advances every instance it owns.

#ifndef MAUL_VFX_INSTANCE_H
#define MAUL_VFX_INSTANCE_H

#include "maul-vfx/effect.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The longest step a context takes in one call, in seconds: a host
// catching up a longer span steps it in parts, which reach the same state.
#define MVFX_MAX_STEP_SECONDS 1.0

    // Names an instance of a context: a 1-based slot, 0 for the null id,
    // and a generation that tells a live instance from earlier occupants of
    // its slot.
    typedef struct mvfxInstanceId
    {
        uint32_t index1;
        uint32_t generation;
    } mvfxInstanceId;

    // How an instance is made. Build it with mvfxDefaultInstanceDef.
    typedef struct mvfxInstanceDef
    {
        uint32_t cookie;
        // The effect it plays, which may not be destroyed while it does.
        mvfxEffectId effect;
        // Where it plays, in the host's world, in double precision so that
        // precision does not depend on the distance from the world's origin;
        // each value finite and at most 2^52 in size.
        double origin[3];
        // How it is turned: a unit quaternion (x, y, z, w), its length within
        // 0.001 of 1.
        float rotation[4];
        // How large it plays, from MVFX_MIN_SCALE to MVFX_MAX_SCALE: shapes,
        // their offsets and initial velocities scale by it.
        float scale;
        // The seed of its random numbers; 0 takes one from the order in which
        // the context created its instances, so the same calls give the same
        // particles.
        uint32_t seed;
    } mvfxInstanceDef;

// The least and greatest scale of an instance.
#define MVFX_MIN_SCALE 1e-6f
#define MVFX_MAX_SCALE 1e6f

// How far an instance may move from a world-space emitter's anchor
// before the anchor moves to it, in world units.
#define MVFX_REBASE_DISTANCE 1024.0

// The cookie of an instance def, which its default sets and
// mvfxCreateInstance checks.
#define MVFX_INSTANCE_DEF_COOKIE 0x6D766969u

    /// Returns the default instance def: no effect yet, at the world's origin,
    /// unturned and at scale 1, seeded by its order of creation.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_API mvfxInstanceDef mvfxDefaultInstanceDef(void);

    /// Creates an instance of an effect, allocating every particle it can
    /// hold in one block from the context's allocator. Its clock starts at 0
    /// and its emitters start spawning with the next step.
    ///
    /// @param context      The context that owns the effect and will own the
    ///                     instance.
    /// @param def          The def, from mvfxDefaultInstanceDef.
    /// @param instanceOut  Receives the instance's id; the null id on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def
    ///         without its cookie, or an origin, rotation or scale out of
    ///         range, which counts as
    ///         misuse when the context is given; `mvfx_errorStale` for an
    ///         effect id that names no live effect; `mvfx_errorLimit` when the
    ///         context holds its limit of instances; `mvfx_errorCapacity` when
    ///         the allocator fails or the block's size does not fit.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateInstance(mvfxContext* context,
                                                          const mvfxInstanceDef* def,
                                                          mvfxInstanceId* instanceOut);

    /// Destroys an instance and its particles.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance (the null id included); `mvfx_errorInvalid` for a NULL
    ///         context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxDestroyInstance(mvfxContext* context,
                                                           mvfxInstanceId instance);

    /// Returns whether an id names a live instance of the context. A stale
    /// id is not misuse.
    ///
    /// @param context   The context.
    /// @param instance  Any id.
    /// @return Whether the instance is alive.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_API bool mvfxInstance_IsValid(const mvfxContext* context, mvfxInstanceId instance);

    /// Sets where an instance is at the end of the next step. Over that step
    /// it moves in a line from where it was (the rotation by normalized
    /// interpolation, the scale linearly), and births within it take its
    /// place at their ticks; its travel feeds spawning by distance and its
    /// velocity what emitters inherit. A teleport moves it at once instead,
    /// with no travel and no velocity. How births along the way fall
    /// depends on how often the host moves it, which is the host's to keep
    /// the same.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param origin    Its place in the world, each value finite and at most
    ///                  2^52 in size.
    /// @param rotation  A unit quaternion (x, y, z, w), within 0.001 of unit
    ///                  length.
    /// @param scale     From MVFX_MIN_SCALE to MVFX_MAX_SCALE.
    /// @param teleport  Whether it jumps there.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL argument or a value out
    ///         of range, which counts as misuse when the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetTransform(mvfxContext* context,
                                                                 mvfxInstanceId instance,
                                                                 const double origin[3],
                                                                 const float rotation[4],
                                                                 float scale, bool teleport);

    // Particles the host adds to an emitter of an instance. Positions are
    // in the emitter's space: the world's for a world-space emitter, the
    // instance's for a local one. Values not given are drawn from the
    // emitter's sources, a velocity in the instance's space at the given
    // place (so the shape's normal and its center are away from the
    // instance's origin).
    typedef struct mvfxSpawnRequest
    {
        // From 1 to the room the emitter's host queue has left.
        int32_t count;
        // x, y and z for each particle, finite and at most 2^52 in size.
        const double* positions;
        // Optional: x, y and z for each, finite and at most MVFX_MAX_EXTENT in
        // size, in the emitter's space; NULL to draw them.
        const float* velocities;
        // Optional: red, green, blue and alpha for each, finite and at most
        // MVFX_MAX_VALUE in size, for an emitter that stores color.
        const float* colors;
        // Optional: x, y and z for each likewise, for an emitter that stores
        // size.
        const float* sizes;
    } mvfxSpawnRequest;

    /// Queues particles for an emitter of an instance: they are born at the
    /// start of the next step, before its other births, each taking a serial
    /// and room like any birth (refused, and counted, when the emitter is
    /// full). The values are copied: the arrays may be released at once.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param emitter   The emitter's index in the effect's def.
    /// @param request   The particles.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL argument, an emitter
    ///         index out of range, a count below 1, a value out of range, or
    ///         a color or size for an emitter that does not store it, which
    ///         counts as misuse when the context is given;
    ///         `mvfx_errorCapacity` when the emitter's host queue lacks the
    ///         room (none of the particles is queued).
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_Spawn(mvfxContext* context,
                                                          mvfxInstanceId instance, int32_t emitter,
                                                          const mvfxSpawnRequest* request);

    /// Advances one instance alone toward a span, in steps of a given length
    /// (the last shorter), at most a number of steps a call, so a host can
    /// spread a long prewarm over frames: it calls again with the span left
    /// until none is. The same calls reach the same state. Spans and steps
    /// are rounded once to ticks.
    ///
    /// @param context       The context that owns the instance.
    /// @param instance      The instance.
    /// @param seconds       The span left, from 0 to 1e9.
    /// @param stepSeconds   Each step's length, above 0 and at most
    ///                      MVFX_MAX_STEP_SECONDS, one tick at least.
    /// @param maxSteps      At least 1.
    /// @param remainingOut  Receives the span left, exactly as ticks; may be
    ///                      NULL.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL context or a value out
    ///         of range, which counts as misuse when the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_Prewarm(mvfxContext* context,
                                                            mvfxInstanceId instance, double seconds,
                                                            double stepSeconds, int32_t maxSteps,
                                                            double* remainingOut);

    /// Draws four random words for a host module: pcg4d (Jarzynski and
    /// Olano, "Hash Functions for GPU Rendering", JCGT 2020) of the
    /// module's seed, a particle's serial, a stream number the module picks
    /// and the step's end tick, so the same call in the same step gives the
    /// same words however the work is split.
    ///
    /// @param call    The module's call.
    /// @param serial  A particle's serial (or any number).
    /// @param stream  The module's own number for this draw.
    /// @param out     Receives the four words.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_API void mvfxModule_Random(const mvfxModuleCall* call, uint32_t serial, uint32_t stream,
                                    uint32_t out[4]);

    /// Makes an emitter of an instance spawn from another mesh, as each
    /// character's skinned mesh feeds its own instance. The mesh may not be
    /// destroyed while the instance is bound to it.
    ///
    /// @param context   The context that owns the instance and the mesh.
    /// @param instance  The instance.
    /// @param emitter   The index, in the effect's def, of an emitter with a
    ///                  mesh shape.
    /// @param mesh      The mesh, with the triangles or tetrahedra the
    ///                  emitter's mesh mode needs.
    /// @return `mvfx_success`; `mvfx_errorStale` for an instance or a mesh id
    ///         that names nothing live; `mvfx_errorInvalid` for a NULL
    ///         context, an emitter index out of range or without a mesh shape,
    ///         or a mesh without the elements its mode needs, which counts as
    ///         misuse.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetEmitterMesh(mvfxContext* context,
                                                                   mvfxInstanceId instance,
                                                                   int32_t emitter,
                                                                   mvfxMeshId mesh);

    /// Sets a parameter of an instance; births from the next step read it.
    /// A restart keeps it.
    ///
    /// @param context    The context that owns the instance.
    /// @param instance   The instance.
    /// @param parameter  An index below the effect's count of parameters.
    /// @param value      Four finite floats.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL argument, an index out
    ///         of range or a value not finite, which counts as misuse when
    ///         the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetParameter(mvfxContext* context,
                                                                 mvfxInstanceId instance,
                                                                 int32_t parameter,
                                                                 const float value[4]);

    /// Reads a parameter of an instance.
    ///
    /// @param context    The context that owns the instance.
    /// @param instance   The instance.
    /// @param parameter  An index below the effect's count of parameters.
    /// @param valueOut   Receives four floats; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL argument or an index
    ///         out of range, which counts as misuse when the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetParameter(mvfxContext* context,
                                                                 mvfxInstanceId instance,
                                                                 int32_t parameter,
                                                                 float valueOut[4]);

    // Where an emitter of an instance stands.
    typedef uint8_t mvfxEmitterStatus;
    enum
    {
        // Its start delay has not passed.
        mvfx_emitterDelayed = 0,
        // It will spawn again.
        mvfx_emitterActive = 1,
        // It will not spawn again, and particles it spawned live on.
        mvfx_emitterStopping = 2,
        // It will not spawn again and holds no particles.
        mvfx_emitterStopped = 3,
    };

    // What an emitter of an instance has done since the instance was made or
    // restarted.
    typedef struct mvfxEmitterStats
    {
        mvfxEmitterStatus status;
        // Particles alive now.
        int32_t live;
        // Particles it owed by now, from its rate and its bursts: spawned,
        // refused, and those whose life ended within the step they were
        // born in.
        uint64_t owed;
        // Particles its capacity refused.
        uint64_t refused;
        // Particles its kill rules (and attractors' kill radii) killed.
        uint64_t killed;
    } mvfxEmitterStats;

    /// Reads what an emitter of an instance has done.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param emitter   The emitter's index in the effect's def.
    /// @param statsOut  Receives the counts; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorStale` for an instance id that names
    ///         no live instance; `mvfx_errorInvalid` for a NULL argument or an
    ///         emitter index out of range, which counts as misuse when the
    ///         context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetEmitterStats(mvfxContext* context,
                                                                    mvfxInstanceId instance,
                                                                    int32_t emitter,
                                                                    mvfxEmitterStats* statsOut);

    /// Returns whether every emitter of an instance is stopped: it will
    /// spawn nothing more and holds no particles, and stepping it costs
    /// nothing. A stale id is not misuse.
    ///
    /// @param context   The context.
    /// @param instance  Any id.
    /// @return Whether the instance is alive and finished.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_API bool mvfxInstance_IsFinished(const mvfxContext* context, mvfxInstanceId instance);

    // How mvfxInstance_Stop treats an instance's particles.
    typedef uint8_t mvfxStopMode;
    enum
    {
        // They live out their lives.
        mvfx_stopEmitting = 0,
        // They are removed at once.
        mvfx_stopAndClear = 1,
    };

    /// Stops every emitter of an instance from spawning past its clock.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param mode      What becomes of its particles.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL context or an unknown
    ///         mode, which counts as misuse when the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_Stop(mvfxContext* context,
                                                         mvfxInstanceId instance,
                                                         mvfxStopMode mode);

    /// Plays an instance again from its start: its particles are removed,
    /// its clock and counts return to 0 and a stop is lifted. Its seed is
    /// kept, so the same steps play the same particles again.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_Restart(mvfxContext* context,
                                                            mvfxInstanceId instance);

    /// Advances every instance of the context by a span of time: each
    /// emitter spawns what it owes by the new time, each particle ages from
    /// its birth, and particles whose life has ended are removed. The span is
    /// rounded once to ticks (MVFX_TICKS_PER_SECOND), so spans whose ticks
    /// sum to the same clock reach the same state.
    ///
    /// @param context  The context.
    /// @param seconds  The span, from 0 to MVFX_MAX_STEP_SECONDS.
    /// @return `mvfx_success`, or `mvfx_errorInvalid` for a NULL context or a
    ///         span out of range, which counts as misuse when the context is
    ///         given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time. Its
    /// work runs on the host's task hooks when the context has them.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxContext_Step(mvfxContext* context, double seconds);

    /// Advances every instance of the context by a number of ticks, as
    /// mvfxContext_Step does without the rounding.
    ///
    /// @param context  The context.
    /// @param ticks    The span in ticks, from 0 to MVFX_MAX_STEP_SECONDS
    ///                 seconds' worth.
    /// @return `mvfx_success`, or `mvfx_errorInvalid` for a NULL context or a
    ///         span out of range, which counts as misuse when the context is
    ///         given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time. Its
    /// work runs on the host's task hooks when the context has them.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxContext_StepTicks(mvfxContext* context, int64_t ticks);

#ifdef __cplusplus
}
#endif

#endif
