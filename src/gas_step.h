// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas's step: in substeps, its sources and forces, then its velocity and
// scalars carried by its velocity (semi-Lagrangian, traced back by the
// midpoint rule, or with MacCormack's correction), each decaying; its
// fuel burning; its pressure projection.

#ifndef MAUL_VFX_SRC_GAS_STEP_H
#define MAUL_VFX_SRC_GAS_STEP_H

#include "force.h"
#include "gas.h"
#include "gas_splat.h"
#include "tasks.h"

#include <stdint.h>

// Where the gas at place p was a substep before, by the faces' velocities:
// traced back by the midpoint rule.
void mvfxGasTraceBack(float* const velocity[3], const mvfxGasLattice* l, const float p[3],
                      float out[3]);

// What drives a gas over a step, in its box's frame: gravity, its forces
// resolved, and the particles pouring into it.
typedef struct mvfxGasDrive
{
    float gravity[3];
    const mvfxResolvedForce* forces;
    int32_t forceCount;
    // The emitters whose particles pour into it.
    const mvfxGasPour* pours;
    int32_t pourCount;
    // The tasks its passes are split over.
    const mvfxTasks* tasks;
} mvfxGasDrive;

// Steps a gas over the ticks from to, its sources pouring at the substeps
// that start before stopTick; then whether it holds anything.
void mvfxStepGas(const mvfxCompiledGas* gas, mvfxGasState* state, const mvfxGasDrive* drive,
                 int64_t from, int64_t to, int64_t stopTick);

#endif
