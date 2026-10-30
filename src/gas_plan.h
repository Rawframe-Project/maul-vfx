// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A planned instance's smoke gases: what each step takes from the
// instance, resolved for its plan (maul-vfx/plan.h), and whether the
// simulation's gases hold anything.

#ifndef MAUL_VFX_SRC_GAS_PLAN_H
#define MAUL_VFX_SRC_GAS_PLAN_H

#include "instance.h"

// Resolves a planned instance's gases' steps where the CPU path steps
// them, at the end of the context's step: their gravity, forces and
// obstacles.
void mvfxPlanGases(mvfxInstance* instance);

// A gas's force f resolved for the step, on either path: its strength
// read at the step's start, by the instance's seconds (a gas has no
// duration), and drawn apart from its emitters' forces.
mvfxResolvedForce mvfxResolveGasForce(const mvfxInstance* instance, int32_t g, int32_t f);

#endif
