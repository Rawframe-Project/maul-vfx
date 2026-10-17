// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Plans: an instance whose particles are simulated elsewhere, on the GPU
// part or the host's own GPU path. A planned instance holds no particles;
// each step advances its clocks, counters, cursors and anchors and
// writes each emitter's plan: the births the step owes, with their
// serials, the ticks they are born at and the ticks they die at, all
// from the CPU path's own arithmetic, so a simulation elsewhere numbers,
// draws and ends its particles as the CPU path does. Which births have
// room is for the simulation to decide: only it knows the particles it
// holds.

#ifndef MAUL_VFX_PLAN_H
#define MAUL_VFX_PLAN_H

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A birth a step owes an emitter: 32 bytes, as a GPU buffer holds it.
    typedef struct mvfxPlannedBirth
    {
        // Its serial, the count of the emitter's births owed before it.
        uint32_t serialLow;
        uint32_t serialHigh;
        // The tick it is born at, from the plan's start.
        uint32_t tick;
        // The tick it dies at, from the plan's start, in two words: the
        // first tick at which its age times its inverse life reaches 1.
        uint32_t deathLow;
        uint32_t deathHigh;
        // One over its lifetime in seconds.
        float inverseLife;
        // Its place in its batch, the births one source owes at its tick
        // (a burst's firing, or the rate's), and the batch's size: what a
        // shape's spread of a burst reads.
        uint32_t batchIndex;
        uint32_t batchCount;
    } mvfxPlannedBirth;

    // Where an instance is: its place in the world, its turn (a unit
    // quaternion) and its scale.
    typedef struct mvfxPlanPlace
    {
        double origin[3];
        float rotation[4];
        float scale;
    } mvfxPlanPlace;

    // A force of an emitter as resolved for a plan's step, in the
    // particles' space: its def's meaning (mvfxForce) with lengths scaled,
    // points and vectors mapped, its strength taken from its parameter.
    typedef struct mvfxPlannedForce
    {
        mvfxForceKind kind;
        mvfxFalloffKind falloff;
        mvfxFieldMode fieldMode;
        // The constant's acceleration, the wind's velocity or the vortex's
        // axis; the attractor's or the vortex's point.
        float vector[3];
        float point[3];
        float strength;
        float softening;
        float killRadius;
        float pull;
        // The falloff's center, axis, radius, the cosine of its cone's
        // half angle and its exponent.
        float center[3];
        float axis[3];
        float radius;
        float cosine;
        float exponent;
        float tightness;
        // A field's map from its own space into the particles': a turn (a
        // unit quaternion) after a scale; a place maps back by their
        // inverse.
        float rotation[4];
        float sigma;
        // A turbulence's noise for the plan: its octaves, its seed (an
        // octave's is seed + octave * 0x9E3779B9), each octave's weight
        // over the last's, its scroll a second in its own space; for each
        // octave, its frequency and the lattice coordinate of the
        // particles' space's origin at the plan's start, a 32-bit cell
        // (wrapping) and the binary32 fraction past it. A place's
        // coordinate adds its own, mapped back, times the frequency.
        int32_t octaves;
        uint32_t seed;
        float gain;
        float scroll[3];
        float frequency[MVFX_MAX_OCTAVES];
        uint32_t cell[MVFX_MAX_OCTAVES][3];
        float fraction[MVFX_MAX_OCTAVES][3];
        // A vector field's box: the particles' origin mapped back into
        // its own space, less the box's center; its size, its turn, and
        // the border its grid reads past it.
        float boxOrigin[3];
        float boxSize[3];
        float boxRotation[4];
        mvfxGridBorder border;
    } mvfxPlannedForce;

    // A collider of an emitter as resolved for a plan's step, in the
    // particles' space and grown by their radius: its def's meaning
    // (mvfxCollider).
    typedef struct mvfxPlannedCollider
    {
        mvfxColliderKind kind;
        bool inside;
        float center[3];
        float normal[3];
        float radius;
        float halfExtents[3];
        float rotation[4];
    } mvfxPlannedCollider;

    // An emitter's plan for the context's last step.
    typedef struct mvfxEmitterPlan
    {
        // The particles the emitter holds at most, at the instance's
        // quality level (0 for an emitter left out), and the instance's
        // seed, the first word of every draw.
        int32_t capacity;
        uint32_t seed;
        // The instance at the plan's start and end, a birth's place
        // between them by its tick, and its velocity over the last step.
        mvfxPlanPlace from;
        mvfxPlanPlace to;
        float velocity[3];
        // The first tick the plan covers and how many: every step the
        // instance took in the context's last step (none when it was
        // paused or held).
        int64_t start;
        int64_t ticks;
        // A world-space emitter's anchor before the plan and after it:
        // particles are offsets from it, and those held move by the
        // difference, rounded to binary32, when it moves.
        double anchorBefore[3];
        double anchorAfter[3];
        // The births owed, in tick order. Past the plan's room (the
        // capacity's births at one tick, twice the capacity in a plan) a
        // birth is refused here, still owed; refused counts them.
        const mvfxPlannedBirth* births;
        int32_t birthCount;
        uint64_t refused;
        // Whether the instance was cleared before the plan (stopped with
        // mvfx_stopAndClear, restarted, or just created): the simulation
        // drops every particle it holds first.
        bool cleared;
        // Its forces as resolved for the step (the last, when the plan
        // covers several), held over the plan: each particle takes them
        // at its place at the plan's start, each birth at its birth.
        int32_t forceCount;
        mvfxPlannedForce forces[MVFX_MAX_FORCES];
        // Its colliders as resolved for the step, its own then the
        // instance's (none when it does not collide), and its collision's
        // response, the rest speed in the particles' space.
        int32_t colliderCount;
        mvfxPlannedCollider colliders[2 * MVFX_MAX_COLLIDERS];
        float restitution;
        float friction;
        float lifeLoss;
        float restSpeed;
        bool killOnCollision;
    } mvfxEmitterPlan;

// What an emitter uses beyond spawning by rate and bursts from a shape
// with a velocity, its lifetimes, gravity and drag: one bit for each part
// of an emitter def, for a simulation to refuse what it does not run.
#define MVFX_FEATURE_RATE_CURVE       (1ull << 0)
#define MVFX_FEATURE_DISTANCE         (1ull << 1)
#define MVFX_FEATURE_HOST_SPAWN       (1ull << 2)
#define MVFX_FEATURE_INHERIT_VELOCITY (1ull << 3)
#define MVFX_FEATURE_SIZE             (1ull << 4)
#define MVFX_FEATURE_COLOR            (1ull << 5)
#define MVFX_FEATURE_ANGLE            (1ull << 6)
#define MVFX_FEATURE_ORIENTATION      (1ull << 7)
#define MVFX_FEATURE_DRAG_SCALE       (1ull << 8)
#define MVFX_FEATURE_CUSTOM           (1ull << 9)
#define MVFX_FEATURE_FORCES           (1ull << 10)
#define MVFX_FEATURE_VELOCITY_MODULES (1ull << 11)
#define MVFX_FEATURE_OVER_LIFE        (1ull << 12)
#define MVFX_FEATURE_KILL_RULES       (1ull << 13)
#define MVFX_FEATURE_COLLISION        (1ull << 14)
#define MVFX_FEATURE_HOST_MODULES     (1ull << 15)
#define MVFX_FEATURE_EVENTS           (1ull << 16)
#define MVFX_FEATURE_EVENT_SPAWN      (1ull << 17)
#define MVFX_FEATURE_NEIGHBORS        (1ull << 18)
#define MVFX_FEATURE_STEERING         (1ull << 19)
#define MVFX_FEATURE_CONTACTS         (1ull << 20)
#define MVFX_FEATURE_PAIR_FORCE       (1ull << 21)
#define MVFX_FEATURE_PLEXUS           (1ull << 22)
#define MVFX_FEATURE_RIBBON           (1ull << 23)
#define MVFX_FEATURE_BEAM             (1ull << 24)
#define MVFX_FEATURE_UNITS            (1ull << 25)
#define MVFX_FEATURE_RIGID            (1ull << 26)
#define MVFX_FEATURE_LIQUID           (1ull << 27)
#define MVFX_FEATURE_GAS_SPLAT        (1ull << 28)
#define MVFX_FEATURE_WATER            (1ull << 29)
#define MVFX_FEATURE_LIGHT            (1ull << 30)
#define MVFX_FEATURE_SORT             (1ull << 31)
#define MVFX_FEATURE_MESH_INDEX       (1ull << 32)
#define MVFX_FEATURE_FRAME            (1ull << 33)

    // A shape as compiled: its def's kind and numbers, its rotation made a
    // row-major matrix.
    typedef struct mvfxShapeView
    {
        mvfxShapeKind kind;
        mvfxArcMode arcMode;
        bool coneVolume;
        float matrix[9];
        float offset[3];
        float radius;
        float length;
        float halfExtents[3];
        float tangent;
        float thickness;
        float arc;
        float arcSpeed;
    } mvfxShapeView;

    // A velocity as compiled: its axis's two tangents and its cone's
    // cosine added to its def.
    typedef struct mvfxVelocityView
    {
        mvfxDirectionMode mode;
        float direction[3];
        float tangentA[3];
        float tangentB[3];
        float coneCosine;
        float randomize;
        float speedMin;
        float speedMax;
    } mvfxVelocityView;

    // An emitter of an effect as compiled, as a simulation elsewhere reads
    // it: what it uses, and the numbers of what every emitter has.
    typedef struct mvfxEmitterView
    {
        uint64_t features;
        mvfxSimulationSpace space;
        float inheritVelocity;
        mvfxShapeView shape;
        mvfxVelocityView velocity;
        float gravity[3];
        mvfxSimulationSpace gravitySpace;
        float drag;
        // A bit for each kind of force it has (1 << mvfx_forceConstant
        // and so on), for a simulation to refuse a kind it does not run,
        // and the grid each of its forces reads (a vector field's; the
        // null id for the others).
        uint32_t forceKinds;
        mvfxGridId forceGrids[MVFX_MAX_FORCES];
        // A bit for each kind of collider of its own (1 <<
        // mvfx_colliderPlane and so on), and whether it collides with the
        // host's scene or a depth image.
        uint32_t colliderKinds;
        bool sceneQueries;
        bool depthImage;
        // Whether its bounds are a fixed box in the instance's space
        // rather than measured from its particles, and the box.
        bool fixedBounds;
        float boundsMin[3];
        float boundsMax[3];
    } mvfxEmitterView;

    /// Returns an emitter of an effect as compiled.
    ///
    /// @param context  The context.
    /// @param effect   The effect.
    /// @param emitter  The emitter's index in the effect.
    /// @param viewOut  Receives the view; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument or
    ///         an emitter out of range; `mvfx_errorStale` for an effect
    ///         that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxEffect_GetEmitterView(const mvfxContext* context,
                                                                 mvfxEffectId effect,
                                                                 int32_t emitter,
                                                                 mvfxEmitterView* viewOut);

    /// Returns an emitter's plan for the context's last step. The births
    /// stay valid until the context's next step or the instance's
    /// destruction.
    ///
    /// @param context   The context.
    /// @param instance  A planned instance.
    /// @param emitter   The emitter's index in its effect.
    /// @param planOut   Receives the plan; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument or
    ///         an emitter out of range; `mvfx_errorStale` for an instance
    ///         that is gone; `mvfx_errorState` for an instance that is not
    ///         planned.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetEmitterPlan(const mvfxContext* context,
                                                                   mvfxInstanceId instance,
                                                                   int32_t emitter,
                                                                   mvfxEmitterPlan* planOut);

#ifdef __cplusplus
}
#endif

#endif
