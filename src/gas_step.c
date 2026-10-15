// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "gas_step.h"

#include "elementary.h"
#include "gas_forces.h"
#include "gas_pressure.h"
#include "gas_solid.h"

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

// An array's trilinear sample at place p (see mvfxGasSample), and the
// least and the most of the points it weighed.
static float SampleRange(const float* values, const mvfxGasLattice* l, int32_t axis,
                         const float p[3], float range[2])
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
    return SampleRange(values, l, axis, p, range);
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

// A point's place in cells: a cell's center, or (across axis) a face's.
static void PlaceOf(int32_t axis, const int32_t at[3], float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = (float)at[k] + (k == axis ? 0.0f : 0.5f);
    }
}

// Carries an array (the cells', or the faces' across axis) from one
// buffer to the other by the velocity, each value times keep:
// semi-Lagrangian, each point traced over the substep (back, or with
// ahead forward) and sampled there.
static void Advect(float* const velocity[3], const mvfxGasLattice* l, int32_t axis,
                   const float* from, float* to, float keep, bool ahead)
{
    mvfxGasLattice turned = *l;
    turned.h = ahead ? -l->h : l->h;
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
                PlaceOf(axis, at, p);
                float back[3];
                mvfxGasTraceBack(velocity, &turned, p, back);
                to[i++] = keep * mvfxGasSample(from, l, axis, back);
            }
        }
    }
}

// MacCormack's correction of an array carried semi-Lagrangian into to,
// its trace forward from there in work: half the error taken back, the
// plain value kept where the result leaves the points sampled from; each
// value times keep.
static void Correct(float* const velocity[3], const mvfxGasLattice* l, int32_t axis,
                    const float* from, float* to, const float* work, float keep)
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
                float p[3];
                PlaceOf(axis, at, p);
                float back[3];
                mvfxGasTraceBack(velocity, l, p, back);
                float range[2];
                float plain = SampleRange(from, l, axis, back, range);
                float sharp = plain + 0.5f * (from[i] - work[i]);
                to[i] = keep * (sharp >= range[0] && sharp <= range[1] ? sharp : plain);
            }
        }
    }
}

// Carries an array by the gas's advection: semi-Lagrangian, or that
// traced forward again into work and corrected.
static void Carry(const mvfxCompiledGas* gas, float* const velocity[3], const mvfxGasLattice* l,
                  int32_t axis, const float* from, float* to, float* work, float keep)
{
    if (gas->def.advection != mvfx_gasMacCormack)
    {
        Advect(velocity, l, axis, from, to, keep, false);
        return;
    }
    Advect(velocity, l, axis, from, to, 1.0f, false);
    Advect(velocity, l, axis, to, work, 1.0f, true);
    Correct(velocity, l, axis, from, to, work, keep);
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

// A substep's sources (while the instance emits) into the current
// buffers.
static void Pour(const mvfxCompiledGas* gas, const mvfxGasLattice* l, float* const velocity[3],
                 float* const scalars[3])
{
    int32_t axes = l->n[2] > 1 ? 3 : 2;
    for (int32_t s = 0; s < gas->def.sourceCount; ++s)
    {
        PourCells(&gas->sources[s], l, scalars);
        for (int32_t k = 0; k < axes && gas->sources[s].setsVelocity; ++k)
        {
            PourFaces(&gas->sources[s], l, k, velocity[k]);
        }
    }
}

// One substep: the sources (while the instance emits), the forces, then
// the velocity and the scalars carried by the velocity as it was, each
// decaying; the fuel burning; the projection.
static void Substep(const mvfxCompiledGas* gas, mvfxGasState* state, const mvfxGasLattice* l,
                    const float gravity[3], double seconds, bool pours)
{
    int32_t now = state->current;
    float* velocity[3] = {state->velocity[0][now], state->velocity[1][now],
                          state->velocity[2][now]};
    float* scalars[3] = {state->scalars[0][now], state->scalars[1][now], state->scalars[2][now]};
    if (pours)
    {
        Pour(gas, l, velocity, scalars);
    }
    mvfxGasBuoyancy(gas, l, gravity, velocity, scalars);
    mvfxGasConfine(gas, l, velocity, state->work);
    const float decays[3] = {gas->def.densityDecay, gas->def.temperatureDecay, gas->def.fuelDecay};
    float keep = mvfxExp(-gas->def.velocityDecay * l->h);
    int32_t axes = l->n[2] > 1 ? 3 : 2;
    for (int32_t k = 0; k < axes; ++k)
    {
        Carry(gas, velocity, l, k, velocity[k], state->velocity[k][1 - now], state->work, keep);
    }
    float* carried[3];
    for (int32_t f = 0; f < 3; ++f)
    {
        carried[f] = state->scalars[f][1 - now];
        Carry(gas, velocity, l, -1, scalars[f], carried[f], state->work,
              mvfxExp(-decays[f] * l->h));
    }
    state->current = 1 - now;
    if (state->solid != nullptr)
    {
        mvfxEmptyGasSolids(gas, state, carried);
    }
    bool expands = gas->def.burnExpansion > 0.0f && gas->def.burnRate > 0.0f;
    mvfxGasBurn(gas, l, carried, expands ? state->work : nullptr);
    if (state->solid != nullptr)
    {
        float* moved[3] = {state->velocity[0][1 - now], state->velocity[1][1 - now],
                           state->velocity[2][1 - now]};
        mvfxMoveGasSolids(gas, state, seconds, moved);
    }
    mvfxProjectGas(gas, state, expands ? state->work : nullptr);
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

void mvfxStepGas(const mvfxCompiledGas* gas, mvfxGasState* state, const float gravity[3],
                 int64_t from, int64_t to, int64_t stopTick)
{
    int32_t substeps = gas->def.substeps;
    mvfxGasLattice l = {.cell = gas->def.cellSize};
    for (int32_t k = 0; k < 3; ++k)
    {
        l.n[k] = gas->def.size[k];
    }
    double seconds = (double)(to - from) / (double)MVFX_TICKS_PER_SECOND;
    if (state->solid != nullptr)
    {
        mvfxMarkGasSolids(gas, state);
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
        Substep(gas, state, &l, gravity, seconds, start < stopTick);
    }
    state->holds = HoldsAny(gas, state);
}
