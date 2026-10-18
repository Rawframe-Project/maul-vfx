// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The numbers of an emitter's step on the GPU (sim.wgsl's Step), from its
// view and its plan.

#ifndef MAUL_VFX_RHI_SRC_NUMBERS_H
#define MAUL_VFX_RHI_SRC_NUMBERS_H

#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/plan.h"

// The sources: the births' size, colour, angle, spin, orientation,
// angular velocity, drag scale, custom values and lifetime.
#define MVFX_GPU_SOURCES        16
#define MVFX_SOURCE_SIZE        0
#define MVFX_SOURCE_COLOR       1
#define MVFX_SOURCE_ANGLE       2
#define MVFX_SOURCE_SPIN        3
#define MVFX_SOURCE_ORIENTATION 4
#define MVFX_SOURCE_ANGULAR     5
#define MVFX_SOURCE_DRAG        6
#define MVFX_SOURCE_CUSTOM      7
#define MVFX_SOURCE_LIFE        15

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
// the velocity modules; 4 of the kill rules' and events' head, 20 of the
// spawning from events' and 20 a rule for the most an emitter has; then,
// unchanged from step to step and uploaded once, 16 a source, 8 of the
// frame's and mesh index's draws, 4 of the bounds' growth by size, 4 of
// how particles turn and move, 12 of the values over life and 12 of how
// it spawns from events.
#define MVFX_VALUES_FIRST     (84 + 132 * MVFX_MAX_FORCES + 56 + 64 * MVFX_MAX_COLLIDERS)
#define MVFX_VALUES_WORDS     112
#define MVFX_KILLS_FIRST      (MVFX_VALUES_FIRST + MVFX_VALUES_WORDS)
#define MVFX_SPAWN_FIRST      (MVFX_KILLS_FIRST + 4)
#define MVFX_KILL_RULES_FIRST (MVFX_KILLS_FIRST + 24)
#define MVFX_SOURCES_FIRST    (MVFX_KILL_RULES_FIRST + 20 * MVFX_MAX_KILL_RULES)
#define MVFX_SPAWN_RULE_FIRST (MVFX_SOURCES_FIRST + 16 * MVFX_GPU_SOURCES + 28)
#define MVFX_SHAPE_FIRST      (MVFX_SPAWN_RULE_FIRST + 12)
#define MVFX_STEP_WORDS       (MVFX_SHAPE_FIRST + 12)

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
// rules' and events' head, the spawning from events' and the rules it
// has. The sources' block
// (MVFX_SOURCES_FIRST on) is written once. The rest keep what an earlier
// step left, never read.
typedef struct mvfxStepSpans
{
    uint32_t first[4];
    uint32_t count[4];
} mvfxStepSpans;

mvfxStepSpans mvfxStepSpansOf(const mvfxEmitterPlan* plan);

// The values over life records read: alpha, the three sizes', each
// custom attribute's.
#define MVFX_RECORD_LIVES (4 + MVFX_MAX_CUSTOM_ATTRIBUTES)

// Where the baked tables are among the grids buffer's words (all ones for
// none): each source's curve and gradient, each value over life's curve
// the step reads; the colour's gradient over life and each value over
// life's curve records read.
typedef struct mvfxStepTables
{
    uint32_t sources[MVFX_GPU_SOURCES][2];
    uint32_t lives[MVFX_GPU_LIVES];
    uint32_t gradient;
    uint32_t recordLives[MVFX_RECORD_LIVES];
} mvfxStepTables;

// An emitter's values over life records read, in the kernel's order.
void mvfxRecordLivesOf(const mvfxEmitterView* view, mvfxLifeView lives[MVFX_RECORD_LIVES]);

// An emitter's events buffer (sim.wgsl's events): EVENT_FACTS words a
// particle of its capacity, four a block of its scan, then its events,
// MVFX_EVENT_WORDS each; the events' first word, and the words in all.
#define MVFX_EVENT_FACTS 9u
#define MVFX_EVENT_WORDS 28u
uint32_t mvfxEventFirst(int32_t capacity);
uint64_t mvfxEventBufferWords(int32_t capacity, int32_t eventCapacity);

// The words of a record pass's numbers (records.wgsl's Records).
#define MVFX_RECORD_WORDS 488

// The orders a record pass writes in: the pool's, its reverse, or sorted
// by place.
#define MVFX_ORDER_POOL     0u
#define MVFX_ORDER_REVERSED 1u
#define MVFX_ORDER_SORTED   2u

// An emitter's order for its records.
uint32_t mvfxRecordOrder(const mvfxEmitterView* view);

// The first word of a record pass's order words: the order, the sort's
// measure and flip, the sort's workgroups.
#define MVFX_RECORD_ORDER 476

// The words of an emitter's events' numbers a plan changes, in two spans
// of MVFX_RECORD_SPAN words: the head (with the clock at the plan's end)
// and the map into the view's space. The rest are the emitter's and the
// part's, its order words among them (its capacity, no view), the same at
// every step.
#define MVFX_RECORD_SPAN 12
#define MVFX_RECORD_MAP  32

// The elements of a sort's block; the blocks of a capacity. An emitter
// that sorts keeps keys and indices twice over its blocks (4 words an
// element) and two sets of digit counts (256 words a block each).
#define MVFX_SORT_BLOCK 1024u
uint32_t mvfxSortBlocks(int32_t capacity);

// Whether a layout is one the record pass writes: its stride a multiple of
// 4 bytes, no ribbon or neighbor field.
bool mvfxIsRecordLayout(const mvfxLayoutView* layout);

// Writes a record pass's numbers: the particle buffer's capacity, the
// records' place, count and layout, the clock at the plan's end, where a
// particle's values are, the map into the view's space, the values over
// life and their tables, the sRGB steps, the order and what a sort
// measures from the eye.
void mvfxRecordNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                       const mvfxLayoutView* layout, const mvfxGpuRecordsDef* def,
                       const mvfxStepTables* tables, const float srgb[MVFX_SRGB_STEPS],
                       uint32_t words[MVFX_RECORD_WORDS]);

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

// An emitter's spawn region in its events buffer, after its own events
// (sim.wgsl's spawn region): 16 words of its counts, then the source's
// events copied and seven arrays of the source's event capacity. Its
// first word, and its words.
uint32_t mvfxSpawnFirst(int32_t capacity, int32_t eventCapacity);
uint64_t mvfxSpawnWords(int32_t sourceEventCapacity);

// The spawn region's word of the source's events written, and of its
// copied events.
#define MVFX_SPAWN_SOURCE 8u
#define MVFX_SPAWN_EVENTS 16u

// Writes an emitter's numbers for spawning from events, beside its step's
// (nothing for one that does not): the ticks whose events spawn, its
// plan's host births, its spawn region, the serials its plan's births
// took, the first vector of its staged plan after hostRecords host
// records, its births' room and the source's anchor less its own; how it
// spawns, the source's event capacity, its start delay and loop.
void mvfxSpawnNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                      const mvfxEmitterView* source, const mvfxEmitterPlan* sourcePlan,
                      int32_t hostRecords, uint32_t words[MVFX_STEP_WORDS]);

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
