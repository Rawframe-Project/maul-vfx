// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An instance as the step runs it: the effect it plays, its origin, seed
// and clock, and per emitter the particles alive and the counts of what it
// owed and refused. Everything lives in one block allocated when the
// instance is created (record mvfx-0001); nothing grows after.

#ifndef MAUL_VFX_SRC_INSTANCE_H
#define MAUL_VFX_SRC_INSTANCE_H

#include "effect.h"

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
} mvfxEmitterState;

// A birth: the particle's serial and tick, and its place among the births
// of its batch (a burst's firing; a rate's births are batches of one).
typedef struct mvfxBirth
{
    uint64_t serial;
    int64_t tick;
    int64_t batchIndex;
    int64_t batchCount;
} mvfxBirth;

// An instance.
typedef struct mvfxInstance
{
    mvfxEffect* effect;
    double origin[3];
    uint32_t seed;
    int64_t clock;
    // No birth past this tick; INT64_MAX until the instance is stopped.
    int64_t stopTick;
    mvfxEmitterState* emitters;
    // Scratch for a step that may reach an emitter's capacity, sized for
    // the largest emitter: its death ticks, and the particles it accepts
    // that live past the step.
    int64_t* deathScratch;
    mvfxBirth* acceptedScratch;
    size_t blockBytes;
} mvfxInstance;

// Creates an instance of a compiled effect in one block from allocator;
// NULL when the allocator fails or the block's size does not fit.
mvfxInstance* mvfxCreateInstanceBlock(mvfxEffect* effect, const double origin[3], uint32_t seed,
                                      const mvfxAllocator* allocator);

// Returns an instance to its start: no particles, its clock, counts and
// cursors at 0, no stop.
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
