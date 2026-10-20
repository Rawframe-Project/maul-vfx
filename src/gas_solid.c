// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "gas_solid.h"

#include "direction.h"
#include "gas_pressure.h"

// A point's place in the box's frame: a cell's center, or (across axis
// 0 to 2) a face's.
static void LocalOf(const int32_t n[3], float cell, int32_t axis, const int32_t at[3], float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        float place = (float)at[k] + (k == axis ? 0.0f : 0.5f);
        out[k] = (place - 0.5f * (float)n[k]) * cell;
    }
}

void mvfxMarkGasSolids(const mvfxCompiledGas* gas, mvfxGasState* state)
{
    const int32_t* n = gas->def.size;
    const mvfxResolvedCollider* obstacles = state->obstacles[state->obstaclesNow];
    int32_t count = state->obstacleCount[state->obstaclesNow];
    int64_t i = 0;
    for (int32_t z = 0; z < n[2]; ++z)
    {
        for (int32_t y = 0; y < n[1]; ++y)
        {
            for (int32_t x = 0; x < n[0]; ++x, ++i)
            {
                const int32_t at[3] = {x, y, z};
                float local[3];
                LocalOf(n, gas->def.cellSize, -1, at, local);
                uint8_t solid = 0;
                for (int32_t c = 0; c < count && solid == 0; ++c)
                {
                    solid =
                        mvfxColliderGap(&obstacles[c], local, false) < 0.0f ? (uint8_t)(c + 1) : 0;
                }
                state->solid[i] = solid;
            }
        }
    }
    mvfxGasAreas(gas, state);
}

void mvfxEmptyGasSolids(const mvfxCompiledGas* gas, const mvfxGasState* state,
                        float* const scalars[3])
{
    for (int64_t i = 0; i < gas->cells; ++i)
    {
        for (int32_t f = 0; f < 3 && state->solid[i] != 0; ++f)
        {
            scalars[f][i] = 0.0f;
        }
    }
}

// Whether two resolved obstacles are one moved: the same kind, sizes and
// grid (a grid's scale goes with its half sizes).
static bool IsMoved(const mvfxResolvedCollider* a, const mvfxResolvedCollider* b)
{
    bool same = a->kind == b->kind && a->inside == b->inside && a->radius == b->radius &&
                a->grid == b->grid;
    for (int32_t k = 0; k < 3 && same; ++k)
    {
        same = a->halfExtents[k] == b->halfExtents[k];
    }
    return same;
}

// Where an obstacle's motion from before to now carried the point p that
// now lies at p: a plane moved along its normal, a sphere moved, any other
// moved and turned as its frame was.
static void Before(const mvfxResolvedCollider* now, const mvfxResolvedCollider* before,
                   const float p[3], float out[3])
{
    float d[3] = {p[0] - now->center[0], p[1] - now->center[1], p[2] - now->center[2]};
    if (now->kind == mvfx_colliderPlane)
    {
        float along = 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            along += (now->center[k] - before->center[k]) * now->normal[k];
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            out[k] = p[k] - along * now->normal[k];
        }
        return;
    }
    if (now->kind == mvfx_colliderSphere)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            out[k] = before->center[k] + d[k];
        }
        return;
    }
    const float back[4] = {-now->rotation[0], -now->rotation[1], -now->rotation[2],
                           now->rotation[3]};
    float local[3];
    mvfxRotate(back, d, local);
    float turned[3];
    mvfxRotate(before->rotation, local, turned);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = before->center[k] + turned[k];
    }
}

bool mvfxGasObstacleMoved(const mvfxGasState* state, int32_t c)
{
    int32_t was = 1 - state->obstaclesNow;
    return state->obstaclesKnown && c < state->obstacleCount[was] &&
           IsMoved(&state->obstacles[state->obstaclesNow][c], &state->obstacles[was][c]);
}

// An obstacle's velocity across axis k at p over a step of seconds.
static float SolidVelocity(const mvfxGasState* state, int32_t c, int32_t k, const float p[3],
                           double seconds)
{
    if (!(seconds > 0.0) || !mvfxGasObstacleMoved(state, c))
    {
        return 0.0f;
    }
    int32_t was = 1 - state->obstaclesNow;
    const mvfxResolvedCollider* now = &state->obstacles[state->obstaclesNow][c];
    const mvfxResolvedCollider* before = &state->obstacles[was][c];
    float from[3];
    Before(now, before, p, from);
    return (float)((double)(p[k] - from[k]) / seconds);
}

// The solid beside the face across k at at (the one below it, else the
// one above), 0 for none.
static uint8_t SolidBeside(const mvfxGasState* state, const int32_t n[3], int32_t k,
                           const int32_t at[3])
{
    const int64_t stride[3] = {1, n[0], (int64_t)n[0] * n[1]};
    int64_t cell = at[2] * stride[2] + at[1] * stride[1] + at[0];
    uint8_t below = at[k] > 0 ? state->solid[cell - stride[k]] : 0;
    uint8_t above = at[k] < n[k] ? state->solid[cell] : 0;
    return below != 0 ? below : above;
}

void mvfxMoveGasSolids(const mvfxCompiledGas* gas, const mvfxGasState* state, double seconds,
                       float* const velocity[3])
{
    const int32_t* n = gas->def.size;
    for (int32_t k = 0; k < 3; ++k)
    {
        const int32_t m[3] = {n[0] + (k == 0 ? 1 : 0), n[1] + (k == 1 ? 1 : 0),
                              n[2] + (k == 2 ? 1 : 0)};
        int64_t f = 0;
        for (int32_t z = 0; n[k] > 1 && z < m[2]; ++z)
        {
            for (int32_t y = 0; y < m[1]; ++y)
            {
                for (int32_t x = 0; x < m[0]; ++x, ++f)
                {
                    const int32_t at[3] = {x, y, z};
                    uint8_t solid = SolidBeside(state, n, k, at);
                    float p[3];
                    LocalOf(n, gas->def.cellSize, k, at, p);
                    velocity[k][f] = solid != 0 ? SolidVelocity(state, solid - 1, k, p, seconds)
                                                : velocity[k][f];
                }
            }
        }
    }
}
