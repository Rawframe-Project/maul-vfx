// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "gas_step.h"

#include "elementary.h"
#include "gas_forces.h"
#include "gas_lattice.h"
#include "gas_pressure.h"
#include "gas_solid.h"

#include "maul-vfx/effect.h"

#include <math.h>
#include <string.h>

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

// The least rows a task's range takes: a small gas is one range.
#define ROW_RANGE 8

// A carrying pass over rows of points, split over tasks: what each range
// reads and writes.
typedef struct CarryPass
{
    float* const* velocity;
    const mvfxGasLattice* l;
    // The lattice traced along, its step turned for a trace ahead.
    mvfxGasLattice traced;
    int32_t axis;
    int32_t m[3];
    const float* from;
    float* to;
    const float* work;
    float keep;
} CarryPass;

static CarryPass PassOf(float* const velocity[3], const mvfxGasLattice* l, int32_t axis)
{
    CarryPass pass = {.velocity = velocity, .l = l, .traced = *l, .axis = axis};
    mvfxGasPoints(l, axis, pass.m);
    return pass;
}

static void AdvectRows(int32_t start, int32_t end, void* context)
{
    const CarryPass* pass = context;
    const int32_t* m = pass->m;
    for (int32_t r = start; r < end; ++r)
    {
        int64_t i = (int64_t)r * m[0];
        for (int32_t x = 0; x < m[0]; ++x, ++i)
        {
            const int32_t at[3] = {x, r % m[1], r / m[1]};
            float p[3];
            PlaceOf(pass->axis, at, p);
            float back[3];
            mvfxGasTraceBack(pass->velocity, &pass->traced, p, back);
            pass->to[i] = pass->keep * mvfxGasSample(pass->from, pass->l, pass->axis, back);
        }
    }
}

// Carries an array of points along axis semi-Lagrangian (traced ahead
// instead of back when ahead) from from into to, each value times keep.
static void Advect(float* const velocity[3], const mvfxGasLattice* l, int32_t axis,
                   const float* from, float* to, float keep, bool ahead, const mvfxTasks* tasks)
{
    CarryPass pass = PassOf(velocity, l, axis);
    pass.traced.h = ahead ? -l->h : l->h;
    pass.from = from;
    pass.to = to;
    pass.keep = keep;
    mvfxRunTasks(tasks, AdvectRows, pass.m[1] * pass.m[2], ROW_RANGE, &pass);
}

static void CorrectRows(int32_t start, int32_t end, void* context)
{
    const CarryPass* pass = context;
    const int32_t* m = pass->m;
    for (int32_t r = start; r < end; ++r)
    {
        int64_t i = (int64_t)r * m[0];
        for (int32_t x = 0; x < m[0]; ++x, ++i)
        {
            const int32_t at[3] = {x, r % m[1], r / m[1]};
            float p[3];
            PlaceOf(pass->axis, at, p);
            float back[3];
            mvfxGasTraceBack(pass->velocity, pass->l, p, back);
            float range[2];
            float plain = mvfxGasSampleRange(pass->from, pass->l, pass->axis, back, range);
            float sharp = plain + 0.5f * (pass->from[i] - pass->work[i]);
            pass->to[i] = pass->keep * (sharp >= range[0] && sharp <= range[1] ? sharp : plain);
        }
    }
}

// MacCormack's correction of an array carried semi-Lagrangian into to,
// its trace forward from there in work: half the error taken back, the
// plain value kept where the result leaves the points sampled from; each
// value times keep.
static void Correct(float* const velocity[3], const mvfxGasLattice* l, int32_t axis,
                    const float* from, float* to, const float* work, float keep,
                    const mvfxTasks* tasks)
{
    CarryPass pass = PassOf(velocity, l, axis);
    pass.from = from;
    pass.to = to;
    pass.work = work;
    pass.keep = keep;
    mvfxRunTasks(tasks, CorrectRows, pass.m[1] * pass.m[2], ROW_RANGE, &pass);
}

// Carries an array by the gas's advection: semi-Lagrangian, or that
// traced forward again into work and corrected.
static void Carry(const mvfxCompiledGas* gas, float* const velocity[3], const mvfxGasLattice* l,
                  int32_t axis, const float* from, float* to, float* work, float keep,
                  const mvfxTasks* tasks)
{
    if (gas->def.advection != mvfx_gasMacCormack)
    {
        Advect(velocity, l, axis, from, to, keep, false, tasks);
        return;
    }
    Advect(velocity, l, axis, from, to, 1.0f, false, tasks);
    Advect(velocity, l, axis, to, work, 1.0f, true, tasks);
    Correct(velocity, l, axis, from, to, work, keep, tasks);
}

// Whether an array of count values is all +0: carrying it, by either
// advection, gives +0 everywhere.
static bool IsEmpty(const float* values, int64_t count)
{
    for (int64_t i = 0; i < count; ++i)
    {
        if (values[i] != 0.0f || signbit(values[i]))
        {
            return false;
        }
    }
    return true;
}

// The scalars a cells' pass carries together.
typedef struct CellsPass
{
    CarryPass carry;
    float* const* from;
    float* const* to;
    const float* keep;
    const bool* live;
} CellsPass;

static void AdvectCellRows(int32_t start, int32_t end, void* context)
{
    const CellsPass* pass = context;
    const mvfxGasLattice* l = pass->carry.l;
    for (int32_t r = start; r < end; ++r)
    {
        int64_t i = (int64_t)r * l->n[0];
        for (int32_t x = 0; x < l->n[0]; ++x, ++i)
        {
            const int32_t at[3] = {x, r % l->n[1], r / l->n[1]};
            float p[3];
            PlaceOf(-1, at, p);
            float back[3];
            mvfxGasTraceBack(pass->carry.velocity, l, p, back);
            for (int32_t f = 0; f < 3; ++f)
            {
                pass->to[f][i] = pass->live[f]
                                     ? pass->keep[f] * mvfxGasSample(pass->from[f], l, -1, back)
                                     : 0.0f;
            }
        }
    }
}

// Carries the scalars semi-Lagrangian, those of live, each times its
// keep: one trace a cell for all of them, the others +0.
static void AdvectCells(float* const velocity[3], const mvfxGasLattice* l, float* const from[3],
                        float* const to[3], const float keep[3], const bool live[3],
                        const mvfxTasks* tasks)
{
    CellsPass pass = {
        .carry = PassOf(velocity, l, -1), .from = from, .to = to, .keep = keep, .live = live};
    mvfxRunTasks(tasks, AdvectCellRows, l->n[1] * l->n[2], ROW_RANGE, &pass);
}

// Carries the scalars by the gas's advection: a field holding nothing
// left +0, the rest semi-Lagrangian sharing their traces, or each by
// MacCormack's.
static void CarryCells(const mvfxCompiledGas* gas, float* const velocity[3],
                       const mvfxGasLattice* l, float* const from[3], float* const to[3],
                       float* work, const mvfxTasks* tasks)
{
    const float keep[3] = {mvfxExp(-gas->def.densityDecay * l->h),
                           mvfxExp(-gas->def.temperatureDecay * l->h),
                           mvfxExp(-gas->def.fuelDecay * l->h)};
    bool live[3];
    for (int32_t f = 0; f < 3; ++f)
    {
        live[f] = !IsEmpty(from[f], gas->cells);
    }
    if (gas->def.advection != mvfx_gasMacCormack)
    {
        AdvectCells(velocity, l, from, to, keep, live, tasks);
        return;
    }
    for (int32_t f = 0; f < 3; ++f)
    {
        if (live[f])
        {
            Carry(gas, velocity, l, -1, from[f], to[f], work, keep[f], tasks);
        }
        else
        {
            memset(to[f], 0, (size_t)gas->cells * sizeof(float));
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

// The points of an array (the cells', or the faces' across axis) a
// source can hold, along each axis from first to end: its bounds' reach,
// a point more on each side for rounding, held within the array. Holds
// still decides each.
static void Reach(const mvfxGasSource* source, const mvfxGasLattice* l, int32_t axis,
                  int32_t first[3], int32_t end[3])
{
    int32_t m[3];
    mvfxGasPoints(l, axis, m);
    for (int32_t k = 0; k < 3; ++k)
    {
        float extent = source->shape == mvfx_gasSphere ? source->radius : source->halfExtents[k];
        float offset = 0.5f * (float)l->n[k] - (k == axis ? 0.0f : 0.5f);
        float low = (source->center[k] - extent) / l->cell + offset;
        float high = (source->center[k] + extent) / l->cell + offset;
        first[k] = low > 1.0f ? (int32_t)fminf(low, (float)m[k]) - 1 : 0;
        end[k] = high < (float)m[k] - 2.0f ? (int32_t)fmaxf(high, 0.0f) + 2 : m[k];
    }
}

// A source's rates over a substep into the cells it holds.
static void PourCells(const mvfxGasSource* source, const mvfxGasLattice* l, float* const scalars[3])
{
    const float rates[3] = {source->density * l->h, source->temperature * l->h,
                            source->fuel * l->h};
    int32_t first[3];
    int32_t end[3];
    Reach(source, l, -1, first, end);
    for (int32_t z = first[2]; z < end[2]; ++z)
    {
        for (int32_t y = first[1]; y < end[1]; ++y)
        {
            for (int32_t x = first[0]; x < end[0]; ++x)
            {
                const int32_t at[3] = {x, y, z};
                int64_t i = ((int64_t)z * l->n[1] + y) * l->n[0] + x;
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
    mvfxGasPoints(l, axis, m);
    int32_t first[3];
    int32_t end[3];
    Reach(source, l, axis, first, end);
    for (int32_t z = first[2]; z < end[2]; ++z)
    {
        for (int32_t y = first[1]; y < end[1]; ++y)
        {
            for (int32_t x = first[0]; x < end[0]; ++x)
            {
                const int32_t at[3] = {x, y, z};
                int64_t i = ((int64_t)z * m[1] + y) * m[0] + x;
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
                    const mvfxGasDrive* drive, int64_t start, double seconds, bool pours)
{
    int32_t now = state->current;
    float* velocity[3] = {state->velocity[0][now], state->velocity[1][now],
                          state->velocity[2][now]};
    float* scalars[3] = {state->scalars[0][now], state->scalars[1][now], state->scalars[2][now]};
    if (pours)
    {
        Pour(gas, l, velocity, scalars);
    }
    for (int32_t p = 0; p < drive->pourCount; ++p)
    {
        mvfxGasSplatInto(l, &drive->pours[p], velocity, scalars);
    }
    mvfxGasBuoyancy(gas, l, drive->gravity, velocity, scalars);
    mvfxGasPush(l, drive->forces, drive->forceCount, (double)start / (double)MVFX_TICKS_PER_SECOND,
                velocity);
    mvfxHoldGasVelocity(gas, velocity);
    mvfxGasConfine(gas, l, velocity, state->work);
    float keep = mvfxExp(-gas->def.velocityDecay * l->h);
    int32_t axes = l->n[2] > 1 ? 3 : 2;
    for (int32_t k = 0; k < axes; ++k)
    {
        Carry(gas, velocity, l, k, velocity[k], state->velocity[k][1 - now], state->work, keep,
              drive->tasks);
    }
    float* carried[3] = {state->scalars[0][1 - now], state->scalars[1][1 - now],
                         state->scalars[2][1 - now]};
    CarryCells(gas, velocity, l, scalars, carried, state->work, drive->tasks);
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
    mvfxProjectGas(gas, state, expands ? state->work : nullptr, drive->tasks);
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

void mvfxStepGas(const mvfxCompiledGas* gas, mvfxGasState* state, const mvfxGasDrive* drive,
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
        Substep(gas, state, &l, drive, start, seconds, start < stopTick);
    }
    state->holds = HoldsAny(gas, state);
}
