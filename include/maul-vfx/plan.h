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
#include "maul-vfx/water.h"

#ifdef __cplusplus
extern "C"
{
#endif

// The most steps a planned instance's plans for one of the context's steps
// hold (mvfxInstance_GetPlanSteps): an instance whose effect has a liquid
// gas cuts each of its steps into at most MVFX_MAX_LIQUID_SUBSTEPS, and a
// context's step takes at most two of its steps.
#define MVFX_MAX_PLAN_STEPS (2 * MVFX_MAX_LIQUID_SUBSTEPS)

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
        // the emitter's duration read (its loop's share, 0 before its start
        // delay ends; or seconds where it has no duration, negative before
        // it begins).
        float sourceAge;
        // The host's particle it is, an index into the plan's host births
        // (all ones for one of the emitter's own).
        uint32_t host;
        // The key an event's birth draws its values by, in two words, its
        // high word's top bit set: the simulation sets it in the births it
        // merges from events. 0 in a plan, whose births draw by their
        // serials.
        uint32_t eventKeyLow;
        uint32_t eventKeyHigh;
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
        // the border its grid reads past it. A gas force's: the particles'
        // origin in the gas's box's frame, and its cell's edge along each
        // axis (its turn none).
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
        bool enable;
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
        // For units held to the host's frames, the unit's frame
        // (mvfxSpawnRequest.attachFrames); 0 otherwise.
        int32_t frame;
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
    // old enough), for mvfx_killSlower; 0 for every other kind.
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

    // A beam as resolved for a plan's step (mvfxBeamDef), its points
    // placed at the plan's end: its curve's points in the instance's
    // space (the start, the controls, the end: controlCount + 2 of them),
    // and the noise's time coordinate then (the seconds of the clock
    // times its speed) as the lattice takes it, a cell wrapped to 32 bits
    // and the fraction past it.
    typedef struct mvfxPlannedBeam
    {
        float points[4][3];
        uint32_t timeCell;
        float timeFraction;
    } mvfxPlannedBeam;

    // Steering's targets as resolved for a plan's step (mvfxSteeringDef),
    // in the particles' space at the plan's start: the seek and flee
    // points, the path's points (pointCount of them; none when it has no
    // weight), its segments (one less than its points for an open path)
    // and a closed path's length, summed over its segments in order; the
    // cosine of the view's half angle (-1 for a view that sees all).
    typedef struct mvfxPlannedSteering
    {
        float seek[3];
        float flee[3];
        float path[MVFX_MAX_PATH_POINTS][3];
        int32_t pointCount;
        int32_t segmentCount;
        float perimeter;
        float viewCosine;
    } mvfxPlannedSteering;

    // A particle liquid's kernels as resolved for a plan's step (its
    // lengths in the particles' space: a world-space emitter's scale with
    // its instance at the step's end): the kernel radius; the unit the
    // kernels measure lengths in, the power of two that leaves the radius
    // from 1 to 2 in it (an offset between particles divided by it, a
    // correction multiplied); and in that unit the radius's square,
    // poly6's and the spiky gradient's factors, the inverse of the rest
    // density, the constraint's softening, the tensile correction's
    // inverse reference and scale; the spacing; the scale. Measured in
    // the unit, each stays within binary32 over the radius's whole range.
    // Zeros for an emitter without one.
    typedef struct mvfxPlannedLiquid
    {
        float radius;
        float unit;
        float square;
        float poly6;
        float spiky;
        float restInverse;
        float softening;
        float tensileInverse;
        float tensileScale;
        float spacing;
        float sigma;
    } mvfxPlannedLiquid;

    // An emitter's water surface (mvfxEmitterDef.water) as resolved for a
    // plan: the surface it meets, -1 for none; the map of the surface's
    // frame, where it stood at the step's start, into the particles' space
    // as stored, a place p going to origin + rotation (sigma p), the
    // origin rounded from binary64; the surface's Y in the particles'
    // space (unit length); the particles' radius in the surface's lengths
    // and gravity's strength in them; the coupling's drag and buoyancy,
    // the push a crossing gives in the surface's lengths, and whether a
    // crossing pours.
    typedef struct mvfxPlannedWaterCoupling
    {
        int32_t water;
        float origin[3];
        float rotation[4];
        float sigma;
        float up[3];
        float radius;
        float strength;
        float drag;
        float buoyancy;
        float push;
        bool pour;
    } mvfxPlannedWaterCoupling;

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
        // capacity's births at one tick, twice the capacity in a plan; in
        // units for an emitter of units, a birth a unit) a birth is dropped
        // here, still owed; birthsDropped counts them. For an
        // emitter that spawns from events they are its own and the host's
        // only: the simulation builds the births events give and numbers
        // them all (mvfxPlannedBirth).
        const mvfxPlannedBirth* births;
        int32_t birthCount;
        uint64_t birthsDropped;
        // The host's particles its births name, staying as the births do.
        const mvfxPlannedHostBirth* hostBirths;
        int32_t hostBirthCount;
        // For units held to the host's frames, the frames the host set
        // (mvfxInstance_SetAttachFrames) at the plan's end in the
        // particles' space, seven floats each (an origin, then a
        // rotation), staying as the births do; none otherwise.
        const float* attachFrames;
        int32_t attachFrameCount;
        // For an emitter that spawns from events, the ticks from the plan's
        // start whose events give births: from spawnFirst to spawnLast,
        // within its delay and loops and up to the instance's stop (none
        // when spawnFirst is past spawnLast).
        int64_t spawnFirst;
        int64_t spawnLast;
        // The serials its births took to the plan's end, dropped or not:
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
        // The ribbon counter as the host moved it on
        // (mvfxInstance_BreakRibbons, a teleport): a simulation elsewhere
        // adds the breaks it finds itself, the planned instance finding
        // none.
        uint32_t ribbonCounter;
        // Whether the current ribbon ends at a pinned head now (its def's
        // pinHead, while the emitter may still spawn), and the head's
        // place in the world: as mvfxInstance_WriteRibbons would find them
        // when the plan is read.
        bool ribbonHead;
        double ribbonHeadPlace[3];
        // Its beam as resolved (zeros for an emitter without one).
        mvfxPlannedBeam beam;
        // Its steering's targets as resolved (zeros for an emitter that
        // does not steer).
        mvfxPlannedSteering steering;
        // Its particle liquid's kernels as resolved.
        mvfxPlannedLiquid liquid;
        // Its water surface's coupling as resolved (water -1 for none).
        mvfxPlannedWaterCoupling water;
        // The mask of its neighbor grid's keys (a cell's key is pcg4d of
        // its coordinates, the grid's salt, masked): the least power of
        // two at least twice the capacity, less one; 0 when its view's
        // gridCell is 0.
        uint32_t gridMask;
    } mvfxEmitterPlan;

    // An emitter pouring into a gas (its mvfxGasSplat) as resolved for a
    // gas plan: the map of its particles into the box's frame as they are
    // stored at the step's end (a world emitter's offsets from its anchor
    // after the step), a place p going to origin + rotation (sigma p) and
    // a velocity v to rotation (sigma v), the origin rounded from
    // binary64; its splat's rates a second (density, temperature, fuel)
    // and drag.
    typedef struct mvfxPlannedPour
    {
        int32_t emitter;
        float origin[3];
        float rotation[4];
        float sigma;
        float rates[3];
        float drag;
    } mvfxPlannedPour;

    // A gas's step as resolved for a plan (mvfxInstance_GetGasPlan),
    // in its box's frame (mvfxInstance_GetGasInfo places the box): what
    // the CPU path's gas step takes from the instance, resolved once at
    // the end of the context's step, as the CPU path resolves it there.
    typedef struct mvfxGasPlan
    {
        // The first tick the plan covers and how many (the emitters'
        // plans'), which the gas's substeps split; a substep starting
        // before the instance's stop tick pours the gas's sources.
        int64_t start;
        int64_t ticks;
        int64_t stopTick;
        // Whether the instance was cleared before the plan (stopped with
        // mvfx_stopAndClear, restarted, or just created): the simulation
        // empties the gas first.
        bool cleared;
        // The gravity in the box's frame.
        float gravity[3];
        // Its forces as resolved for the step, in the box's frame: a
        // face takes them at its place.
        int32_t forceCount;
        mvfxPlannedForce forces[MVFX_MAX_FORCES];
        // Its obstacles as resolved at the step's end, its own then the
        // instance's (with its def's hostObstacles), the first
        // ownObstacleCount its own; and as resolved at the last plan's
        // end, which obstaclesKnown says (not after a clear or a
        // teleport): a solid's velocity is its move between the two.
        int32_t obstacleCount;
        int32_t ownObstacleCount;
        mvfxPlannedCollider obstacles[2 * MVFX_MAX_COLLIDERS];
        bool obstaclesKnown;
        mvfxPlannedCollider obstaclesBefore[2 * MVFX_MAX_COLLIDERS];
        // Whether each obstacle is the last plan's of its index moved
        // (that one known, of the same kind, sizes and grid): only such a
        // one's solid has a velocity, else 0.
        bool obstacleMoved[2 * MVFX_MAX_COLLIDERS];
        // The emitters pouring into it, in the effect's order: each
        // substep splats each one's live particles, in its pool's order,
        // after the sources.
        int32_t pourCount;
        mvfxPlannedPour pours[MVFX_MAX_EMITTERS];
        // A liquid gas's particles, or those a surface gas wraps: their
        // emitter's map into the box, as a pour's (no rates or drag);
        // emitter -1 for smoke.
        mvfxPlannedPour liquid;
    } mvfxGasPlan;

    // A host's push on a water surface (mvfxInstance_PushWater) as resolved
    // for a plan, in the surface's frame where it stands at the step's end:
    // the point's X and Z, the radius and depth in the surface's lengths.
    typedef struct mvfxPlannedWaterPush
    {
        float at[2];
        float radius;
        float depth;
    } mvfxPlannedWaterPush;

    // A water surface's step as resolved for a plan
    // (mvfxInstance_GetWaterPlan), in its frame (mvfxInstance_GetWaterInfo
    // places it): what the CPU path's water step takes from the instance,
    // resolved where it steps, after every emitter of the step.
    typedef struct mvfxWaterPlan
    {
        // The first tick the plan covers and how many; a shallow water's
        // step starting before the instance's stop tick pours its sources.
        int64_t start;
        int64_t ticks;
        int64_t stopTick;
        // Whether the instance was cleared before the plan (stopped with
        // mvfx_stopAndClear, restarted, or just created): the simulation
        // stills and empties the surface first, and shallow water fills it
        // again at this step.
        bool cleared;
        // Its substeps, each one's length in seconds, and whether the step
        // is slowed (its time past MVFX_MAX_WATER_SUBSTEPS of the longest
        // stable substep): the waves' from the def, shallow water's from
        // the speed reported (mvfxInstance_SetWaterSpeed).
        int32_t substeps;
        float dt;
        bool slowed;
        // The host's pushes this step, in order; for waves, the sum of what
        // they sink over the cells (binary64, cell by cell, push by push,
        // as the CPU path sums it), which the surface then rises by evenly
        // over its cells with what the particles' crossings sink.
        int32_t pushCount;
        mvfxPlannedWaterPush pushes[MVFX_MAX_WATER_PUSHES];
        double pushSank;
        // Shallow water: the depth each source adds this step (rate times
        // the step's seconds, rounded once), 0 for each from the stop tick
        // on.
        float sourceDepths[MVFX_MAX_WATER_SOURCES];
        // Shallow water's ground: its terrain resolved into the surface's
        // frame (its own, or with hostTerrain the instance's first
        // heightfield collider), or none (enableTerrain false) for the flat
        // floor at the grid's center.
        bool enableTerrain;
        mvfxPlannedCollider terrain;
        // The surface's volume in the world's lengths over its own: its
        // scale cubed where it stands at the step's end, which a volume
        // the particles pour takes into the world's (binary64 from the
        // scale's binary32, as the CPU path's mvfxWaterInfo.volumePoured
        // counts).
        double volumeScale;
    } mvfxWaterPlan;

    // A water surface of an effect as compiled (mvfxEffect_GetWaterView),
    // for a simulation elsewhere.
    typedef struct mvfxWaterView
    {
        // Its def as compiled: its sources point at the effect's copies,
        // valid while the effect lives.
        mvfxWaterDef def;
        // Its cells, and shallow water's faces across X and across Z.
        int64_t cells;
        int64_t faces[2];
    } mvfxWaterView;

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
    // row-major matrix; a mesh shape's mode (its mesh by
    // mvfxInstance_GetEmitterMesh).
    typedef struct mvfxShapeView
    {
        mvfxShapeKind kind;
        mvfxArcMode arcMode;
        bool coneVolume;
        mvfxMeshMode meshMode;
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
        // The ribbon a particle joins (a word) and the ribbon's distance at
        // its birth.
        int16_t ribbon;
        int16_t ribbonDistance;
        // Its neighbors' count (a binary32 value) and the offsets to their
        // mean place and mean velocity (mvfxNeighborDef).
        int16_t neighborCount;
        int16_t neighborCenter;
        int16_t neighborVelocity;
        // A unit's pinned points' anchors (3), and at a unit's first point
        // its frame's origin (3) and rotation (4), its flags and its
        // source (a word each; mvfxUnitDef's pins and attach); a unit's
        // point's torn edges (a word: 1 across, 2 down; mvfxUnitDef's
        // tearStrain).
        int16_t anchor;
        int16_t attach;
        int16_t attachRotation;
        int16_t attachFlags;
        int16_t attachSource;
        int16_t torn;
        // A rigid body's sleep (mvfxRigidDef.sleepSpeed): its still time in
        // seconds, or below 0 asleep (-1 before its first sleeping substep,
        // -2 after), then the acceleration its forces gave it as it fell
        // asleep (3); four words in all.
        int16_t sleep;
        // A grid liquid's particle's affine velocity (APIC's C, nine
        // values, row by row: row k the gradient of velocity component k),
        // which its gas reads and gives back (mvfxLiquidDef.solver).
        int16_t affine;
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
    // value over life where scaled. A birth's colour and size inherited
    // from an event (mvfxEventSpawn) are held to colorCaps and sizeCaps on
    // each channel and axis, below 0 to their negatives: MVFX_MAX_COLOR and
    // MVFX_MAX_EXTENT over the most these values over life scale them, so
    // what a record shows stays within those limits.
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
        float colorCaps[4];
        float sizeCaps[3];
    } mvfxOverLifeView;

    // An emitter's ribbons as compiled (mvfxRibbonDef): how its births
    // join them (mvfx_ribbonNone for an emitter not drawn as ribbons), by
    // the counter broken where a birth lies farther than breakDistance (0
    // for never) from its ribbon's youngest point; the most pieces a
    // segment is cut into and, with a bend angle, the cosine of the turn
    // k + 1 pieces allow (bendCosines[0] below -1.5 for none: every
    // segment cut into all of them); the texture coordinate along, its
    // tile length and scroll; the facing and the normal it faces across
    // (in the particles' space); whether the current ribbon ends at a
    // pinned head; the width over the normalized length (a value over
    // life, read at the length); the colour over it (its baked samples,
    // MVFX_CURVE_SAMPLES of linear RGBA; NULL for white), staying while
    // the effect does.
    typedef struct mvfxRibbonView
    {
        mvfxRibbonIds ids;
        float breakDistance;
        int32_t pieces;
        float bendCosines[MVFX_MAX_RIBBON_PIECES];
        mvfxRibbonTexture texture;
        float tileLength;
        float scroll;
        mvfxRibbonFacing facing;
        float normal[3];
        bool pinHead;
        mvfxLifeView width;
        const float* color;
    } mvfxRibbonView;

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
        // the grid each of its forces reads (a vector field's; the null id
        // for the others), and the gas each follows (a gas force's index
        // in the effect; -1 for the others).
        uint32_t forceKinds;
        mvfxGridId forceGrids[MVFX_MAX_FORCES];
        int32_t forceGases[MVFX_MAX_FORCES];
        // The gas it pours into (its mvfxGasSplat's index in the effect;
        // -1 for none).
        int32_t splatGas;
        // Its liquid as compiled.
        mvfxLiquidDef liquid;
        // Its coupling to a water surface as compiled (not enabled for
        // none).
        mvfxWaterCoupling water;
        // A bit for each kind of collider of its own (1 <<
        // mvfx_colliderPlane and so on), the grid each reads (a
        // heightfield's or a distance grid's; the null id for the others),
        // and whether it collides with the host's scene or a depth image.
        uint32_t colliderKinds;
        mvfxGridId colliderGrids[MVFX_MAX_COLLIDERS];
        bool enableSceneQueries;
        bool enableDepthImage;
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
        mvfxEventMask events;
        int32_t eventCapacity;
        // Its lifetime in seconds as a birth draws it (one component, held
        // to [2^-20, MVFX_MAX_LIFETIME] after the draw), and the births it
        // takes from another emitter's events (eventSpawn.enable), which a
        // simulation of its plan builds, numbers and draws the lives of
        // itself (mvfxPlannedBirth).
        mvfxSourceView lifetime;
        mvfxEventSpawn eventSpawn;
        // Its start delay and loop in ticks (a loop of 0 for none), from
        // which an event birth's source age follows (mvfxPlannedBirth).
        int64_t delayTicks;
        int64_t loopTicks;
        // How it is drawn as ribbons (mvfxEmitterDef's ribbon).
        mvfxRibbonView ribbon;
        // How its points are struck and placed as a beam (mvfxEmitterDef's
        // beam; not enabled for none).
        mvfxBeamDef beam;
        // The lights its particles carry (mvfxEmitterDef's light; not
        // enabled for none).
        mvfxLightDef light;
        // How its particles are expanded into quads (mvfxEmitterDef's
        // sprite).
        mvfxSpriteDef sprite;
        // How it finds neighbors (mvfxEmitterDef's neighbors, its source
        // resolved to an index: its own for -1; not enabled for none), and
        // the cells of the grid its particles make for the emitters that
        // query it (the largest radius among them; 0 when none does).
        mvfxNeighborDef neighbors;
        float gridCell;
        // Its steering (mvfxEmitterDef's steering, the path's points the
        // effect's copy; not enabled for none; a plan resolves its targets),
        // and its pair force: the radius (0 for none) and the strength over
        // the distance as a share of it.
        mvfxSteeringDef steering;
        float pairRadius;
        mvfxLifeView pairStrength;
        // How its particles are kept apart (mvfxEmitterDef's contacts; not
        // enabled for none).
        mvfxContactDef contacts;
        // Whether its particles may be killed within a step: by kill rules,
        // colliders that kill (a depth image's view too) or an attractor's
        // kill radius.
        bool kills;
        // Its plexus links (mvfxEmitterDef's plexus; not enabled for none).
        mvfxPlexusDef plexus;
        // Its units (mvfxEmitterDef's unit): the def, its directions
        // normalized; the points a unit has (1 for particles) and across a
        // row; the rest lengths of its constraints before the instance's
        // scale (across, down, along the diagonal across and down and
        // along the other; bending takes twice the first two).
        mvfxUnitDef unit;
        int32_t unitPoints;
        int32_t unitWidth;
        float unitRests[4];
        // Its rigid particles (mvfxEmitterDef's rigid; not enabled for
        // none), and for a hull its shape at a size of 1 on each axis about
        // its centroid, which a body's size scales along its axes: its
        // vertices (x, y and z each; NULL for none, owned by the effect),
        // its volume and its second moments of volume (xx, yy, zz, xy, xz,
        // yz), and its faces: face f's corners are the vertices
        // hullLoop[hullFaceStart[f]] up to hullFaceStart[f + 1],
        // counterclockwise seen from outside (hullFaceCount + 1 starts),
        // its plane hullPlanes[4 f] on (its outward unit normal and its
        // offset, n . x = d on it).
        mvfxRigidDef rigid;
        int32_t hullVertexCount;
        const float* hullVertices;
        float hullVolume;
        float hullMoments[6];
        int32_t hullFaceCount;
        const uint16_t* hullFaceStart;
        const uint8_t* hullLoop;
        const float* hullPlanes;
    } mvfxEmitterView;

// The most levels of a gas's multigrid: from MVFX_MAX_GAS_SIZE cells along
// an axis halved to 2.
#define MVFX_MAX_GAS_LEVELS 10

    // A gas of an effect as compiled (mvfxEffect_GetGasView), for a
    // simulation elsewhere.
    typedef struct mvfxGasView
    {
        // Its def as compiled: its sources, forces and obstacles point at
        // the effect's copies, valid while the effect lives.
        mvfxGasDef def;
        // Its cells, and along each axis its faces across that axis.
        int64_t cells;
        int64_t faces[3];
        // Its multigrid's levels, the first its own cells: each level's
        // cells along each axis (an axis halved, rounding up, while every
        // axis of more than one cell keeps at least 2).
        int32_t levelCount;
        int32_t levels[MVFX_MAX_GAS_LEVELS][3];
        // For a liquid gas the emitter whose particles move on it, for a
        // surface gas the one whose particles it wraps; -1 for smoke.
        int32_t emitter;
        // A surface gas's buckets of its particles: a bucket's cells along
        // each axis (its reach in cells rounded up, at least 1), and the
        // buckets along each axis (along an axis of more than one cell,
        // those over its cells and one beyond each end; else 1). 0 for
        // other gases.
        int32_t bucket;
        int32_t buckets[3];
    } mvfxGasView;

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

    /// Returns how many steps a planned instance's plans for the context's
    /// last step hold: 1, or for an effect with a liquid gas each step the
    /// instance took, cut into substeps by its liquid gases
    /// (mvfxGasDef.courant), at most MVFX_MAX_PLAN_STEPS. A simulation runs
    /// them in turn, each step every emitter's plan of it
    /// (mvfxInstance_GetEmitterStepPlan) and then the gases'
    /// (mvfxInstance_GetGasStepPlan).
    ///
    /// @param context   The context.
    /// @param instance  A planned instance.
    /// @param countOut  Receives the count; 0 on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument;
    ///         `mvfx_errorStale` for an instance that is gone;
    ///         `mvfx_errorUnsupported` for an instance that is not planned.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetPlanSteps(const mvfxContext* context,
                                                                 mvfxInstanceId instance,
                                                                 int32_t* countOut);

    /// Returns an emitter's plan for one step of the context's last
    /// (mvfxInstance_GetPlanSteps), as the CPU path takes that step: its
    /// span; the instance's move over the first step alone, standing still
    /// after it; its births, their ticks and deaths from its start; the
    /// serials owed and births dropped by its end; its forces resolved at
    /// its start; the rest as at the plan's end. With one step it is the
    /// plan mvfxInstance_GetEmitterPlan returns. The births stay valid
    /// until the context's next step or the instance's destruction.
    ///
    /// @param context   The context.
    /// @param instance  A planned instance.
    /// @param emitter   The emitter's index in its effect.
    /// @param step      The step's index, below the plans' step count.
    /// @param planOut   Receives the plan; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, an
    ///         emitter or a step out of range; `mvfx_errorStale` for an
    ///         instance that is gone; `mvfx_errorUnsupported` for an
    ///         instance that is not planned.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetEmitterStepPlan(const mvfxContext* context,
                                                                       mvfxInstanceId instance,
                                                                       int32_t emitter,
                                                                       int32_t step,
                                                                       mvfxEmitterPlan* planOut);

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
    ///         that is gone; `mvfx_errorUnsupported` for an instance that
    ///         is not planned; `mvfx_errorState` for one whose plans hold
    ///         more than one step (mvfxInstance_GetPlanSteps), each with
    ///         its own plan.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetEmitterPlan(const mvfxContext* context,
                                                                   mvfxInstanceId instance,
                                                                   int32_t emitter,
                                                                   mvfxEmitterPlan* planOut);

    /// Returns a gas of an effect as compiled.
    ///
    /// @param context  The context.
    /// @param effect   The effect.
    /// @param gas      The gas's index in the effect.
    /// @param viewOut  Receives the view; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument or
    ///         a gas out of range; `mvfx_errorStale` for an effect that is
    ///         gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxEffect_GetGasView(const mvfxContext* context,
                                                             mvfxEffectId effect, int32_t gas,
                                                             mvfxGasView* viewOut);

    /// Returns a gas's plan for the context's last step.
    ///
    /// @param context  The context.
    /// @param instance A planned instance.
    /// @param gas      The gas's index in its effect.
    /// @param planOut  Receives the plan; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument or a
    ///         gas out of range; `mvfx_errorStale` for an instance that is
    ///         gone; `mvfx_errorUnsupported` for an instance that is not
    ///         planned; `mvfx_errorState` for one whose plans hold more
    ///         than one step (mvfxInstance_GetPlanSteps), each with its own
    ///         plan.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetGasPlan(const mvfxContext* context,
                                                               mvfxInstanceId instance, int32_t gas,
                                                               mvfxGasPlan* planOut);

    /// Returns a gas's plan for one step of the context's last
    /// (mvfxInstance_GetPlanSteps), as the CPU path takes that step: its
    /// span; a clear in the first step alone; its obstacles before the
    /// step the last plan's in the first, then those the first left, the
    /// instance standing still after it; its forces resolved at its start;
    /// the rest as at the plan's end. With one step it is the plan
    /// mvfxInstance_GetGasPlan returns.
    ///
    /// @param context  The context.
    /// @param instance A planned instance.
    /// @param gas      The gas's index in its effect.
    /// @param step     The step's index, below the plans' step count.
    /// @param planOut  Receives the plan; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a
    ///         gas or a step out of range; `mvfx_errorStale` for an
    ///         instance that is gone; `mvfx_errorUnsupported` for an
    ///         instance that is not planned.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetGasStepPlan(const mvfxContext* context,
                                                                   mvfxInstanceId instance,
                                                                   int32_t gas, int32_t step,
                                                                   mvfxGasPlan* planOut);

    /// Tells a planned instance whether its simulation's gas holds
    /// anything (a value of a field past nothing) at the end of a step: a
    /// stopped instance runs while a gas holds anything, which only the
    /// simulation knows. A planned gas holds until told otherwise, so an
    /// instance whose simulation never reports runs on after it stops,
    /// and one whose does finishes as late as the report comes. That a
    /// gas holds always counts; that it holds nothing counts only for a
    /// step that ended at or after the instance's stop tick and every
    /// last death the plans give the emitters splatting into the gas,
    /// past which nothing pours into it, and is otherwise ignored.
    ///
    /// @param context  The context.
    /// @param instance A planned instance.
    /// @param gas      The gas's index in its effect.
    /// @param tick     The tick of the step's end the report is of, at
    ///                 most the instance's clock.
    /// @param holds    Whether the gas held anything then.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL context, a
    ///         gas out of range or a tick past the clock or below 0, which
    ///         counts as misuse when the context is given;
    ///         `mvfx_errorStale` for an instance that is gone;
    ///         `mvfx_errorUnsupported` for an instance that is not planned.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetGasHolds(mvfxContext* context,
                                                                mvfxInstanceId instance,
                                                                int32_t gas, int64_t tick,
                                                                bool holds);

    /// Tells a planned instance its liquid gas's fastest particle speed at
    /// the end of a step, which its simulation measures: the instance
    /// counts the substeps it cuts its steps into (mvfxGasDef.courant)
    /// from the speed of the latest step reported, as an instance it
    /// simulates counts them from its particles' speeds at each step's
    /// start. While no particle of the gas's emitter lives the speed is
    /// 0, as there; while one lives and nothing is reported yet, each step
    /// takes MVFX_MAX_LIQUID_SUBSTEPS. A report older than the latest one
    /// is ignored.
    ///
    /// @param context  The context.
    /// @param instance A planned instance.
    /// @param gas      A liquid gas's index in its effect.
    /// @param tick     The tick of the step's end the report is of, at
    ///                 most the instance's clock.
    /// @param speed    The fastest particle's speed then, in its emitter's
    ///                 units a second, finite and at least 0.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL context, a
    ///         gas out of range or not a liquid, a tick past the clock or
    ///         below 0 or a speed not finite or below 0, which counts as
    ///         misuse when the context is given; `mvfx_errorStale` for an
    ///         instance that is gone; `mvfx_errorUnsupported` for an
    ///         instance that is not planned.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetLiquidSpeed(mvfxContext* context,
                                                                   mvfxInstanceId instance,
                                                                   int32_t gas, int64_t tick,
                                                                   float speed);

    /// Returns a water surface of an effect as compiled.
    ///
    /// @param context  The context.
    /// @param effect   The effect.
    /// @param water    The surface's index in the effect.
    /// @param viewOut  Receives the view; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument or
    ///         a surface out of range; `mvfx_errorStale` for an effect that
    ///         is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxEffect_GetWaterView(const mvfxContext* context,
                                                               mvfxEffectId effect, int32_t water,
                                                               mvfxWaterView* viewOut);

    /// Returns a water surface's plan for the context's last step.
    ///
    /// @param context  The context.
    /// @param instance A planned instance.
    /// @param water    The surface's index in its effect.
    /// @param planOut  Receives the plan; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument or a
    ///         surface out of range; `mvfx_errorStale` for an instance that
    ///         is gone; `mvfx_errorUnsupported` for an instance that is not
    ///         planned; `mvfx_errorState` for one whose plans hold more
    ///         than one step (mvfxInstance_GetPlanSteps), each with its own
    ///         plan.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetWaterPlan(const mvfxContext* context,
                                                                 mvfxInstanceId instance,
                                                                 int32_t water,
                                                                 mvfxWaterPlan* planOut);

    /// Returns a water surface's plan for one step of the context's last
    /// (mvfxInstance_GetPlanSteps), as the CPU path takes that step after
    /// the step's emitters: its span and substeps; a clear and the host's
    /// pushes in the first step alone; its sources' depths over its span;
    /// the rest as at the plan's end. With one step it is the plan
    /// mvfxInstance_GetWaterPlan returns.
    ///
    /// @param context  The context.
    /// @param instance A planned instance.
    /// @param water    The surface's index in its effect.
    /// @param step     The step's index, below the plans' step count.
    /// @param planOut  Receives the plan; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a
    ///         surface or a step out of range; `mvfx_errorStale` for an
    ///         instance that is gone; `mvfx_errorUnsupported` for an
    ///         instance that is not planned.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetWaterStepPlan(const mvfxContext* context,
                                                                     mvfxInstanceId instance,
                                                                     int32_t water, int32_t step,
                                                                     mvfxWaterPlan* planOut);

    /// Tells a planned instance whether a water surface moved at the end of
    /// a step (a rate, or a slope that will make one; for shallow water a
    /// flow), which only its simulation knows: a stopped instance runs
    /// while a surface moves. A planned surface moves until told
    /// otherwise, so an instance whose simulation never reports runs on
    /// after it stops. That a surface moves always counts; that it is
    /// still counts only for a step that ended at or after the instance's
    /// stop tick and every last death the plans give the emitters whose
    /// particles meet it, past which nothing stirs it but the host's
    /// pushes, and is otherwise ignored.
    ///
    /// @param context  The context.
    /// @param instance A planned instance.
    /// @param water    The surface's index in its effect.
    /// @param tick     The tick of the step's end the report is of, at
    ///                 most the instance's clock.
    /// @param holds    Whether the surface moved then.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL context, a
    ///         surface out of range or a tick past the clock or below 0,
    ///         which counts as misuse when the context is given;
    ///         `mvfx_errorStale` for an instance that is gone;
    ///         `mvfx_errorUnsupported` for an instance that is not planned.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetWaterHolds(mvfxContext* context,
                                                                  mvfxInstanceId instance,
                                                                  int32_t water, int64_t tick,
                                                                  bool holds);

    /// Tells a planned instance a shallow water surface's fastest speed
    /// (its largest face speed plus sqrt(g deepest)) at the end of a step,
    /// which its simulation measures: each step of the next context step
    /// counts its substeps from the speed of the latest step reported, as
    /// an instance it simulates counts them from its own state then. A
    /// surface's first step after a clear counts from no water (one
    /// substep); a later step with nothing reported yet takes
    /// MVFX_MAX_WATER_SUBSTEPS. A report older than the latest one is
    /// ignored.
    ///
    /// @param context  The context.
    /// @param instance A planned instance.
    /// @param water    A shallow water surface's index in its effect.
    /// @param tick     The tick of the step's end the report is of, at
    ///                 most the instance's clock.
    /// @param speed    The speed then, in the surface's lengths a second,
    ///                 finite and at least 0.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL context, a
    ///         surface out of range or not shallow water, a tick past the
    ///         clock or below 0 or a speed not finite or below 0, which
    ///         counts as misuse when the context is given;
    ///         `mvfx_errorStale` for an instance that is gone;
    ///         `mvfx_errorUnsupported` for an instance that is not planned.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetWaterSpeed(mvfxContext* context,
                                                                  mvfxInstanceId instance,
                                                                  int32_t water, int64_t tick,
                                                                  float speed);

    /// Tells a planned instance what a water surface's step measured,
    /// which its simulation counts: its reach (the farthest its surface
    /// stood from rest, in the surface's lengths), which its bounds take,
    /// and the faces shallow water's bound held and the volume the
    /// particles poured, which mvfxInstance_GetWaterInfo's counts add
    /// (each step's once: a report of a tick at or before the latest one
    /// taken is ignored). Its steps slowed the instance counts itself,
    /// from its plans.
    ///
    /// @param context      The context.
    /// @param instance     A planned instance.
    /// @param water        A surface's index in its effect.
    /// @param tick         The tick of the step's end the report is of, at
    ///                     most the instance's clock.
    /// @param reach        The reach then, finite and at least 0.
    /// @param heldFaceCount    The faces held over the step's substeps (0 for
    ///                     waves).
    /// @param volumePoured The volume poured over the step, in the world's
    ///                     lengths cubed, finite and at least 0 (0 for
    ///                     waves).
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL context, a
    ///         surface out of range, a tick past the clock or below 0, or a
    ///         reach or volume not finite or below 0, which counts as
    ///         misuse when the context is given; `mvfx_errorStale` for an
    ///         instance that is gone; `mvfx_errorUnsupported` for an
    ///         instance that is not planned.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetWaterCounts(
        mvfxContext* context, mvfxInstanceId instance, int32_t water, int64_t tick, float reach,
        uint64_t heldFaceCount, double volumePoured);

#ifdef __cplusplus
}
#endif

#endif
