// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas's lattice and its sampling: trilinear, from a cell's array or a
// face's, which the gas's step and the particles following it share.

#ifndef MAUL_VFX_SRC_GAS_LATTICE_H
#define MAUL_VFX_SRC_GAS_LATTICE_H

#include <stdint.h>

// A gas's lattice: its cells along each axis and their edge, and a
// substep's span.
typedef struct mvfxGasLattice
{
    int32_t n[3];
    float cell;
    float h;
} mvfxGasLattice;

// An array's points along each axis: the cells', or the faces' across
// axis (0 to 2); for -1 the cells.
void mvfxGasPoints(const mvfxGasLattice* l, int32_t axis, int32_t out[3]);

// An array of a lattice's values at place p, in cells (a cell's center at
// its index plus one half): the cells' for axis -1, else the faces'
// across axis; by trilinear weights, p held within the array's points.
float mvfxGasSample(const float* values, const mvfxGasLattice* l, int32_t axis, const float p[3]);

// As mvfxGasSample, also giving the least and the most of the points it
// weighed.
float mvfxGasSampleRange(const float* values, const mvfxGasLattice* l, int32_t axis,
                         const float p[3], float range[2]);

#endif
