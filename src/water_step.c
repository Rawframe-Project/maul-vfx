// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Waves on a water surface: the wave equation u_tt = c^2 (u_xx + u_zz) as
// u_t = v, v_t = c^2 L(u), by semi-implicit Euler over the five-point
// Laplacian (Muller, the height field chapter of Bridson and Muller,
// "Fluid Simulation", SIGGRAPH 2007 course notes), each rate damped by
// exp(-damping dt). The scheme is stable for c dt / h <= 1 / sqrt(2) (the
// Laplacian's largest eigenvalue is 8 / h^2), so substeps are at most 0.9
// of it. A closed wall mirrors its edge's value; an open wall's edge
// follows Mur's first-order absorbing condition ("Absorbing Boundary
// Conditions for the Finite-Difference Approximation of the Time-Domain
// Electromagnetic-Field Equations", IEEE Trans. EMC 1981). Each pass is a
// per-cell gather from the buffer the substep began with.

#include "water_step.h"

#include "box.h"
#include "elementary.h"
#include "water.h"
#include "water_api.h"
#include "water_shallow.h"

#include <math.h>

// The least rows a task's range takes.
#define ROW_RANGE 8

// A substep's view of a surface: its cells, the factors of its update and
// its walls, and its buffers.
typedef struct Waves
{
    const int32_t* n;
    // dt c^2 / h^2, exp(-damping dt), (c dt - h) / (c dt + h), dt.
    float stiffness;
    float decay;
    float mur;
    float dt;
    uint8_t closed;
    const float* now;
    float* next;
    float* rates;
} Waves;

// Whether cell (x, z) lies on an open wall, its value then Mur's.
static bool OnOpenWall(const Waves* w, int32_t x, int32_t z)
{
    return (x == 0 && (w->closed & mvfx_waterWallMinX) == 0) ||
           (x == w->n[0] - 1 && (w->closed & mvfx_waterWallMaxX) == 0) ||
           (z == 0 && (w->closed & mvfx_waterWallMinZ) == 0) ||
           (z == w->n[1] - 1 && (w->closed & mvfx_waterWallMaxZ) == 0);
}

static void RateRows(int32_t start, int32_t end, void* context)
{
    const Waves* w = context;
    int32_t n0 = w->n[0];
    for (int32_t z = start; z < end; ++z)
    {
        int64_t i = (int64_t)z * n0;
        for (int32_t x = 0; x < n0; ++x, ++i)
        {
            // Beyond an edge, the edge's own value: no slope across it.
            float u = w->now[i];
            float left = x > 0 ? w->now[i - 1] : u;
            float right = x + 1 < n0 ? w->now[i + 1] : u;
            float down = z > 0 ? w->now[i - n0] : u;
            float up = z + 1 < w->n[1] ? w->now[i + n0] : u;
            float laplacian = left + right + down + up - 4.0f * u;
            w->rates[i] = (w->rates[i] + w->stiffness * laplacian) * w->decay;
        }
    }
}

static void HeightRows(int32_t start, int32_t end, void* context)
{
    const Waves* w = context;
    for (int32_t z = start; z < end; ++z)
    {
        int64_t i = (int64_t)z * w->n[0];
        for (int32_t x = 0; x < w->n[0]; ++x, ++i)
        {
            w->next[i] = w->now[i] + w->dt * w->rates[i];
        }
    }
}

// An open wall's cell at i, its inner neighbor at i + inward: Mur's
// first-order edge, u_b' = u_in + m (u_in' - u_b), its rate what moved it.
static void Absorb(const Waves* w, int64_t i, int64_t inward)
{
    float u = w->now[inward + i] + w->mur * (w->next[inward + i] - w->now[i]);
    w->rates[i] = (u - w->now[i]) / w->dt;
    w->next[i] = u;
}

// Every open wall's cells, the corners by their X wall.
static void OpenWalls(const Waves* w)
{
    int32_t n0 = w->n[0];
    int32_t n1 = w->n[1];
    for (int32_t z = 0; z < n1; ++z)
    {
        for (int32_t x = 0; x < n0; x += (z == 0 || z == n1 - 1) ? 1 : n0 - 1)
        {
            if (!OnOpenWall(w, x, z))
            {
                continue;
            }
            int64_t i = (int64_t)z * n0 + x;
            bool minX = x == 0 && (w->closed & mvfx_waterWallMinX) == 0;
            bool maxX = x == n0 - 1 && (w->closed & mvfx_waterWallMaxX) == 0;
            int64_t inward = minX ? 1 : (maxX ? -1 : (z == 0 ? n0 : -(int64_t)n0));
            Absorb(w, i, inward);
        }
    }
}

double mvfxSinkWaves(const mvfxCompiledWater* water, float* u, const float at[2], float radius,
                     float depth)
{
    const int32_t* n = water->def.size;
    float h = water->def.cellSize;
    // The cells' centers within the radius: along each axis, by index.
    float cx = at[0] / h + 0.5f * (float)n[0] - 0.5f;
    float cz = at[1] / h + 0.5f * (float)n[1] - 0.5f;
    float reach = radius / h;
    int32_t xs[2];
    int32_t zs[2];
    mvfxWaterSpan(cx, reach, n[0], xs);
    mvfxWaterSpan(cz, reach, n[1], zs);
    double sank = 0.0;
    for (int32_t z = zs[0]; z <= zs[1]; ++z)
    {
        for (int32_t x = xs[0]; x <= xs[1]; ++x)
        {
            float dx = ((float)x - cx) / reach;
            float dz = ((float)z - cz) / reach;
            float s = 1.0f - (dx * dx + dz * dz);
            float by = s > 0.0f ? depth * s * s : 0.0f;
            if (u != nullptr)
            {
                u[(int64_t)z * n[0] + x] -= by;
            }
            sank += (double)by;
        }
    }
    return sank;
}

// A lift of every cell: its rows' heights and the lift.
typedef struct Lift
{
    float* u;
    int32_t width;
    float by;
} Lift;

static void LiftRows(int32_t start, int32_t end, void* context)
{
    const Lift* lift = context;
    for (int64_t i = (int64_t)start * lift->width; i < (int64_t)end * lift->width; ++i)
    {
        lift->u[i] += lift->by;
    }
}

// Raises every cell evenly by what the step's pushes sank, one pass for
// them all: the water a body displaces lifts the level, and the
// surface's volume stays. Kept in the heights rather than as a level
// apart, their mean stays at the rest level however long a body pushes.
static void Displace(const mvfxCompiledWater* water, mvfxWaterState* state, double sank,
                     const mvfxTasks* tasks)
{
    Lift lift = {.u = state->heights[state->current],
                 .width = water->def.size[0],
                 .by = (float)(sank / (double)water->cells)};
    mvfxRunTasks(tasks, LiftRows, water->def.size[1], ROW_RANGE, &lift);
}

// Whether the surface moves (a rate, or a slope that will make one: an
// open wall may leave it flat off its rest level, still), and its largest
// displacement's size.
static void Settle(const mvfxCompiledWater* water, mvfxWaterState* state)
{
    const float* u = state->heights[state->current];
    float lowest = u[0];
    float highest = u[0];
    float fastest = 0.0f;
    for (int64_t i = 0; i < water->cells; ++i)
    {
        lowest = fminf(lowest, u[i]);
        highest = fmaxf(highest, u[i]);
        fastest = fmaxf(fastest, fabsf(state->rates[i]));
    }
    state->reach = fmaxf(-lowest, highest);
    state->holds = highest - lowest > MVFX_WATER_STILL || fastest > MVFX_WATER_STILL;
}

int32_t mvfxWaveSubsteps(const mvfxCompiledWater* water, double seconds, float* dt, bool* slowed)
{
    *dt = 0.0f;
    *slowed = false;
    if (!(seconds > 0.0))
    {
        return 0;
    }
    // The longest stable substep, and the step's count of them.
    double longest = 0.9 * (double)water->def.cellSize / ((double)water->def.waveSpeed * sqrt(2.0));
    double count = ceil(seconds / longest);
    *slowed = count > (double)MVFX_MAX_WATER_SUBSTEPS;
    *dt = (float)(*slowed ? longest : seconds / count);
    return *slowed ? MVFX_MAX_WATER_SUBSTEPS : (int32_t)count;
}

void mvfxStepWater(mvfxInstance* instance, int32_t w, const mvfxTasks* tasks)
{
    if (instance->effect->waters[w].def.model == mvfx_waterShallow)
    {
        mvfxStepShallow(instance, w, tasks);
        return;
    }
    const mvfxCompiledWater* water = &instance->effect->waters[w];
    mvfxWaterState* state = &instance->waters[w];
    // The host's pushes, then the particles' crossings.
    mvfxBoxPlace place = mvfxWaterPlace(instance, w);
    mvfxSpaceMap map = mvfxBoxMap(instance, &place, mvfx_spaceWorld);
    double sank = 0.0;
    for (int32_t p = 0; p < state->pushCount; ++p)
    {
        const mvfxWaterPush* push = &state->pushes[p];
        float local[3];
        mvfxMapPointDouble(&map, push->point, local);
        const float at[2] = {local[0], local[2]};
        sank += mvfxSinkWaves(water, state->heights[state->current], at, push->radius * map.sigma,
                              push->depth * map.sigma);
    }
    for (int64_t c = 0; c < state->crossingCount; ++c)
    {
        const mvfxWaterCrossing* crossing = &state->crossings[c];
        sank += mvfxSinkWaves(water, state->heights[state->current], crossing->at, crossing->radius,
                              crossing->depth);
    }
    if (state->pushCount > 0 || state->crossingCount > 0)
    {
        Displace(water, state, sank, tasks);
    }
    state->pushCount = 0;
    state->crossingCount = 0;
    double seconds =
        (double)(instance->clock - instance->stepStart) / (double)MVFX_TICKS_PER_SECOND;
    float dt = 0.0f;
    bool slowed = false;
    int32_t substeps = mvfxWaveSubsteps(water, seconds, &dt, &slowed);
    if (substeps == 0)
    {
        Settle(water, state);
        return;
    }
    float h = water->def.cellSize;
    float c = water->def.waveSpeed;
    state->stepsSlowed += slowed ? 1u : 0u;
    Waves waves = {.n = water->def.size,
                   .stiffness = dt * c * c / (h * h),
                   .decay = mvfxExp(-water->def.damping * dt),
                   .mur = (c * dt - h) / (c * dt + h),
                   .dt = dt,
                   .closed = water->def.closedWalls,
                   .rates = state->rates};
    for (int32_t s = 0; s < substeps; ++s)
    {
        waves.now = state->heights[state->current];
        waves.next = state->heights[1 - state->current];
        mvfxRunTasks(tasks, RateRows, water->def.size[1], ROW_RANGE, &waves);
        mvfxRunTasks(tasks, HeightRows, water->def.size[1], ROW_RANGE, &waves);
        OpenWalls(&waves);
        state->current = 1 - state->current;
    }
    Settle(water, state);
}
