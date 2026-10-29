// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "gas_forces.h"

#include "direction.h"
#include "elementary.h"

#include <math.h>

// A lattice's strides between cells along each axis.
static void Strides(const mvfxGasLattice* l, int64_t out[3])
{
    out[0] = 1;
    out[1] = l->n[0];
    out[2] = (int64_t)l->n[0] * l->n[1];
}

// The buoyant factor at a cell: densityWeight density - heatLift
// temperature.
static float Lift(const mvfxGasDef* def, float* const scalars[3], int64_t i)
{
    return def->densityWeight * scalars[0][i] - def->heatLift * scalars[1][i];
}

// The mean of the buoyant factor over the cells beside the face across k
// at place along k: the two cells', or a wall face's one.
static float FaceLift(const mvfxGasDef* def, float* const scalars[3], int32_t place, int32_t n,
                      int64_t cell, int64_t stride)
{
    bool low = place > 0;
    bool high = place < n;
    float sum =
        (low ? Lift(def, scalars, cell - stride) : 0.0f) + (high ? Lift(def, scalars, cell) : 0.0f);
    return low && high ? 0.5f * sum : sum;
}

void mvfxGasBuoyancy(const mvfxCompiledGas* gas, const mvfxGasLattice* l, const float gravity[3],
                     float* const velocity[3], float* const scalars[3])
{
    const mvfxGasDef* def = &gas->def;
    if (def->densityWeight == 0.0f && def->heatLift == 0.0f)
    {
        return;
    }
    int64_t stride[3];
    Strides(l, stride);
    for (int32_t k = 0; k < 3; ++k)
    {
        if (l->n[k] == 1 || gravity[k] == 0.0f)
        {
            continue;
        }
        const int32_t m[3] = {l->n[0] + (k == 0 ? 1 : 0), l->n[1] + (k == 1 ? 1 : 0),
                              l->n[2] + (k == 2 ? 1 : 0)};
        float scale = l->h * gravity[k];
        int64_t f = 0;
        for (int32_t z = 0; z < m[2]; ++z)
        {
            for (int32_t y = 0; y < m[1]; ++y)
            {
                for (int32_t x = 0; x < m[0]; ++x, ++f)
                {
                    const int32_t at[3] = {x, y, z};
                    int64_t cell = z * stride[2] + y * stride[1] + x;
                    velocity[k][f] +=
                        scale * FaceLift(def, scalars, at[k], l->n[k], cell, stride[k]);
                }
            }
        }
    }
}

// The cell-centered velocity's component c at a cell: the mean of its two
// faces across c (0 along a 2D gas's Z).
static float Centered(const mvfxGasLattice* l, float* const velocity[3], int32_t c,
                      const int32_t at[3])
{
    if (l->n[c] == 1)
    {
        return 0.0f;
    }
    const int32_t m[3] = {l->n[0] + (c == 0 ? 1 : 0), l->n[1] + (c == 1 ? 1 : 0),
                          l->n[2] + (c == 2 ? 1 : 0)};
    int64_t low = ((int64_t)at[2] * m[1] + at[1]) * m[0] + at[0];
    const int64_t step[3] = {1, m[0], (int64_t)m[0] * m[1]};
    return 0.5f * (velocity[c][low] + velocity[c][low + step[c]]);
}

// The derivative along a of the centered velocity's component c at a
// cell: a central difference, one-sided at the box's edges (0 across a
// 2D gas's Z).
static float Derivative(const mvfxGasLattice* l, float* const velocity[3], int32_t c, int32_t a,
                        const int32_t at[3])
{
    if (l->n[a] == 1 || l->n[c] == 1)
    {
        return 0.0f;
    }
    int32_t low[3] = {at[0], at[1], at[2]};
    int32_t high[3] = {at[0], at[1], at[2]};
    low[a] = at[a] > 0 ? at[a] - 1 : at[a];
    high[a] = at[a] + 1 < l->n[a] ? at[a] + 1 : at[a];
    float span = (float)(high[a] - low[a]) * l->cell;
    return (Centered(l, velocity, c, high) - Centered(l, velocity, c, low)) / span;
}

// The curl at each cell into work: a 2D gas's about Z alone, a 3D gas's
// three components one array after another.
static void Curl(const mvfxGasLattice* l, float* const velocity[3], float* work)
{
    int64_t cells = (int64_t)l->n[0] * l->n[1] * l->n[2];
    bool flat = l->n[2] == 1;
    int64_t i = 0;
    for (int32_t z = 0; z < l->n[2]; ++z)
    {
        for (int32_t y = 0; y < l->n[1]; ++y)
        {
            for (int32_t x = 0; x < l->n[0]; ++x, ++i)
            {
                const int32_t at[3] = {x, y, z};
                float about = Derivative(l, velocity, 1, 0, at) - Derivative(l, velocity, 0, 1, at);
                if (flat)
                {
                    work[i] = about;
                    continue;
                }
                work[i] = Derivative(l, velocity, 2, 1, at) - Derivative(l, velocity, 1, 2, at);
                work[cells + i] =
                    Derivative(l, velocity, 0, 2, at) - Derivative(l, velocity, 2, 0, at);
                work[2 * cells + i] = about;
            }
        }
    }
}

// The curl's components at cell i (a 2D gas's about Z alone).
static void CurlAt(const mvfxGasLattice* l, const float* work, int64_t i, float out[3])
{
    int64_t cells = (int64_t)l->n[0] * l->n[1] * l->n[2];
    bool flat = l->n[2] == 1;
    out[0] = flat ? 0.0f : work[i];
    out[1] = flat ? 0.0f : work[cells + i];
    out[2] = flat ? work[i] : work[2 * cells + i];
}

// The curl's size at a cell.
static float Swirl(const mvfxGasLattice* l, const float* work, const int32_t at[3])
{
    int64_t i = ((int64_t)at[2] * l->n[1] + at[1]) * l->n[0] + at[0];
    float w[3];
    CurlAt(l, work, i, w);
    return sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
}

// Vorticity confinement's force at a cell: epsilon h (N x w), N the
// gradient of the curl's size over the larger of its own size and 1e-4 of
// its scale (the largest size it was taken from over the cell's edge): a
// unit vector where the curl's size changes, tapering to nothing where it
// is even, rather than float's noise there made a unit.
static void ConfineAt(const mvfxGasLattice* l, const float* work, float epsilon,
                      const int32_t at[3], float out[3])
{
    float gradient[3] = {0.0f, 0.0f, 0.0f};
    float peak = 0.0f;
    for (int32_t a = 0; a < 3; ++a)
    {
        if (l->n[a] == 1)
        {
            continue;
        }
        int32_t low[3] = {at[0], at[1], at[2]};
        int32_t high[3] = {at[0], at[1], at[2]};
        low[a] = at[a] > 0 ? at[a] - 1 : at[a];
        high[a] = at[a] + 1 < l->n[a] ? at[a] + 1 : at[a];
        float below = Swirl(l, work, low);
        float above = Swirl(l, work, high);
        peak = fmaxf(peak, fmaxf(below, above));
        gradient[a] = (above - below) / ((float)(high[a] - low[a]) * l->cell);
    }
    float size = mvfxLengthOf(gradient[0], gradient[1], gradient[2]);
    float w[3];
    CurlAt(l, work, ((int64_t)at[2] * l->n[1] + at[1]) * l->n[0] + at[0], w);
    float floor = 1e-4f * peak / l->cell;
    float scale = peak > 0.0f ? epsilon * l->cell / fmaxf(size, floor) : 0.0f;
    out[0] = scale * (gradient[1] * w[2] - gradient[2] * w[1]);
    out[1] = scale * (gradient[2] * w[0] - gradient[0] * w[2]);
    out[2] = scale * (gradient[0] * w[1] - gradient[1] * w[0]);
}

// Confinement's force across k at a face: the mean of the cells' beside
// it, or a wall face's one.
static float ConfineFace(const mvfxGasLattice* l, const float* work, float epsilon, int32_t k,
                         const int32_t at[3])
{
    float sum = 0.0f;
    int32_t count = 0;
    float force[3];
    if (at[k] > 0)
    {
        const int32_t low[3] = {at[0] - (k == 0 ? 1 : 0), at[1] - (k == 1 ? 1 : 0),
                                at[2] - (k == 2 ? 1 : 0)};
        ConfineAt(l, work, epsilon, low, force);
        sum += force[k];
        count += 1;
    }
    if (at[k] < l->n[k])
    {
        ConfineAt(l, work, epsilon, at, force);
        sum += force[k];
        count += 1;
    }
    return count == 2 ? 0.5f * sum : sum;
}

void mvfxGasConfine(const mvfxCompiledGas* gas, const mvfxGasLattice* l, float* const velocity[3],
                    float* work)
{
    float epsilon = gas->def.vorticity;
    if (epsilon == 0.0f)
    {
        return;
    }
    Curl(l, velocity, work);
    for (int32_t k = 0; k < 3; ++k)
    {
        if (l->n[k] == 1)
        {
            continue;
        }
        const int32_t m[3] = {l->n[0] + (k == 0 ? 1 : 0), l->n[1] + (k == 1 ? 1 : 0),
                              l->n[2] + (k == 2 ? 1 : 0)};
        int64_t f = 0;
        for (int32_t z = 0; z < m[2]; ++z)
        {
            for (int32_t y = 0; y < m[1]; ++y)
            {
                for (int32_t x = 0; x < m[0]; ++x, ++f)
                {
                    const int32_t at[3] = {x, y, z};
                    velocity[k][f] += l->h * ConfineFace(l, work, epsilon, k, at);
                }
            }
        }
    }
}

// The place in the box's frame of the face across k at at.
static void FacePlace(const mvfxGasLattice* l, int32_t k, const int32_t at[3], float out[3])
{
    for (int32_t j = 0; j < 3; ++j)
    {
        float place = (float)at[j] + (j == k ? 0.0f : 0.5f);
        out[j] = (place - 0.5f * (float)l->n[j]) * l->cell;
    }
}

// A face's velocity across k after the forces at its place p over a
// substep of h: v e^(-dh) + a / d (1 - e^(-dh)) under a drag d, else
// v + a h.
static float Pushed(const mvfxResolvedForce* forces, int32_t count, const float p[3], double time,
                    int32_t k, float h, float v)
{
    float a[3] = {0.0f, 0.0f, 0.0f};
    float drag = 0.0f;
    mvfxForcesAt(forces, count, p, time, a, &drag);
    if (!(drag > 0.0f))
    {
        return v + a[k] * h;
    }
    float keep = mvfxExp(-drag * h);
    return v * keep + a[k] / drag * (1.0f - keep);
}

void mvfxGasPush(const mvfxGasLattice* l, const mvfxResolvedForce* forces, int32_t count,
                 double time, float* const velocity[3])
{
    for (int32_t k = 0; k < 3 && count > 0; ++k)
    {
        const int32_t m[3] = {l->n[0] + (k == 0 ? 1 : 0), l->n[1] + (k == 1 ? 1 : 0),
                              l->n[2] + (k == 2 ? 1 : 0)};
        int64_t f = 0;
        for (int32_t z = 0; l->n[k] > 1 && z < m[2]; ++z)
        {
            for (int32_t y = 0; y < m[1]; ++y)
            {
                for (int32_t x = 0; x < m[0]; ++x, ++f)
                {
                    const int32_t at[3] = {x, y, z};
                    float p[3];
                    FacePlace(l, k, at, p);
                    velocity[k][f] = Pushed(forces, count, p, time, k, l->h, velocity[k][f]);
                }
            }
        }
    }
}

void mvfxGasBurn(const mvfxCompiledGas* gas, const mvfxGasLattice* l, float* const scalars[3],
                 float* expansion)
{
    const mvfxGasDef* def = &gas->def;
    if (def->burnRate == 0.0f)
    {
        return;
    }
    float share = 1.0f - mvfxExp(-def->burnRate * l->h);
    // A unit burned grows its cell by burnExpansion over the substep: a
    // divergence of burnExpansion / h, times the cell's edge in the
    // projection's units.
    float grow = def->burnExpansion * l->cell / l->h;
    for (int64_t i = 0; i < gas->cells; ++i)
    {
        float fuel = scalars[2][i];
        float burned =
            scalars[1][i] >= def->ignitionTemperature && fuel > 0.0f ? fuel * share : 0.0f;
        scalars[2][i] = fuel - burned;
        scalars[1][i] += def->burnHeat * burned;
        scalars[0][i] += def->burnSmoke * burned;
        if (expansion != nullptr)
        {
            expansion[i] = grow * burned;
        }
    }
}
