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

// One emitter's particles and counts. Live particles are [0, count) of each
// attribute array, oldest first.
typedef struct mvfxEmitterState
{
    unsigned char* particles;
    int32_t count;
    uint64_t owed;
    uint64_t refused;
} mvfxEmitterState;

// An instance.
typedef struct mvfxInstance
{
    mvfxEffect* effect;
    double origin[3];
    uint32_t seed;
    int64_t clock;
    mvfxEmitterState* emitters;
    // Scratch for a step that may reach an emitter's capacity, sized for
    // the largest emitter: its death ticks, and the particles it accepts
    // that live past the step.
    int64_t* deathScratch;
    uint64_t* acceptedScratch;
    size_t blockBytes;
} mvfxInstance;

// Creates an instance of a compiled effect in one block from allocator;
// NULL when the allocator fails or the block's size does not fit.
mvfxInstance* mvfxCreateInstanceBlock(mvfxEffect* effect, const double origin[3], uint32_t seed,
                                      const mvfxAllocator* allocator);

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
