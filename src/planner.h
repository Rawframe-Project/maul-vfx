// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A planned instance's steps: its emitters' births owed, written as plans
// for a simulation elsewhere (maul-vfx/plan.h).

#ifndef MAUL_VFX_SRC_PLANNER_H
#define MAUL_VFX_SRC_PLANNER_H

#include "instance.h"

// Begins the plans of a context's step: empty, from the clock.
void mvfxBeginPlans(mvfxInstance* instance);

// Plans an emitter's step: its births owed up to the clock appended to its
// plan, those past the plan's room refused, its cursors and counters
// moved on as the CPU path's step moves them.
void mvfxPlanEmitter(mvfxInstance* instance, int32_t emitter);

#endif
