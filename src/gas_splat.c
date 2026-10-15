// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "gas_splat.h"

#include "elementary.h"

#include <math.h>

// The 8 points of an array (the cells', or the faces' across axis) nearest
// place p in cells, held within the array as sampling holds them, and
// their trilinear weights (summing to 1). False for a place outside the
// box (a 2D gas's Z not judged).
static bool Nearest(const mvfxGasLattice* l, int32_t axis, const float p[3], int64_t at[8],
                    float weight[8])
{
    int32_t m[3];
    mvfxGasPoints(l, axis, m);
    int32_t low[3];
    int32_t high[3];
    float t[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        if (l->n[k] > 1 && !(p[k] >= 0.0f && p[k] <= (float)l->n[k]))
        {
            return false;
        }
        float c = fminf(fmaxf(p[k] - (k == axis ? 0.0f : 0.5f), 0.0f), (float)(m[k] - 1));
        float base = floorf(c);
        low[k] = (int32_t)base;
        high[k] = low[k] + 1 < m[k] ? low[k] + 1 : low[k];
        t[k] = c - base;
    }
    for (int32_t corner = 0; corner < 8; ++corner)
    {
        int32_t point[3];
        weight[corner] = 1.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            bool up = (corner >> k & 1) != 0;
            point[k] = up ? high[k] : low[k];
            weight[corner] *= up ? t[k] : 1.0f - t[k];
        }
        at[corner] = ((int64_t)point[2] * m[1] + point[1]) * m[0] + point[0];
    }
    return true;
}

// One particle into the gas: p its place in cells, v its velocity in the
// box's frame, pull the share of the way to it a face goes at full weight.
static void SplatOne(const mvfxGasLattice* l, const mvfxGasSplat* splat, const float p[3],
                     const float v[3], float pull, float* const velocity[3],
                     float* const scalars[3])
{
    int64_t at[8];
    float weight[8];
    const float rates[3] = {splat->density * l->h, splat->temperature * l->h, splat->fuel * l->h};
    if (!Nearest(l, -1, p, at, weight))
    {
        return;
    }
    for (int32_t c = 0; c < 8; ++c)
    {
        for (int32_t f = 0; f < 3; ++f)
        {
            scalars[f][at[c]] += weight[c] * rates[f];
        }
    }
    for (int32_t k = 0; k < 3 && pull > 0.0f; ++k)
    {
        if (l->n[k] == 1 || !Nearest(l, k, p, at, weight))
        {
            continue;
        }
        for (int32_t c = 0; c < 8; ++c)
        {
            float* face = &velocity[k][at[c]];
            *face += weight[c] * pull * (v[k] - *face);
        }
    }
}

void mvfxGasSplatInto(const mvfxGasLattice* l, const mvfxGasPour* pour, float* const velocity[3],
                      float* const scalars[3])
{
    float pull = 1.0f - mvfxExp(-pour->splat->drag * l->h);
    for (int32_t i = 0; i < pour->count; ++i)
    {
        const float x[3] = {pour->position[0][i], pour->position[1][i], pour->position[2][i]};
        const float u[3] = {pour->velocity[0][i], pour->velocity[1][i], pour->velocity[2][i]};
        float local[3];
        mvfxMapPoint(&pour->map, x, local);
        float v[3];
        mvfxMapVector(&pour->map, u, pour->map.sigma, v);
        float p[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            p[k] = local[k] / l->cell + 0.5f * (float)l->n[k];
        }
        SplatOne(l, pour->splat, p, v, pull, velocity, scalars);
    }
}
