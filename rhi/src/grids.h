// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's grids on the GPU, in one buffer of words: those its forces
// and its own colliders read (fixed by its effect) laid out once, then a
// region of the room the simulation's def sets aside for the grids its
// instance's colliders read, placed each step; after them the effect's
// baked tables the emitter's values read, uploaded once. Each grid is
// uploaded when its serial differs from its copy's.

#ifndef MAUL_VFX_RHI_SRC_GRIDS_H
#define MAUL_VFX_RHI_SRC_GRIDS_H

#include "maul-rhi/encoder.h"
#include "maul-vfx/plan.h"

// The most grids an emitter reads: a force's or a collider's each, its own
// and its instance's.
#define MVFX_GPU_GRIDS (MVFX_MAX_FORCES + 2 * MVFX_MAX_COLLIDERS)

// The most baked tables an emitter reads: a curve and a gradient for each
// of its 16 sources, a curve for each of its 6 values over life the step
// reads, its colour's gradient over life and a curve for each of the 12
// values over life its records read.
#define MVFX_GPU_TABLES 51

// A baked table in the buffer: its values (the effect's) and words, and
// its first word.
typedef struct mvfxGpuTable
{
    const float* values;
    uint32_t words;
    uint32_t first;
} mvfxGpuTable;

// A grid in the buffer: its id, first word, size, format, and the serial
// of the copy there (0 for none yet).
typedef struct mvfxGpuGrid
{
    mvfxGridId id;
    uint32_t first;
    int32_t size[3];
    mvfxGridFormat format;
    uint64_t serial;
} mvfxGpuGrid;

// The grids: the fixed ones, then the instance's in the region from
// hostFirst, hostWords long; for each force and own collider the grid it
// reads (-1 for none); the buffer's words.
typedef struct mvfxGpuGrids
{
    int32_t count;
    int32_t fixedCount;
    mvfxGpuGrid grids[MVFX_GPU_GRIDS];
    int32_t forceGrid[MVFX_MAX_FORCES];
    int32_t colliderGrid[MVFX_MAX_COLLIDERS];
    uint32_t hostFirst;
    uint32_t hostWords;
    mvfxGpuTable tables[MVFX_GPU_TABLES];
    int32_t tableCount;
    bool tablesWritten;
    uint32_t words;
} mvfxGpuGrids;

// Lays out an emitter's fixed grids from its view and the instance's
// region of hostWords; false when a grid is gone.
bool mvfxLayGrids(const mvfxContext* context, const mvfxEmitterView* view, uint32_t hostWords,
                  mvfxGpuGrids* grids);

// Places the grids the plan's instance colliders read in the region, in
// order, each once; false when one is gone or they pass the region.
bool mvfxPlaceHostGrids(const mvfxContext* context, const mvfxEmitterPlan* plan,
                        int32_t ownColliders, mvfxGpuGrids* grids);

// Writes a grid as the step's numbers give it: its first word, its size
// along X, Y and Z, its format (all 0 for none): force f's, or the plan's
// collider c's.
void mvfxForceGridWords(const mvfxGpuGrids* grids, int32_t f, uint32_t out[5]);
void mvfxColliderGridWords(const mvfxGpuGrids* grids, const mvfxEmitterPlan* plan,
                           int32_t ownColliders, int32_t c, uint32_t out[5]);

// The bytes the room for the instance's grids needs for colliders: each
// heightfield's and distance grid's grid once, its bytes rounded up to 4,
// as mvfxPlaceHostGrids places them; false when one is gone.
bool mvfxHostGridBytes(const mvfxContext* context, const mvfxCollider* colliders, int32_t count,
                       int64_t* bytes);

// Adds a baked table of words values after the buffer's other words (once
// the grids are laid out): its first word; all ones for NULL values.
uint32_t mvfxAddTable(mvfxGpuGrids* grids, const float* values, uint32_t words);

// Records the uploads of the grids whose serial differs from the copy's,
// and of the tables at the first write, into the pass, then takes their
// serials, their bytes added to uploaded; false when the device refuses
// one or a grid is gone.
bool mvfxWriteGrids(mrhiDevice* device, uint64_t* uploaded, mrhiPassId pass, mrhiResourceId buffer,
                    const mvfxContext* context, mvfxGpuGrids* grids);

#endif
