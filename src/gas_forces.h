// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas's forces and its combustion (Fedkiw, Stam and Jensen 2001;
// Nguyen, Fedkiw and Jensen 2002): buoyancy from its density and
// temperature, vorticity confinement, and fuel burning to heat, smoke and
// expansion. Each pass is a gather per point, so the bits do not depend on
// how the points are split.

#ifndef MAUL_VFX_SRC_GAS_FORCES_H
#define MAUL_VFX_SRC_GAS_FORCES_H

#include "force.h"
#include "gas.h"

// Speeds each face by gravity (in the box's frame) times (densityWeight
// density - heatLift temperature), from the mean of the cells beside it.
void mvfxGasBuoyancy(const mvfxCompiledGas* gas, const mvfxGasLattice* l, const float gravity[3],
                     float* const velocity[3], float* const scalars[3]);

// Speeds each face by vorticity confinement, its curl kept in work.
void mvfxGasConfine(const mvfxCompiledGas* gas, const mvfxGasLattice* l, float* const velocity[3],
                    float* work);

// Speeds each face by the forces (resolved into the box's frame) taken at
// its place at time seconds: its acceleration across the face, a wind's
// or a velocity field's pull exact over the substep.
void mvfxGasPush(const mvfxGasLattice* l, const mvfxResolvedForce* forces, int32_t count,
                 double time, float* const velocity[3]);

// Burns fuel where it is hot enough; with expansion, each cell's
// divergence the burning asks for (in the projection's units: lengths a
// second summed over its faces) into it, 0 elsewhere.
void mvfxGasBurn(const mvfxCompiledGas* gas, const mvfxGasLattice* l, float* const scalars[3],
                 float* expansion);

#endif
