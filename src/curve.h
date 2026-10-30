// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Curves baked for the simulation's loops: a curve becomes
// MVFX_CURVE_SAMPLES samples over its domain, read with one blend between
// two samples, the same table on both paths.

#ifndef MAUL_VFX_SRC_CURVE_H
#define MAUL_VFX_SRC_CURVE_H

#include "field_path.h"

#include "maul-vfx/curve.h"

#include <stdbool.h>

// A baked curve: samples at start + i / (MVFX_CURVE_SAMPLES - 1) of the
// domain, and the factor that maps a time to a sample position (0 for a
// domain of one point).
typedef struct mvfxCurveTable
{
    float samples[MVFX_CURVE_SAMPLES];
    float start;
    float scale;
} mvfxCurveTable;

// Whether a curve is valid: keys present, every field finite, times that
// never decrease, interpolations and the tangent rule known, and with
// unitDomain every time in [0, 1] (else mvfx_errorInvalid); at most
// MVFX_CURVE_MAX_KEYS keys (else mvfx_errorLimit). Names the field it
// refuses below at.
mvfxResult mvfxCheckCurve(const mvfxFieldPath* at, const mvfxCurve* curve, bool unitDomain);

// The curve's exact value at time, in binary64, from its keys.
double mvfxCurveValueAt(const mvfxCurve* curve, double time);

// Bakes a valid curve over [start, end], start <= end.
void mvfxBakeCurve(const mvfxCurve* curve, float start, float end, mvfxCurveTable* tableOut);

// The baked value at time: the blend of the two samples around it, the
// end samples outside the domain (the first for a NaN time).
static inline float mvfxCurveTableAt(const mvfxCurveTable* table, float time)
{
    float position = (time - table->start) * table->scale;
    position = !(position >= 0.0f) ? 0.0f : position;
    position =
        position > (float)(MVFX_CURVE_SAMPLES - 1) ? (float)(MVFX_CURVE_SAMPLES - 1) : position;
    int32_t index = (int32_t)position;
    index = index > MVFX_CURVE_SAMPLES - 2 ? MVFX_CURVE_SAMPLES - 2 : index;
    float fraction = position - (float)index;
    float low = table->samples[index];
    return low + (table->samples[index + 1] - low) * fraction;
}

// The largest size of the samples mvfxBakeCurve bakes over [start, end]
// (NaN if one is).
double mvfxCurveLargest(const mvfxCurve* curve, float start, float end);

// Whether no sample of a baked curve is below 0, as a rate must be.
bool mvfxIsCurveTableNonNegative(const mvfxCurveTable* table);

#endif
