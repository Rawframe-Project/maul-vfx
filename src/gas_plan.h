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

// The age a gas's forces' strengths read at a step's start: the
// instance's seconds, a gas having no duration.
static inline float mvfxGasSourceAge(const mvfxInstance* instance)
{
    return (float)((double)instance->stepStart / (double)MVFX_TICKS_PER_SECOND);
}

#endif
