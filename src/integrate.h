// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The integrator: particles move exactly under a constant acceleration
// and linear drag over a span h,
//
//   v1 = v0 e + a h phi1,  x1 = x0 + v0 h phi1 + a h^2 phi2,
//
// with e = exp(-k h), phi1 = (1 - e) / (k h) and phi2 = (1 - phi1) /
// (k h), each a series near k h = 0 (Hochbruck and Ostermann,
// "Exponential integrators", Acta Numerica 2010). Rotation moves alike
// under angular drag.

#ifndef MAUL_VFX_SRC_INTEGRATE_H
#define MAUL_VFX_SRC_INTEGRATE_H

#include "instance.h"

// The factors drag gives a span: how much velocity is left (e), how far
// a velocity carries (h phi1) and how far an acceleration pushes
// (h^2 phi2).
typedef struct mvfxDragFactors
{
    float decay;
    float carry;
    float push;
} mvfxDragFactors;

// The factors of drag k a second over a span of seconds, in binary64.
mvfxDragFactors mvfxDragFactorsOf(double drag, double seconds);

// Moves the particles an emitter holds at the step's start over the whole
// step.
void mvfxIntegrateEmitter(mvfxInstance* instance, int32_t emitter);

// Moves a particle born within the step from its birth to the step's end.
void mvfxIntegrateNewborn(mvfxInstance* instance, int32_t emitter, int32_t at, int64_t birthTick);

#endif
