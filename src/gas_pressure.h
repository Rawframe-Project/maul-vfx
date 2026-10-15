// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas's pressure projection: its velocity made free of divergence by a
// Poisson solve, geometric multigrid V cycles with weighted Jacobi
// smoothing (Briggs, Henson and McCormick 2000), every pass a per-cell
// gather from the other buffer and its counts fixed, so the bits do not
// depend on how the cells are split. Pressure p sits at the cells'
// centers; a face's velocity loses the rise of p across it over the
// distance between the centers, so a cell's divergence after is its
// divergence before less the weighted sum of its neighbors' p less the
// weights times its own: the solve finds p with that the divergence.
// Across a closed wall there is no neighbor and the face stays 0; across
// an open one p is 0 on the wall, half a cell from the center.

#ifndef MAUL_VFX_SRC_GAS_PRESSURE_H
#define MAUL_VFX_SRC_GAS_PRESSURE_H

#include "gas.h"

// Sets the faces on a gas's closed walls (and a 2D gas's Z faces) to 0.
void mvfxCloseGasWalls(const mvfxCompiledGas* gas, float* const velocity[3]);

// Projects a gas's current velocity: its closed walls' faces set to 0,
// then (with pressure cycles) its divergence removed by the solve, which
// starts from the pressure the last solve left.
void mvfxProjectGas(const mvfxCompiledGas* gas, mvfxGasState* state);

#endif
