// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The numbers of an emitter's step on the GPU (sim.wgsl's Step), from its
// view and its plan.

#ifndef MAUL_VFX_RHI_SRC_NUMBERS_H
#define MAUL_VFX_RHI_SRC_NUMBERS_H

#include "maul-vfx/plan.h"

// The words the step's numbers take: 80 of the emitter and its plan, 4
// of its forces' head, 24 a force and 108 its field for the most forces
// an emitter has, 8 of its collision's head and response, 32 a collider
// for the most it has.
#define MVFX_STEP_WORDS (84 + 132 * MVFX_MAX_FORCES + 8 + 64 * MVFX_MAX_COLLIDERS)

// The workgroups of 64 update takes for a capacity, striding over the
// particles: at most 65,535, the most a dispatch has along one axis on
// every device.
uint32_t mvfxUpdateGroups(int32_t capacity);

// The scan's blocks for a capacity (reduce's and scatter's workgroups):
// its live particles and twice its births, 1,024 elements a block; at most
// 49,152 for the largest capacity, 2^24.
uint32_t mvfxScanBlocks(int32_t capacity);

// Writes emitter's step numbers: the plan's counts and ticks, the
// emitter's shape, velocity and motion, the instance's places from the
// plan's anchor after, gravity in the particles' space, the drag factors
// over the plan for the drag every particle has (the emitter's and its
// winds' and velocity fields' without a falloff), its forces and its
// colliders, each one's grid as forceGrids and colliderGrids give it.
// The words of the step's numbers a plan's kernels read, in two spans
// from their first words: the head, the forces' slots and the fields of
// the forces it has; the collision's head and the colliders it has. The
// rest keep what an earlier step left, never read.
typedef struct mvfxStepSpans
{
    uint32_t first[2];
    uint32_t count[2];
} mvfxStepSpans;

mvfxStepSpans mvfxStepSpansOf(int32_t forceCount, int32_t colliderCount);

void mvfxStepNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan, int32_t emitter,
                     const uint32_t (*forceGrids)[5], const uint32_t (*colliderGrids)[5],
                     uint32_t words[MVFX_STEP_WORDS]);

#endif
