// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Units of particles joined by constraints, solved by XPBD in substeps.

#ifndef MAUL_VFX_SRC_UNIT_H
#define MAUL_VFX_SRC_UNIT_H

#include "instance.h"

// Moves an emitter of units' points over the step (those alive at its
// start, once the ended ones are gone): each substep moves them as
// particles, holds pinned points, projects the constraints set by set,
// takes the velocities from the places and damps them along each
// constraint.
void mvfxStepUnits(mvfxInstance* instance, int32_t emitter);

#endif
