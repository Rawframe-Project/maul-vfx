// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A surface gas's step on the GPU (surface_gas.wgsl), as the core's
// mvfxStepSurfaceGas orders it: its emitter's particles after their step
// mapped into the box, keyed by bucket, sorted (sort.wgsl) and run; each
// cell's distance from the buckets about it, into the first buffer; then
// the smoothing passes, each a mean into the other buffer and a lower
// back. An emitter without room has no particles: its runs stay empty,
// and every cell holds the reach.

#include "numbers.h"
#include "records.h"
#include "sim_gas.h"
#include "sim_state.h"

#include <string.h>

static void Push(SimGas* gas, mvfxKernel kernel, uint32_t a, uint32_t b, uint32_t groups,
                 GasBinding binds)
{
    gas->dispatches[gas->count++] = (GasDispatch){.kernel = kernel,
                                                  .root = {a, b, 0, 0},
                                                  .groups = groups,
                                                  .binds = binds,
                                                  .splat = LIQUID_SPLAT};
}

static uint32_t Groups(uint32_t points)
{
    return (points + GAS_GROUP - 1u) / GAS_GROUP;
}

// A loop's workgroups, at most a row's: the keys and the sort's scatter
// step over blocks by the workgroups dispatched, along X alone.
static uint32_t Capped(uint32_t groups)
{
    return groups < GAS_ROW ? (groups > 0u ? groups : 1u) : GAS_ROW;
}

int32_t mvfxSimSurfaceGasPasses(const mvfxGasView* view)
{
    // The keys, the sort, the runs; the field; a mean and a lower a
    // smoothing pass.
    return 1 + 2 * MVFX_SORT_PASSES + 1 + 1 + 2 * view->def.surface.smoothing;
}

// The surface's numbers (surface_gas.wgsl's Surface): its emitter's
// capacity and places, the keys' workgroups and its map into the box, the
// buckets, the reach and the radius in cells as the core's SurfaceOf
// divides them, the thresholds, the reach in lengths.
static void Numbers(const mvfxGpuSim* sim, SimGas* gas, const mvfxGasPlan* plan)
{
    SimSplat* surface = &gas->liquid;
    const mvfxGasView* view = &gas->view;
    const mvfxGasSurface* def = &view->def.surface;
    uint32_t blocks = surface->span / MVFX_SORT_BLOCK;
    const uint32_t head[4] = {surface->capacity,
                              mvfxWordOf(sim->views[surface->emitter].arrays.position), 0,
                              Capped(blocks)};
    memcpy(surface->words, head, sizeof head);
    const mvfxPlannedPour* map = &plan->liquid;
    mvfxFloatWords(surface->words + 4, map->origin[0], map->origin[1], map->origin[2], map->sigma);
    memcpy(surface->words + 8, map->rotation, 4 * sizeof(float));
    const uint32_t buckets[4] = {(uint32_t)view->buckets[0], (uint32_t)view->buckets[1],
                                 (uint32_t)view->buckets[2], (uint32_t)view->bucket};
    memcpy(surface->words + 12, buckets, sizeof buckets);
    float cell = view->def.cellSize;
    float reach = def->reach * def->radius;
    mvfxFloatWords(surface->words + 16, reach / cell, def->radius / cell, def->low, def->high);
    mvfxFloatWords(surface->words + 20, reach, 0.0f, 0.0f, 0.0f);
}

void mvfxPlanSimSurface(const mvfxGpuSim* sim, SimGas* gas, const mvfxGasPlan* plan)
{
    Numbers(sim, gas, plan);
    const SimSplat* surface = &gas->liquid;
    uint32_t cells = Groups((uint32_t)gas->view.cells);
    if (surface->capacity > 0)
    {
        uint32_t blocks = Capped(surface->span / MVFX_SORT_BLOCK);
        Push(gas, mvfx_kernelSurfaceKeys, 0, 0, blocks, GAS_BINDS_SURFACE);
        for (uint32_t shift = 0; shift < 32u; shift += 8u)
        {
            gas->dispatches[gas->count++] =
                (GasDispatch){.kernel = mvfx_kernelSortOffsets,
                              .root = {shift, surface->capacity, blocks, 0},
                              .groups = 1,
                              .binds = GAS_BINDS_SORT,
                              .splat = LIQUID_SPLAT};
            gas->dispatches[gas->count++] =
                (GasDispatch){.kernel = mvfx_kernelSortScatter,
                              .root = {shift, surface->capacity, blocks, 0},
                              .groups = blocks,
                              .binds = GAS_BINDS_SORT,
                              .splat = LIQUID_SPLAT};
        }
        Push(gas, mvfx_kernelSurfaceRuns, 0, 0, Groups(surface->capacity), GAS_BINDS_SURFACE);
    }
    Push(gas, mvfx_kernelSurfaceField, 0, 0, cells, GAS_BINDS_SURFACE);
    for (int32_t pass = 0; pass < gas->view.def.surface.smoothing; ++pass)
    {
        // The mean from the first buffer into the second, the lower back.
        Push(gas, mvfx_kernelSurfaceSmooth, 0, 0, cells, GAS_BINDS_SURFACE);
        Push(gas, mvfx_kernelSurfaceSmooth, 1, 1, cells, GAS_BINDS_SURFACE);
    }
}
