// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A liquid gas's step on the GPU (liquid_gas.wgsl about gas.wgsl's
// projection), as the core's mvfxStepLiquidGas orders it: its emitter's
// particles after their step mapped into the box, sorted by cell and run;
// the cells marked; the faces gathered; the solids' faces moved; the
// projection; the faces of liquid kept; the velocity extended two layers;
// the walls closed; the particles' velocities and affine matrices read
// back, with the fastest speed and whether the gas holds anything.

#include "numbers.h"
#include "records.h"
#include "sim_gas.h"
#include "sim_state.h"

#include <math.h>
#include <string.h>

// The layers the velocity is extended into the air (gas_liquid.c's
// EXTEND_LAYERS).
#define EXTEND_LAYERS 2u

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

int32_t mvfxSimLiquidGasPasses(const mvfxGasView* view, int32_t projection)
{
    bool solids = view->def.obstacleCount > 0 || view->def.hostObstacles;
    // The keys, the sort, the runs; a mark a level; the gather, the
    // solids' move; the projection; the faces kept, the extension's
    // layers, the walls and the read back.
    return 1 + 2 * MVFX_SORT_PASSES + 1 + view->levelCount + 1 + (solids ? 1 : 0) + projection + 1 +
           (int32_t)EXTEND_LAYERS + 1 + 1;
}

// The liquid's numbers (liquid_gas.wgsl's Liquid): its emitter's head and
// map into the box, its affine matrices' first array, a particle's share of
// a cell (a cube, or a square, of its spacing in the gas's cells), the
// growth's time and the step's seconds.
static void Numbers(const mvfxGpuSim* sim, SimGas* gas, const mvfxGasPlan* plan)
{
    SimSplat* liquid = &gas->liquid;
    const mvfxEmitterView* view = &sim->views[liquid->emitter];
    const mvfxGasDef* def = &gas->view.def;
    uint32_t blocks = liquid->span / MVFX_SORT_BLOCK;
    const uint32_t head[4] = {liquid->capacity, mvfxWordOf(view->arrays.position),
                              mvfxWordOf(view->arrays.velocity), Capped(blocks)};
    memcpy(liquid->words, head, sizeof head);
    const mvfxPlannedPour* map = &plan->liquid;
    mvfxFloatWords(liquid->words + 4, map->origin[0], map->origin[1], map->origin[2], map->sigma);
    memcpy(liquid->words + 8, map->rotation, 4 * sizeof(float));
    float seconds = (float)((double)plan->ticks / (double)MVFX_TICKS_PER_SECOND);
    float spacing = view->liquid.spacing * map->sigma / def->cellSize;
    float volume = def->size[2] > 1 ? spacing * spacing * spacing : spacing * spacing;
    liquid->words[12] = mvfxWordOf(view->arrays.affine);
    mvfxFloatWords(liquid->words + 13, volume, fmaxf(seconds, def->fillTime), seconds, 0.0f);
}

void mvfxPlanSimLiquid(const mvfxGpuSim* sim, SimGas* gas, const mvfxGasPlan* plan, uint32_t now)
{
    if (!gas->hasLiquid)
    {
        mvfxSimGasProject(gas, 0, now);
        return;
    }
    Numbers(sim, gas, plan);
    const SimSplat* liquid = &gas->liquid;
    const mvfxGasView* view = &gas->view;
    uint32_t blocks = Capped(liquid->span / MVFX_SORT_BLOCK);
    uint32_t particles = Groups(liquid->capacity);
    uint32_t cells = Groups((uint32_t)view->cells);
    uint32_t faces = Groups((uint32_t)(view->faces[0] + view->faces[1] + view->faces[2]));
    gas->dispatches[gas->count++] = (GasDispatch){.kernel = mvfx_kernelLiquidGasKeys,
                                                  .groups = blocks,
                                                  .binds = GAS_BINDS_LIQUID,
                                                  .splat = LIQUID_SPLAT};
    for (uint32_t shift = 0; shift < 32u; shift += 8u)
    {
        const uint32_t root[4] = {shift, liquid->capacity, blocks, 0};
        gas->dispatches[gas->count++] = (GasDispatch){.kernel = mvfx_kernelSortOffsets,
                                                      .root = {root[0], root[1], root[2], 0},
                                                      .groups = 1,
                                                      .binds = GAS_BINDS_SORT,
                                                      .splat = LIQUID_SPLAT};
        gas->dispatches[gas->count++] = (GasDispatch){.kernel = mvfx_kernelSortScatter,
                                                      .root = {root[0], root[1], root[2], 0},
                                                      .groups = blocks,
                                                      .binds = GAS_BINDS_SORT,
                                                      .splat = LIQUID_SPLAT};
    }
    Push(gas, mvfx_kernelLiquidGasRuns, 0, now, particles, GAS_BINDS_LIQUID);
    Push(gas, mvfx_kernelLiquidGasMark, 0, now, cells, GAS_BINDS_LIQUID);
    for (int32_t l = 1; l < view->levelCount; ++l)
    {
        uint32_t coarse = (uint32_t)(view->levels[l][0] * view->levels[l][1] * view->levels[l][2]);
        Push(gas, mvfx_kernelLiquidGasCoarse, (uint32_t)l, now, Groups(coarse), GAS_BINDS_LIQUID);
    }
    Push(gas, mvfx_kernelLiquidGasGather, 0, now, faces, GAS_BINDS_LIQUID);
    if (view->def.obstacleCount > 0 || view->def.hostObstacles)
    {
        gas->dispatches[gas->count++] = (GasDispatch){
            .kernel = mvfx_kernelGasMove, .root = {0, now, 0, 0}, .groups = faces, .splat = -1};
    }
    mvfxSimGasProject(gas, 0, now);
    Push(gas, mvfx_kernelLiquidGasKeep, 0, now, faces, GAS_BINDS_LIQUID);
    // The layers through the other buffer and set, back into this one.
    for (uint32_t layer = 0; layer < EXTEND_LAYERS; ++layer)
    {
        uint32_t from = layer % 2u == 0u ? now : 1u - now;
        Push(gas, mvfx_kernelLiquidGasExtend, layer % 2u, from, faces, GAS_BINDS_LIQUID);
    }
    Push(gas, mvfx_kernelLiquidGasWalls, 0, now, faces, GAS_BINDS_LIQUID);
    Push(gas, mvfx_kernelLiquidGasRead, 0, now, particles, GAS_BINDS_LIQUID);
}
