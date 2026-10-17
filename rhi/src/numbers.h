// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The numbers of an emitter's step on the GPU (sim.wgsl's Step), from its
// view and its plan.

#ifndef MAUL_VFX_RHI_SRC_NUMBERS_H
#define MAUL_VFX_RHI_SRC_NUMBERS_H

#include "maul-vfx/plan.h"

// The sources: the births' size, colour, angle, spin, orientation,
// angular velocity, drag scale and custom values.
#define MVFX_GPU_SOURCES        15
#define MVFX_SOURCE_SIZE        0
#define MVFX_SOURCE_COLOR       1
#define MVFX_SOURCE_ANGLE       2
#define MVFX_SOURCE_SPIN        3
#define MVFX_SOURCE_ORIENTATION 4
#define MVFX_SOURCE_ANGULAR     5
#define MVFX_SOURCE_DRAG        6
#define MVFX_SOURCE_CUSTOM      7

// The values over life the kernels read: the rotation speed, then the
// velocity modules' speed limit, motion scale, added speed, radial speed
// and orbit rate.
#define MVFX_GPU_LIVES 6

// The step's numbers (sim.wgsl's Step), in words: 84 of the emitter, its
// plan and its forces' head, 24 a force and 108 its field for the most
// forces an emitter has; 8 of its collision's head and response, 48 of
// its depth image, 32 a collider for the most it has; 28 of where a
// particle's values are among its words, 64 of the instance's
// parameters, 4 of the angular drag's factors over the plan and 16 of
// the velocity modules; 4 of the kill rules' head and 20 a rule for the
// most an emitter has; then, unchanged from step to step and uploaded
// once, 16 a source, 8 of the frame's and mesh index's draws, 4 of the
// bounds' growth by size, 4 of how particles turn and move, and 12 of
// the values over life.
#define MVFX_VALUES_FIRST  (84 + 132 * MVFX_MAX_FORCES + 56 + 64 * MVFX_MAX_COLLIDERS)
#define MVFX_VALUES_WORDS  112
#define MVFX_KILLS_FIRST   (MVFX_VALUES_FIRST + MVFX_VALUES_WORDS)
#define MVFX_SOURCES_FIRST (MVFX_KILLS_FIRST + 4 + 20 * MVFX_MAX_KILL_RULES)
#define MVFX_STEP_WORDS    (MVFX_SOURCES_FIRST + 16 * MVFX_GPU_SOURCES + 28)

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

// The words of the step's numbers a plan's kernels read, in four spans
// from their first words: the head, the forces' slots and the fields of
// the forces it has; the collision's head, its depth image and the
// colliders it has; where a particle's values are, the instance's
// parameters, the angular factors and the velocity modules; the kill
// rules' head and the rules it has. The sources' block
// (MVFX_SOURCES_FIRST on) is written once. The rest keep what an earlier
// step left, never read.
typedef struct mvfxStepSpans
{
    uint32_t first[4];
    uint32_t count[4];
} mvfxStepSpans;

mvfxStepSpans mvfxStepSpansOf(const mvfxEmitterPlan* plan);

// Where the step's baked tables are among the grids buffer's words (all
// ones for none): each source's curve and gradient, each value over
// life's curve.
typedef struct mvfxStepTables
{
    uint32_t sources[MVFX_GPU_SOURCES][2];
    uint32_t lives[MVFX_GPU_LIVES];
} mvfxStepTables;

// Writes emitter's step numbers: the plan's counts and ticks, the
// emitter's shape, velocity and motion, the instance's places from the
// plan's anchor after, gravity in the particles' space, the drag factors
// over the plan for the drag every particle has (the emitter's and its
// winds' and velocity fields' without a falloff), its forces and its
// colliders, each one's grid as forceGrids and colliderGrids give it, and
// its depth image, its normals read from a texture when normals is set,
// the instance's parameters, the angular drag's factors over the plan,
// its velocity modules and kill rules, its sources, its bounds' growth by
// size, how its particles turn and move, and its values over life, each
// table where tables gives it.
void mvfxStepNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan, int32_t emitter,
                     const uint32_t (*forceGrids)[5], const uint32_t (*colliderGrids)[5],
                     bool normals, const mvfxStepTables* tables, uint32_t words[MVFX_STEP_WORDS]);

// The words a host's particle takes after a plan's births (sim.wgsl's
// HostBirth).
#define MVFX_HOST_BIRTH_WORDS 16

// Writes a plan's host births in the kernels' form: each one's place in
// the particles' space (a world-space emitter's an offset from the
// plan's anchor after) and what it gives; its velocity; its colour; its
// size.
void mvfxHostBirthWords(const mvfxEmitterView* view, const mvfxEmitterPlan* plan, uint32_t* words);

// An emitter's sources (MVFX_SOURCE_SIZE and so on), a null one's kind
// none.
void mvfxSourcesOf(const mvfxEmitterView* view, mvfxSourceView sources[MVFX_GPU_SOURCES]);

// An emitter's values over life in the kernels' order.
void mvfxLivesOf(const mvfxEmitterView* view, mvfxLifeView lives[MVFX_GPU_LIVES]);

#endif
