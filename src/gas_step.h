// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas's step: in substeps, its sources, then its velocity and scalars
// carried by its velocity (semi-Lagrangian, traced back by the midpoint
// rule), each decaying.

#ifndef MAUL_VFX_SRC_GAS_STEP_H
#define MAUL_VFX_SRC_GAS_STEP_H

#include "gas.h"

#include <stdint.h>

// A gas's lattice: its cells along each axis and their edge, and a
// substep's span.
typedef struct mvfxGasLattice
{
    int32_t n[3];
    float cell;
    float h;
} mvfxGasLattice;

// An array of a lattice's values at place p, in cells (a cell's center at
// its index plus one half): the cells' for axis -1, else the faces'
// across axis; by trilinear weights, p held within the array's points.
float mvfxGasSample(const float* values, const mvfxGasLattice* l, int32_t axis, const float p[3]);

// Where the gas at place p was a substep before, by the faces' velocities:
// traced back by the midpoint rule.
void mvfxGasTraceBack(float* const velocity[3], const mvfxGasLattice* l, const float p[3],
                      float out[3]);

// Steps a gas over the ticks from to, its sources pouring at the substeps
// that start before stopTick; then whether it holds anything.
void mvfxStepGas(const mvfxCompiledGas* gas, mvfxGasState* state, int64_t from, int64_t to,
                 int64_t stopTick);

#endif
