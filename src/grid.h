// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A grid as the library keeps it: its size, components and values in
// binary32 or binary16, and its trilinear sampling.

#ifndef MAUL_VFX_SRC_GRID_H
#define MAUL_VFX_SRC_GRID_H

#include "allocator.h"

#include "maul-vfx/effect.h"

typedef struct mvfxGrid
{
    int32_t size[3];
    int32_t components;
    mvfxGridFormat format;
    // Points times components.
    int64_t count;
    // float or uint16_t values, after the grid in its block.
    void* values;
    size_t blockBytes;
    // How many effects read it.
    int32_t users;
    // Its values' serial, new from the context's count at its creation and
    // at each change of its values: a grid's values seen before are told
    // from new ones (and from another grid's at the same address).
    uint64_t serial;
    // The points its values changed at since the context's step `step`
    // (first to end, empty when first is not below end), every change
    // after serial `since` among them: a collider seen at or after since
    // changed only there.
    uint64_t step;
    uint64_t since;
    int32_t first[3];
    int32_t end[3];
} mvfxGrid;

// Whether a grid def is valid.
bool mvfxIsGridDefValid(const mvfxGridDef* def);

// Whether count values are valid for a format.
bool mvfxAreGridValuesValid(mvfxGridFormat format, const float* values, int64_t count);

// Builds a grid from a valid def; NULL when the allocator fails.
mvfxGrid* mvfxBuildGrid(const mvfxGridDef* def, const mvfxAllocator* allocator);

void mvfxReleaseGrid(mvfxGrid* grid, const mvfxAllocator* allocator);

// Stores valid values for the grid's points.
void mvfxStoreGridValues(mvfxGrid* grid, const float* values);

// Stores new values for the points from first, size along each axis,
// given as the grid's (X fastest), already checked.
void mvfxStoreGridBox(mvfxGrid* grid, const int32_t first[3], const int32_t size[3],
                      const float* values);

// Samples the grid's components trilinearly at u, the box's own place with
// [0, 1] from face to face, under a border; false (and nothing) outside
// the box under the none border.
bool mvfxSampleGrid(const mvfxGrid* grid, const float u[3], mvfxGridBorder border, float out[4]);

#endif
