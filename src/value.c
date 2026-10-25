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

// Whether a valid curve's baked samples times ends of a reach stay within
// MVFX_MAX_DRAW.
static bool IsCurveDrawBounded(const mvfxCurve* curve, bool hasDuration, double reach)
{
    float start = 0.0f;
    float end = 0.0f;
    CurveDomain(curve, hasDuration, &start, &end);
    return mvfxCurveLargest(curve, start, end) * reach <= (double)MVFX_MAX_DRAW;
}

// Whether a valid gradient's channels times ends of a reach stay within
// MVFX_MAX_DRAW: its baked colours blend its keys, so its keys bound them.
static bool IsGradientDrawBounded(const mvfxGradient* gradient, double reach)
{
    double largest = 0.0;
    for (int32_t i = 0; i < gradient->colorKeyCount; ++i)
    {
        const mvfxColorKey* key = &gradient->colorKeys[i];
        double sizes[3] = {fabs((double)key->red), fabs((double)key->green),
                           fabs((double)key->blue)};
        for (int32_t c = 0; c < 3; ++c)
        {
            largest = sizes[c] > largest ? sizes[c] : largest;
        }
    }
    for (int32_t i = 0; i < gradient->alphaKeyCount; ++i)
    {
        double size = fabs((double)gradient->alphaKeys[i].alpha);
        largest = size > largest ? size : largest;
    }
    return largest * reach <= (double)MVFX_MAX_DRAW;
}

bool mvfxIsScalarSourceValid(const mvfxScalarSource* source, bool optional, bool hasDuration,
                             int32_t parameterCount)
{
    switch (source->kind)
    {
    case mvfx_sourceNone:
        return optional;
    case mvfx_sourceRange:
        return AreBoundsValid(source->range.min, source->range.max);
    case mvfx_sourceCurve:
        return AreBoundsValid(source->range.min, source->range.max) &&
               mvfxIsCurveValid(&source->curve, hasDuration) &&
               IsCurveDrawBounded(&source->curve, hasDuration,
                                  Reach(source->range.min, source->range.max));
    case mvfx_sourceParameter:
        return AreBoundsValid(source->range.min, source->range.max) && source->parameter >= 0 &&
               source->parameter < parameterCount;
    default:
        return false;
    }
}

bool mvfxIsVectorSourceValid(const mvfxVectorSource* source, int32_t components, bool hasDuration,
                             int32_t parameterCount)
{
    if (source->kind == mvfx_sourceNone)
    {
        return true;
    }
    double reach = 0.0;
    for (int32_t c = 0; c < components; ++c)
    {
        if (!AreEndsValid(source->min[c], source->max[c]))
        {
            return false;
        }
        double size = Reach(source->min[c], source->max[c]);
        reach = size > reach ? size : reach;
    }
    switch (source->kind)
    {
    case mvfx_sourceRange:
        return true;
    case mvfx_sourceCurve:
        return mvfxIsCurveValid(&source->curve, hasDuration) &&
               IsCurveDrawBounded(&source->curve, hasDuration, reach);
    case mvfx_sourceParameter:
        return source->parameter >= 0 && source->parameter < parameterCount;
    case mvfx_sourceGradient:
        return components == 4 && mvfxIsGradientValid(&source->gradient) &&
               IsGradientDrawBounded(&source->gradient, reach);
    case mvfx_sourceGradientOverAge:
        return components == 4 && hasDuration && mvfxIsGradientValid(&source->gradient) &&
               IsGradientDrawBounded(&source->gradient, reach);
    default:
        return false;
    }
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

void mvfxScalarParameterReach(const mvfxScalarSource* source, float reach[MVFX_MAX_PARAMETERS][4])
{
    if (source->kind == mvfx_sourceParameter && source->parameter >= 0 &&
        source->parameter < MVFX_MAX_PARAMETERS)
    {
        float size = (float)Reach(source->range.min, source->range.max);
        float* held = &reach[source->parameter][0];
        *held = size > *held ? size : *held;
    }
}

void mvfxVectorParameterReach(const mvfxVectorSource* source, int32_t components,
                              float reach[MVFX_MAX_PARAMETERS][4])
{
    if (source->kind == mvfx_sourceParameter && source->parameter >= 0 &&
        source->parameter < MVFX_MAX_PARAMETERS)
    {
        for (int32_t c = 0; c < components && c < 4; ++c)
        {
            float size = (float)Reach(source->min[c], source->max[c]);
            float* held = &reach[source->parameter][c];
            *held = size > *held ? size : *held;
        }
    }
}

bool mvfxIsParameterWithinReach(const float value[4], const float reach[4])
{
    for (int32_t c = 0; c < 4; ++c)
    {
        if (!(fabs((double)value[c]) * (double)reach[c] <= (double)MVFX_MAX_DRAW))
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
