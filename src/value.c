// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Value sources.

#include "value.h"

#include <math.h>

// Whether low <= x <= high; false for NaN.
static bool IsWithin(float x, float low, float high)
{
    return x >= low && x <= high;
}

// Whether a vector's ends are finite and in range, in either order.
static bool AreEndsValid(float min, float max)
{
    return IsWithin(min, -MVFX_MAX_VALUE, MVFX_MAX_VALUE) &&
           IsWithin(max, -MVFX_MAX_VALUE, MVFX_MAX_VALUE);
}

static bool AreBoundsValid(float min, float max)
{
    return IsWithin(min, -MVFX_MAX_VALUE, MVFX_MAX_VALUE) &&
           IsWithin(max, -MVFX_MAX_VALUE, MVFX_MAX_VALUE) && min <= max;
}

// The larger size of two ends.
static double Reach(float min, float max)
{
    double low = fabs((double)min);
    double high = fabs((double)max);
    return low > high ? low : high;
}

// Where a curve source's curve is baked: over [0, 1] with a duration, else
// over its keys' span in seconds.
static void CurveDomain(const mvfxCurve* curve, bool hasDuration, float* start, float* end)
{
    *start = hasDuration ? 0.0f : curve->keys[0].time;
    *end = hasDuration ? 1.0f : curve->keys[curve->keyCount - 1].time;
}

// The largest size a valid curve source's base takes: its largest baked
// sample.
static double CurveBase(const mvfxCurve* curve, bool hasDuration)
{
    float start = 0.0f;
    float end = 0.0f;
    CurveDomain(curve, hasDuration, &start, &end);
    return mvfxCurveLargest(curve, start, end);
}

// The largest size a valid gradient source's base takes: its largest key.
static double GradientBase(const mvfxGradient* gradient)
{
    double color = 0.0;
    double alpha = 0.0;
    mvfxGradientLargest(gradient, &color, &alpha);
    return color > alpha ? color : alpha;
}

// Whether a base's largest size times a reach stays within a cap; false
// for NaN.
static bool IsWithinCap(double base, double reach, float cap)
{
    return base * reach <= (double)cap;
}

bool mvfxIsScalarSourceValid(const mvfxScalarSource* source, bool optional, bool hasDuration,
                             int32_t parameterCount, float cap)
{
    double reach = Reach(source->range.min, source->range.max);
    switch (source->kind)
    {
    case mvfx_sourceNone:
        return optional;
    case mvfx_sourceRange:
        return AreBoundsValid(source->range.min, source->range.max) && IsWithinCap(1.0, reach, cap);
    case mvfx_sourceCurve:
        return AreBoundsValid(source->range.min, source->range.max) &&
               mvfxIsCurveValid(&source->curve, hasDuration) &&
               IsWithinCap(CurveBase(&source->curve, hasDuration), reach, cap);
    case mvfx_sourceParameter:
        return AreBoundsValid(source->range.min, source->range.max) && source->parameter >= 0 &&
               source->parameter < parameterCount;
    default:
        return false;
    }
}

bool mvfxIsVectorSourceValid(const mvfxVectorSource* source, int32_t components, bool hasDuration,
                             int32_t parameterCount, const float* cap)
{
    if (source->kind == mvfx_sourceNone)
    {
        return true;
    }
    for (int32_t c = 0; c < components; ++c)
    {
        if (!AreEndsValid(source->min[c], source->max[c]))
        {
            return false;
        }
    }
    double base = 1.0;
    switch (source->kind)
    {
    case mvfx_sourceRange:
        break;
    case mvfx_sourceCurve:
        if (!mvfxIsCurveValid(&source->curve, hasDuration))
        {
            return false;
        }
        base = CurveBase(&source->curve, hasDuration);
        break;
    case mvfx_sourceParameter:
        return source->parameter >= 0 && source->parameter < parameterCount;
    case mvfx_sourceGradient:
    case mvfx_sourceGradientOverAge:
        if (components != 4 || (source->kind == mvfx_sourceGradientOverAge && !hasDuration) ||
            !mvfxIsGradientValid(&source->gradient))
        {
            return false;
        }
        base = GradientBase(&source->gradient);
        break;
    default:
        return false;
    }
    for (int32_t c = 0; c < components; ++c)
    {
        if (!IsWithinCap(base, Reach(source->min[c], source->max[c]), cap[c]))
        {
            return false;
        }
    }
    return true;
}

int32_t mvfxScalarCurves(const mvfxScalarSource* source)
{
    return source->kind == mvfx_sourceCurve ? 1 : 0;
}

int32_t mvfxVectorCurves(const mvfxVectorSource* source)
{
    return source->kind == mvfx_sourceCurve ? 1 : 0;
}

int32_t mvfxVectorGradients(const mvfxVectorSource* source)
{
    return source->kind == mvfx_sourceGradient || source->kind == mvfx_sourceGradientOverAge ? 1
                                                                                             : 0;
}

// Bakes a curve over its domain (CurveDomain).
static const mvfxCurveTable* BakeInto(const mvfxCurve* curve, bool hasDuration,
                                      mvfxTableSpace* space)
{
    mvfxCurveTable* table = &space->curves[space->nextCurve++];
    float start = 0.0f;
    float end = 0.0f;
    CurveDomain(curve, hasDuration, &start, &end);
    mvfxBakeCurve(curve, start, end, table);
    return table;
}

void mvfxScalarParameterReach(const mvfxScalarSource* source, float cap,
                              double reach[MVFX_MAX_PARAMETERS][4])
{
    if (source->kind == mvfx_sourceParameter && source->parameter >= 0 &&
        source->parameter < MVFX_MAX_PARAMETERS)
    {
        double share = Reach(source->range.min, source->range.max) / (double)cap;
        double* held = &reach[source->parameter][0];
        *held = share > *held ? share : *held;
    }
}

void mvfxVectorParameterReach(const mvfxVectorSource* source, int32_t components, const float* cap,
                              double reach[MVFX_MAX_PARAMETERS][4])
{
    if (source->kind == mvfx_sourceParameter && source->parameter >= 0 &&
        source->parameter < MVFX_MAX_PARAMETERS)
    {
        for (int32_t c = 0; c < components && c < 4; ++c)
        {
            double share = Reach(source->min[c], source->max[c]) / (double)cap[c];
            double* held = &reach[source->parameter][c];
            *held = share > *held ? share : *held;
        }
    }
}

bool mvfxIsParameterWithinReach(const float value[4], const double reach[4])
{
    for (int32_t c = 0; c < 4; ++c)
    {
        if (!(fabs((double)value[c]) * reach[c] <= 1.0))
        {
            return false;
        }
    }
    return true;
}

void mvfxCompileScalar(const mvfxScalarSource* source, bool hasDuration, mvfxTableSpace* space,
                       mvfxCompiledScalar* out)
{
    *out = (mvfxCompiledScalar){
        .kind = source->kind,
        .min = source->range.min,
        .max = source->range.max,
        .parameter = source->parameter,
    };
    if (source->kind == mvfx_sourceCurve)
    {
        out->curve = BakeInto(&source->curve, hasDuration, space);
    }
}

void mvfxCompileVector(const mvfxVectorSource* source, int32_t components, bool hasDuration,
                       mvfxTableSpace* space, mvfxCompiledVector* out)
{
    *out = (mvfxCompiledVector){
        .kind = source->kind,
        .uniform = source->uniform,
        .components = components,
        .parameter = source->parameter,
    };
    for (int32_t c = 0; c < 4; ++c)
    {
        out->min[c] = c < components ? source->min[c] : 0.0f;
        out->max[c] = c < components ? source->max[c] : 0.0f;
    }
    if (source->kind == mvfx_sourceCurve)
    {
        out->curve = BakeInto(&source->curve, hasDuration, space);
    }
    if (mvfxVectorGradients(source) > 0)
    {
        mvfxGradientTable* table = &space->gradients[space->nextGradient++];
        mvfxBakeGradient(&source->gradient, table);
        out->gradient = table;
    }
}

float mvfxDrawScalar(const mvfxCompiledScalar* source, float age, const float (*parameters)[4],
                     mvfxWords4 words)
{
    float value = source->min + (source->max - source->min) * mvfxUnitFloat(words.x);
    switch (source->kind)
    {
    case mvfx_sourceCurve:
        return mvfxCurveTableAt(source->curve, age) * value;
    case mvfx_sourceParameter:
        return parameters[source->parameter][0] * value;
    case mvfx_sourceRange:
        return value;
    default:
        return 0.0f;
    }
}

void mvfxDrawVector(const mvfxCompiledVector* source, float age, const float (*parameters)[4],
                    mvfxWords4 words, float out[4])
{
    uint32_t draws[4] = {words.x, words.y, words.z, words.w};
    float base[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    switch (source->kind)
    {
    case mvfx_sourceCurve:
    {
        float scale = mvfxCurveTableAt(source->curve, age);
        base[0] = base[1] = base[2] = base[3] = scale;
        break;
    }
    case mvfx_sourceParameter:
        for (int32_t c = 0; c < 4; ++c)
        {
            base[c] = parameters[source->parameter][c];
        }
        break;
    case mvfx_sourceGradient:
        mvfxGradientTableAt(source->gradient, mvfxUnitFloat(mvfxPcg4dMix(words).x), base);
        break;
    case mvfx_sourceGradientOverAge:
        mvfxGradientTableAt(source->gradient, age, base);
        break;
    case mvfx_sourceRange:
        break;
    default:
        for (int32_t c = 0; c < 4; ++c)
        {
            out[c] = 0.0f;
        }
        return;
    }
    for (int32_t c = 0; c < 4; ++c)
    {
        float u = mvfxUnitFloat(source->uniform ? draws[0] : draws[c]);
        float value = source->min[c] + (source->max[c] - source->min[c]) * u;
        out[c] = c < source->components ? base[c] * value : 0.0f;
    }
}
