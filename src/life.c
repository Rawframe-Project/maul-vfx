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
