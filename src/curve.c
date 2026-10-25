// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Curve keys to values (cubic Hermite segments, with tangents given, from
// neighbors, or by Fritsch and Carlson's monotone rule), baking to tables,
// integrals of baked curves and their exact inverse, and gradients.

#include "curve.h"

#include <math.h>
#include <string.h>

// The largest a key's field may be in size: past it, products in the
// Hermite form could overflow binary32.
#define CURVE_FIELD_LIMIT 1e18f

static bool IsFieldValid(float x)
{
    return x >= -CURVE_FIELD_LIMIT && x <= CURVE_FIELD_LIMIT;
}

bool mvfxIsCurveValid(const mvfxCurve* curve, bool unitDomain)
{
    if (curve == nullptr || curve->keys == nullptr || curve->keyCount < 1 ||
        curve->keyCount > MVFX_CURVE_MAX_KEYS || curve->tangents > mvfx_tangentsMonotone)
    {
        return false;
    }
    for (int32_t i = 0; i < curve->keyCount; ++i)
    {
        const mvfxCurveKey* key = &curve->keys[i];
        if (!IsFieldValid(key->time) || !IsFieldValid(key->value) ||
            !IsFieldValid(key->inTangent) || !IsFieldValid(key->outTangent) ||
            key->interpolation > mvfx_interpolationCubic)
        {
            return false;
        }
        if (unitDomain && (key->time < 0.0f || key->time > 1.0f))
        {
            return false;
        }
        if (i > 0 && key->time < curve->keys[i - 1].time)
        {
            return false;
        }
    }
    return true;
}

// The slope from key a to key b, 0 across a step.
static double Secant(const mvfxCurveKey* a, const mvfxCurveKey* b)
{
    double span = (double)b->time - (double)a->time;
    return span > 0.0 ? ((double)b->value - (double)a->value) / span : 0.0;
}

// The tangent the smooth rule gives key i: the slope between its
// neighbors, one-sided at the ends.
static double SmoothTangent(const mvfxCurve* curve, int32_t i)
{
    int32_t before = i > 0 ? i - 1 : i;
    int32_t after = i < curve->keyCount - 1 ? i + 1 : i;
    return Secant(&curve->keys[before], &curve->keys[after]);
}

// The tangents Fritsch and Carlson's rule gives every key, one per key
// for both segments around it: each key's the mean of the secants either
// side (0 where they differ in sign, the end keys their one secant); then,
// segment by segment in order, both ends 0 on a flat segment, and both
// scaled into the circle of radius 3 that keeps the segment monotone.
static void MonotoneTangents(const mvfxCurve* curve, double* tangentsOut)
{
    const mvfxCurveKey* keys = curve->keys;
    int32_t last = curve->keyCount - 1;
    double secants[MVFX_CURVE_MAX_KEYS] = {0};
    for (int32_t k = 0; k < last; ++k)
    {
        secants[k] = Secant(&keys[k], &keys[k + 1]);
    }
    tangentsOut[0] = secants[0];
    tangentsOut[last] = last > 0 ? secants[last - 1] : 0.0;
    for (int32_t k = 1; k < last; ++k)
    {
        double before = secants[k - 1];
        double after = secants[k];
        tangentsOut[k] = before * after <= 0.0 ? 0.0 : (before + after) / 2.0;
    }
    for (int32_t k = 0; k < last; ++k)
    {
        double here = secants[k];
        if (here == 0.0)
        {
            tangentsOut[k] = 0.0;
            tangentsOut[k + 1] = 0.0;
            continue;
        }
        double alpha = tangentsOut[k] / here;
        double beta = tangentsOut[k + 1] / here;
        double radius = alpha * alpha + beta * beta;
        if (radius > 9.0)
        {
            double tau = 3.0 / sqrt(radius);
            tangentsOut[k] = tau * alpha * here;
            tangentsOut[k + 1] = tau * beta * here;
        }
    }
}

// The cubic Hermite segment from key i to key i + 1 at fraction u.
static double CubicAt(const mvfxCurve* curve, int32_t i, double u)
{
    const mvfxCurveKey* a = &curve->keys[i];
    const mvfxCurveKey* b = &curve->keys[i + 1];
    double span = (double)b->time - (double)a->time;
    double start = (double)a->outTangent;
    double end = (double)b->inTangent;
    if (curve->tangents == mvfx_tangentsSmooth)
    {
        start = SmoothTangent(curve, i);
        end = SmoothTangent(curve, i + 1);
    }
    else if (curve->tangents == mvfx_tangentsMonotone)
    {
        double tangents[MVFX_CURVE_MAX_KEYS] = {0};
        MonotoneTangents(curve, tangents);
        start = tangents[i];
        end = tangents[i + 1];
    }
    double u2 = u * u;
    double u3 = u2 * u;
    double h00 = 2.0 * u3 - 3.0 * u2 + 1.0;
    double h10 = u3 - 2.0 * u2 + u;
    double h01 = -2.0 * u3 + 3.0 * u2;
    double h11 = u3 - u2;
    return h00 * (double)a->value + h10 * span * start + h01 * (double)b->value + h11 * span * end;
}

double mvfxCurveValueAt(const mvfxCurve* curve, double time)
{
    const mvfxCurveKey* keys = curve->keys;
    int32_t last = curve->keyCount - 1;
    if (time <= (double)keys[0].time)
    {
        return (double)keys[0].value;
    }
    if (time >= (double)keys[last].time)
    {
        return (double)keys[last].value;
    }
    // The last key at or before time: of two keys at one time, the later.
    int32_t i = 0;
    while (i < last && (double)keys[i + 1].time <= time)
    {
        ++i;
    }
    double span = (double)keys[i + 1].time - (double)keys[i].time;
    double u = (time - (double)keys[i].time) / span;
    switch (keys[i].interpolation)
    {
    case mvfx_interpolationConstant:
        return (double)keys[i].value;
    case mvfx_interpolationLinear:
        return (double)keys[i].value + ((double)keys[i + 1].value - (double)keys[i].value) * u;
    default:
        return CubicAt(curve, i, u);
    }
}

// Sample i of a curve baked over start and a span.
static float SampleAt(const mvfxCurve* curve, float start, double span, int32_t i)
{
    double time = (double)start + span * (double)i / (double)(MVFX_CURVE_SAMPLES - 1);
    return (float)mvfxCurveValueAt(curve, time);
}

void mvfxBakeCurve(const mvfxCurve* curve, float start, float end, mvfxCurveTable* tableOut)
{
    double span = (double)end - (double)start;
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        tableOut->samples[i] = SampleAt(curve, start, span, i);
    }
    tableOut->start = start;
    tableOut->scale = span > 0.0 ? (float)((double)(MVFX_CURVE_SAMPLES - 1) / span) : 0.0f;
}

double mvfxCurveLargest(const mvfxCurve* curve, float start, float end)
{
    double span = (double)end - (double)start;
    double largest = 0.0;
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        double size = fabs((double)SampleAt(curve, start, span, i));
        if (isnan(size))
        {
            return size;
        }
        largest = size > largest ? size : largest;
    }
    return largest;
}

bool mvfxIsCurveTableNonNegative(const mvfxCurveTable* table)
{
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        if (table->samples[i] < 0.0f)
        {
            return false;
        }
    }
    return true;
}
