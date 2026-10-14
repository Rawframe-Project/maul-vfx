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
#include "maul-vfx/grid.h"
#include "maul-vfx/modules.h"
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

// The most drag an emitter or a particle may have, a second.
#define MVFX_MAX_DRAG 1000.0f

// The most forces an emitter has.
#define MVFX_MAX_FORCES 8

// The least softening radius of an attractor.
#define MVFX_MIN_SOFTENING 1e-3f

// The steepest falloff.
#define MVFX_MAX_FALLOFF_EXPONENT 8.0f

    // The region a force acts in, weighting it from 1 at its heart to 0 at
    // its edge by (1 - share)^exponent, the share how far toward the edge a
    // particle is.
    typedef uint8_t mvfxFalloffKind;
    enum
    {
        // Everywhere, a weight of 1.
        mvfx_falloffNone = 0,
        // Within radius of center, the share its distance over radius.
        mvfx_falloffSphere = 1,
        // Within radius of the line through center along axis, the share
        // its distance from the line over radius.
        mvfx_falloffTube = 2,
        // Within radius of center and angle of axis from it, both shares
        // weighting: the angle's by its cosine, from 1 on the axis to 0 at
        // angle.
        mvfx_falloffCone = 3,
    };

    // A force's falloff, in the force's space; fields its kind does not
    // read are not checked.
    typedef struct mvfxFalloff
    {
        mvfxFalloffKind kind;
        float center[3];
        // The tube's or the cone's axis, not of length 0; normalized.
        float axis[3];
        // Above 0 and at most MVFX_MAX_EXTENT.
        float radius;
        // The cone's half angle, above 0 and at most MVFX_MAX_CONE_ANGLE.
        float angle;
        // From 0 (a hard edge) to MVFX_MAX_FALLOFF_EXPONENT.
        float exponent;
    } mvfxFalloff;

    // What a force does. Each acceleration is held over a step, taken where
    // a particle is at the step's start (or its birth); wind is exact, as
    // drag is.
    typedef uint8_t mvfxForceKind;
    enum
    {
        // A constant acceleration: vector times strength.
        mvfx_forceConstant = 0,
        // A drag toward the wind's velocity (vector): (vector - v) times
        // strength, which is how much of the difference goes a second, from
        // 0 to MVFX_MAX_DRAG.
        mvfx_forceWind = 1,
        // Toward point: strength at the softening radius and beyond it
        // falling as the square of the distance, within it falling linearly
        // to 0 at point; a negative strength repels.
        mvfx_forceAttractor = 2,
        // About the axis (vector) through point: strength along the turn the
        // right hand gives the axis, and pull toward the axis.
        mvfx_forceVortex = 3,
        // Turbulence: strength times the curl of a noise potential (noise),
        // a field without divergence, so particles swirl without bunching.
        mvfx_forceTurbulence = 4,
        // A vector field: strength times a grid of three components spanning
        // a box centered on point, sampled trilinearly.
        mvfx_forceVectorField = 5,
    };

    // Where a vector field reads past its box.
    typedef uint8_t mvfxGridBorder;
    enum
    {
        // Nothing: no force outside the box.
        mvfx_borderNone = 0,
        // The values at the box's faces.
        mvfx_borderClamp = 1,
        // The grid again, tiled: its last point joins its first, so a period
        // is the box's size.
        mvfx_borderRepeat = 2,
    };

    // How a field (turbulence, a vector field) acts.
    typedef uint8_t mvfxFieldMode;
    enum
    {
        // As an acceleration: strength times the field.
        mvfx_fieldAcceleration = 0,
        // As a velocity particles are pulled toward, strength times the
        // field, tightness of the difference going a second, exactly as
        // wind's.
        mvfx_fieldVelocity = 1,
    };

// The most octaves of a noise.
#define MVFX_MAX_OCTAVES 8

// The highest frequency of a noise, in cycles a unit.
#define MVFX_MAX_FREQUENCY 1e4f

    // A noise potential: three of Perlin's improved gradient noises
    // (Perlin, "Improving Noise", SIGGRAPH 2002), their gradients picked by
    // pcg4d of the lattice point and the seed, summed over octaves, each
    // divided by its frequency so the curl's size does not grow with it
    // (Bridson, Hourihan and Nordenstam, "Curl-Noise for Procedural Fluid
    // Flow", SIGGRAPH 2007). Places are taken in the force's space, in
    // binary64, so the field is the same however far from the world's
    // origin.
    typedef struct mvfxNoise
    {
        // The first octave's frequency, above 0 and at most
        // MVFX_MAX_FREQUENCY.
        float frequency;
        // From 1 to MVFX_MAX_OCTAVES.
        int32_t octaves;
        // Each octave's frequency over the last's, from 1 to 4.
        float lacunarity;
        // Each octave's weight over the last's, from 0 to 1.
        float gain;
        // How the field moves a second, in the force's space, each value
        // finite and at most MVFX_MAX_EXTENT in size.
        float scroll[3];
        uint32_t seed;
        // Whether each instance's seed is mixed in, so instances differ.
        bool seedPerInstance;
    } mvfxNoise;

    // A force on an emitter's particles. Its vectors, points and lengths
    // are in its space, the world's or the emitter's (turning and scaling
    // with the instance); each value finite and at most MVFX_MAX_EXTENT in
    // size.
    typedef struct mvfxForce
    {
        mvfxForceKind kind;
        mvfxSimulationSpace space;
        // The constant's acceleration, the wind's velocity or the vortex's
        // axis (not of length 0; normalized).
        float vector[3];
        // The attractor's point or a point on the vortex's axis.
        float point[3];
        float strength;
        // The attractor's softening radius, from MVFX_MIN_SOFTENING.
        float softening;
        // The attractor's kill radius: a particle whose step passes within it
        // of point is killed, as an inside-sphere kill rule; 0 for none, at
        // most MVFX_MAX_EXTENT.
        float killRadius;
        // The vortex's acceleration toward its axis; a negative one pushes
        // out.
        float pull;
        // Whether strength is multiplied by a parameter's x, then held to
        // MVFX_MAX_EXTENT in size (to 0 and MVFX_MAX_DRAG for wind).
        bool strengthFromParameter;
        int32_t parameter;
        mvfxFalloff falloff;
        // A field's mode and, as a velocity, its tightness, from 0 to
        // MVFX_MAX_DRAG.
        mvfxFieldMode fieldMode;
        float tightness;
        // The turbulence's noise.
        mvfxNoise noise;
        // The vector field's grid, of three components (it may not be
        // destroyed while the effect is alive), and its box: the size along
        // each axis (above 0 and at most MVFX_MAX_EXTENT) and how it is
        // turned (a unit quaternion, its length within 0.001 of 1), the grid's
        // points spread over it from face to face (one point a cell more
        // with the repeat border), and the vectors turning with it.
        mvfxGridId grid;
        float boxSize[3];
        float boxRotation[4];
        mvfxGridBorder border;
    } mvfxForce;

    // How an emitter's particles move: each step moves them exactly under
    // their gravity and drag (an exponential integrator), whatever the
    // steps' lengths, with the forces that vary held for the step.
    typedef struct mvfxMotion
    {
        // A constant acceleration, each component finite and at most
        // MVFX_MAX_EXTENT in size, in units a second squared.
        float gravity[3];
        // The space gravity is given in: the world's, or the emitter's (it
        // turns and scales with the instance).
        mvfxSimulationSpace gravitySpace;
        // Linear drag a second, from 0 to MVFX_MAX_DRAG: the share of its
        // velocity a particle loses is 1 - e^(-drag t).
        float drag;
        // Optional, a multiplier of drag drawn for each particle; the
        // product is held to 0 and MVFX_MAX_DRAG.
        mvfxScalarSource dragScale;
        // Angular drag a second likewise, slowing spin and angular velocity.
        float angularDrag;
    } mvfxMotion;

    // How an emitter is made. Build it with mvfxDefaultEmitterDef.
    //
    // Times are rounded once to ticks of 2^-20 seconds when the effect is
    // created, so a repeating burst never drifts. The particles an emitter's
    // rate and its repeating bursts spawn a second, at most, may not exceed
    // MVFX_MAX_SPAWN_RATE each.
    // The order an emitter's records are written in for a view.
    typedef uint8_t mvfxSortMode;
    enum
    {
        // The pool's: oldest first.
        mvfx_sortNone = 0,
        // Youngest first, the pool's reversed.
        mvfx_sortYoungestFirst = 1,
        // Farthest first, for blending over what is behind.
        mvfx_sortBackToFront = 2,
        // Nearest first.
        mvfx_sortFrontToBack = 3,
    };

    // What back to front and front to back measure.
    typedef uint8_t mvfxSortMeasure;
    enum
    {
        // The distance from the view's eye.
        mvfx_sortByDistance = 0,
        // The depth from the eye along the view's forward direction.
        mvfx_sortByDepth = 1,
    };

    // Which ribbon a particle joins: a ribbon is the run of an emitter's
    // live particles with one ribbon id, oldest first.
    typedef uint8_t mvfxRibbonIds;
    enum
    {
        // Not drawn as ribbons.
        mvfx_ribbonNone = 0,
        // Every particle in one ribbon.
        mvfx_ribbonOne = 1,
        // The particles born at one tick together (a burst's firing) in
        // one ribbon: its id is the serial of the batch's first.
        mvfx_ribbonPerBatch = 2,
        // A particle born of an event in the ribbon of the event's
        // particle: its id is that particle's serial (each spark leaves a
        // ribbon of its own); others as with mvfx_ribbonOne.
        mvfx_ribbonPerSource = 3,
    };

    // A ribbon's texture coordinate along it.
    typedef uint8_t mvfxRibbonTexture;
    enum
    {
        // Its normalized length: the texture stretched once over it.
        mvfx_ribbonStretch = 0,
        // The segment's index and the place within it: once a segment.
        mvfx_ribbonTilePerSegment = 1,
        // The ribbon's distance stored at each point's birth over the tile's
        // length, so the texture stays on the points as the ribbon grows.
        mvfx_ribbonTileByDistance = 2,
    };

    // Which way a ribbon faces.
    typedef uint8_t mvfxRibbonFacing;
    enum
    {
        // Toward the view's eye, about its tangent.
        mvfx_ribbonFaceCamera = 0,
        // Across a fixed normal in the particles' space.
        mvfx_ribbonFaceNormal = 1,
    };

// The most pieces a ribbon's segment is cut into.
#define MVFX_MAX_RIBBON_PIECES 16

    // An emitter drawn as ribbons through its particles.
    typedef struct mvfxRibbonDef
    {
        mvfxRibbonIds ids;
        // Each segment cut into this many pieces along a centripetal
        // Catmull-Rom spline through the points, from 1 to
        // MVFX_MAX_RIBBON_PIECES; with bendAngle above 0, into as few as
        // keep each piece's turn at most bendAngle (radians, at most pi).
        int32_t pieces;
        float bendAngle;
        // The texture coordinate along: tileLength (above 0, at most
        // MVFX_MAX_EXTENT) for a tile by distance, in the particles' space;
        // plus scroll (finite, at most MVFX_MAX_EXTENT in size) times the
        // instance's seconds.
        mvfxRibbonTexture texture;
        float tileLength;
        float scroll;
        // Its facing, and the normal it faces across (finite, not 0).
        mvfxRibbonFacing facing;
        float normal[3];
        // A point's width is its size's X times this over the ribbon's
        // normalized length (0 at its oldest point, 1 at its youngest).
        mvfxLifeCurve width;
        // Its colour times this over the normalized length; no keys is
        // white.
        mvfxGradient color;
    } mvfxRibbonDef;

// The most meshes an emitter's particles choose from.
#define MVFX_MAX_MESH_INDEX 65536

    // The lights an emitter's particles carry: every n-th particle by
    // serial, so the same particles keep their lights from step to step.
    typedef struct mvfxLightDef
    {
        bool enabled;
        // A particle carries one when its serial is a multiple of this,
        // from 1 to 65,536.
        int32_t every;
        // Its colour is the particle's red, green and blue times this (and
        // times its alpha with alphaScales), from 0 to MVFX_MAX_EXTENT.
        float intensity;
        bool alphaScales;
        // Its radius is this plus radiusBySize times the particle's largest
        // size component in the world, each from 0 to MVFX_MAX_EXTENT.
        float radius;
        float radiusBySize;
    } mvfxLightDef;

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
        // How they move.
        mvfxMotion motion;
        // The forces on them, at most MVFX_MAX_FORCES, applied in order;
        // NULL when forceCount is 0.
        const mvfxForce* forces;
        int32_t forceCount;
        // Their velocity modules.
        mvfxVelocityModules velocityModules;
        // The values over life they show.
        mvfxOverLife overLife;
        // The rules that kill them before their life ends, at most
        // MVFX_MAX_KILL_RULES; NULL when killRuleCount is 0.
        const mvfxKillRule* killRules;
        int32_t killRuleCount;
        // How they collide, and the emitter's colliders, at most
        // MVFX_MAX_COLLIDERS; NULL when colliderCount is 0.
        mvfxCollision collision;
        const mvfxCollider* colliders;
        int32_t colliderCount;
        // The host's own modules, at most MVFX_MAX_HOST_MODULES, run in
        // order at their stages; NULL when hostModuleCount is 0. An effect
        // with them cannot run on a GPU.
        const mvfxHostModule* hostModules;
        int32_t hostModuleCount;
        // The events its particles write each step, a mask of
        // mvfx_eventMaskBirth, mvfx_eventMaskDeath and mvfx_eventMaskCollision
        // (collisions only where they collide), at most eventCapacity a step
        // (from 1 to MVFX_MAX_EVENTS when any is asked; the rest counted
        // dropped). The host reads them after the step
        // (mvfxInstance_GetEvents).
        uint8_t events;
        int32_t eventCapacity;
        // Spawning from another emitter's events.
        mvfxEventSpawn eventSpawn;
        // The lights its particles carry (mvfxInstance_WriteLights).
        mvfxLightDef light;
        // Its ribbons (mvfxInstance_WriteRibbons).
        mvfxRibbonDef ribbon;
        // The order its records are written in, and what a sort by place
        // measures; equal keys keep the pool's order.
        mvfxSortMode sortMode;
        mvfxSortMeasure sortMeasure;
        // Its place in the order emitters are drawn in, lower first
        // (mvfxContext_OrderEmitters).
        int32_t drawPriority;
        // Whether its bounds are a fixed box in its instance's space (each
        // value finite, at most MVFX_MAX_EXTENT in size, min at most max)
        // rather than measured from its particles each step.
        bool fixedBounds;
        float boundsMin[3];
        float boundsMax[3];
        // The optional initial values, each stored only when its source is
        // set: size per axis (X, Y, Z), linear color (red, green, blue,
        // alpha), a sprite's angle in radians and spin in radians a second
        // (either sets both, the other 0).
        mvfxVectorSource size;
        mvfxVectorSource color;
        mvfxScalarSource angle;
        mvfxScalarSource spin;
        // A mesh particle's orientation (a quaternion) and angular velocity
        // in radians a second in the emitter's space (either sets both, the
        // other identity or 0);
        // orientation reads Euler angles in radians in the euler mode.
        mvfxOrientationMode orientationMode;
        mvfxVectorSource orientation;
        mvfxVectorSource angularVelocity;
        // The flipbook frame it starts at.
        mvfxFrameStart frame;
        // Which of the host's meshes it is drawn as, kept for its life
        // (the frame modes: none, one, or any from first to last alike;
        // last below MVFX_MAX_MESH_INDEX).
        mvfxFrameStart meshIndex;
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
    /// unturned, whole arcs, along the shape's normal at a speed of 0), with
    /// no gravity, no drag, no forces, and velocity modules that leave the
    /// motion as it is (a motion scale of 1, nothing added, no limit).
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
