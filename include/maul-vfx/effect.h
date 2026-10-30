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
#include "maul-vfx/gas.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/modules.h"
#include "maul-vfx/shape.h"
#include "maul-vfx/value.h"
#include "maul-vfx/water.h"

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

// The fastest a particle moves, in the emitter's units a second (a
// world-space emitter's times its instance's scale): each step holds a
// moved particle's speed within it, scaling a faster velocity down to it
// so its heading is kept, and so how far a particle travels in its life
// is bounded. Its forces (up to 1e12 a second squared) and the instance's
// velocity it inherits (its move over a step of a tick, 2^20 times the
// move) build a speed up only to here; far past what a scene asks for, it
// holds only such extremes. A gas holds each face's velocity within it in
// its box's lengths.
#define MVFX_MAX_SPEED 1e9f

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

// The most a particle's buoyancy is (mvfxWaterCoupling), the water's
// density over its: an air bubble's in water is about 800.
#define MVFX_MAX_BUOYANCY 1000.0f

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
        // A gas's velocity: strength times the velocity of the effect's gas
        // gas where the particle is, as the step before left it, sampled
        // trilinearly; nothing outside its box (a 2D gas's reaches along its
        // Z without end). Its space is the gas's box. Only an emitter's
        // particles follow a gas.
        mvfx_forceGas = 6,
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
// The most lattice cells a baked noise's period spans along an axis.
#define MVFX_MAX_NOISE_PERIOD 4096

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
        // The force's strength, a value source that means here not what it
        // means for a particle's values: it is resolved for the whole
        // instance once a step (the forces hold over the step), and never
        // stored in particles. A range draws once per instance and force
        // (its seed, the force's index) and keeps that draw for every step;
        // a curve reads the emitter's loop age at the step's start (its
        // seconds since it began without a duration; a gas's force, the
        // instance's seconds); a parameter, the parameter's x at the step,
        // times the range's draw. None is a strength of 0. Its ends' larger
        // size times its base's largest is at most MVFX_MAX_EXTENT (wind's
        // at most MVFX_MAX_DRAG, its min at least 0); a parameter's
        // strength is held to MVFX_MAX_EXTENT in size (wind's to 0 and
        // MVFX_MAX_DRAG), not refused.
        mvfxScalarSource strength;
        // The attractor's softening radius, from MVFX_MIN_SOFTENING.
        float softening;
        // The attractor's kill radius: a particle whose step passes within it
        // of point is killed, as an inside-sphere kill rule; 0 for none, at
        // most MVFX_MAX_EXTENT.
        float killRadius;
        // The vortex's acceleration toward its axis; a negative one pushes
        // out.
        float pull;
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
        // The gas a gas force follows: its index in the effect's def.
        int32_t gas;
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
        // Every particle in the emitter's current ribbon: its id is the
        // emitter's ribbon counter, which a break advances (the host's
        // mvfxInstance_BreakRibbons, the instance's teleport, or a birth
        // farther than breakDistance from the ribbon's youngest point), so
        // a trail ends and the next begins.
        mvfx_ribbonOne = 1,
        // The particles born at one tick together (a burst's firing) in
        // one ribbon: its id is the serial of the batch's first.
        mvfx_ribbonPerBatch = 2,
        // A particle born of an event in the ribbon of the event's
        // particle: its id is that particle's serial's low 31 bits (each
        // spark leaves a ribbon of its own); others as with
        // mvfx_ribbonOne (the counter and its breaks), their ids the
        // counter with the top bit set, so the emitter's own ribbon never
        // joins a source's.
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
        // mvfx_ribbonOne for a beam (mvfxBeamDef); mvfx_ribbonNone for an
        // emitter of sheets (mvfxUnitDef).
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
        // A birth farther than this from its ribbon's youngest point (in the
        // particles' space) starts the next ribbon; 0 never, else at most
        // MVFX_MAX_EXTENT. For ribbons by the counter.
        float breakDistance;
        // With mvfx_ribbonOne: while the emitter may still spawn, its
        // current ribbon ends at a point pinned to its shape's origin at the
        // step's end, so a trail never lags behind its source; the point
        // takes the youngest particle's values. Never for a beam
        // (mvfxBeamDef), whose ends are placed.
        bool pinHead;
        // A point's width is its size's X times this over the ribbon's
        // normalized length (0 at its oldest point, 1 at its youngest).
        mvfxLifeCurve width;
        // Its colour times this over the normalized length; no keys is
        // white.
        mvfxGradient color;
    } mvfxRibbonDef;

// The most points of a chain, the most along either side of a sheet, and
// the most substeps a step of an emitter of units takes.
#define MVFX_MAX_CHAIN_POINTS 256
#define MVFX_MAX_SHEET_SIDE   64
#define MVFX_MAX_SUBSTEPS     32

// The most frames the host moves an emitter's units on.
#define MVFX_MAX_ATTACH_FRAMES 4096

    // What one birth of an emitter is.
    typedef uint8_t mvfxUnitKind;
    enum
    {
        // A particle.
        mvfx_unitParticle = 0,
        // A chain (a rope, a tail): points particles in a line, each joined
        // to the next and the one after by constraints.
        mvfx_unitChain = 1,
        // A sheet of cloth (a banner, a cape, a shred): points across by rows,
        // row after row, each joined to its neighbors across and down
        // (stretch), across its quads' diagonals (shear) and to the points
        // two away across and down (bending); drawn as a mesh by
        // mvfxInstance_ExpandSheets.
        mvfx_unitSheet = 2,
    };

    // What a unit's pinned points are held to. Each unit has a frame,
    // fixed when its birth's step ends; its pinned points keep their places
    // in that frame then, so they move as it moves and turn as it turns,
    // eased from where it was at a step's start to where it is at its end
    // over the substeps.
    typedef uint8_t mvfxAttachKind;
    enum
    {
        // Their birth places in the particles' space (a local emitter's move
        // with its instance).
        mvfx_attachNone = 0,
        // The instance, for a world-space emitter: they move, turn and scale
        // with it as it moves in the world (a cape on a character), where a
        // local emitter's units would move rigidly with it.
        mvfx_attachInstance = 1,
        // A frame the host moves (mvfxInstance_SetAttachFrames): a host
        // spawn names its unit's (mvfxSpawnRequest.attachFrames), any other
        // birth takes frame 0. A unit whose frame is not set when its birth's
        // step ends never moves; one whose frame is no longer set is held
        // where it was.
        mvfx_attachHost = 2,
        // The particle of the emitter's event source whose event spawned it,
        // its place and (where the source stores one) its orientation: a
        // banner on a pole the source carries. When that particle is gone
        // the unit is let go, its points free; a unit born otherwise never
        // moves.
        mvfx_attachParticles = 3,
    };

    // The units an emitter's births are, solved by XPBD (Macklin et al.
    // 2016) in substeps (Macklin et al. 2019): each substep moves every
    // point as a particle (gravity, drag, forces, velocity modules,
    // collisions), holds pinned points, projects each constraint once, set
    // by set, adds the projections' correction to the velocities and damps
    // them along each constraint. A unit's points are born together with
    // its root's place, velocity, life and values, the place of a chain's
    // point j its root's plus j spacings along direction, of a sheet's
    // point in row r and place c across its root's plus c spacings along
    // direction and r along downDirection; they die together, a kill of
    // one kills the unit. Counts (owed, refused,
    // capacities of host spawns and event spawns) are in units, the
    // emitter's capacity in points, as their comments say.
    typedef struct mvfxUnitDef
    {
        // Units are never a beam's points (mvfxBeamDef), and a sheet's
        // emitter draws no ribbons (ribbon ids mvfx_ribbonNone).
        mvfxUnitKind kind;
        // A chain's points, from 2 to MVFX_MAX_CHAIN_POINTS; a sheet's points
        // across, and its rows (read only for a sheet), each from 2 to
        // MVFX_MAX_SHEET_SIDE.
        int32_t points;
        int32_t rows;
        // The rest distance between neighbors, above 0 and at most
        // MVFX_MAX_EXTENT, in the emitter's space (scaling with the
        // instance).
        float spacing;
        // From each point to the next at birth, in the emitter's space, and
        // for a sheet from each row to the next (read only for a sheet); not
        // of length 0 (normalized), a sheet's two at least 3 degrees apart.
        float direction[3];
        float downDirection[3];
        // From 1 to MVFX_MAX_SUBSTEPS.
        int32_t substeps;
        // The compliance (inverse stiffness, 0 rigid) of the distance between
        // neighbors, of that across a sheet's quads' diagonals (shear) and
        // of that between points two apart (bending), each from 0 to
        // MVFX_MAX_EXTENT.
        float stretchCompliance;
        float shearCompliance;
        float bendCompliance;
        // The relative speed along each constraint lost as
        // 1 - e^(-damping t), from 0 to MVFX_MAX_DRAG.
        float damping;
        // Whether a unit's first point, and a sheet's whole first row (a
        // banner on a pole; read only for a sheet), stay at their birth
        // places in the emitter's space (moving with a local emitter's
        // instance).
        bool pinFirst;
        bool pinFirstRow;
        // A sheet's aerodynamics (read only for a sheet; 0 for none, each up
        // to MVFX_MAX_EXTENT): each whole triangle of its quads (as
        // mvfxInstance_ExpandSheets draws them) pushes its three points by
        // -normalDrag A (v.n)|v.n| n - tangentDrag A |v_t| v_t, shared among
        // them, A its area, n its normal, v its points' mean velocity less
        // the air's at its middle and v_t that less its part along n. The
        // air moves as the emitter's winds and velocity fields pull: their
        // targets' mean weighted by their pulls, still where none reaches.
        // Each coefficient is half the air's density times the plate's drag
        // coefficient over a point's mass; a tilted sheet so takes lift. A
        // triangle takes at most a sixth of its relative speed a substep.
        float normalDrag;
        float tangentDrag;
        // Self-collision (selfThickness 0 for none, else up to
        // MVFX_MAX_EXTENT, in the emitter's space, scaling with the instance
        // as the spacing does): the emitter's units' points are kept
        // selfThickness apart, those of different units always, those of one
        // unit where their rest distance is at least the thickness (so
        // neighbors are left to their constraints). A point's candidates are
        // found once a step, those within the thickness plus twice the
        // farthest a point goes in the step at its speed and gravity, at most
        // selfContacts of them (1 to MVFX_MAX_NEIGHBORS; a point with more is
        // counted, mvfxEmitterStats.selfContactsCapped); in each substep
        // after the constraints, each pair closer than the thickness is
        // parted and its sliding over the substep slowed by up to
        // selfFriction (0 to 1) times its overlap, all pairs from the same
        // places, each point moved by its pairs' mean.
        float selfThickness;
        float selfFriction;
        int32_t selfContacts;
        // How far a stretch constraint (between neighbors across or down) may
        // stretch before it tears for good: it tears when, after a substep's
        // projections, it is longer than its rest times 1 + tearStrain. A
        // torn edge takes the bending across it and its quads' shear with
        // it: a chain drawn as ribbons splits there, a sheet's mesh drops
        // those quads. 0 never tears, else up to MVFX_MAX_EXTENT.
        float tearStrain;
        // What the pinned points are held to; any but mvfx_attachNone needs a
        // pinned point, mvfx_attachInstance a world-space emitter,
        // mvfx_attachParticles spawning from events of an emitter in the same
        // space.
        mvfxAttachKind attach;
        // For mvfx_attachHost, the most frames the host sets, 1 to
        // MVFX_MAX_ATTACH_FRAMES.
        int32_t attachFrameCapacity;
    } mvfxUnitDef;

// The most contacts a rigid particle takes with the world in a substep.
#define MVFX_MAX_RIGID_CONTACTS 8

// The most contacts with each other an emitter's rigid particles take a
// substep, a body.
#define MVFX_MAX_BODY_CONTACTS 64

// The longest a rigid particle must be still before it sleeps, in seconds.
#define MVFX_MAX_SLEEP_TIME 3600.0f

    // A rigid particle's shape, sized by the particle's size in the
    // particles' space and turned by its orientation. Each size axis is the
    // shape's extent along it, so a host may draw a mesh scaled by the size:
    // an axis a shape does not set is X, as the emitter's size and size
    // over life say. A spawn request's size that differs there is refused;
    // a host module that writes one kills the particle (valuesKilled). A
    // birth takes X's draw for those axes, and a size the effect does not
    // fix, a parameter's or an event's inherited one, takes X for them,
    // their own values ignored.
    typedef uint8_t mvfxRigidShape;
    enum
    {
        // A sphere of diameter size X; Y and Z are X.
        mvfx_rigidSphere = 0,
        // A capsule along its Y of diameter size X and length size Y (end to
        // end, at least its diameter); Z is X.
        mvfx_rigidCapsule = 1,
        // A box of size X by Y by Z.
        mvfx_rigidBox = 2,
        // The convex hull of a mesh's vertices (mvfxRigidDef.hull) scaled by
        // size along the mesh's axes, at most MVFX_MAX_HULL_VERTICES
        // vertices. Its place is its centroid: a host draws its mesh moved
        // by the scaled mvfxMesh_GetHullCenter from the particle.
        mvfx_rigidHull = 3,
    };

// The most vertices a rigid particle's hull has.
#define MVFX_MAX_HULL_VERTICES 64

    // Rigid particles, by XPBD rigid bodies (Muller, Macklin, Chentanez,
    // Jeschke and Kim, "Detailed Rigid Body Simulation with Extended
    // Position Based Dynamics", SCA 2020) in substeps, so they tumble, roll
    // and come to rest on an edge or a face. Each particle is a body of a
    // shape and a density; its orientation and angular velocity (stored for
    // it) turn by its free flow, keeping its angular momentum, after the
    // angular drag slows its spin. Each substep a
    // body is moved as a particle is, then meets its contacts at its
    // predicted pose: the colliders of its emitter and, with
    // hostColliders, the instance's, outside or inside, at the shape's
    // surface (a box on a box face to face, edge to edge or on an edge;
    // grids at points: a sphere's center, a capsule's core at most its
    // radius apart, a box's corners and its faces' centers where the
    // surface bulges under them), its scene plane with enableSceneQueries, and the
    // depth image with enableDepthImage (each point's way over the substep, and
    // killed outside the view with killOutsideView). Contacts push it out; friction holds or slows
    // each patch of contacts with one collider or one face of a box kept inside (at their center,
    // PhysX's patch friction); its velocities are its pose's change, then turned back by
    // restitution, which contacts struck at once share. A depth there at a substep's start is
    // pushed out without becoming velocity, so bodies born into a surface, met by a moving collider
    // or stepped by a single tick do not pop. A newborn takes what is left of its step's substeps.
    // The collision def says whether it collides and with what, its kills, life loss and events,
    // each for a strike (a contact met faster into the surface than twice gravity's speed over a
    // substep), the event its first of a step; its restitution, friction,
    // rest speed and radius are not read. A body takes at most
    // MVFX_MAX_RIGID_CONTACTS contacts a substep, the shallowest past them
    // dropped and counted (mvfxEmitterStats.rigidContactsCapped).
    typedef struct mvfxRigidDef
    {
        // Rigid bodies are plain particles: never units (mvfxUnitDef) or a
        // beam's points (mvfxBeamDef).
        bool enable;
        mvfxRigidShape shape;
        // Mass a unit of volume, above 0, at most MVFX_MAX_EXTENT.
        float density;
        // From 1 to MVFX_MAX_SUBSTEPS.
        int32_t substeps;
        // The share of the speed into a surface it bounces back with (none
        // below a strike's speed, so resting bodies do not jitter), 0 to 1;
        // its static and dynamic friction coefficients, each 0 to
        // MVFX_MAX_DRAG, the static at least the dynamic: a patch holds
        // still while what holds it is at most the static coefficient times
        // what presses it, else is slowed by the dynamic times that. A body
        // tips where its center passes over its support's edge; held within
        // about a tenth of that limit it creeps (a 0.4 by 0.6 box at 0.95
        // of it, 22 mm in 3 s) until it sleeps.
        float restitution;
        float staticFriction;
        float dynamicFriction;
        // Whether its bodies meet each other, the most contacts between them
        // a substep for each body (pooled over the emitter's live bodies),
        // from 0 (they pass through each other) to MVFX_MAX_BODY_CONTACTS.
        // Pairs are found by the neighbor grid at the predicted poses, each
        // pair met as a body meets a sphere or a box (two capsules by their
        // cores) and solved with both bodies' masses from their densities;
        // those past the pool dropped and counted
        // (mvfxEmitterStats.rigidContactsCapped). A body born in a step
        // meets the others from its next. Mass ratios show: at rest a body
        // under one N times heavier sinks about N / 50 mm into what holds
        // it (0.5 mm at 27); the heavier landing presses it further for the
        // impact's substeps (dropped 2 m, 1.2 mm at 8, 9 mm at 16, 22 mm at
        // 27), pushed back out after.
        int32_t bodyContacts;
        // A hull's mesh, its hull taken from its vertices when the effect is
        // created (the effect does not follow its later positions).
        mvfxMeshId hull;
        // Sleeping: a body is still while its farthest point's speed (its
        // speed plus its angular speed times its reach) is below sleepSpeed,
        // in the particles' lengths a second, from 0 (never sleeps) to
        // MVFX_MAX_EXTENT; bodies joined by contacts sleep together once
        // all have been still sleepTime seconds, above 0 (when sleepSpeed
        // is) and at most MVFX_MAX_SLEEP_TIME. Asleep, a body keeps its
        // pose and meets nothing (it still ages and dies). It wakes when an
        // awake body comes within its reach (with the sleepers it touches
        // in turn, before that substep's solve), when its forces change by
        // more than sleepSpeed over sleepTime, when a collider it touches
        // or comes to touch changes or its depth image is set again, or by
        // mvfxInstance_WakeBodies.
        // mvfxDefaultEmitterDef sets 0.05 and 0.5.
        float sleepSpeed;
        float sleepTime;
    } mvfxRigidDef;

// The most density iterations a liquid's substep takes.
#define MVFX_MAX_LIQUID_ITERATIONS 32

// The least and the most a liquid's kernel radius is of its spacing.
#define MVFX_MIN_LIQUID_KERNEL 1.5f
#define MVFX_MAX_LIQUID_KERNEL 4.0f

// The most a liquid's tensile correction is raised to.
#define MVFX_MAX_LIQUID_TENSILE_POWER 8

    // A liquid, by position-based fluids (Macklin and Muller, "Position
    // Based Fluids", SIGGRAPH 2013): the emitter's particles keep their
    // rest density. Each substep they are moved as particles are (forces,
    // drag, collision), then a fixed number of iterations corrects their
    // places where their density is above 1 (the constraint only parts
    // particles, as Macklin et al. 2014's fluids do, so a free surface does
    // not clump), each particle's density the poly6 kernel's sum over its
    // neighbors within the kernel radius over the same sum in an ideal
    // lattice at the rest spacing (cubic, or square for a planar liquid),
    // its correction by the spiky kernel's gradient; each
    // correction's way is followed with the colliders, so it slides along
    // them rather than crossing. The velocity gains the correction over the
    // substep; then XSPH viscosity blends it toward the neighbors', and
    // vorticity confinement restores swirls. Particles born in a step join
    // the liquid from the next. Lengths are in the particles' space (a
    // world-space emitter's scale with its instance). A particle counts at
    // most maxNeighbors neighbors in the grid's order, those past it
    // counted (mvfxEmitterStats.neighborsCapped, once a step).
    // mvfxDefaultEmitterDef sets a spacing of 0.1, a kernel of 2, a
    // relaxation of 0.01, viscosity of 0.01 and 64 neighbors.
    // How a liquid moves.
    typedef uint8_t mvfxLiquidSolver;
    enum
    {
        // Position-based fluids on its particles alone.
        mvfx_liquidParticles = 0,
        // APIC on a liquid gas (mvfx_gasLiquid): each step a particle's
        // place moves by the velocity the gas gave it, followed along the
        // colliders, while its velocity gains its forces, gravity and drag
        // as any particle's does (bounces, kills, life and events too);
        // then the gas, once every instance has stepped, makes that
        // velocity incompressible and gives it back with each particle's
        // affine matrix. Of this def only the spacing is read, as the
        // particles' spacing at rest.
        mvfx_liquidGrid = 1,
    };

    typedef struct mvfxLiquidDef
    {
        // A liquid's particles are plain particles: never units
        // (mvfxUnitDef), rigid bodies or a beam's points (mvfxBeamDef).
        bool enable;
        // Particles (the default) or a liquid gas, and for a grid the gas's
        // index in the effect's def: a liquid gas no other emitter names.
        mvfxLiquidSolver solver;
        int32_t gas;
        // Whether it moves in its emitter's XY plane, its corrections and
        // swirls along Z 0.
        bool planar;
        // The particles' distance apart at rest, from
        // MVFX_MIN_NEIGHBOR_RADIUS to MVFX_MAX_NEIGHBOR_RADIUS over
        // kernelScale.
        float spacing;
        // The kernel radius over the spacing, from MVFX_MIN_LIQUID_KERNEL to
        // MVFX_MAX_LIQUID_KERNEL.
        float kernelScale;
        // From 1 to MVFX_MAX_SUBSTEPS, and 1 to MVFX_MAX_LIQUID_ITERATIONS (4
        // and 3 by default: a column 24 particles tall then settles within
        // 1.3% of its rest density).
        int32_t substeps;
        int32_t iterations;
        // The constraint's softening, a share of its denominator in a full
        // lattice, above 0, at most 1: larger is softer and steadier.
        float relaxation;
        // The tensile correction, an artificial pressure parting close pairs
        // (the paper's -k (W(r) / W(dq))^n, weighing as a density excess of
        // its strength would): its strength from 0 (none, the default) to
        // 1, its reference distance a share of the kernel radius above 0
        // and below 1, and its power from 1 to
        // MVFX_MAX_LIQUID_TENSILE_POWER (0.2 and 4 by default).
        float tensile;
        float tensileDistance;
        int32_t tensilePower;
        // XSPH viscosity's share of the neighbors' velocity, 0 to 1.
        float viscosity;
        // Vorticity confinement's strength a second, from 0 to
        // MVFX_MAX_EXTENT, as a gas's.
        float vorticity;
        // From 1 to MVFX_MAX_NEIGHBORS.
        int32_t maxNeighbors;
    } mvfxLiquidDef;

// The most plexus links an emitter keeps a step.
#define MVFX_MAX_PLEXUS_LINKS (1 << 20)

    // Plexus links: at the end of each step, a line between each two of an
    // emitter's particles closer than distance, each pair once (owned by the
    // one born first), drawn as two-point ribbons (mvfxInstance_WritePlexus,
    // mvfxInstance_ExpandPlexus): straight, the coordinate along from 0 to
    // 1, facing the eye.
    typedef struct mvfxPlexusDef
    {
        bool enable;
        // From MVFX_MIN_NEIGHBOR_RADIUS to MVFX_MAX_NEIGHBOR_RADIUS.
        float distance;
        // The most links an emitter keeps, from 1 to MVFX_MAX_PLEXUS_LINKS,
        // and the most a particle owns, from 1 to MVFX_MAX_NEIGHBORS, in the
        // particles' order then the grid's; links past either are counted
        // (mvfxEmitterStats.linksDropped).
        int32_t maxLinks;
        int32_t maxPerParticle;
        // A link point's width over its particle's size X, from 0 to
        // MVFX_MAX_EXTENT.
        float width;
        // Whether a link's colour fades with its length: its alpha times 1
        // less its length over distance.
        bool fade;
    } mvfxPlexusDef;

// The most segments a beam has.
#define MVFX_MAX_BEAM_SEGMENTS 1024

    // A point of a beam's curve in the instance's space: fixed, or an
    // instance parameter's x, y and z (so the host aims the beam).
    typedef struct mvfxBeamPoint
    {
        // Finite, at most MVFX_MAX_EXTENT in size.
        float point[3];
        bool fromParameter;
        // From 0 below the effect's parameter count.
        int32_t parameter;
    } mvfxBeamPoint;

    // An emitter whose particles are the points of a beam: struck as
    // segments + 1 points at once (all with the first's lifetime), placed
    // each step along a curve from start to end with noise across it, and
    // struck again when they die, with new noise. A beam is drawn as one
    // ribbon; its points are placed, not moved, so it takes no rate,
    // bursts, distance rate, events in, host spawns, forces, collisions,
    // steering, contacts, pair forces or birth events (refused at enable).
    // Its emitter's capacity, ribbon ids and ribbon head are as theirs say,
    // and it has no units or rigid bodies.
    typedef struct mvfxBeamDef
    {
        bool enable;
        // From 1 to MVFX_MAX_BEAM_SEGMENTS.
        int32_t segments;
        mvfxBeamPoint start;
        mvfxBeamPoint end;
        // 0 for a line, 1 for a quadratic and 2 for a cubic Bezier curve
        // through the controls.
        int32_t controlCount;
        mvfxBeamPoint controls[2];
        // Noise displacing each point across the beam, faded to 0 at both
        // ends: its size in the instance's space (from 0 to
        // MVFX_MAX_EXTENT), its cycles over the beam (from 0 to 1,000) and
        // a second (finite, at most 1,000 in size).
        float noiseAmplitude;
        float noiseFrequency;
        float noiseSpeed;
    } mvfxBeamDef;

    // How an expanded sprite's quad is turned (mvfxInstance_ExpandSprites).
    typedef uint8_t mvfxBillboard;
    enum
    {
        // In the view's plane, upright on its up.
        mvfx_billboardCamera = 0,
        // In the view's plane, turned about its forward by the particle's
        // angle.
        mvfx_billboardRotated = 1,
        // Upright on the world's Y, turned about it to face the eye.
        mvfx_billboardAxisY = 2,
        // In the plane of the particle's orientation's X and Y.
        mvfx_billboardFixed = 3,
        // High along the particle's velocity, turned about it to face the
        // eye; stretched by its speed.
        mvfx_billboardDirectional = 4,
    };

    // How an emitter's particles are expanded into quads.
    typedef struct mvfxSpriteDef
    {
        mvfxBillboard billboard;
        // A directional quad's height grows by this times its speed (from 0
        // to MVFX_MAX_EXTENT seconds), so it streaks the way it moves.
        float stretch;
    } mvfxSpriteDef;

// The most quality levels an effect has below its full one.
#define MVFX_MAX_QUALITY_LEVELS 8

    // A quality level of an effect: what it scales and leaves out.
    typedef struct mvfxQualityLevel
    {
        // Each emitter's capacity times this, rounded down and at least 1
        // (a beam's kept whole); above 0 and at most 1.
        float capacityScale;
        // Each emitter's rate, distance rate and burst counts times this
        // (counts rounded to nearest, halves up); from 0 to 1.
        float rateScale;
        // Bit e leaves emitter e out: it has no particles, spawns nothing,
        // and the host's spawns into it are refused and counted.
        uint64_t omitted;
    } mvfxQualityLevel;

// The most meshes an emitter's particles choose from.
#define MVFX_MAX_MESH_INDEX 65536

    // The lights an emitter's particles carry: every n-th particle by
    // serial, so the same particles keep their lights from step to step.
    typedef struct mvfxLightDef
    {
        bool enable;
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

    // How an emitter's live particles pour into a gas of the effect: each
    // adds density, temperature and fuel a second (each finite and at most
    // MVFX_MAX_EXTENT in size) to the 8 cells nearest it, and pulls the 8
    // faces across each axis nearest it toward its velocity by drag a
    // second (0 to MVFX_MAX_DRAG), each by trilinear weights, in the
    // particles' order, every substep of the gas; nothing from one outside
    // the gas's box (a 2D gas's reaches along its Z without end).
    typedef struct mvfxGasSplat
    {
        bool enable;
        // The gas: its index in the effect's def.
        int32_t gas;
        float density;
        float temperature;
        float fuel;
        float drag;
    } mvfxGasSplat;

    // How an emitter's particles meet a water surface of the effect, as
    // the last water step left it. A particle is in the water where some of
    // it is under the surface over it, over the grid (on shallow water, its
    // center above the ground): its share under runs from 0 with its center
    // its radius r above the surface to 1 with it r below. There buoyancy,
    // the water's density over the particle's, lifts it by buoyancy times
    // gravity's strength times that share, up the surface's Y (taken
    // implicitly over the step where the share is partial, so a stiff band
    // settles), and it is drawn toward the water's velocity there by drag
    // times its share a second: at rest it floats with its center r (2 /
    // buoyancy - 1) below the surface (half under at 2), and below 1 it
    // sinks. A particle whose step (each substep) goes from above the surface to
    // below it, both ends over the grid, crosses it where its height over
    // the surface, linear along the step, is 0, if there is water there (on
    // shallow water, its ground below its surface), once a step at most: the
    // surface's next step pushes it there, as mvfxInstance_PushWater would,
    // with radius R, the larger of its radius and a cell, and depth push
    // (radius / R)^2 (not counted against MVFX_MAX_WATER_PUSHES); with
    // pour, on shallow water only, its volume, a sphere of its radius,
    // joins the depth of the four cells about the crossing and it dies;
    // with mvfx_eventMaskWater it writes an mvfx_eventWater event there.
    // The memory for crossings is 20 bytes a particle of its capacity.
    typedef struct mvfxWaterCoupling
    {
        bool enable;
        // The surface: its index in the effect's def.
        int32_t water;
        // Its particles' radius in its space, above 0 and at most
        // MVFX_MAX_EXTENT.
        float radius;
        // From 0 to MVFX_MAX_EXTENT.
        float push;
        bool pour;
        // From 0 to MVFX_MAX_BUOYANCY.
        float buoyancy;
        // From 0 to MVFX_MAX_DRAG.
        float drag;
    } mvfxWaterCoupling;

    typedef struct mvfxEmitterDef
    {
        uint32_t cookie;
        // The particles it holds alive at once, from 1 to
        // MVFX_MAX_EMITTER_CAPACITY; a spawn past them is refused and
        // counted. With units it counts their points, a multiple of a
        // unit's (mvfxUnitDef); with a beam, at least its segments plus 1
        // (mvfxBeamDef).
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
        // (mvfxInstance_Spawn), from 0 to its capacity (with units, a count
        // of units, at most the units its capacity holds), reserved in each
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
        // How they pour into a gas.
        mvfxGasSplat gasSplat;
        // How they meet a water surface.
        mvfxWaterCoupling water;
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
        // mvfx_eventMaskBirth, mvfx_eventMaskDeath, mvfx_eventMaskCollision
        // (collisions only where they collide), mvfx_eventMaskTear (tears
        // only of units that tear) and mvfx_eventMaskWater (crossings only
        // where they meet a water surface), at most eventCapacity a
        // context step, whatever steps the instance took in it (a liquid
        // gas's substeps; from 1 to MVFX_MAX_EVENTS when any is asked; the
        // rest counted dropped). The host reads them after the context step
        // (mvfxInstance_GetEvents).
        uint8_t events;
        int32_t eventCapacity;
        // Spawning from another emitter's events.
        mvfxEventSpawn eventSpawn;
        // Finding its neighbors.
        mvfxNeighborDef neighbors;
        // Flocking and steering.
        mvfxSteeringDef steering;
        // Its particles kept apart.
        mvfxContactDef contacts;
        // Its particles pulling and pushing each other.
        mvfxPairForceDef pairForce;
        // The lights its particles carry (mvfxInstance_WriteLights).
        mvfxLightDef light;
        // Its ribbons (mvfxInstance_WriteRibbons).
        mvfxRibbonDef ribbon;
        // Its plexus links.
        mvfxPlexusDef plexus;
        // What each of its births is: a particle, or a unit of them.
        mvfxUnitDef unit;
        // Whether its particles are rigid bodies (particles, not units).
        mvfxRigidDef rigid;
        // Whether its particles are a liquid (particles, not units nor
        // rigid bodies).
        mvfxLiquidDef liquid;
        // A beam through its particles.
        mvfxBeamDef beam;
        // How its particles expand into quads.
        mvfxSpriteDef sprite;
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
        // set: size per axis (X, Y, Z; for a rigid sphere or capsule, its
        // ends on the axes its shape does not set those of X, else the
        // effect is refused, mvfxRigidShape), linear
        // color (red, green, blue, alpha), a sprite's angle in radians and
        // spin in radians a second (either sets both, the other 0).
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
        // Its emitters, from 0 to MVFX_MAX_EMITTERS, each from
        // mvfxDefaultEmitterDef, NULL when emitterCount is 0. The effect
        // copies what it needs: the array may be released once the effect
        // is created.
        const mvfxEmitterDef* emitters;
        int32_t emitterCount;
        // The defaults of its instances' parameters, four floats each, finite,
        // each keeping the sources reading it within their caps (value.h's
        // MVFX_MAX_DRAW); at most MVFX_MAX_PARAMETERS, NULL when
        // parameterCount is 0.
        const float* parameters;
        int32_t parameterCount;
        // Its quality levels below the full one: level 0 is the def as given,
        // level k from 1 is qualityLevels[k - 1], each meant to cost less
        // than the one before; from 0 to MVFX_MAX_QUALITY_LEVELS, NULL when
        // qualityLevelCount is 0. An instance plays at one level, chosen at
        // its creation (mvfxInstanceDef.quality and the context's budget).
        const mvfxQualityLevel* qualityLevels;
        int32_t qualityLevelCount;
        // Its gases, from 0 to MVFX_MAX_GASES, each from mvfxDefaultGasDef,
        // NULL when gasCount is 0; an effect has at least one emitter, gas
        // or water surface. The effect copies them.
        const mvfxGasDef* gases;
        int32_t gasCount;
        // Its water surfaces, from 0 to MVFX_MAX_WATERS, each from
        // mvfxDefaultWaterDef, NULL when waterCount is 0. The effect copies
        // them.
        const mvfxWaterDef* waters;
        int32_t waterCount;
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

// The most steps of a field's path an effect refusal holds: from
// mvfxEffectDef down to the field, a step for each struct the path enters.
#define MVFX_MAX_FIELD_DEPTH 16

    // Why an effect def was refused, and the field refused: its status,
    // and the path of the field's tags (tools/file-tags.txt, the effect
    // file's tags, which keep their meaning across versions) from
    // mvfxEffectDef down to it, tags[0] a field of mvfxEffectDef. A step
    // into an array or a fixed array of structs names its item in items,
    // -1 at a step that names no item. A refusal of a whole struct or
    // item (a def without its cookie, an array's item) ends at that struct
    // or item; one of no field (a NULL argument, the context's effects
    // full, the allocator's failure, an effect file's header) holds no
    // tags. A rule between fields names the field whose header comment
    // states it, a reference to another emitter, gas or water surface the
    // field that refers. emitter, gas and water name the emitter, gas or
    // water surface the field is in, -1 for none. The steps past tagCount
    // hold 0.
    // mvfxGetEffectFieldPath writes the path as text.
    typedef struct mvfxEffectRefusal
    {
        mvfxResult result;
        int32_t emitter;
        int32_t gas;
        int32_t water;
        int32_t tagCount;
        uint32_t tags[MVFX_MAX_FIELD_DEPTH];
        int32_t items[MVFX_MAX_FIELD_DEPTH];
    } mvfxEffectRefusal;

    /// Writes the path of the field a refusal names as text, each field by
    /// its name in the headers and each item by its index:
    /// `emitters[2].overLife.size.curve.keys[3].time`. A refusal that
    /// names no field writes an empty path. The text is ASCII of the
    /// length given, not terminated.
    ///
    /// @param refusal    The refusal, as a creation or a file call wrote it.
    /// @param textOut    Receives the text; NULL when capacity is 0.
    /// @param capacity   Its size in bytes, at least 0.
    /// @param lengthOut  Receives the text's length in bytes, also when the
    ///                   capacity is too small; 0 on another failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument or a
    ///         refusal whose path names no field of this version (a tag it
    ///         does not know, an item given where the step names none or
    ///         missing where it does, a tag count past
    ///         MVFX_MAX_FIELD_DEPTH); `mvfx_errorCapacity` when the text
    ///         does not fit. On failure the whole capacity is zeroed.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxGetEffectFieldPath(const mvfxEffectRefusal* refusal,
                                                              char* textOut, int64_t capacity,
                                                              int64_t* lengthOut);

    /// Creates an effect from a def: checks every field of it and of its
    /// emitters, then compiles it, allocating its compiled form in one block
    /// from the context's allocator.
    ///
    /// @param context     The context that owns the effect.
    /// @param def         The def, from mvfxDefaultEffectDef.
    /// @param effectOut   Receives the effect's id; the null id on failure.
    /// @param refusalOut  Optional: receives why the def was refused and the
    ///                    field refused; on success, mvfx_success and no
    ///                    field.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a
    ///         def or an emitter def without its cookie, a field out of its
    ///         range or an invalid curve, which counts as misuse when the
    ///         context is given; `mvfx_errorLimit` for a field past one of
    ///         the library's named bounds (`MVFX_MAX_*`, `MVFX_MIN_*`), or a
    ///         rigid hull past MVFX_MAX_HULL_VERTICES, which counts none; the
    ///         first check the def fails decides between the two;
    ///         `mvfx_errorStale` for a mesh shape's mesh that names no live
    ///         mesh, `mvfx_errorInvalid` too for a mesh without the elements
    ///         its mode needs; `mvfx_errorCapacity` when the context holds as
    ///         many effects as its def allows, or when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateEffect(mvfxContext* context,
                                                        const mvfxEffectDef* def,
                                                        mvfxEffectId* effectOut,
                                                        mvfxEffectRefusal* refusalOut);

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

    /// Bakes a turbulence's curl noise into a grid's values over one period
    /// of a periodic lattice, for a vector field force that repeats it
    /// without a seam: the grid's n points along an axis at i / n of the
    /// period, as the repeat border reads them; the noise's octaves, gain
    /// and seed as a turbulence force takes them (an instance's seed is not
    /// mixed in), each octave's lattice wrapped every period times the
    /// lacunarity to the octave cells. A value is the turbulence's curl at
    /// that place in lattice units bit for bit, but where an octave puts it
    /// in its period's last cell, whose far corners wrap to the first. The
    /// field's box spans period / frequency along each axis; the noise's
    /// scroll is not baked (the host may move the box).
    ///
    /// @param noise       A turbulence's noise, its lacunarity a whole
    ///                    number.
    /// @param period      The first octave's period in lattice cells, from 1
    ///                    to MVFX_MAX_NOISE_PERIOD.
    /// @param size        The grid's points along X, Y and Z, each from 1 to
    ///                    MVFX_MAX_GRID_SIZE, at most MVFX_MAX_GRID_POINTS in
    ///                    all.
    /// @param valuesOut   Receives three values a point, X fastest, then Y,
    ///                    then Z.
    /// @param valueCount  The values the array holds, at least three a point.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a
    ///         noise out of its ranges or with a lacunarity that is not
    ///         whole, a period or size below 1, or too few values;
    ///         `mvfx_errorLimit` for a noise past its named bounds, a period
    ///         past MVFX_MAX_NOISE_PERIOD, a size past MVFX_MAX_GRID_SIZE or
    ///         more points than MVFX_MAX_GRID_POINTS; nothing is written
    ///         then.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxBakeCurlNoise(const mvfxNoise* noise, int32_t period,
                                                         const int32_t size[3], float* valuesOut,
                                                         int64_t valueCount);

#ifdef __cplusplus
}
#endif

#endif
