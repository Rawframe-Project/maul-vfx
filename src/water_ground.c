// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shallow water's ground, first fill, sources and pushes, each in the
// surface's box frame: X and Z across its grid, Y up from its center.

#include "water_ground.h"

#include "box.h"
#include "collide.h"
#include "direction.h"
#include "grid.h"
#include "water.h"
#include "water_api.h"

#include <math.h>
#include <string.h>

// The least rows a task's range takes.
#define ROW_RANGE 8

// A surface's ground being read: its terrain resolved into the box's
// frame, its cells and their edge, and the heights it writes.
typedef struct Ground
{
    mvfxResolvedCollider terrain;
    const int32_t* n;
    float cell;
    float* out;
} Ground;

// The terrain surface w stands on now and its grid: the def's, or the
// instance's first heightfield collider; NULL for the floor.
static const mvfxCollider* TerrainOf(const mvfxInstance* instance, int32_t w, const mvfxGrid** grid)
{
    const mvfxCompiledWater* water = &instance->effect->waters[w];
    if (water->def.hostTerrain)
    {
        for (int32_t c = 0; c < instance->hostColliderCount; ++c)
        {
            if (instance->hostColliders[c].kind == mvfx_colliderHeightfield)
            {
                *grid = instance->hostColliderGrids[c];
                return &instance->hostColliders[c];
            }
        }
        return nullptr;
    }
    *grid = water->terrainGrid;
    return water->def.hasTerrain ? &water->terrain : nullptr;
}

// The ground's height under a point (x, z) of the box's frame: where its
// vertical meets the field, through the point below it in the field's
// frame; the floor (0) outside the field's box.
static float HeightAt(const mvfxResolvedCollider* t, float x, float z)
{
    const float back[4] = {-t->rotation[0], -t->rotation[1], -t->rotation[2], t->rotation[3]};
    const float offset[3] = {x - t->center[0], -t->center[1], z - t->center[2]};
    float local[3];
    mvfxRotate(back, offset, local);
    const float u[3] = {local[0] / (2.0f * t->halfExtents[0]) + 0.5f, 0.5f,
                        local[2] / (2.0f * t->halfExtents[2]) + 0.5f};
    float value[4];
    if (!mvfxSampleGrid(t->grid, u, mvfx_borderNone, value))
    {
        return 0.0f;
    }
    const float on[3] = {local[0], value[0] * t->scale, local[2]};
    float turned[3];
    mvfxRotate(t->rotation, on, turned);
    return t->center[1] + turned[1];
}

static void GroundRows(int32_t start, int32_t end, void* context)
{
    const Ground* g = context;
    int32_t n0 = g->n[0];
    for (int32_t z = start; z < end; ++z)
    {
        float at = ((float)z + 0.5f - 0.5f * (float)g->n[1]) * g->cell;
        for (int32_t x = 0; x < n0; ++x)
        {
            float across = ((float)x + 0.5f - 0.5f * (float)n0) * g->cell;
            g->out[(int64_t)z * n0 + x] = HeightAt(&g->terrain, across, at);
        }
    }
}

void mvfxReadWaterGround(mvfxInstance* instance, int32_t w, const mvfxTasks* tasks)
{
    const mvfxCompiledWater* water = &instance->effect->waters[w];
    mvfxWaterState* state = &instance->waters[w];
    const mvfxGrid* grid = nullptr;
    const mvfxCollider* terrain = TerrainOf(instance, w, &grid);
    if (terrain == nullptr || grid == nullptr)
    {
        memset(state->ground, 0, (size_t)water->cells * sizeof(float));
        return;
    }
    // A terrain's space: 0 the world's, 1 the instance's frame.
    mvfxBoxPlace place = mvfxWaterPlace(instance, w);
    mvfxSpaceMap map =
        mvfxBoxMap(instance, &place, terrain->space == 0 ? mvfx_spaceWorld : mvfx_spaceLocal);
    Ground g = {.terrain = mvfxResolveCollider(terrain, grid, &map, 0.0f),
                .n = water->def.size,
                .cell = water->def.cellSize,
                .out = state->ground};
    mvfxRunTasks(tasks, GroundRows, water->def.size[1], ROW_RANGE, &g);
}

void mvfxFillWater(mvfxInstance* instance, int32_t w)
{
    const mvfxCompiledWater* water = &instance->effect->waters[w];
    mvfxWaterState* state = &instance->waters[w];
    float* depth = state->heights[state->current];
    for (int64_t i = 0; i < water->cells; ++i)
    {
        depth[i] = fmaxf(water->def.fillLevel - state->ground[i], 0.0f);
    }
    state->filled = true;
}

// Whether a source's footprint covers the point (x, z) of the box's frame.
static bool Covers(const mvfxWaterSource* source, float x, float z)
{
    float dx = x - source->center[0];
    float dz = z - source->center[2];
    if (source->shape == mvfx_gasSphere)
    {
        return dx * dx + dz * dz <= source->radius * source->radius;
    }
    return fabsf(dx) <= source->halfExtents[0] && fabsf(dz) <= source->halfExtents[2];
}

void mvfxPourWaterSources(mvfxInstance* instance, int32_t w, double seconds)
{
    const mvfxCompiledWater* water = &instance->effect->waters[w];
    mvfxWaterState* state = &instance->waters[w];
    float* depth = state->heights[state->current];
    const int32_t* n = water->def.size;
    float h = water->def.cellSize;
    for (int32_t s = 0; s < water->def.sourceCount; ++s)
    {
        const mvfxWaterSource* source = &water->sources[s];
        bool sphere = source->shape == mvfx_gasSphere;
        float reach[2] = {sphere ? source->radius : source->halfExtents[0],
                          sphere ? source->radius : source->halfExtents[2]};
        int32_t xs[2];
        int32_t zs[2];
        mvfxWaterSpan(source->center[0] / h + 0.5f * (float)n[0] - 0.5f, reach[0] / h, n[0], xs);
        mvfxWaterSpan(source->center[2] / h + 0.5f * (float)n[1] - 0.5f, reach[1] / h, n[1], zs);
        float add = (float)((double)source->rate * seconds);
        for (int32_t z = zs[0]; z <= zs[1]; ++z)
        {
            for (int32_t x = xs[0]; x <= xs[1]; ++x)
            {
                float px = ((float)x + 0.5f - 0.5f * (float)n[0]) * h;
                float pz = ((float)z + 0.5f - 0.5f * (float)n[1]) * h;
                depth[(int64_t)z * n[0] + x] += Covers(source, px, pz) ? add : 0.0f;
            }
        }
    }
}

// A push in the box's frame: its center in cells (0 at the first cell's
// center), its radius in cells and its depth in the box's lengths; the
// cells within twice its radius.
typedef struct Spill
{
    float c[2];
    float reach;
    float depth;
    int32_t xs[2];
    int32_t zs[2];
} Spill;

// A cell's weight in a push at r radii from its center: within the radius
// (1 - r^2)^2, which it sinks by; in the ring to twice it the bump
// (1 - (2 r - 3)^2)^2, which it spills into.
static float Weight(float r, bool* inner)
{
    *inner = r < 1.0f;
    float s = *inner ? 1.0f - r * r : 1.0f - (2.0f * r - 3.0f) * (2.0f * r - 3.0f);
    return r < 2.0f && s > 0.0f ? s * s : 0.0f;
}

// What a push takes from cell i of weight wt: at most the water there.
static float Taken(const Spill* p, const float* d, int64_t i, float wt, bool inner)
{
    bool takes = inner == (p->depth > 0.0f);
    return takes ? fminf(fabsf(p->depth) * wt, d[i]) : 0.0f;
}

// One pass over a push's cells: with move false, the sums of the inner
// and ring weights and of what it takes; with move true, the water moved
// from where it is taken to where it lands, by those sums.
static void Visit(const Spill* p, const int32_t* n, float* d, bool move, double sums[3])
{
    for (int32_t z = p->zs[0]; z <= p->zs[1]; ++z)
    {
        for (int32_t x = p->xs[0]; x <= p->xs[1]; ++x)
        {
            float dx = (float)x - p->c[0];
            float dz = (float)z - p->c[1];
            bool inner = false;
            float wt = Weight(sqrtf(dx * dx + dz * dz) / p->reach, &inner);
            int64_t i = (int64_t)z * n[0] + x;
            float taken = Taken(p, d, i, wt, inner);
            if (!move)
            {
                sums[inner ? 0 : 1] += (double)wt;
                sums[2] += (double)taken;
                continue;
            }
            // Water lands where it is not taken: the ring for a sinking
            // push, the inside for a lifting one.
            bool lands = inner != (p->depth > 0.0f);
            double share = (double)wt / sums[inner ? 0 : 1];
            d[i] += lands ? (float)(sums[2] * share) : -taken;
        }
    }
}

void mvfxPushShallow(mvfxInstance* instance, int32_t w)
{
    const mvfxCompiledWater* water = &instance->effect->waters[w];
    mvfxWaterState* state = &instance->waters[w];
    const int32_t* n = water->def.size;
    float h = water->def.cellSize;
    float* d = state->heights[state->current];
    mvfxBoxPlace place = mvfxWaterPlace(instance, w);
    mvfxSpaceMap map = mvfxBoxMap(instance, &place, mvfx_spaceWorld);
    for (int32_t k = 0; k < state->pushCount; ++k)
    {
        const mvfxWaterPush* push = &state->pushes[k];
        float local[3];
        mvfxMapPointDouble(&map, push->point, local);
        Spill p = {.c = {local[0] / h + 0.5f * (float)n[0] - 0.5f,
                         local[2] / h + 0.5f * (float)n[1] - 0.5f},
                   .reach = push->radius * map.sigma / h,
                   .depth = push->depth * map.sigma};
        mvfxWaterSpan(p.c[0], 2.0f * p.reach, n[0], p.xs);
        mvfxWaterSpan(p.c[1], 2.0f * p.reach, n[1], p.zs);
        double sums[3] = {0.0, 0.0, 0.0};
        Visit(&p, n, d, false, sums);
        // Nothing to take, or nowhere on the grid to land: no change.
        if (sums[0] > 0.0 && sums[1] > 0.0 && sums[2] > 0.0)
        {
            Visit(&p, n, d, true, sums);
        }
    }
    state->pushCount = 0;
}
