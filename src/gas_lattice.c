// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "gas_lattice.h"

#include <math.h>

void mvfxGasPoints(const mvfxGasLattice* l, int32_t axis, int32_t out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = l->n[k] + (k == axis ? 1 : 0);
    }
}

// The points of an array about place p along each axis, below and above
// (held within the array), and the share of the way between them: by
// truncation, which is the floor for a place held at 0 or above.
typedef struct Corners
{
    int32_t m[3];
    int32_t low[3];
    int32_t high[3];
    float t[3];
} Corners;

static Corners CornersAt(const mvfxGasLattice* l, int32_t axis, const float p[3])
{
    Corners c;
    mvfxGasPoints(l, axis, c.m);
    for (int32_t k = 0; k < 3; ++k)
    {
        float place = fminf(fmaxf(p[k] - (k == axis ? 0.0f : 0.5f), 0.0f), (float)(c.m[k] - 1));
        c.low[k] = (int32_t)place;
        c.high[k] = c.low[k] + 1 < c.m[k] ? c.low[k] + 1 : c.low[k];
        c.t[k] = place - (float)c.low[k];
    }
    return c;
}

float mvfxGasSampleRange(const float* values, const mvfxGasLattice* l, int32_t axis,
                         const float p[3], float range[2])
{
    Corners c = CornersAt(l, axis, p);
    float sum = 0.0f;
    range[0] = INFINITY;
    range[1] = -INFINITY;
    for (int32_t corner = 0; corner < 8; ++corner)
    {
        int32_t at[3];
        float weight = 1.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            bool up = (corner >> k & 1) != 0;
            at[k] = up ? c.high[k] : c.low[k];
            weight *= up ? c.t[k] : 1.0f - c.t[k];
        }
        float value = values[((int64_t)at[2] * c.m[1] + at[1]) * c.m[0] + at[0]];
        sum += weight * value;
        range[0] = fminf(range[0], value);
        range[1] = fmaxf(range[1], value);
    }
    return sum;
}

float mvfxGasSample(const float* values, const mvfxGasLattice* l, int32_t axis, const float p[3])
{
    Corners c = CornersAt(l, axis, p);
    float wx[2] = {1.0f - c.t[0], c.t[0]};
    float wy[2] = {1.0f - c.t[1], c.t[1]};
    const int32_t x[2] = {c.low[0], c.high[0]};
    if (c.m[2] == 1)
    {
        // A 2D array: the trilinear sum's four terms along Z weigh exactly
        // 0 and the rest exactly their bilinear weights, so this sum, in
        // the same corner order, is the trilinear one bit for bit.
        const float* low = values + (int64_t)c.low[1] * c.m[0];
        const float* high = values + (int64_t)c.high[1] * c.m[0];
        float sum = 0.0f;
        sum += wx[0] * wy[0] * low[x[0]];
        sum += wx[1] * wy[0] * low[x[1]];
        sum += wx[0] * wy[1] * high[x[0]];
        sum += wx[1] * wy[1] * high[x[1]];
        return sum;
    }
    float wz[2] = {1.0f - c.t[2], c.t[2]};
    const int32_t y[2] = {c.low[1], c.high[1]};
    const int32_t z[2] = {c.low[2], c.high[2]};
    float sum = 0.0f;
    for (int32_t corner = 0; corner < 8; ++corner)
    {
        int32_t a = corner & 1;
        int32_t b = corner >> 1 & 1;
        int32_t d = corner >> 2;
        float weight = wx[a] * wy[b] * wz[d];
        sum += weight * values[((int64_t)z[d] * c.m[1] + y[b]) * c.m[0] + x[a]];
    }
    return sum;
}
