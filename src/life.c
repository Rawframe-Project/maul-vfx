// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "life.h"

#include <math.h>

bool mvfxIsLifeCurveValid(const mvfxLifeCurve* life)
{
    return fabsf(life->value) <= MVFX_MAX_VALUE &&
           (life->curve.keyCount == 0 || mvfxIsCurveValid(&life->curve, true));
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
