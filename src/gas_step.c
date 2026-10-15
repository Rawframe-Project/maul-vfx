// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "gas_step.h"

#include "elementary.h"

#include "maul-vfx/effect.h"

#include <math.h>

// An array's points along each axis: the cells', or the faces' across
// axis (0 to 2) for -1 the cells.
static void PointsOf(const mvfxGasLattice* l, int32_t axis, int32_t out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = l->n[k] + (k == axis ? 1 : 0);
    }
}

float mvfxGasSample(const float* values, const mvfxGasLattice* l, int32_t axis, const float p[3])
{
    int32_t m[3];
    PointsOf(l, axis, m);
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
        sum += weight * values[((int64_t)at[2] * m[1] + at[1]) * m[0] + at[0]];
    }
    return sum;
}

// The velocity at place p, in cells a second.
static void VelocityAt(float* const velocity[3], const mvfxGasLattice* l, const float p[3],
                       float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = l->n[k] > 1 || k < 2 ? mvfxGasSample(velocity[k], l, k, p) / l->cell : 0.0f;
    }
}

void mvfxGasTraceBack(float* const velocity[3], const mvfxGasLattice* l, const float p[3],
                      float out[3])
{
    float v[3];
    VelocityAt(velocity, l, p, v);
    float mid[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        mid[k] = p[k] - 0.5f * l->h * v[k];
    }
    VelocityAt(velocity, l, mid, v);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = p[k] - l->h * v[k];
    }
}

// Carries an array (the cells', or the faces' across axis) from one
// buffer to the other by the velocity, each value times keep.
static void Advect(float* const velocity[3], const mvfxGasLattice* l, int32_t axis,
                   const float* from, float* to, float keep)
{
    int32_t m[3];
    PointsOf(l, axis, m);
    int64_t i = 0;
    for (int32_t z = 0; z < m[2]; ++z)
    {
        for (int32_t y = 0; y < m[1]; ++y)
        {
            for (int32_t x = 0; x < m[0]; ++x)
            {
                const int32_t at[3] = {x, y, z};
                float p[3];
                for (int32_t k = 0; k < 3; ++k)
                {
                    p[k] = (float)at[k] + (k == axis ? 0.0f : 0.5f);
                }
                float back[3];
                mvfxGasTraceBack(velocity, l, p, back);
                to[i++] = keep * mvfxGasSample(from, l, axis, back);
            }
        }
    }
}

// Whether a source holds a place in the box's frame.
static bool Holds(const mvfxGasSource* source, const float local[3])
{
    float d[3];
    float square = 0.0f;
    bool inside = true;
    for (int32_t k = 0; k < 3; ++k)
    {
        d[k] = local[k] - source->center[k];
        square += d[k] * d[k];
        inside = inside && fabsf(d[k]) <= source->halfExtents[k];
    }
    return source->shape == mvfx_gasSphere ? square <= source->radius * source->radius : inside;
}

// A point's place in the box's frame, a cell's center or (across axis) a
// face's.
static void LocalOf(const mvfxGasLattice* l, int32_t axis, const int32_t at[3], float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        float place = (float)at[k] + (k == axis ? 0.0f : 0.5f);
        out[k] = (place - 0.5f * (float)l->n[k]) * l->cell;
    }
}

// A source's rates over a substep into the cells it holds.
static void PourCells(const mvfxGasSource* source, const mvfxGasLattice* l, float* const scalars[3])
{
    const float rates[3] = {source->density * l->h, source->temperature * l->h,
                            source->fuel * l->h};
    int64_t i = 0;
    for (int32_t z = 0; z < l->n[2]; ++z)
    {
        for (int32_t y = 0; y < l->n[1]; ++y)
        {
            for (int32_t x = 0; x < l->n[0]; ++x, ++i)
            {
                const int32_t at[3] = {x, y, z};
                float local[3];
                LocalOf(l, -1, at, local);
                for (int32_t f = 0; f < 3 && Holds(source, local); ++f)
                {
                    scalars[f][i] += rates[f];
                }
            }
        }
    }
}

// A source's velocity onto the faces across axis it holds.
static void PourFaces(const mvfxGasSource* source, const mvfxGasLattice* l, int32_t axis,
                      float* faces)
{
    int32_t m[3];
    PointsOf(l, axis, m);
    int64_t i = 0;
    for (int32_t z = 0; z < m[2]; ++z)
    {
        for (int32_t y = 0; y < m[1]; ++y)
        {
            for (int32_t x = 0; x < m[0]; ++x, ++i)
            {
                const int32_t at[3] = {x, y, z};
                float local[3];
                LocalOf(l, axis, at, local);
                faces[i] = Holds(source, local) ? source->velocity[axis] : faces[i];
            }
        }
    }
}

// One substep: the sources (while the instance emits), then the velocity
// and the scalars carried by the velocity as it was, each decaying.
static void Substep(const mvfxCompiledGas* gas, mvfxGasState* state, const mvfxGasLattice* l,
                    bool pours)
{
    int32_t now = state->current;
    float* velocity[3] = {state->velocity[0][now], state->velocity[1][now],
                          state->velocity[2][now]};
    float* scalars[3] = {state->scalars[0][now], state->scalars[1][now], state->scalars[2][now]};
    int32_t axes = l->n[2] > 1 ? 3 : 2;
    for (int32_t s = 0; pours && s < gas->def.sourceCount; ++s)
    {
        PourCells(&gas->sources[s], l, scalars);
        for (int32_t k = 0; k < axes && gas->sources[s].setsVelocity; ++k)
        {
            PourFaces(&gas->sources[s], l, k, velocity[k]);
        }
    }
    const float decays[3] = {gas->def.densityDecay, gas->def.temperatureDecay, gas->def.fuelDecay};
    float keep = mvfxExp(-gas->def.velocityDecay * l->h);
    for (int32_t k = 0; k < axes; ++k)
    {
        Advect(velocity, l, k, velocity[k], state->velocity[k][1 - now], keep);
    }
    for (int32_t f = 0; f < 3; ++f)
    {
        Advect(velocity, l, -1, scalars[f], state->scalars[f][1 - now], mvfxExp(-decays[f] * l->h));
    }
    state->current = 1 - now;
}

// Whether any scalar holds more than nothing.
static bool HoldsAny(const mvfxCompiledGas* gas, const mvfxGasState* state)
{
    for (int32_t f = 0; f < 3; ++f)
    {
        const float* values = state->scalars[f][state->current];
        for (int64_t i = 0; i < gas->cells; ++i)
        {
            if (fabsf(values[i]) > MVFX_GAS_EMPTY)
            {
                return true;
            }
        }
    }
    return false;
}

void mvfxStepGas(const mvfxCompiledGas* gas, mvfxGasState* state, int64_t from, int64_t to,
                 int64_t stopTick)
{
    int32_t substeps = gas->def.substeps;
    mvfxGasLattice l = {.cell = gas->def.cellSize};
    for (int32_t k = 0; k < 3; ++k)
    {
        l.n[k] = gas->def.size[k];
    }
    for (int32_t s = 0; s < substeps; ++s)
    {
        int64_t start = from + (to - from) * s / substeps;
        int64_t end = from + (to - from) * (s + 1) / substeps;
        if (end <= start)
        {
            continue;
        }
        l.h = (float)((double)(end - start) / (double)MVFX_TICKS_PER_SECOND);
        Substep(gas, state, &l, start < stopTick);
    }
    state->holds = HoldsAny(gas, state);
}
