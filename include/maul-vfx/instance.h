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

    // What an instance does in the steps the host has it unseen
    // (mvfxInstance_SetVisible).
    typedef uint8_t mvfxOffscreen;
    enum
    {
        // It steps as when seen.
        mvfx_offscreenSimulate = 0,
        // It steps every offscreenInterval-th step, by the steps' summed
        // length (at most MVFX_MAX_STEP_SECONDS a step: a longer sum steps
        // what it has first), each instance in its own phase of the
        // interval from its seed so that instances unseen together do not
        // all step together. The same steps give the same particles, but
        // not those of stepping every time. Seen again, it steps what it
        // has with the next step.
        mvfx_offscreenThrottle = 1,
        // Its clock stops: it does not step, and seen again it goes on from
        // where it stopped.
        mvfx_offscreenPause = 2,
    };

// The most steps a throttled instance lets pass unseen.
#define MVFX_MAX_OFFSCREEN_INTERVAL 64

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
        // The quality level it asks for, from 0 (the full one) to its
        // effect's level count; it plays at the first level from this one
        // whose particles fit the context's budget.
        int32_t quality;
        // What it does while unseen, and for mvfx_offscreenThrottle every
        // how many steps it steps, from 2 to MVFX_MAX_OFFSCREEN_INTERVAL
        // (read only for that policy). It is made seen.
        mvfxOffscreen offscreen;
        int32_t offscreenInterval;
        // Whether its particles are simulated elsewhere (maul-vfx/plan.h):
        // it then holds none, and each step writes its emitters' plans, its
        // gases' and its water surfaces', whose fields the simulation
        // holds.
        bool planned;
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
    /// with no travel and no velocity, and ends every ribbon by the counter
    /// (mvfxInstance_BreakRibbons). How births along the way fall
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

    /// Tells an instance whether the host sees it, which its off-screen
    /// policy reads at each step. The library reports bounds
    /// (mvfxInstance_GetBounds) for the host to decide by.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param visible   Whether it is seen.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetVisible(mvfxContext* context,
                                                               mvfxInstanceId instance,
                                                               bool visible);

    /// Changes what an instance does while unseen. Steps it holds back stay
    /// held until it steps.
    ///
    /// @param context    The context that owns the instance.
    /// @param instance   The instance.
    /// @param offscreen  The policy.
    /// @param interval   For mvfx_offscreenThrottle, every how many steps it
    ///                   steps: from 2 to MVFX_MAX_OFFSCREEN_INTERVAL; read
    ///                   only for that policy.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL context or a policy or
    ///         interval out of range, which counts as misuse when the context
    ///         is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetOffscreen(mvfxContext* context,
                                                                 mvfxInstanceId instance,
                                                                 mvfxOffscreen offscreen,
                                                                 int32_t interval);

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
        // Optional: for an emitter of units held to the host's frames
        // (mvfx_attachHost), each unit's frame, from 0 to below the
        // emitter's attachFrameCapacity; NULL for frame 0.
        const int32_t* attachFrames;
    } mvfxSpawnRequest;

    /// Queues particles for an emitter of an instance: they are born at the
    /// start of the next step, before its other births, each taking a serial
    /// and room like any birth (refused, and counted, when the emitter is
    /// full); a planned instance's plan carries them (mvfxEmitterPlan's
    /// hostBirths). The values are copied: the arrays may be released at
    /// once.
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

    // A frame the host moves units on: its place in the world and its
    // orientation there, a unit quaternion (x, y, z, w).
    typedef struct mvfxAttachFrame
    {
        double position[3];
        float rotation[4];
    } mvfxAttachFrame;

    /// Sets the frames an emitter's units are held to (mvfx_attachHost) at
    /// the end of the next step: each unit's pinned points move from where
    /// its frame was at that step's start to where it is at its end over
    /// the substeps. The frames are copied; frames past count are no longer
    /// set, and units on them are held where they are.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param emitter   The emitter's index in the effect's def.
    /// @param frames    count frames; NULL only for a count of 0.
    /// @param count     From 0 to the emitter's attachFrameCapacity.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL context, an emitter
    ///         index out of range or not held to the host's frames, a count
    ///         out of range, NULL frames with a count, a position not finite
    ///         or over 2^52 in size, or a rotation not of length 1 within
    ///         1e-3, which counts as misuse when the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetAttachFrames(mvfxContext* context,
                                                                    mvfxInstanceId instance,
                                                                    int32_t emitter,
                                                                    const mvfxAttachFrame* frames,
                                                                    int32_t count);

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
    ///         instance; `mvfx_errorState` for a planned instance;
    ///         `mvfx_errorInvalid` for a NULL context or a value out of range,
    ///         which counts as misuse when the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_Prewarm(mvfxContext* context,
                                                            mvfxInstanceId instance, double seconds,
                                                            double stepSeconds, int32_t maxSteps,
                                                            double* remainingOut);

    /// Sets the colliders of an instance that its emitters collide with when
    /// their collision asks for the instance's (a character, a moving door),
    /// replacing the last set; the next step reads them. They are copied.
    ///
    /// @param context    The context that owns the instance.
    /// @param instance   The instance.
    /// @param colliders  From 0 to MVFX_MAX_COLLIDERS colliders; NULL when
    ///                   count is 0.
    /// @param count      How many.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL context, a count out
    ///         of range or a collider out of range, which counts as misuse
    ///         when the context is given (the colliders are then unchanged).
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetColliders(mvfxContext* context,
                                                                 mvfxInstanceId instance,
                                                                 const mvfxCollider* colliders,
                                                                 int32_t count);

    // A scene query: a moving particle, its place in the world and the
    // segment it covers in the emitter's lookahead at its velocity.
    typedef struct mvfxSceneQuery
    {
        // Its index among the emitter's particles now, and its serial.
        int32_t particle;
        uint32_t serial;
        double origin[3];
        float segment[3];
    } mvfxSceneQuery;

    // The host's answer to a scene query: a contact plane through point, the
    // side normal points to the one the particle keeps to; or none.
    typedef struct mvfxSceneAnswer
    {
        bool hit;
        // In the world: each coordinate at most 2^52 in size; normal finite,
        // not of length 0.
        double point[3];
        float normal[3];
    } mvfxSceneAnswer;

    /// Reads the scene queries an emitter of an instance wrote at the end
    /// of the last step, which the host answers before the next step. They
    /// stay until answered, the next step or a restart.
    ///
    /// @param context     The context that owns the instance.
    /// @param instance    The instance.
    /// @param emitter     The emitter's index in the effect's def.
    /// @param queriesOut  Receives the queries (NULL when there are none),
    ///                    the instance's own, read only.
    /// @param countOut    Receives how many.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL argument or an emitter
    ///         out of range, which counts as misuse when the context is
    ///         given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult
    mvfxInstance_GetSceneQueries(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                                 const mvfxSceneQuery** queriesOut, int32_t* countOut);

    /// Answers an emitter's scene queries, one answer each in their order:
    /// each particle takes its answer's plane, or none, for the steps
    /// after. The queries are then gone.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param emitter   The emitter's index in the effect's def.
    /// @param answers   The answers; NULL when count is 0.
    /// @param count     How many: the queries' count.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL context, an emitter
    ///         out of range, a count not the queries' or an answer out of
    ///         range, which counts as misuse when the context is given (no
    ///         answer is then taken).
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult
    mvfxInstance_AnswerSceneQueries(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                                    const mvfxSceneAnswer* answers, int32_t count);

// The most pixels a depth image has along a side.
#define MVFX_MAX_DEPTH_SIZE 16384

    // How a depth image keeps its pixels: device depth from 0 to 1.
    typedef uint8_t mvfxDepthFormat;
    enum
    {
        // A binary32 a pixel.
        mvfx_depthFloat32 = 0,
        // A uint16 a pixel, over 65,535.
        mvfx_depthUnorm16 = 1,
        // The low 24 bits of a uint32 a pixel, over 16,777,215 (the high 8,
        // a stencil's, not read).
        mvfx_depthUnorm24 = 2,
    };

    // A view's depth image, as a camera saw the world (often the frame
    // before, so a moving camera sees it a frame late). Its pixels are the
    // host's, borrowed: they stay valid while the image is set. Pixels in
    // the host's byte order, rows from the image's top. A planned
    // instance's image may have no pixels and no normals (NULL, their byte
    // counts and strides not read): its simulation reads them on the GPU.
    typedef struct mvfxDepthImage
    {
        const void* pixels;
        // The bytes pixels holds, at least (height - 1) stride + width a
        // pixel's size.
        int64_t byteCount;
        // From 1 to MVFX_MAX_DEPTH_SIZE pixels a side; a row's bytes, at
        // least a row of pixels.
        int32_t width;
        int32_t height;
        int32_t stride;
        mvfxDepthFormat format;
        // Whether the far plane is at 0 rather than at 1. A pixel at the far
        // plane (or not a number, or outside 0 to 1) holds no surface.
        bool reversed;
        // The view's projection, column-major: clip = projection (x, y, z, 1)
        // from the view's space; device depth z / w from 0 to 1, x / w from
        // -1 (the left) to 1, y / w from -1 (the bottom) to 1. Finite and
        // invertible: perspective, orthographic, reversed or not.
        float projection[16];
        // The view's place in the world (each coordinate at most 2^52 in
        // size) and its turn, a unit quaternion within 0.001: the world's
        // point is origin + turn(rotation, the view's).
        double origin[3];
        float rotation[4];
        // Optionally, the surface's normal at each pixel in the view's
        // space, three binary32 a pixel, with its bytes and its row's (as
        // for pixels); NULL to make it from the depth.
        const float* normals;
        int64_t normalByteCount;
        int32_t normalStride;
    } mvfxDepthImage;

    /// Sets the depth image an instance's emitters collide with when their
    /// collision asks for it, until it is set again or cleared (NULL). It
    /// is checked once, here; its pixels are borrowed, not copied, and must
    /// stay valid while it is set.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param image     The image, or NULL for none.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL context or an image
    ///         out of range (a size, a stride, a byte count short of its
    ///         rows, a format, a projection not finite or not invertible,
    ///         a place or a turn), which counts as misuse when the context
    ///         is given (the image set before is then kept).
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetDepthImage(mvfxContext* context,
                                                                  mvfxInstanceId instance,
                                                                  const mvfxDepthImage* image);

    /// Wakes an emitter's sleeping rigid particles (mvfxRigidDef.sleepSpeed)
    /// whose reach meets a sphere in its particles' space, now: for what the
    /// instance cannot see change (the host's scene answers, a depth
    /// image's pixels rewritten in place) or anything the host moved. The
    /// emitter's own colliders, the host's colliders and a depth image set
    /// again wake its bodies by themselves.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param emitter   The emitter's index in the effect's def; an emitter
    ///                  whose particles never sleep is left as it is.
    /// @param center    The sphere's center, finite.
    /// @param radius    Its radius, 0 or more; INFINITY wakes them all.
    /// @return `mvfx_success`; `mvfx_errorStale` for an instance id that
    ///         names no live instance; `mvfx_errorInvalid` for a NULL
    ///         argument, an emitter index out of range, a center not finite
    ///         or a radius below 0 or NaN, which counts as misuse when the
    ///         context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_WakeBodies(mvfxContext* context,
                                                               mvfxInstanceId instance,
                                                               int32_t emitter,
                                                               const float center[3], float radius);

    /// Reads the events an emitter of an instance wrote in the context's last
    /// step: those of every step the instance took in it (a liquid gas's
    /// substeps, mvfxInstance_GetPlanSteps; a step of the ticks it held off
    /// screen), one step's after another's, each in the order the step met
    /// them: deaths by age at its start, then as particles move
    /// (collisions; a unit's points' first contacts and tears after its
    /// substeps, point by point, each point's contact, its tear across, its
    /// tear down; a rigid body's first strike after its substeps, body by
    /// body) and are born (births, their collisions), then deaths by kills
    /// at its end. They stay until the context's next step or a restart; a
    /// finished instance's step clears them.
    ///
    /// @param context    The context that owns the instance.
    /// @param instance   The instance.
    /// @param emitter    The emitter's index in the effect's def.
    /// @param eventsOut  Receives the events (NULL when there are none), the
    ///                   instance's own, read only.
    /// @param countOut   Receives how many.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL argument or an emitter
    ///         out of range, which counts as misuse when the context is
    ///         given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetEvents(mvfxContext* context,
                                                              mvfxInstanceId instance,
                                                              int32_t emitter,
                                                              const mvfxEvent** eventsOut,
                                                              int32_t* countOut);

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
        // Particles alive now, and how many of them are rigid particles
        // asleep.
        int32_t live;
        int32_t asleep;
        // Particles it owed by now, from its rate and its bursts: spawned,
        // refused, and those whose life ended within the step they were
        // born in.
        uint64_t owed;
        // Particles its capacity refused.
        uint64_t refused;
        // Particles its kill rules (and attractors' kill radii) killed.
        uint64_t killed;
        // Scene queries its capacity of them dropped.
        uint64_t queriesDropped;
        // Events its capacity of them dropped.
        uint64_t eventsDropped;
        // Neighbor queries that reached their most neighbors.
        uint64_t neighborsCapped;
        // Particles whose contacts in a pass were more than the most.
        uint64_t contactsCapped;
        // Plexus links past the most an emitter keeps or a particle owns.
        uint64_t linksDropped;
        // Units' stretch constraints torn.
        uint64_t torn;
        // Points of units with more self-collision candidates than
        // selfContacts, once a step each.
        uint64_t selfContactsCapped;
        // Rigid particles' contacts past MVFX_MAX_RIGID_CONTACTS in a
        // substep, the shallowest dropped.
        uint64_t rigidContactsCapped;
        // Rigid hulls' and boxes' nearest-point and depth searches (GJK
        // and EPA) stopped at their most steps: the pair is kept as
        // touching, its depth then at most the true one.
        uint64_t rigidSearchesCapped;
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

    /// Reads the quality level an instance plays at.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param levelOut  Receives its level: 0 the full one.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL argument, which counts
    ///         as misuse when the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetQuality(mvfxContext* context,
                                                               mvfxInstanceId instance,
                                                               int32_t* levelOut);

    /// Ends an emitter's current ribbon, so its next births begin another:
    /// advances its ribbon counter (mvfx_ribbonOne), as a teleport does for
    /// every emitter drawn as ribbons. Emitters not drawn as ribbons are
    /// left alone.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param emitter   The emitter's index in the effect's def, or -1 for
    ///                  all.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL context or an emitter
    ///         out of range, which counts as misuse when the context is
    ///         given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_BreakRibbons(mvfxContext* context,
                                                                 mvfxInstanceId instance,
                                                                 int32_t emitter);

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

    // What a gas is now: its cells and their edge, and its box's center
    // and orientation (x, y, z, w) in the world, and the scale of its box
    // (the instance's for a gas in the instance's space, else 1): a cell's
    // edge in the world is its def's times it.
    typedef struct mvfxGasInfo
    {
        int32_t size[3];
        float cellSize;
        double center[3];
        float rotation[4];
        float scale;
    } mvfxGasInfo;

    /// Reads what a gas of an instance is now.
    ///
    /// @param context  The context that owns the instance.
    /// @param instance The instance.
    /// @param gas      The gas's index in its effect's def.
    /// @param infoOut  Receives the gas's info; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL argument or a gas
    ///         index out of range, which counts as misuse when the context is
    ///         given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetGasInfo(const mvfxContext* context,
                                                               mvfxInstanceId instance, int32_t gas,
                                                               mvfxGasInfo* infoOut);

    /// Copies a field of a gas of an instance at its cells' centers, X
    /// fastest, then Y, then Z; a velocity's component the mean of the two
    /// faces' about the center.
    ///
    /// @param context  The context that owns the instance.
    /// @param instance The instance.
    /// @param gas      The gas's index in its effect's def.
    /// @param field    The field.
    /// @param out      Receives count values.
    /// @param count    How many: the gas's cells.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL argument, a gas index
    ///         or field out of range (a surface's only field its distance,
    ///         which no other gas has) or a count not the gas's cells,
    ///         which counts as misuse when the context is given;
    ///         `mvfx_errorState` for a planned instance, whose gases'
    ///         fields its simulation holds.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ReadGas(const mvfxContext* context,
                                                            mvfxInstanceId instance, int32_t gas,
                                                            mvfxGasField field, float* out,
                                                            int64_t count);

    /// Pushes a water surface of an instance at the start of the instance's
    /// next step, after the pushes already given for it (at most
    /// MVFX_MAX_WATER_PUSHES in all); each push's cells sink by depth times
    /// its falloff at their centers, and what they sank raises every cell
    /// evenly (the water a body displaces lifts the level: the surface's
    /// volume stays).
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param water     The surface's index in its effect's def.
    /// @param pushes    count pushes; NULL for 0.
    /// @param count     How many, from 0.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorCapacity` for more pushes than the step
    ///         holds, with none of these taken; `mvfx_errorInvalid` for a
    ///         NULL context or pushes with a count, a surface index out of
    ///         range, a negative count or a push out of range, which counts
    ///         as misuse when the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_PushWater(mvfxContext* context,
                                                              mvfxInstanceId instance,
                                                              int32_t water,
                                                              const mvfxWaterPush* pushes,
                                                              int32_t count);

    // What a water surface is now: its cells and their edge, and its grid's
    // center and orientation (x, y, z, w) in the world, the frame whose Y
    // its heights are along, and its scale (as mvfxGasInfo's).
    typedef struct mvfxWaterInfo
    {
        int32_t size[2];
        float cellSize;
        double center[3];
        float rotation[4];
        float scale;
        // Since the instance was made or restarted: the steps whose time
        // outran MVFX_MAX_WATER_SUBSTEPS substeps (the water slowed), and
        // shallow water's faces whose velocity the substep's bound, half a
        // cell, held (a fall off a cliff), summed over substeps. A scene
        // that leans on either moves slower than its water would. And the
        // volume particles poured into shallow water (mvfxEmitterDef.water),
        // in the world's lengths cubed at each pour, which no host push
        // adds. A planned instance counts its steps slowed from its plans,
        // and the others as its simulation reports them
        // (mvfxInstance_SetWaterCounts).
        uint64_t stepsSlowed;
        uint64_t facesHeld;
        double volumePoured;
    } mvfxWaterInfo;

    /// Reads what a water surface of an instance is now.
    ///
    /// @param context  The context that owns the instance.
    /// @param instance The instance.
    /// @param water    The surface's index in its effect's def.
    /// @param infoOut  Receives the surface's info; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL argument or a surface
    ///         index out of range, which counts as misuse when the context is
    ///         given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetWaterInfo(const mvfxContext* context,
                                                                 mvfxInstanceId instance,
                                                                 int32_t water,
                                                                 mvfxWaterInfo* infoOut);

#ifdef __cplusplus
}
#endif

#endif
