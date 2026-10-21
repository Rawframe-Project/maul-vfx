// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Liquids by position-based fluids (Macklin and Muller 2013): an emitter's
// particles kept at their rest density by Jacobi iterations on their
// places, each a gather over the neighbors in a grid's order.

#ifndef MAUL_VFX_SRC_LIQUID_H
#define MAUL_VFX_SRC_LIQUID_H

#include "instance.h"
#include "liquid_kernel.h"

// Moves an emitter's particles alive at the step's start as its liquid,
// in its substeps.
void mvfxStepLiquid(mvfxInstance* instance, int32_t emitter);

// Moves a grid liquid's particles alive at the step's start: their
// velocities as the integrator gives any particle's (forces, drag,
// bounces), for their gas to project; their places by the velocity the
// gas gave them, followed along the colliders. With the integrator's
// places, gravity's h^2 / 2 a step would escape the projection, and a
// pool at rest would sink by it each step.
void mvfxMoveGridLiquid(mvfxInstance* instance, int32_t emitter);

#endif
