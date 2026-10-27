// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid particles: bodies of a shape turned by their orientations, moved
// by XPBD rigid bodies (Muller et al. 2020) in substeps against the
// emitter's planes and spheres.

#ifndef MAUL_VFX_SRC_RIGID_H
#define MAUL_VFX_SRC_RIGID_H

#include "instance.h"

// Moves an emitter's rigid particles alive at the step's start over the
// step: each substep integrates them, turns each by its free flow, then
// meets each body's contacts by position (with static friction) and
// velocity (with dynamic friction and restitution).
void mvfxStepRigid(mvfxInstance* instance, int32_t emitter);

// Moves a rigid particle born at birthTick, at at, to the step's end over
// what is left of the step's substeps.
void mvfxIntegrateRigidNewborn(mvfxInstance* instance, int32_t emitter, int32_t at,
                               int64_t birthTick);

#endif
