// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas's solids: the cells within its obstacles, the faces beside them
// taking the obstacles' velocities (their motion over the step), nothing
// living in them.

#ifndef MAUL_VFX_SRC_GAS_SOLID_H
#define MAUL_VFX_SRC_GAS_SOLID_H

#include "gas.h"

// Marks the cells whose centers are on an obstacle's wrong side (the
// first such obstacle's), then builds the solve's open areas.
void mvfxMarkGasSolids(const mvfxCompiledGas* gas, mvfxGasState* state);

// Zeroes the scalars in solid cells.
void mvfxEmptyGasSolids(const mvfxCompiledGas* gas, const mvfxGasState* state,
                        float* const scalars[3]);

// Whether obstacle c is the step before's obstacle c moved: the step
// before's is known, and it has the same kind, sizes and grid.
bool mvfxGasObstacleMoved(const mvfxGasState* state, int32_t c);

// Gives each face beside a solid cell the obstacle's velocity across it,
// its motion over a step of seconds (0 when the step before's obstacle
// is not known or not the same shape).
void mvfxMoveGasSolids(const mvfxCompiledGas* gas, const mvfxGasState* state, double seconds,
                       float* const velocity[3]);

#endif
