// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A liquid gas's step: APIC (Jiang et al. 2015) between its emitter's
// particles and its faces.

#ifndef MAUL_VFX_SRC_GAS_LIQUID_H
#define MAUL_VFX_SRC_GAS_LIQUID_H

#include "instance.h"
#include "tasks.h"

// Steps liquid gas g of an instance over its last step, once its emitter
// has moved its particles: their momentum onto the faces, the velocity
// made incompressible with air cells at pressure 0, extended into the air,
// and given back to the particles with their affine matrices; the passes
// split over tasks.
void mvfxStepLiquidGas(mvfxInstance* instance, int32_t g, const mvfxTasks* tasks);

#endif
