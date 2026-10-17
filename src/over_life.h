// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Values over life: their checks and their compiled form.

#ifndef MAUL_VFX_SRC_OVER_LIFE_H
#define MAUL_VFX_SRC_OVER_LIFE_H

#include "gradient.h"
#include "life.h"

#include "maul-vfx/value.h"

// The values over life as compiled; a gradient of NULL is white.
typedef struct mvfxCompiledOverLife
{
    bool color;
    mvfxOverLifeInput colorInput;
    float colorRange[2];
    const mvfxGradientTable* gradient;
    bool scaleAlpha;
    mvfxCompiledLife alpha;
    bool size;
    mvfxOverLifeInput sizeInput;
    float sizeRange[2];
    bool perAxis;
    mvfxCompiledLife sizes[3];
    // The largest each axis's factor is at any input, which bounds are
    // grown by: 1 without sizes over life; and a hair more for the
    // rounding of reading the curve and of the product (1 without).
    float sizeBounds[3];
    float sizeMargin;
    mvfxFrameOverLife frame;
    bool rotation;
    mvfxCompiledLife rotationScale;
    bool customScaled[MVFX_MAX_CUSTOM_ATTRIBUTES];
    mvfxCompiledLife custom[MVFX_MAX_CUSTOM_ATTRIBUTES];
} mvfxCompiledOverLife;

// Whether the values over life and the custom attributes' are valid.
bool mvfxIsOverLifeValid(const mvfxOverLife* overLife, const mvfxCustomAttribute* custom,
                         int32_t customCount);

// The curve and gradient tables they need, added to the counts.
void mvfxOverLifeTables(const mvfxOverLife* overLife, const mvfxCustomAttribute* custom,
                        int32_t customCount, int32_t* curves, int32_t* gradients);

void mvfxCompileOverLife(const mvfxOverLife* overLife, const mvfxCustomAttribute* custom,
                         int32_t customCount, mvfxTableSpace* space, mvfxCompiledOverLife* out);

#endif
