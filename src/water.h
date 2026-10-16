// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A water surface as the library keeps it: its def checked and copied,
// and its fields' places in an instance's block.

#ifndef MAUL_VFX_SRC_WATER_H
#define MAUL_VFX_SRC_WATER_H

#include "allocator.h"

#include "maul-vfx/water.h"

#include <stdbool.h>
#include <stddef.h>

// A water def as compiled: its cells.
typedef struct mvfxCompiledWater
{
    mvfxWaterDef def;
    int64_t cells;
} mvfxCompiledWater;

// Where a water surface's buffers lie in an instance's block.
typedef struct mvfxWaterAt
{
    size_t heights;
    size_t rates;
    size_t pushes;
} mvfxWaterAt;

// A water surface's state in an instance: each cell's displacement from
// the rest level in two buffers (current the one now; a substep gathers
// the neighbors' from it into the other) and its rate; the pushes for the
// next step; where a world surface's grid was put (the instance's place at
// its start); whether it moves at all (a rate, or a slope), and its
// largest displacement's size.
typedef struct mvfxWaterState
{
    float* heights[2];
    int32_t current;
    float* rates;
    mvfxWaterPush* pushes;
    int32_t pushCount;
    double origin[3];
    float rotation[4];
    float scale;
    bool holds;
    float reach;
} mvfxWaterState;

// A rate, or a difference of displacements, at most this is still.
#define MVFX_WATER_STILL 1e-6f

// Whether a water def is valid.
bool mvfxIsWaterDefValid(const mvfxWaterDef* def);

// A valid water def compiled.
mvfxCompiledWater mvfxCompileWater(const mvfxWaterDef* def);

// Places a water surface's buffers in a layout.
void mvfxAddWaterLayout(mvfxLayout* layout, const mvfxCompiledWater* water, mvfxWaterAt* at);

// Points a water surface's state at its buffers in block, still.
void mvfxStartWater(mvfxWaterState* state, const mvfxCompiledWater* water, unsigned char* block,
                    const mvfxWaterAt* at);

// Makes a water surface still at its rest level, its pushes dropped.
void mvfxClearWater(mvfxWaterState* state, const mvfxCompiledWater* water);

#endif
