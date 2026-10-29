// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "life.h"

#include "refusal.h"

#include <math.h>

double mvfxLifeCurveLargest(const mvfxLifeCurve* life)
{
    double value = fabs((double)life->value);
    return life->curve.keyCount == 0 ? value : value * mvfxCurveLargest(&life->curve, 0.0f, 1.0f);
}

mvfxResult mvfxCheckLifeCurve(const mvfxLifeCurve* life)
{
    // A value past MVFX_MAX_VALUE is refused before the curve is sampled,
    // so a zero curve cannot hide it.
    mvfxResult value = mvfxBounded(life->value, -MVFX_MAX_VALUE, MVFX_MAX_VALUE);
    if (value != mvfx_success)
    {
        return value;
    }
    mvfxResult curve =
        life->curve.keyCount == 0 ? mvfx_success : mvfxCheckCurve(&life->curve, true);
    if (curve != mvfx_success)
    {
        return curve;
    }
    return mvfxWithinBound(mvfxLifeCurveLargest(life) <= (double)MVFX_MAX_LIFE_SCALE);
}

int32_t mvfxLifeCurves(const mvfxLifeCurve* life)
{
    return life->curve.keyCount > 0 ? 1 : 0;
}

float mvfxLifeBound(const mvfxCompiledLife* life)
{
    float most = 1.0f;
    if (life->curve != nullptr)
    {
        most = 0.0f;
        for (int32_t k = 0; k < MVFX_CURVE_SAMPLES; ++k)
        {
            most = fmaxf(most, fabsf(life->curve->samples[k]));
        }
    }
    return fabsf(life->value) * most;
}

void mvfxCompileLife(const mvfxLifeCurve* life, mvfxTableSpace* space, mvfxCompiledLife* out)
{
    out->value = life->value;
    out->curve = nullptr;
    if (life->curve.keyCount > 0)
    {
        mvfxCurveTable* table = &space->curves[space->nextCurve];
        space->nextCurve += 1;
        mvfxBakeCurve(&life->curve, 0.0f, 1.0f, table);
        out->curve = table;
    }
}
