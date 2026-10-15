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

// Steps a gas over the ticks from to, its sources pouring at the substeps
// that start before stopTick; then whether it holds anything.
void mvfxStepGas(const mvfxCompiledGas* gas, mvfxGasState* state, int64_t from, int64_t to,
                 int64_t stopTick);

#endif
