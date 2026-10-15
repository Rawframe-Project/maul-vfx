// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "gas_pressure.h"

// The passes of weighted Jacobi before and after each coarser level's
// correction, and on the coarsest level (each an even count: a pass reads
// one buffer and writes the other).
#define SMOOTH_PASSES   2
#define COARSEST_PASSES 64

// A level of the solve: its cells along each axis, the fine cells along
// each axis and how many a level cell spans (2 to the level), and its
// faces' open areas across each axis. Each level is a finite-volume form
// of the same box: a cell spans its fine cells within the box (the last
// along an axis may span fewer), and two cells meet across a face of its
// open area over the distance between their centers.
typedef struct Level
{
    const int32_t* n;
    const int32_t* fine;
    int32_t span;
    float* const* areas;
} Level;

// The fine cells a level cell spans along axis k.
static float Width(const Level* l, int32_t k, int32_t i)
{
    if (l->fine[k] == 1)
    {
        return 1.0f;
    }
    int32_t end = (i + 1) * l->span;
    end = end < l->fine[k] ? end : l->fine[k];
    return (float)(end - i * l->span);
}

// The distance between the centers across face i along k (0 to n[k]):
// half of each cell's width beside it, a wall face's one cell's.
static float Distance(const Level* l, int32_t k, int32_t i)
{
    float below = i > 0 ? Width(l, k, i - 1) : 0.0f;
    float above = i < l->n[k] ? Width(l, k, i) : 0.0f;
    return 0.5f * (below + above);
}

// The index of the face across k before cell at, among a level of n
// cells' faces across k.
static int64_t FaceAt(const int32_t n[3], int32_t k, const int32_t at[3])
{
    const int32_t m[3] = {n[0] + (k == 0 ? 1 : 0), n[1] + (k == 1 ? 1 : 0),
                          n[2] + (k == 2 ? 1 : 0)};
    return ((int64_t)at[2] * m[1] + at[1]) * m[0] + at[0];
}

// The pressure's neighbors about cell at, weighted by their faces'
// coefficients (a face's open area over the distance between the
// centers), and the coefficients' sum: across a wall's open face a
// pressure of 0 on the wall, half the cell's width away.
static float Neighbors(const Level* l, const float* p, const int32_t at[3], float* weight)
{
    const int64_t stride[3] = {1, l->n[0], (int64_t)l->n[0] * l->n[1]};
    int64_t i = at[2] * stride[2] + at[1] * stride[1] + at[0];
    float sum = 0.0f;
    *weight = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        if (l->n[k] == 1)
        {
            continue;
        }
        // Along k the faces across it lie as the cells do.
        int64_t low = FaceAt(l->n, k, at);
        int64_t step = stride[k];
        float below = l->areas[k][low] / Distance(l, k, at[k]);
        float above = l->areas[k][low + step] / Distance(l, k, at[k] + 1);
        sum += at[k] > 0 ? below * p[i - stride[k]] : 0.0f;
        sum += at[k] + 1 < l->n[k] ? above * p[i + stride[k]] : 0.0f;
        *weight += below + above;
    }
    return sum;
}

// One pass of weighted Jacobi on (weighted sum - weight p) = rhs, from one
// buffer into the other.
static void Jacobi(const Level* l, const float* from, float* to, const float* rhs, float omega)
{
    int64_t i = 0;
    for (int32_t z = 0; z < l->n[2]; ++z)
    {
        for (int32_t y = 0; y < l->n[1]; ++y)
        {
            for (int32_t x = 0; x < l->n[0]; ++x, ++i)
            {
                const int32_t at[3] = {x, y, z};
                float weight = 0.0f;
                float sum = Neighbors(l, from, at, &weight);
                float solved = weight > 0.0f ? (sum - rhs[i]) / weight : 0.0f;
                to[i] = (1.0f - omega) * from[i] + omega * solved;
            }
        }
    }
}

// passes passes of Jacobi on a level, its pressure left in its buffer.
static void Smooth(const Level* l, mvfxGasLevel* v, int32_t passes, float omega)
{
    for (int32_t s = 0; s < passes; s += 2)
    {
        Jacobi(l, v->pressure, v->scratch, v->rhs, omega);
        Jacobi(l, v->scratch, v->pressure, v->rhs, omega);
    }
}

// The residual rhs - (weighted sum - weight p), into the scratch buffer.
static void Residual(const Level* l, mvfxGasLevel* v)
{
    int64_t i = 0;
    for (int32_t z = 0; z < l->n[2]; ++z)
    {
        for (int32_t y = 0; y < l->n[1]; ++y)
        {
            for (int32_t x = 0; x < l->n[0]; ++x, ++i)
            {
                const int32_t at[3] = {x, y, z};
                float weight = 0.0f;
                float sum = Neighbors(l, v->pressure, at, &weight);
                v->scratch[i] = v->rhs[i] - (sum - weight * v->pressure[i]);
            }
        }
    }
}

// The coarse level's right-hand side: the sum of the fine residual over
// each coarse cell's children (a cell's flux balance is its children's),
// and its pressure 0.
static void Restrict(const Level* fine, const float* residual, const Level* coarse, mvfxGasLevel* v)
{
    int64_t cells = (int64_t)coarse->n[0] * coarse->n[1] * coarse->n[2];
    for (int64_t c = 0; c < cells; ++c)
    {
        v->rhs[c] = 0.0f;
        v->pressure[c] = 0.0f;
    }
    int64_t i = 0;
    for (int32_t z = 0; z < fine->n[2]; ++z)
    {
        for (int32_t y = 0; y < fine->n[1]; ++y)
        {
            for (int32_t x = 0; x < fine->n[0]; ++x, ++i)
            {
                int64_t c = ((int64_t)(z / 2) * coarse->n[1] + y / 2) * coarse->n[0] + x / 2;
                v->rhs[c] += residual[i];
            }
        }
    }
}

// Along one axis, a fine cell's parent and the parent beside it toward
// the cell, each weighted (3/4 and 1/4: linear between the coarse
// centers), held within the coarse cells.
static void Parents(int32_t i, int32_t coarse, int32_t fine, int32_t at[2], float weight[2])
{
    at[0] = i / 2;
    int32_t beside = (i % 2 == 1) ? at[0] + 1 : at[0] - 1;
    at[1] = beside < 0 ? 0 : (beside >= coarse ? coarse - 1 : beside);
    weight[0] = fine > 1 ? 0.75f : 1.0f;
    weight[1] = fine > 1 ? 0.25f : 0.0f;
}

// Adds the coarse level's pressure, interpolated, to the fine level's.
static void Prolong(const Level* coarse, const float* e, const Level* fine, float* p)
{
    int64_t i = 0;
    for (int32_t z = 0; z < fine->n[2]; ++z)
    {
        for (int32_t y = 0; y < fine->n[1]; ++y)
        {
            for (int32_t x = 0; x < fine->n[0]; ++x, ++i)
            {
                int32_t ax[2];
                int32_t ay[2];
                int32_t az[2];
                float wx[2];
                float wy[2];
                float wz[2];
                Parents(x, coarse->n[0], fine->n[0], ax, wx);
                Parents(y, coarse->n[1], fine->n[1], ay, wy);
                Parents(z, coarse->n[2], fine->n[2], az, wz);
                float sum = 0.0f;
                for (int32_t corner = 0; corner < 8; ++corner)
                {
                    int32_t a = corner & 1;
                    int32_t b = corner >> 1 & 1;
                    int32_t c = corner >> 2;
                    int64_t at = ((int64_t)az[c] * coarse->n[1] + ay[b]) * coarse->n[0] + ax[a];
                    sum += wx[a] * wy[b] * wz[c] * e[at];
                }
                p[i] += sum;
            }
        }
    }
}

// A V cycle from level l down.
static void Cycle(const mvfxCompiledGas* gas, mvfxGasState* state, int32_t l, float omega)
{
    const Level here = {gas->levels[l], gas->def.size, 1 << l, state->areas[l]};
    mvfxGasLevel* v = &state->levels[l];
    if (l + 1 == gas->levelCount)
    {
        Smooth(&here, v, COARSEST_PASSES, omega);
        return;
    }
    Smooth(&here, v, SMOOTH_PASSES, omega);
    Residual(&here, v);
    const Level below = {gas->levels[l + 1], gas->def.size, 2 << l, state->areas[l + 1]};
    Restrict(&here, v->scratch, &below, &state->levels[l + 1]);
    Cycle(gas, state, l + 1, omega);
    Prolong(&below, state->levels[l + 1].pressure, &here, v->pressure);
    Smooth(&here, v, SMOOTH_PASSES, omega);
}

void mvfxCloseGasWalls(const mvfxCompiledGas* gas, float* const velocity[3])
{
    const int32_t* n = gas->def.size;
    for (int32_t k = 0; k < 3; ++k)
    {
        bool walls = n[k] == 1 || (gas->def.closedWalls & (3u << (2 * k))) != 0;
        int32_t a = (k + 1) % 3;
        int32_t b = (k + 2) % 3;
        for (int32_t j = 0; walls && j < n[b]; ++j)
        {
            for (int32_t i = 0; i < n[a]; ++i)
            {
                int32_t at[3];
                at[a] = i;
                at[b] = j;
                for (int32_t side = 0; side < 2; ++side)
                {
                    at[k] = side == 0 ? 0 : n[k];
                    bool closed =
                        n[k] == 1 || (gas->def.closedWalls & ((1u + side) << (2 * k))) != 0;
                    velocity[k][FaceAt(n, k, at)] = closed ? 0.0f : velocity[k][FaceAt(n, k, at)];
                }
            }
        }
    }
}

// Whether a cell has a face that lets the gas through (a solid's have
// none).
static bool IsOpen(const mvfxGasState* state, const int32_t n[3], const int32_t at[3])
{
    const int64_t stride[3] = {1, n[0], (int64_t)n[0] * n[1]};
    for (int32_t k = 0; k < 3; ++k)
    {
        int64_t low = n[k] > 1 ? FaceAt(n, k, at) : 0;
        if (n[k] > 1 &&
            (state->areas[0][k][low] > 0.0f || state->areas[0][k][low + stride[k]] > 0.0f))
        {
            return true;
        }
    }
    return false;
}

// The divergence of each open cell, its faces' outflow, into the first
// level's right-hand side; 0 for a cell nothing passes into.
static void Divergence(const mvfxCompiledGas* gas, const mvfxGasState* state,
                       float* const velocity[3], float* rhs)
{
    const int32_t* n = gas->def.size;
    int64_t i = 0;
    for (int32_t z = 0; z < n[2]; ++z)
    {
        for (int32_t y = 0; y < n[1]; ++y)
        {
            for (int32_t x = 0; x < n[0]; ++x, ++i)
            {
                int32_t at[3] = {x, y, z};
                float out = 0.0f;
                for (int32_t k = 0; k < 3; ++k)
                {
                    int64_t low = FaceAt(n, k, at);
                    at[k] += 1;
                    out += n[k] > 1 ? velocity[k][FaceAt(n, k, at)] - velocity[k][low] : 0.0f;
                    at[k] -= 1;
                }
                rhs[i] = IsOpen(state, n, at) ? out : 0.0f;
            }
        }
    }
}

// A face's velocity less the pressure's rise across it over the distance
// between the centers, the face f across axis k before the cell at index
// cell (at its place along k): across an open wall the pressure is 0 on
// the wall, half a cell away; a closed face (a closed wall's, a solid's)
// keeps its velocity.
static float Corrected(const mvfxCompiledGas* gas, const mvfxGasState* state, int32_t k, int64_t f,
                       int32_t at, int64_t cell, int64_t stride, float velocity)
{
    if (state->areas[0][k][f] == 0.0f)
    {
        return velocity;
    }
    bool low = at == 0;
    bool high = at == gas->def.size[k];
    float before = low ? 0.0f : state->levels[0].pressure[cell - stride];
    float after = high ? 0.0f : state->levels[0].pressure[cell];
    return velocity - (low || high ? 2.0f : 1.0f) * (after - before);
}

// Each face's velocity corrected by the pressure.
static void Correct(const mvfxCompiledGas* gas, const mvfxGasState* state, float* const velocity[3])
{
    const int32_t* n = gas->def.size;
    const int64_t stride[3] = {1, n[0], (int64_t)n[0] * n[1]};
    for (int32_t k = 0; k < 3; ++k)
    {
        if (n[k] == 1)
        {
            continue;
        }
        const int32_t m[3] = {n[0] + (k == 0 ? 1 : 0), n[1] + (k == 1 ? 1 : 0),
                              n[2] + (k == 2 ? 1 : 0)};
        int64_t f = 0;
        for (int32_t z = 0; z < m[2]; ++z)
        {
            for (int32_t y = 0; y < m[1]; ++y)
            {
                for (int32_t x = 0; x < m[0]; ++x, ++f)
                {
                    const int32_t at[3] = {x, y, z};
                    int64_t cell = z * stride[2] + y * stride[1] + x;
                    velocity[k][f] =
                        Corrected(gas, state, k, f, at[k], cell, stride[k], velocity[k][f]);
                }
            }
        }
    }
}

// Whether every wall a gas meets is closed.
static bool IsSealed(const mvfxCompiledGas* gas)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        if (gas->def.size[k] > 1 && (gas->def.closedWalls & (3u << (2 * k))) != (3u << (2 * k)))
        {
            return false;
        }
    }
    return true;
}

// The divergence the expansion asks for taken from each open cell's, so
// the solve leaves it there; in a sealed box, less its mean over the open
// cells (it has nowhere to go), summed in the cells' order.
static void Expand(const mvfxCompiledGas* gas, const mvfxGasState* state, const float* expansion,
                   float* rhs)
{
    const int32_t* n = gas->def.size;
    bool sealed = IsSealed(gas);
    double sum = 0.0;
    int64_t open = 0;
    for (int64_t i = 0; sealed && i < gas->cells; ++i)
    {
        const int32_t at[3] = {(int32_t)(i % n[0]), (int32_t)(i / n[0] % n[1]),
                               (int32_t)(i / ((int64_t)n[0] * n[1]))};
        bool counts = IsOpen(state, n, at);
        sum += counts ? (double)expansion[i] : 0.0;
        open += counts ? 1 : 0;
    }
    float mean = open > 0 ? (float)(sum / (double)open) : 0.0f;
    for (int64_t i = 0; i < gas->cells; ++i)
    {
        const int32_t at[3] = {(int32_t)(i % n[0]), (int32_t)(i / n[0] % n[1]),
                               (int32_t)(i / ((int64_t)n[0] * n[1]))};
        rhs[i] -= IsOpen(state, n, at) ? expansion[i] - mean : 0.0f;
    }
}

void mvfxProjectGas(const mvfxCompiledGas* gas, mvfxGasState* state, const float* expansion)
{
    float* velocity[3] = {state->velocity[0][state->current], state->velocity[1][state->current],
                          state->velocity[2][state->current]};
    mvfxCloseGasWalls(gas, velocity);
    if (gas->def.pressureCycles == 0)
    {
        return;
    }
    // Jacobi's best smoothing weights for the 5 and 7-point Laplacians
    // (Briggs, Henson and McCormick 2000).
    float omega = gas->def.size[2] > 1 ? 6.0f / 7.0f : 4.0f / 5.0f;
    Divergence(gas, state, velocity, state->levels[0].rhs);
    if (expansion != nullptr)
    {
        Expand(gas, state, expansion, state->levels[0].rhs);
    }
    for (int32_t c = 0; c < gas->def.pressureCycles; ++c)
    {
        Cycle(gas, state, 0, omega);
    }
    Correct(gas, state, velocity);
}

// A finest face's open area across k at at (at[k] from 0 to n[k]): 0 on a
// closed wall or beside a solid cell, else 1.
static float FineArea(const mvfxCompiledGas* gas, const uint8_t* solid, int32_t k,
                      const int32_t at[3])
{
    const int32_t* n = gas->def.size;
    const int64_t stride[3] = {1, n[0], (int64_t)n[0] * n[1]};
    bool low = at[k] == 0;
    bool high = at[k] == n[k];
    bool closed = (low && (gas->def.closedWalls & (1u << (2 * k))) != 0) ||
                  (high && (gas->def.closedWalls & (2u << (2 * k))) != 0);
    int64_t cell = at[2] * stride[2] + at[1] * stride[1] + at[0];
    bool below = !low && solid != nullptr && solid[cell - stride[k]] != 0;
    bool above = !high && solid != nullptr && solid[cell] != 0;
    return closed || below || above ? 0.0f : 1.0f;
}

// A coarser face's open area across k at at, on level l: the sum of the
// finer faces' on its plane within the box.
static float CoarseArea(const mvfxCompiledGas* gas, const mvfxGasState* state, int32_t l, int32_t k,
                        const int32_t at[3])
{
    const int32_t* fine = gas->levels[l - 1];
    int32_t a = (k + 1) % 3;
    int32_t b = (k + 2) % 3;
    int32_t child[3];
    child[k] = 2 * at[k] < fine[k] ? 2 * at[k] : fine[k];
    float sum = 0.0f;
    for (int32_t j = 0; j < 2; ++j)
    {
        for (int32_t i = 0; i < 2; ++i)
        {
            child[a] = 2 * at[a] + i;
            child[b] = 2 * at[b] + j;
            bool within = child[a] < fine[a] && child[b] < fine[b];
            sum += within ? state->areas[l - 1][k][FaceAt(fine, k, child)] : 0.0f;
        }
    }
    return sum;
}

// A level's open areas across k.
static void LevelAreas(const mvfxCompiledGas* gas, mvfxGasState* state, int32_t l, int32_t k)
{
    const int32_t* n = gas->levels[l];
    const int32_t m[3] = {n[0] + (k == 0 ? 1 : 0), n[1] + (k == 1 ? 1 : 0),
                          n[2] + (k == 2 ? 1 : 0)};
    float* areas = state->areas[l][k];
    int64_t f = 0;
    for (int32_t z = 0; z < m[2]; ++z)
    {
        for (int32_t y = 0; y < m[1]; ++y)
        {
            for (int32_t x = 0; x < m[0]; ++x, ++f)
            {
                const int32_t at[3] = {x, y, z};
                areas[f] =
                    l == 0 ? FineArea(gas, state->solid, k, at) : CoarseArea(gas, state, l, k, at);
            }
        }
    }
}

void mvfxGasAreas(const mvfxCompiledGas* gas, mvfxGasState* state)
{
    for (int32_t l = 0; l < gas->levelCount; ++l)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            if (gas->levels[l][k] > 1)
            {
                LevelAreas(gas, state, l, k);
            }
        }
    }
}
