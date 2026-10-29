// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gradients baked for the simulation's loops, as curves are.

#ifndef MAUL_VFX_SRC_GRADIENT_H
#define MAUL_VFX_SRC_GRADIENT_H

#include "maul-vfx/curve.h"

#include <stdbool.h>
#include <stdint.h>

// A baked gradient: linear RGBA at MVFX_CURVE_SAMPLES points over [0, 1].
typedef struct mvfxGradientTable
{
    float rgba[MVFX_CURVE_SAMPLES][4];
} mvfxGradientTable;

// Whether a gradient is valid: both lists present, times in [0, 1] that
// never decrease, every channel finite (else mvfx_errorInvalid); both
// lists at most MVFX_CURVE_MAX_KEYS long, every channel at most
// MVFX_MAX_COLOR in size (else mvfx_errorLimit).
mvfxResult mvfxCheckGradient(const mvfxGradient* gradient);

// The largest size of a valid gradient's colour keys' channels and of its
// alpha keys: its baked colours blend its keys, so they bound them.
void mvfxGradientLargest(const mvfxGradient* gradient, double* color, double* alpha);

// Bakes a valid gradient.
void mvfxBakeGradient(const mvfxGradient* gradient, mvfxGradientTable* tableOut);

// The baked color at t in [0, 1] (clamped, a NaN read as 0), as four
// floats into rgbaOut.
static inline void mvfxGradientTableAt(const mvfxGradientTable* table, float t, float rgbaOut[4])
{
    float position = t * (float)(MVFX_CURVE_SAMPLES - 1);
    position = !(position >= 0.0f) ? 0.0f : position;
    position =
        position > (float)(MVFX_CURVE_SAMPLES - 1) ? (float)(MVFX_CURVE_SAMPLES - 1) : position;
    int32_t index = (int32_t)position;
    index = index > MVFX_CURVE_SAMPLES - 2 ? MVFX_CURVE_SAMPLES - 2 : index;
    float fraction = position - (float)index;
    for (int32_t c = 0; c < 4; ++c)
    {
        float low = table->rgba[index][c];
        rgbaOut[c] = low + (table->rgba[index + 1][c] - low) * fraction;
    }
}

#endif
