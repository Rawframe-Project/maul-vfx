// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The integral of a baked spawn-rate curve, in binary64: a spawn count is
// read from it at an absolute time, and the time a count is reached is
// solved from it exactly, so neither depends on how time was split into
// steps.

#ifndef MAUL_VFX_SRC_CURVE_INTEGRAL_H
#define MAUL_VFX_SRC_CURVE_INTEGRAL_H

#include "curve.h"

// The integral of a baked curve from its domain's start: its value at each
// sample, and the domain's ends in binary64.
typedef struct mvfxCurveIntegral
{
    double cumulative[MVFX_CURVE_SAMPLES];
    double start;
    double end;
} mvfxCurveIntegral;

// The integral of a baked curve, which mvfxCurveIntegralAt and
// mvfxCurveIntegralTime read; end is the domain's end the curve was baked
// over.
void mvfxIntegrateCurve(const mvfxCurveTable* table, double end, mvfxCurveIntegral* integralOut);

// The integral of the baked curve from its domain's start to time, the
// curve held at its end samples outside the domain (negative before the
// start).
double mvfxCurveIntegralAt(const mvfxCurveTable* table, const mvfxCurveIntegral* integral,
                           double time);

// The first time the integral reaches target, for a curve no sample of
// which is below 0; INFINITY when it never does.
double mvfxCurveIntegralTime(const mvfxCurveTable* table, const mvfxCurveIntegral* integral,
                             double target);

#endif
