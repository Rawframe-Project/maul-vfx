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

#include "maul-vfx/instance.h"

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
    // An event's birth: its tick, and its place in the queue as built.
    int64_t tick;
    int32_t order;
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
typedef struct mvfxEmitterState
{
    unsigned char* particles;
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
    // The emitter's forces as resolved for the step being taken.
    struct mvfxResolvedForce* forces;
    // Its velocity modules as resolved for the step.
    mvfxResolvedVelocityModules velocityModules;
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
    int64_t clock;
    // No birth past this tick; INT64_MAX until the instance is stopped.
    int64_t stopTick;
    // Its parameters, from the effect's defaults until the host sets them.
    float parameters[MVFX_MAX_PARAMETERS][4];
    mvfxEmitterState* emitters;
    // Scratch for a step that may reach an emitter's capacity, sized for
    // the largest emitter: its death ticks, and the particles it accepts
    // that live past the step.
    int64_t* deathScratch;
    mvfxBirth* acceptedScratch;
    // Whether each particle of the emitter being stepped was killed this
    // step.
    uint8_t* killScratch;
    // The depth image the host set, checked, its pixels borrowed.
    mvfxDepthView depthView;
    bool hasDepthView;
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

// Creates an instance of a compiled effect in one block from allocator;
// NULL when the allocator fails or the block's size does not fit.
mvfxInstance* mvfxCreateInstanceBlock(mvfxEffect* effect, const mvfxTransform* transform,
                                      uint32_t seed, const mvfxAllocator* allocator);

// The instance's transform at a tick of the step being taken.
void mvfxTransformAt(const mvfxInstance* instance, int64_t tick, mvfxTransform* out);

// Returns an instance to its start: no particles, its clock, counts and
// cursors at 0, no stop, its anchors at its place.
void mvfxResetInstance(mvfxInstance* instance);

// Returns an instance's block.
void mvfxReleaseInstance(mvfxInstance* instance, const mvfxAllocator* allocator);

// The array of an attribute of an emitter's particles.
static inline void* mvfxAttributeArray(const mvfxInstance* instance, int32_t emitter,
                                       mvfxAttribute attribute)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    return instance->emitters[emitter].particles + compiled->plan.offsets[attribute];
}

// A hash of everything an instance's step decides: its clock, each
// emitter's counts, and every attribute of every live particle.
uint64_t mvfxHashInstance(const mvfxInstance* instance, uint64_t seed);

#endif
