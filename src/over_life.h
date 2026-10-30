// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Values over life: their checks and their compiled form.

#ifndef MAUL_VFX_SRC_OVER_LIFE_H
#define MAUL_VFX_SRC_OVER_LIFE_H

#include "gradient.h"
#include "life.h"

#include "maul-vfx/value.h"

// The most each value a particle stores may be in size, so that it times
// its value over life stays within its role's limit: a size's axis within
// MVFX_MAX_EXTENT, a colour's channel within MVFX_MAX_COLOR, a spin and
// each axis of an angular velocity within MVFX_MAX_SPIN, a custom
// attribute's components within MVFX_MAX_VALUE; at most MVFX_MAX_DRAW,
// which a value over life of 0 leaves.
typedef struct mvfxStoredCaps
{
    float size[3];
    float color[4];
    float spin;
    float custom[MVFX_MAX_CUSTOM_ATTRIBUTES];
} mvfxStoredCaps;

// The caps of an emitter's value sources, a cap per component: what its
// particles store, MVFX_MAX_DRAW for what nothing scales (an orientation).
typedef struct mvfxSourceCaps
{
    mvfxStoredCaps stored;
    float spin[4];
    float any[4];
    float custom[MVFX_MAX_CUSTOM_ATTRIBUTES][4];
} mvfxSourceCaps;

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
    mvfxStoredCaps caps;
} mvfxCompiledOverLife;

// Whether the values over life and the custom attributes' are valid:
// mvfx_errorLimit for a field past its named bound, mvfx_errorInvalid for
// anything else wrong. Names the field it refuses below the path of the
// emitter that holds them.
mvfxResult mvfxCheckOverLife(const mvfxFieldPath* emitter, const mvfxOverLife* overLife,
                             const mvfxCustomAttribute* custom, int32_t customCount);

// The caps of what particles store, for valid values over life.
void mvfxStoredCapsOf(const mvfxOverLife* overLife, const mvfxCustomAttribute* custom,
                      int32_t customCount, mvfxStoredCaps* out);

// The caps of the value sources, for valid values over life.
void mvfxSourceCapsOf(const mvfxOverLife* overLife, const mvfxCustomAttribute* custom,
                      int32_t customCount, mvfxSourceCaps* out);

// The curve and gradient tables they need, added to the counts.
void mvfxOverLifeTables(const mvfxOverLife* overLife, const mvfxCustomAttribute* custom,
                        int32_t customCount, int32_t* curves, int32_t* gradients);

void mvfxCompileOverLife(const mvfxOverLife* overLife, const mvfxCustomAttribute* custom,
                         int32_t customCount, mvfxTableSpace* space, mvfxCompiledOverLife* out);

#endif
