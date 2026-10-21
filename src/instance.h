// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An instance as the step runs it: the effect it plays, its transform, seed
// and clock, and per emitter the particles alive and the counts of what it
// owed and refused. Everything lives in one block allocated when the
// instance is created (record mvfx-0001); nothing grows after.

#ifndef MAUL_VFX_SRC_INSTANCE_H
#define MAUL_VFX_SRC_INSTANCE_H

#include "depth.h"
#include "effect.h"
#include "force.h"
#include "gas_splat.h"
#include "rigid_shape.h"

#include "maul-vfx/instance.h"
#include "maul-vfx/plan.h"

// Where an emitter's rate stands: the loop it is in, the rate birth of
// that loop it spawns next, and the loop tick of the last one it spawned
// (birth ticks never go back, whatever the rounding of a curve's inverse).
typedef struct mvfxRateCursor
{
    uint64_t loop;
    uint64_t next;
    int64_t last;
} mvfxRateCursor;

// Where a burst stands: the loop it is in and the firing of that loop it
// fires next.
typedef struct mvfxBurstCursor
{
    uint64_t loop;
    uint64_t firing;
} mvfxBurstCursor;

// A particle the host queued: its place in the emitter's space, and the
// values it gave (each flag set for a value given).
typedef struct mvfxHostBirth
{
    double position[3];
    float velocity[3];
    float color[4];
    float size[3];
    uint8_t given;
    // An event's birth: its tick, its place in the queue as built, the
    // serial of the event's particle, and the key its values are drawn by
    // in place of its serial (mvfxEventBirthKey).
    int64_t tick;
    int32_t order;
    uint32_t source;
    uint64_t key;
    // A host's unit's frame (mvfx_attachHost).
    int32_t frame;
} mvfxHostBirth;

enum
{
    mvfx_givenVelocity = 1,
    mvfx_givenColor = 2,
    mvfx_givenSize = 4,
    // The velocity is added to the one drawn (an event's inherited share,
    // in the particles' space).
    mvfx_givenAddedVelocity = 8,
};

// One emitter's particles, counts and cursors. Live particles are [0,
// count) of each attribute array, oldest first; owed is also the serial
// of the next birth.
//
// An emitter's neighbor grid: its particles alive at the step's start,
// sorted by their cells' hashed keys (stable, so in their order within a
// key), with their places and velocities copied in that order; entry j's
// key k has table[k] <= j < table[k + 1].
typedef struct mvfxNeighborGrid
{
    int32_t* table;
    uint32_t* keys;
    int32_t* index;
    // Six arrays of the capacity: places x, y, z, then velocities.
    float* values;
    int32_t capacity;
    uint32_t mask;
} mvfxNeighborGrid;

// An emitter's room for its liquid: a grid of the predicted places
// (keeping no values); the predicted places, the places an iteration
// moves to, each particle's multiplier, a velocity pass's output and its
// swirl, thirteen arrays of the capacity, the last twelve as three each.
typedef struct mvfxLiquidRoom
{
    mvfxNeighborGrid grid;
    float* predicted;
    float* moved;
    float* multipliers;
    float* velocities;
    float* swirls;
} mvfxLiquidRoom;

// An emitter's room for its particle contacts: a grid of its places after
// motion (keeping no values), those places, a pass's new places (then
// the impulses), and each particle's radius; three arrays of the capacity
// each for places.
typedef struct mvfxContactRoom
{
    mvfxNeighborGrid grid;
    float* built;
    float* moved;
    float* radii;
} mvfxContactRoom;

// An emitter's plexus links: a grid of its places at the step's end
// (keeping no values), and the links found then, pairs of particle
// indices (the owner first), valid until the next step.
typedef struct mvfxPlexusRoom
{
    mvfxNeighborGrid grid;
    int32_t* links;
    int32_t linkCount;
} mvfxPlexusRoom;

typedef struct mvfxEmitterState
{
    unsigned char* particles;
    // Its capacity at the instance's quality level (0 for an emitter left
    // out), and the bytes of each of its arrays.
    int32_t capacity;
    size_t arrayBytes;
    int32_t count;
    uint64_t owed;
    uint64_t refused;
    mvfxRateCursor rate;
    mvfxBurstCursor* bursts;
    // The mesh a mesh shape spawns from: the effect's, or the instance's
    // own.
    struct mvfxMesh* mesh;
    // A world-space emitter's anchor: its particles' positions are offsets
    // from it.
    double anchor[3];
    // A planned instance's plan: room for twice the capacity's births, the
    // births written, those refused, and the anchor before the plan; and
    // room for the host's queue's particles, those written.
    mvfxPlannedBirth* plan;
    int32_t planCount;
    uint64_t planRefused;
    double planAnchor[3];
    mvfxPlannedHostBirth* planHosts;
    int32_t planHostCount;
    // For an effect with a liquid gas, each of the plan's steps' first
    // birth, and its serials owed and births refused by its end.
    int32_t planStepFirst[MVFX_MAX_PLAN_STEPS];
    uint64_t planStepOwed[MVFX_MAX_PLAN_STEPS];
    uint64_t planStepRefused[MVFX_MAX_PLAN_STEPS];
    // The latest tick a planned birth dies at: the emitter holds
    // particles elsewhere until then, as far as the core knows.
    int64_t lastDeath;
    // The emitter's forces as resolved for the step being taken.
    struct mvfxResolvedForce* forces;
    // Its velocity modules as resolved for the step.
    mvfxResolvedVelocityModules velocityModules;
    // Its water surface as resolved for the step.
    mvfxResolvedWater water;
    // Its kill rules as resolved for the step, and the particles they
    // killed.
    struct mvfxResolvedKillRule* killRules;
    uint64_t killed;
    // Its colliders as resolved for the step (the effect's, then the
    // instance's when it takes them).
    struct mvfxResolvedCollider* colliders;
    int32_t colliderCount;
    // Its scene queries from the last step, the map from the world to its
    // particles' space they were written under, and those dropped.
    struct mvfxSceneQuery* queries;
    int32_t queryCount;
    mvfxSpaceMap queryMap;
    uint64_t queriesDropped;
    // The instance's depth image as resolved for the step, when it takes
    // one and one is set.
    mvfxResolvedDepth depth;
    bool hasDepth;
    // Its events from the last step, and those dropped.
    struct mvfxEvent* events;
    int32_t eventCount;
    uint64_t eventsDropped;
    // Its grid (for an emitter queried), and the queries that were capped.
    mvfxNeighborGrid grid;
    uint64_t neighborsCapped;
    // Its contacts' room, and the particles whose contacts were capped.
    mvfxContactRoom contacts;
    uint64_t contactsCapped;
    // Its plexus links, and those dropped.
    mvfxPlexusRoom plexus;
    uint64_t linksDropped;
    // For an emitter of units, its points' places the integrator predicts
    // in a substep, three arrays of the capacity.
    float* unitPrevious;
    // For one that holds its contacts (mvfxHoldsContacts) or its tears
    // (mvfxHoldsTears): each point's first contact's event of the step,
    // its tears' across and down (two a point), which of them it holds
    // (MVFX_HELD_* bits, event.h), and whether its substeps are running.
    mvfxEvent* unitContacts;
    mvfxEvent* unitTears;
    uint8_t* unitHeld;
    bool holdingContacts;
    bool holdingTears;
    // For units held to frames: the host's frames and how many are set,
    // and each unit's frame at the step's end (seven floats a unit: its
    // origin, then its orientation).
    mvfxAttachFrame* attachFrames;
    // For a planned instance's units held to the host's frames, those
    // frames at the plan's end in the particles' space (seven floats a
    // frame of the capacity).
    float* planFrames;
    int32_t attachFrameCount;
    float* attachTargets;
    // Its units' stretch constraints torn.
    uint64_t torn;
    // Its units' self-collision room: a grid of the places at the step's
    // start (keeping no values), the places at a substep's start and each
    // point's move in a substep's pass (three arrays of the capacity each),
    // each point's candidates (selfContacts each, of other points) and
    // their count; and the points whose candidates were capped.
    mvfxNeighborGrid selfGrid;
    // For rigid particles: seven arrays of the capacity, their places and
    // orientations at a substep's start; five, their inverse masses,
    // inverse inertias and reaches; each one's flags (MVFX_RIGID_*); its
    // contacts with the world (MVFX_MAX_RIGID_CONTACTS each) and their
    // count; and for bodies meeting each other, a grid of their places and
    // a pool of the contacts between them (bodyContacts a body) with its
    // count.
    float* rigidStart;
    float* rigidMass;
    uint8_t* rigidFlags;
    mvfxBodyContact* rigidContacts;
    int32_t* rigidCounts;
    mvfxNeighborGrid rigidGrid;
    mvfxPairContact* rigidPairs;
    int32_t rigidPairCount;
    // The pool's runs (a pair's contacts) coloured for the passes
    // (rigid_pair.c): each run's first contact, the runs in the passes'
    // order, their priorities and colours, each body's runs (where its
    // list starts, then the lists) and the stamps a least free colour is
    // found by, all laid out with the pool; how many runs and colours.
    int32_t* rigidRunStart;
    int32_t* rigidRunOrder;
    uint64_t* rigidRunKeys;
    int32_t* rigidRunColours;
    int32_t* rigidBodyRunsAt;
    int32_t* rigidBodyRuns;
    int32_t* rigidStamps;
    int32_t rigidRunCount;
    int32_t rigidColourCount;
    // A body's candidates for pairs as the grid finds them (an array of
    // the capacity), and the grid's entries the search has examined.
    int32_t* rigidCandidates;
    uint64_t rigidExamined;
    // For sleeping rigid particles: each one's island (an array of the
    // capacity), and the depth image it saw.
    int32_t* rigidIslands;
    uint32_t depthSerial;
    float* selfStart;
    float* selfMoves;
    int32_t* selfPairs;
    int32_t* selfCounts;
    uint64_t selfContactsCapped;
    uint64_t rigidContactsCapped;
    uint64_t rigidSearchesCapped;
    // Its liquid's room.
    mvfxLiquidRoom liquid;
    // The id of its current ribbon by the counter: breaks advance it.
    uint32_t ribbonCounter;
    // A beam's next strike: the tick its points die at.
    int64_t beamStrike;
    // Its particles' box in their space at the step's end, grown by their
    // largest size; empty with none (or fixed bounds).
    float boundsMin[3];
    float boundsMax[3];
    bool hasBounds;
    // The births its source's events give it this step, in tick order.
    mvfxHostBirth* eventQueue;
    int32_t eventQueueCount;
    // The particles the host queued for the next step.
    mvfxHostBirth* hostQueue;
    int32_t hostCount;
    // Distance births: the fraction of one the travel owes past the last
    // (below 1), and the step's plan: the integral the step adds, how many
    // of its births are refused at the start (a jump past the capacity),
    // how many it takes, and the next of those to be born.
    double distanceCarry;
    double distanceDelta;
    double distanceSkip;
    uint64_t distanceTake;
    uint64_t distanceNext;
} mvfxEmitterState;

// Where an instance is: its origin, rotation (x, y, z, w) and scale.
typedef struct mvfxTransform
{
    double origin[3];
    float rotation[4];
    float scale;
} mvfxTransform;

// A birth: the particle's serial and tick, and its place among the births
// of its batch (a burst's firing; a rate's births are batches of one).
typedef struct mvfxBirth
{
    uint64_t serial;
    int64_t tick;
    int64_t batchIndex;
    int64_t batchCount;
    // The host's queued particle it is, or -1.
    int32_t host;
    // The event's birth it is in the emitter's event queue, from 1; 0 for
    // none.
    int32_t event1;
} mvfxBirth;

// An instance.
typedef struct mvfxInstance
{
    mvfxEffect* effect;
    // Where it is at the clock, and where it will be at the end of the next
    // step.
    mvfxTransform from;
    mvfxTransform to;
    // The distance it travels over the step being taken, and its velocity
    // over it.
    double stepDistance;
    float velocity[3];
    // The clock at the start of the step being taken.
    int64_t stepStart;
    uint32_t seed;
    // Its quality level, the rate scale it gives, and the particles its
    // emitters' capacities hold, which count against the context's budget.
    int32_t quality;
    float rateScale;
    // Whether the host sees it, what it does unseen, and the ticks and
    // unseen steps it holds back.
    bool visible;
    mvfxOffscreen offscreen;
    int32_t offscreenInterval;
    int64_t heldTicks;
    uint64_t unseenSteps;
    int64_t particles;
    int64_t clock;
    // Whether it is planned (no particles, a plan an emitter each step), and
    // the first tick of its plans.
    bool planned;
    int64_t planStart;
    // Where it was when its plans began.
    mvfxTransform planFrom;
    // For an effect with a liquid gas (stepped), the plans' steps: how
    // many, each one's first tick (the clock ends the last), and the
    // instance's velocity over the first, the one it moves in.
    bool stepped;
    int32_t planSteps;
    int64_t planStepStart[MVFX_MAX_PLAN_STEPS];
    float planVelocity[3];
    // The tick a plan's births and deaths count from: its start, or for
    // an effect with a liquid gas its step's.
    int64_t planBase;
    // Whether the host cleared it (a stop that clears, a restart) since
    // the last plans began, and whether the last plans begin after a clear.
    bool clearPending;
    bool planCleared;
    // No birth past this tick; INT64_MAX until the instance is stopped.
    int64_t stopTick;
    // Its parameters, from the effect's defaults until the host sets them.
    float parameters[MVFX_MAX_PARAMETERS][4];
    // What the gas being stepped is driven by, in its block (not on the
    // stack, which is small on some targets): its forces resolved, room for
    // the most forces a gas of its effect has, and the emitters pouring
    // into it, room for every emitter when the effect has a gas.
    mvfxResolvedForce* gasForces;
    mvfxGasPour* gasPours;
    // Whether its gases and water surfaces are due a step: its last step's,
    // which the context steps once every instance's task has finished.
    bool gasesDue;
    mvfxEmitterState* emitters;
    // Its gases' states and its water surfaces', one for each of its
    // effect's, in its block with their fields and buffers.
    mvfxGasState* gases;
    mvfxWaterState* waters;
    // Scratch for a step that may reach an emitter's capacity, sized for
    // the largest emitter: its death ticks, and the particles it accepts
    // that live past the step.
    int64_t* deathScratch;
    mvfxBirth* acceptedScratch;
    // Whether each particle of the emitter being stepped was killed this
    // step, and whether it crossed into a water surface.
    uint8_t* killScratch;
    uint8_t* crossScratch;
    // The depth image the host set, checked, its pixels borrowed.
    mvfxDepthView depthView;
    bool hasDepthView;
    // Counts the depth images set (and cleared), for bodies asleep on one.
    uint32_t depthSerial;
    // The colliders the host set, compiled, and their grids, held.
    mvfxCollider hostColliders[MVFX_MAX_COLLIDERS];
    struct mvfxGrid* hostColliderGrids[MVFX_MAX_COLLIDERS];
    int32_t hostColliderCount;
    size_t blockBytes;
} mvfxInstance;

// a + b, held at UINT64_MAX: an emitter's counts never wrap, even after
// jumps whose refusals are counted in the billions of billions.
static inline uint64_t mvfxSaturatingAdd(uint64_t a, uint64_t b)
{
    return a > UINT64_MAX - b ? UINT64_MAX : a + b;
}

// Creates an instance of a compiled effect in one block from allocator,
// planned or holding its particles; NULL when the allocator fails or the
// block's size does not fit.
mvfxInstance* mvfxCreateInstanceBlock(mvfxEffect* effect, int32_t quality,
                                      const mvfxTransform* transform, uint32_t seed, bool planned,
                                      const mvfxAllocator* allocator);

// An emitter's capacity at a quality level: its own times the level's
// scale, rounded down and at least 1 (a beam's whole); 0 when left out.
int32_t mvfxCapacityAt(const mvfxEffect* effect, int32_t emitter, int32_t quality);

// The particles an effect's emitters hold at a quality level.
int64_t mvfxParticlesAt(const mvfxEffect* effect, int32_t quality);

// The instance's transform at a tick of the step being taken.
void mvfxTransformAt(const mvfxInstance* instance, int64_t tick, mvfxTransform* out);

// Returns an instance to its start: no particles, its clock, counts and
// cursors at 0, no stop, its anchors at its place.
void mvfxResetInstance(mvfxInstance* instance);

// The entries of a hashed grid's table at a capacity: the least power of
// two at least twice it (and at least 2).
uint32_t mvfxGridTableSize(int32_t capacity);

// Returns an instance's block.
void mvfxReleaseInstance(mvfxInstance* instance, const mvfxAllocator* allocator);

// The array of an attribute of an emitter's particles.
static inline void* mvfxAttributeArray(const mvfxInstance* instance, int32_t emitter,
                                       mvfxAttribute attribute)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxEmitterState* state = &instance->emitters[emitter];
    return state->particles + (size_t)compiled->plan.slots[attribute] * state->arrayBytes;
}

// A hash of everything an instance's step decides: its clock, each
// emitter's counts, and every attribute of every live particle.
uint64_t mvfxHashInstance(const mvfxInstance* instance, uint64_t seed);

#endif
