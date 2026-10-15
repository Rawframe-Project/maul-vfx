// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Units of particles joined by constraints, solved by XPBD in substeps.

#ifndef MAUL_VFX_SRC_UNIT_H
#define MAUL_VFX_SRC_UNIT_H

#include "instance.h"

// Whether the quad from a unit's point q across and down, in a unit of a
// width, is whole: none of its four edges torn (bit 0 of a point's torn
// edges its edge across, bit 1 its edge down).
static inline bool mvfxIsQuadWhole(const uint32_t* torn, int32_t q, int32_t width)
{
    return torn == nullptr ||
           (((torn[q] | torn[q + width]) & 1u) == 0 && ((torn[q] | torn[q + 1]) & 2u) == 0);
}

// Moves an emitter of units' points over the step (those alive at its
// start, once the ended ones are gone): each substep moves them as
// particles, holds pinned points, projects the constraints set by set,
// takes the velocities from the places and damps them along each
// constraint.
void mvfxStepUnits(mvfxInstance* instance, int32_t emitter);

#endif
