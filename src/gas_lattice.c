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

float mvfxGasSampleRange(const float* values, const mvfxGasLattice* l, int32_t axis,
                         const float p[3], float range[2])
{
    int32_t m[3];
    mvfxGasPoints(l, axis, m);
    int32_t low[3];
    int32_t high[3];
    float t[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        float c = p[k] - (k == axis ? 0.0f : 0.5f);
        c = fminf(fmaxf(c, 0.0f), (float)(m[k] - 1));
        float base = floorf(c);
        low[k] = (int32_t)base;
        high[k] = low[k] + 1 < m[k] ? low[k] + 1 : low[k];
        t[k] = c - base;
    }
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
            at[k] = up ? high[k] : low[k];
            weight *= up ? t[k] : 1.0f - t[k];
        }
        float value = values[((int64_t)at[2] * m[1] + at[1]) * m[0] + at[0]];
        sum += weight * value;
        range[0] = fminf(range[0], value);
        range[1] = fmaxf(range[1], value);
    }
    return sum;
}

float mvfxGasSample(const float* values, const mvfxGasLattice* l, int32_t axis, const float p[3])
{
    float range[2];
    return mvfxGasSampleRange(values, l, axis, p, range);
}
