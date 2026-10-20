// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a planned instance's step resolved, as its plan hands it over: the
// emitter's forces, its colliders and its collision's response, and
// colliders alike for a gas's obstacles.

#ifndef MAUL_VFX_SRC_PLAN_RESOLVED_H
#define MAUL_VFX_SRC_PLAN_RESOLVED_H

#include "instance.h"

#include "maul-vfx/plan.h"

// Writes what the plan's step resolved into the plan: the forces, the
// colliders and the collision's response.
void mvfxPlanResolved(const mvfxInstance* instance, int32_t emitter, mvfxEmitterPlan* plan);

// A resolved collider as a plan hands it over, with its def's grid.
mvfxPlannedCollider mvfxPlannedColliderOf(const mvfxResolvedCollider* c, mvfxGridId grid);

#endif
