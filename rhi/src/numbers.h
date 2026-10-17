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
// an emitter has, 8 of its collision's head and response, 48 of its depth
// image, 32 a collider for the most it has, 28 of where a particle's
// values are among its words, 64 of the instance's parameters, 16 a source
// of the births' values, 8 of their frame's and mesh index's draws and 4
// of the bounds' growth by size.
#define MVFX_STEP_WORDS                                                                            \
    (84 + 132 * MVFX_MAX_FORCES + 56 + 64 * MVFX_MAX_COLLIDERS + 28 + 64 + 16 * MVFX_GPU_SOURCES + \
     12)

// The births' sources: size, colour, angle, spin, orientation, angular
// velocity, drag scale, then the custom values.
#define MVFX_GPU_SOURCES   15
#define MVFX_SOURCE_SIZE   0
#define MVFX_SOURCE_COLOR  1
#define MVFX_SOURCE_CUSTOM 7

// The sources' block among the step's numbers, with the draws and the
// growth: unchanged from step to step, uploaded once.
#define MVFX_SOURCES_FIRST (MVFX_STEP_WORDS - 16 * MVFX_GPU_SOURCES - 12)

// The words a particle of an emitter takes on the GPU: its arrays, the
// death tick's two words and the step's word.
#define MVFX_GPU_OWN_WORDS 3

// The workgroups of 64 update takes for a capacity, striding over the
// particles: at most 65,535, the most a dispatch has along one axis on
// every device.
uint32_t mvfxUpdateGroups(int32_t capacity);

// The scan's blocks for a capacity (reduce's and scatter's workgroups):
// its live particles and twice its births, 1,024 elements a block; at most
// 49,152 for the largest capacity, 2^24.
uint32_t mvfxScanBlocks(int32_t capacity);

// The words of the step's numbers a plan's kernels read, in three spans
// from their first words: the head, the forces' slots and the fields of
// the forces it has; the collision's head, its depth image and the
// colliders it has; where a particle's values are and the instance's
// parameters. The sources' block (MVFX_SOURCES_FIRST on) is written once.
// The rest keep what an earlier step left, never read.
typedef struct mvfxStepSpans
{
    uint32_t first[3];
    uint32_t count[3];
} mvfxStepSpans;

mvfxStepSpans mvfxStepSpansOf(int32_t forceCount, int32_t colliderCount);

// Writes emitter's step numbers: the plan's counts and ticks, the
// emitter's shape, velocity and motion, the instance's places from the
// plan's anchor after, gravity in the particles' space, the drag factors
// over the plan for the drag every particle has (the emitter's and its
// winds' and velocity fields' without a falloff), its forces and its
// colliders, each one's grid as forceGrids and colliderGrids give it, and
// its depth image, its normals read from a texture when normals is set,
// the instance's parameters, its births' sources, each one's curve and
// gradient at the first words sourceTables gives (all ones for none), and
// its bounds' growth by size.
void mvfxStepNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan, int32_t emitter,
                     const uint32_t (*forceGrids)[5], const uint32_t (*colliderGrids)[5],
                     bool normals, const uint32_t (*sourceTables)[2],
                     uint32_t words[MVFX_STEP_WORDS]);

// An emitter's sources in the view's order (MVFX_SOURCE_SIZE and so on),
// a null one's kind none.
void mvfxSourcesOf(const mvfxEmitterView* view, mvfxSourceView sources[MVFX_GPU_SOURCES]);

#endif
