// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Value sources.

#include "value.h"

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
               mvfxIsCurveValid(&source->curve, hasDuration);
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
    for (int32_t c = 0; c < components; ++c)
    {
        if (!AreEndsValid(source->min[c], source->max[c]))
        {
            return false;
        }
    }
    switch (source->kind)
    {
    case mvfx_sourceRange:
        return true;
    case mvfx_sourceCurve:
        return mvfxIsCurveValid(&source->curve, hasDuration);
    case mvfx_sourceParameter:
        return source->parameter >= 0 && source->parameter < parameterCount;
    case mvfx_sourceGradient:
        return components == 4 && mvfxIsGradientValid(&source->gradient);
    case mvfx_sourceGradientOverAge:
        return components == 4 && hasDuration && mvfxIsGradientValid(&source->gradient);
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

// Bakes a curve over [0, 1] with a duration, else over its keys' span in
// seconds.
static const mvfxCurveTable* BakeInto(const mvfxCurve* curve, bool hasDuration,
                                      mvfxTableSpace* space)
{
    mvfxCurveTable* table = &space->curves[space->nextCurve++];
    float start = hasDuration ? 0.0f : curve->keys[0].time;
    float end = hasDuration ? 1.0f : curve->keys[curve->keyCount - 1].time;
    mvfxBakeCurve(curve, start, end, table);
    return table;
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
