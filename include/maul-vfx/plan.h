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
#include "maul-vfx/mesh.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A birth a step owes an emitter: 48 bytes, as a GPU buffer holds it.
    typedef struct mvfxPlannedBirth
    {
        // Its serial, the count of the emitter's births owed before it.
        // For an emitter that spawns from events, which the simulation
        // numbers, the count of its own and the host's births before it
        // (the simulation adds the births events gave before it), and its
        // death tick and inverse life are 0: they follow from the serial.
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
        // The emitter's source age at its tick: what its values drawn over
        // the emitter's duration read (its loop's share, or seconds where
        // it has no duration).
        float sourceAge;
        // The host's particle it is, an index into the plan's host births
        // (all ones for one of the emitter's own).
        uint32_t host;
        uint32_t reserved[2];
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
        // A heightfield's or distance grid's grid, the scale of its values
        // into the particles' space and the radius it keeps them off by.
        mvfxGridId grid;
        float scale;
        float grow;
    } mvfxPlannedCollider;

    // An emitter's depth image as resolved for a plan's step: the maps
    // between its particles' space and the view's clip space (clip =
    // toClip (x, 1), column-major, rounded once from binary64; fromClip
    // its inverse), the turn of the view's normals into the particles'
    // space, the image's size and range, and how its particles meet it in
    // the particles' space: their radius, the thickness behind a surface,
    // the samples a step takes at most, and whether leaving the view
    // kills.
    typedef struct mvfxPlannedDepth
    {
        bool enabled;
        float toClip[16];
        float fromClip[16];
        float normalTurn[4];
        int32_t width;
        int32_t height;
        bool reversed;
        float radius;
        float thickness;
        int32_t samples;
        bool killOutsideView;
    } mvfxPlannedDepth;

// The values a host's particle gives beside its place and velocity
// (mvfxPlannedHostBirth's given), and for an emitter that spawns from
// events how its velocity is given.
#define MVFX_HOST_GIVES_COLOR    1u
#define MVFX_HOST_GIVES_SIZE     2u
#define MVFX_HOST_GIVES_VELOCITY 4u
#define MVFX_HOST_ADDS_VELOCITY  8u

    // A particle the host queued (mvfxInstance_Spawn) as a plan hands it
    // over: its place as given (the world's for a world-space emitter, the
    // instance's otherwise), its velocity in the particles' space, given
    // or drawn as the CPU path draws it, and the colour and size it gives
    // (MVFX_HOST_GIVES_COLOR and MVFX_HOST_GIVES_SIZE in given). For an
    // emitter that spawns from events, whose serials the simulation
    // decides, the velocity is the host's own: given
    // (MVFX_HOST_GIVES_VELOCITY), added to the one drawn at the serial
    // (MVFX_HOST_ADDS_VELOCITY), or neither, the drawn one alone.
    typedef struct mvfxPlannedHostBirth
    {
        double position[3];
        float velocity[3];
        float color[4];
        float size[3];
        uint8_t given;
    } mvfxPlannedHostBirth;

    // An emitter's velocity modules as resolved for a plan's step: the
    // emitter's space's scale in the particles' space, and the added
    // direction, orbit axis and centers mapped there.
    typedef struct mvfxPlannedModules
    {
        float sigma;
        float addedDirection[3];
        float orbitAxis[3];
        float orbitCenter[3];
        float radialCenter[3];
    } mvfxPlannedModules;

    // A kill rule of an emitter as resolved for a plan's step, in the
    // particles' space: its def's meaning (mvfxKillRule) with lengths and
    // the speed scaled, points, normals and turns mapped, and its least
    // age in ticks, rounded up (a particle at least that many ticks old is
    // old enough).
    typedef struct mvfxPlannedKillRule
    {
        mvfxKillKind kind;
        float center[3];
        float radius;
        float halfExtents[3];
        float rotation[4];
        float normal[3];
        float speed;
        uint64_t minimumAgeTicks;
        int32_t collisions;
    } mvfxPlannedKillRule;

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
        // birth is refused here, still owed; refused counts them. For an
        // emitter that spawns from events they are its own and the host's
        // only: the simulation builds the births events give and numbers
        // them all (mvfxPlannedBirth).
        const mvfxPlannedBirth* births;
        int32_t birthCount;
        uint64_t refused;
        // The host's particles its births name, staying as the births do.
        const mvfxPlannedHostBirth* hostBirths;
        int32_t hostBirthCount;
        // For an emitter that spawns from events, the ticks from the plan's
        // start whose events give births: from spawnFirst to spawnLast,
        // within its delay and loops and up to the instance's stop (none
        // when spawnFirst is past spawnLast).
        int64_t spawnFirst;
        int64_t spawnLast;
        // The serials its births took to the plan's end, refused or not:
        // the next birth's (for an emitter that spawns from events, its own
        // and the host's births only).
        uint64_t owed;
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
        // instance's (none when it does not collide), the first
        // ownColliderCount its own, and its collision's response, the rest
        // speed in the particles' space.
        int32_t colliderCount;
        int32_t ownColliderCount;
        mvfxPlannedCollider colliders[2 * MVFX_MAX_COLLIDERS];
        float restitution;
        float friction;
        float lifeLoss;
        float restSpeed;
        bool killOnCollision;
        // Its depth image as resolved for the step (not enabled when it
        // does not collide with one or the instance has none).
        mvfxPlannedDepth depth;
        // Its velocity modules and kill rules as resolved for the step.
        mvfxPlannedModules modules;
        int32_t killRuleCount;
        mvfxPlannedKillRule killRules[MVFX_MAX_KILL_RULES];
        // The instance's parameters at the plan's end, which values drawn
        // from a parameter read.
        float parameters[MVFX_MAX_PARAMETERS][4];
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
    // Where an emitter's particles keep their values, as the CPU path
    // stores them: one array a component, each value's arrays in a row
    // from its first (-1 for a value it does not store), count in all.
    // Place, velocity and size take 3, colour and orientation 4, angular
    // velocity 3, the birth tick 2 (its low and high words), a custom
    // value its components.
    typedef struct mvfxAttributeArrays
    {
        int16_t position;
        int16_t birth;
        int16_t inverseLife;
        int16_t serial;
        int16_t velocity;
        int16_t size;
        int16_t color;
        int16_t angle;
        int16_t spin;
        int16_t orientation;
        int16_t angular;
        int16_t frame;
        int16_t drag;
        int16_t custom[MVFX_MAX_CUSTOM_ATTRIBUTES];
        int16_t collisions;
        int16_t mesh;
        int32_t count;
    } mvfxAttributeArrays;

    // A value source as compiled, for a GPU part to draw as the CPU path
    // does: its kind, whether one draw serves every component, its
    // components, its bounds, the instance parameter it reads, and its
    // baked curve (MVFX_CURVE_SAMPLES samples, then the domain's start
    // and the factor to a sample position) or gradient (MVFX_CURVE_SAMPLES
    // linear RGBA samples), each NULL where it has none, staying while the
    // effect does.
    typedef struct mvfxSourceView
    {
        mvfxSourceKind kind;
        bool uniform;
        int32_t components;
        float min[4];
        float max[4];
        int32_t parameter;
        const float* curve;
        const float* gradient;
    } mvfxSourceView;

    // A value over a particle's life as compiled: its value times its
    // curve's baked samples (MVFX_CURVE_SAMPLES, then the domain's start
    // and the factor to a sample's place) at the normalized age, the value
    // alone without a curve (NULL).
    typedef struct mvfxLifeView
    {
        float value;
        const float* curve;
    } mvfxLifeView;

    // An emitter's velocity modules as compiled (mvfxVelocityModules):
    // whether the speed is limited and how fast it falls to the limit,
    // whether any module moves particles, and the values over life.
    typedef struct mvfxModulesView
    {
        bool limitSpeed;
        float limitDampen;
        bool moves;
        mvfxLifeView speedLimit;
        mvfxLifeView motionScale;
        mvfxLifeView addedSpeed;
        mvfxLifeView radialSpeed;
        mvfxLifeView orbitRate;
    } mvfxModulesView;

    // An emitter's values over life as its records show them, compiled
    // (mvfxOverLife): the colour times a gradient (its baked samples,
    // MVFX_CURVE_SAMPLES of linear RGBA; NULL for white) at the age or the
    // speed's share of its range, alpha times a value over life; the size
    // times a value over life (one an axis where perAxis) at the age or the
    // speed's share; the flipbook; each custom attribute's values times a
    // value over life where scaled.
    typedef struct mvfxOverLifeView
    {
        bool color;
        mvfxOverLifeInput colorInput;
        float colorRange[2];
        const float* gradient;
        bool scaleAlpha;
        mvfxLifeView alpha;
        bool size;
        mvfxOverLifeInput sizeInput;
        float sizeRange[2];
        bool perAxis;
        mvfxLifeView sizes[3];
        mvfxFrameOverLife frame;
        bool customScaled[MVFX_MAX_CUSTOM_ATTRIBUTES];
        mvfxLifeView custom[MVFX_MAX_CUSTOM_ATTRIBUTES];
    } mvfxOverLifeView;

    // A flipbook frame's or a mesh index's draw at birth: from first,
    // drawn up to last when random.
    typedef struct mvfxIndexView
    {
        int32_t first;
        int32_t last;
        bool random;
    } mvfxIndexView;

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
        // mvfx_colliderPlane and so on), the grid each reads (a
        // heightfield's or a distance grid's; the null id for the others),
        // and whether it collides with the host's scene or a depth image.
        uint32_t colliderKinds;
        mvfxGridId colliderGrids[MVFX_MAX_COLLIDERS];
        bool sceneQueries;
        bool depthImage;
        // Whether its bounds are a fixed box in the instance's space
        // rather than measured from its particles, and the box.
        bool fixedBounds;
        float boundsMin[3];
        float boundsMax[3];
        // Measured bounds grow by the largest size a particle can have:
        // its largest stored size component (1 when it stores no size)
        // times the most its size over life scales that axis (1 without
        // one), times the margin for the rounding of reading the curve
        // and of the product (1 without a size over life).
        float sizeBounds[3];
        float sizeMargin;
        // Where its particles keep their values.
        mvfxAttributeArrays arrays;
        // How its births draw their size, colour and custom values (one
        // a custom attribute it has), frame and mesh index; their angle
        // and spin, orientation (by orientationMode: none, Euler angles
        // from its source, or random) and angular velocity, and drag
        // scale (a scalar's source has one component).
        mvfxSourceView size;
        mvfxSourceView color;
        mvfxSourceView custom[MVFX_MAX_CUSTOM_ATTRIBUTES];
        mvfxIndexView frame;
        mvfxIndexView meshIndex;
        mvfxSourceView angle;
        mvfxSourceView spin;
        mvfxOrientationMode orientationMode;
        mvfxSourceView orientation;
        mvfxSourceView angularVelocity;
        mvfxSourceView dragScale;
        // How its angles and orientations turn: the angular drag, and the
        // rotation speed over life (1 without one).
        float angularDrag;
        mvfxLifeView rotationSpeed;
        // Its velocity modules.
        mvfxModulesView modules;
        // How many particles the host may queue for it between steps.
        int32_t hostSpawnCapacity;
        // Its values over life as its records show them.
        mvfxOverLifeView overLife;
        // The order its records are written in for a view, and what a sort
        // by place measures.
        mvfxSortMode sortMode;
        mvfxSortMeasure sortMeasure;
        // The events its particles write each step (a mask of the kinds),
        // at most eventCapacity of them (0 when it writes none).
        uint8_t events;
        int32_t eventCapacity;
        // Its lifetime in seconds as a birth draws it (one component, held
        // to [2^-20, MVFX_MAX_LIFETIME] after the draw), and the births it
        // takes from another emitter's events (eventSpawn.enabled), which a
        // simulation of its plan builds, numbers and draws the lives of
        // itself (mvfxPlannedBirth).
        mvfxSourceView lifetime;
        mvfxEventSpawn eventSpawn;
        // Its start delay and loop in ticks (a loop of 0 for none), from
        // which an event birth's source age follows (mvfxPlannedBirth).
        int64_t delayTicks;
        int64_t loopTicks;
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

    /// Returns the mesh an instance's emitter spawns from now (its effect's,
    /// or the one mvfxInstance_SetEmitterMesh gave it), as mvfxMesh_GetView
    /// gives a mesh: its serial tells a change of the mesh or another mesh.
    ///
    /// @param context   The context.
    /// @param instance  The instance.
    /// @param emitter   The emitter's index in its effect, a mesh shape's.
    /// @param viewOut   Receives the view; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, an
    ///         emitter out of range or one whose shape is not a mesh;
    ///         `mvfx_errorStale` for an instance that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetEmitterMesh(const mvfxContext* context,
                                                                   mvfxInstanceId instance,
                                                                   int32_t emitter,
                                                                   mvfxMeshView* viewOut);

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
