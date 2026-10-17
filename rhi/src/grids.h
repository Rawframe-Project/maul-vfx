// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's grids on the GPU: the grids its forces read (fixed by its
// effect), laid out once in one buffer of words, each uploaded when its
// serial differs from the copy's.

#ifndef MAUL_VFX_RHI_SRC_GRIDS_H
#define MAUL_VFX_RHI_SRC_GRIDS_H

#include "maul-rhi/encoder.h"
#include "maul-vfx/plan.h"

// The grids: each distinct one's id, first word in the buffer and the
// serial of the copy there (0 for none yet), its size and format; for
// each force the grid it reads (-1 for none); the buffer's words.
typedef struct mvfxGpuGrids
{
    int32_t count;
    mvfxGridId ids[MVFX_MAX_FORCES];
    uint32_t first[MVFX_MAX_FORCES];
    uint64_t serials[MVFX_MAX_FORCES];
    int32_t size[MVFX_MAX_FORCES][3];
    mvfxGridFormat format[MVFX_MAX_FORCES];
    int32_t forceGrid[MVFX_MAX_FORCES];
    uint32_t words;
} mvfxGpuGrids;

// Lays out an emitter's grids from its view; false when one is gone.
bool mvfxLayGrids(const mvfxContext* context, const mvfxEmitterView* view, mvfxGpuGrids* grids);

// Writes force f's grid as the step's numbers give it: its first word,
// its size along X, Y and Z, its format (all 0 for a force with none).
void mvfxGridWords(const mvfxGpuGrids* grids, int32_t f, uint32_t out[5]);

// Records the uploads of the grids whose serial differs from the copy's,
// into the pass, then takes their serials; false when the device refuses
// one or a grid is gone.
bool mvfxWriteGrids(mrhiDevice* device, mrhiPassId pass, mrhiResourceId buffer,
                    const mvfxContext* context, mvfxGpuGrids* grids);

#endif
