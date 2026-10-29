// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "gas.h"

#include "refusal.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

mvfxGasDef mvfxDefaultGasDef(void)
{
    return (mvfxGasDef){
        .cookie = MVFX_GAS_DEF_COOKIE,
        .size = {64, 64, 1},
        .cellSize = 0.1f,
        .fillTime = 1.0f / 60.0f,
        .courant = 1.0f,
        .space = mvfx_gasLocal,
        .substeps = 1,
        .pressureCycles = 4,
        .gravity = {0.0f, -9.81f, 0.0f},
        .gravitySpace = mvfx_gasWorld,
        .surface = {.radius = 0.1f, .reach = 4.0f, .low = 0.4f, .high = 2.0f, .smoothing = 1},
    };
}

// A value's status: finite, and at most MVFX_MAX_EXTENT in size.
static mvfxResult CheckExtent(float value)
{
    return mvfxBounded(value, -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT);
}

static mvfxResult CheckSource(const mvfxGasSource* source)
{
    if (source->shape != mvfx_gasSphere && source->shape != mvfx_gasBox)
    {
        return mvfx_errorInvalid;
    }
    mvfxResult valid =
        mvfxFirst(mvfxFirst(CheckExtent(source->density), CheckExtent(source->temperature)),
                  mvfxFirst(CheckExtent(source->fuel),
                            mvfxFirst(mvfxBoundedVector(source->center, MVFX_MAX_EXTENT),
                                      mvfxBoundedVector(source->velocity, MVFX_MAX_EXTENT))));
    if (valid != mvfx_success)
    {
        return valid;
    }
    if (source->shape == mvfx_gasSphere)
    {
        return mvfxPositiveUpTo(source->radius, MVFX_MAX_EXTENT);
    }
    return mvfxFirst(mvfxPositiveUpTo(source->halfExtents[0], MVFX_MAX_EXTENT),
                     mvfxFirst(mvfxPositiveUpTo(source->halfExtents[1], MVFX_MAX_EXTENT),
                               mvfxPositiveUpTo(source->halfExtents[2], MVFX_MAX_EXTENT)));
}

// Whether a gas's size is in range: each axis from 1 (2 along X and Y)
// (else mvfx_errorInvalid) to the most, at most the most cells in all
// (else mvfx_errorLimit).
static mvfxResult CheckSize(const int32_t size[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        if (size[k] < (k < 2 ? 2 : 1))
        {
            return mvfx_errorInvalid;
        }
    }
    int64_t cells = 1;
    for (int32_t k = 0; k < 3; ++k)
    {
        if (size[k] > MVFX_MAX_GAS_SIZE)
        {
            return mvfx_errorLimit;
        }
        cells *= size[k];
    }
    return mvfxWithinBound(cells <= MVFX_MAX_GAS_CELLS);
}

// Whether a gas's place, advection, buoyancy, confinement and combustion
// are in range.
static mvfxResult CheckForces(const mvfxGasDef* def)
{
    if ((def->advection != mvfx_gasSemiLagrangian && def->advection != mvfx_gasMacCormack) ||
        (def->gravitySpace != mvfx_gasLocal && def->gravitySpace != mvfx_gasWorld))
    {
        return mvfx_errorInvalid;
    }
    const float weights[7] = {def->densityWeight, def->heatLift,  def->vorticity,    def->burnRate,
                              def->burnHeat,      def->burnSmoke, def->burnExpansion};
    mvfxResult valid = CheckExtent(def->ignitionTemperature);
    for (int32_t i = 0; valid == mvfx_success && i < 7; ++i)
    {
        valid = mvfxUpToBound(weights[i], 0.0f, MVFX_MAX_EXTENT);
    }
    return mvfxFirst(valid, mvfxFirst(mvfxBoundedVector(def->center, MVFX_MAX_EXTENT),
                                      mvfxBoundedVector(def->gravity, MVFX_MAX_EXTENT)));
}

// Whether a liquid or surface gas keeps to what its kind has: no sources
// or forces, one substep, and for a surface no obstacles and its
// particles' radius, reach, thresholds and smoothing in range.
static mvfxResult CheckKind(const mvfxGasDef* def)
{
    if (def->kind == mvfx_gasSmoke)
    {
        return mvfx_success;
    }
    if (def->sourceCount > 0 || def->forceCount > 0 || def->substeps != 1)
    {
        return mvfx_errorInvalid;
    }
    if (def->kind == mvfx_gasLiquid)
    {
        return mvfx_success;
    }
    const mvfxGasSurface* s = &def->surface;
    if (def->obstacleCount != 0 || def->hostObstacles || !(s->reach >= 1.0f) ||
        s->smoothing < 0)
    {
        return mvfx_errorInvalid;
    }
    return mvfxFirst(
        mvfxFirst(mvfxPositiveUpTo(s->radius, MVFX_MAX_EXTENT),
                  mvfxBounded(s->reach, 1.0f, MVFX_MAX_SURFACE_REACH)),
        mvfxFirst(mvfxFirst(mvfxUpToBound(s->low, 0.0f, MVFX_MAX_EXTENT),
                            mvfxUpToBound(s->high, 0.0f, MVFX_MAX_EXTENT)),
                  mvfxWithinBound(s->smoothing <= MVFX_MAX_SURFACE_SMOOTHING)));
}

mvfxResult mvfxCheckGasDef(const mvfxGasDef* def)
{
    if (def->cookie != MVFX_GAS_DEF_COOKIE ||
        (def->space != mvfx_gasLocal && def->space != mvfx_gasWorld) || def->substeps < 1 ||
        def->sourceCount < 0 || (def->sourceCount > 0 && def->sources == nullptr) ||
        def->closedWalls > 63 || def->pressureCycles < 0 || !(def->fillTime > 0.0f) ||
        !(def->courant > 0.0f) || def->kind > mvfx_gasSurface || def->obstacleCount < 0 ||
        (def->obstacleCount > 0 && def->obstacles == nullptr))
    {
        return mvfx_errorInvalid;
    }
    // The counts bound the arrays read below.
    const mvfxResult fields[] = {
        CheckSize(def->size),
        mvfxPositiveUpTo(def->cellSize, MVFX_MAX_EXTENT),
        mvfxWithinBound(def->substeps <= MVFX_MAX_GAS_SUBSTEPS),
        mvfxWithinBound(def->sourceCount <= MVFX_MAX_GAS_SOURCES),
        mvfxWithinBound(def->pressureCycles <= MVFX_MAX_GAS_CYCLES),
        mvfxBounded(def->fillTime, 0.0f, (float)MVFX_MAX_STEP_SECONDS),
        mvfxBounded(def->courant, 0.0f, (float)MVFX_MAX_GAS_SIZE),
        mvfxWithinBound(def->obstacleCount <= MVFX_MAX_COLLIDERS),
        CheckKind(def),
        mvfxUpToBound(def->densityDecay, 0.0f, MVFX_MAX_DRAG),
        mvfxUpToBound(def->temperatureDecay, 0.0f, MVFX_MAX_DRAG),
        mvfxUpToBound(def->fuelDecay, 0.0f, MVFX_MAX_DRAG),
        mvfxUpToBound(def->velocityDecay, 0.0f, MVFX_MAX_DRAG),
        CheckForces(def),
    };
    for (size_t k = 0; k < sizeof(fields) / sizeof(fields[0]); ++k)
    {
        if (fields[k] != mvfx_success)
        {
            return fields[k];
        }
    }
    for (int32_t s = 0; s < def->sourceCount; ++s)
    {
        mvfxResult source = CheckSource(&def->sources[s]);
        if (source != mvfx_success)
        {
            return source;
        }
    }
    for (int32_t c = 0; c < def->obstacleCount; ++c)
    {
        mvfxResult obstacle = mvfxCheckCollider(&def->obstacles[c]);
        if (obstacle != mvfx_success)
        {
            return obstacle;
        }
    }
    return mvfx_success;
}

// Whether a level can be halved: every axis of more than one cell keeps
// at least 2.
static bool Coarsens(const int32_t n[3])
{
    bool any = false;
    for (int32_t k = 0; k < 3; ++k)
    {
        if (n[k] > 1 && n[k] < 4)
        {
            return false;
        }
        any = any || n[k] > 1;
    }
    return any;
}

// A surface gas has no faces, levels or work, and buckets of whole cells
// at least its reach on a side (at least one: a reach far below a cell
// rounds to 0), one more beyond the box at each end of an axis of more
// than one cell (the particles a box's cells reach).
static void CompileSurface(mvfxCompiledGas* gas)
{
    float cells = fmaxf(ceilf(mvfxSurfaceReach(gas) / gas->def.cellSize), 1.0f);
    gas->bucket = cells < (float)MVFX_MAX_GAS_SIZE ? (int32_t)cells : MVFX_MAX_GAS_SIZE;
    gas->bucketCount = 1;
    for (int32_t k = 0; k < 3; ++k)
    {
        int32_t n = gas->def.size[k];
        gas->faces[k] = 0;
        gas->buckets[k] = n > 1 ? (n + gas->bucket - 1) / gas->bucket + 2 : 1;
        gas->bucketCount *= gas->buckets[k];
    }
    gas->levelCount = 0;
    gas->work = 0;
}

mvfxCompiledGas mvfxCompileGas(const mvfxGasDef* def)
{
    mvfxCompiledGas gas = {.def = *def, .emitter = -1};
    gas.def.sources = nullptr;
    gas.def.obstacles = nullptr;
    gas.def.forces = nullptr;
    for (int32_t f = 0; f < def->forceCount; ++f)
    {
        gas.forces[f] = mvfxCompileForce(&def->forces[f]);
    }
    for (int32_t s = 0; s < def->sourceCount; ++s)
    {
        gas.sources[s] = def->sources[s];
    }
    for (int32_t c = 0; c < def->obstacleCount; ++c)
    {
        gas.obstacles[c] = mvfxCompileCollider(&def->obstacles[c]);
    }
    gas.hasObstacles = def->obstacleCount > 0 || def->hostObstacles;
    gas.cells = (int64_t)def->size[0] * def->size[1] * def->size[2];
    for (int32_t k = 0; k < 3; ++k)
    {
        gas.faces[k] = gas.cells / def->size[k] * (def->size[k] + 1);
        gas.levels[0][k] = def->size[k];
    }
    gas.levelCount = 1;
    while (gas.levelCount < MVFX_GAS_LEVELS && Coarsens(gas.levels[gas.levelCount - 1]))
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            gas.levels[gas.levelCount][k] = (gas.levels[gas.levelCount - 1][k] + 1) / 2;
        }
        gas.levelCount += 1;
    }
    int64_t curl = def->vorticity > 0.0f ? (def->size[2] > 1 ? 3 : 1) * gas.cells : 0;
    // MacCormack's work holds one carried array at a time: the largest
    // faces array of the axes a gas carries.
    int64_t faces = 0;
    for (int32_t k = 0; k < (def->size[2] > 1 ? 3 : 2) && def->advection == mvfx_gasMacCormack; ++k)
    {
        faces = faces > gas.faces[k] ? faces : gas.faces[k];
    }
    int64_t expansion = def->burnExpansion > 0.0f && def->burnRate > 0.0f ? gas.cells : 0;
    gas.work = curl > faces ? curl : faces;
    gas.work = gas.work > expansion ? gas.work : expansion;
    if (def->kind == mvfx_gasSurface)
    {
        CompileSurface(&gas);
    }
    return gas;
}

void mvfxAddGasLayout(mvfxLayout* layout, const mvfxCompiledGas* gas, int32_t particles,
                      mvfxGasAt* at)
{
    bool surface = gas->def.kind == mvfx_gasSurface;
    for (int32_t k = 0; k < 3; ++k)
    {
        at->velocity[k] =
            mvfxLayoutAdd(layout, 2u * (size_t)gas->faces[k], sizeof(float), alignof(float));
        // A surface holds its distance only.
        size_t cells = surface && k > 0 ? 0u : (size_t)gas->cells;
        at->scalars[k] = mvfxLayoutAdd(layout, 2u * cells, sizeof(float), alignof(float));
    }
    for (int32_t l = 0; l < gas->levelCount; ++l)
    {
        at->levels[l] = mvfxLayoutAdd(layout, 3u * (size_t)mvfxGasLevelCells(gas, l), sizeof(float),
                                      alignof(float));
    }
    for (int32_t l = 0; l < gas->levelCount; ++l)
    {
        at->areas[l] = mvfxLayoutAdd(layout, (size_t)mvfxGasLevelFaces(gas, l, 3), sizeof(float),
                                     alignof(float));
        at->coefficients[l] = mvfxLayoutAdd(layout, (size_t)mvfxGasLevelFaces(gas, l, 3),
                                            sizeof(float), alignof(float));
        at->weights[l] =
            mvfxLayoutAdd(layout, (size_t)mvfxGasLevelCells(gas, l), sizeof(float), alignof(float));
    }
    at->solid = mvfxLayoutAdd(layout, gas->hasObstacles ? (size_t)gas->cells : 0, 1, 1);
    at->work = mvfxLayoutAdd(layout, (size_t)gas->work, sizeof(float), alignof(float));
    bool liquid = gas->def.kind == mvfx_gasLiquid;
    size_t starts = liquid ? (size_t)gas->cells + 1u : 0u;
    starts = surface ? (size_t)gas->bucketCount + 1u : starts;
    size_t room = liquid || surface ? (size_t)particles : 0u;
    at->cellStart = mvfxLayoutAdd(layout, starts, sizeof(int32_t), alignof(int32_t));
    at->order = mvfxLayoutAdd(layout, room, sizeof(int32_t), alignof(int32_t));
    at->local = mvfxLayoutAdd(layout, (surface ? 3u : 15u) * room, sizeof(float), alignof(float));
    size_t masks = 0;
    for (int32_t l = 0; liquid && l < gas->levelCount; ++l)
    {
        masks += (size_t)mvfxGasLevelCells(gas, l);
    }
    at->masks = mvfxLayoutAdd(layout, masks, 1, 1);
    size_t faces = liquid ? (size_t)(gas->faces[0] + gas->faces[1] + gas->faces[2]) : 0u;
    at->valid = mvfxLayoutAdd(layout, 2u * faces, 1, 1);
}

// Points a liquid or surface gas's room into the block.
static void StartRoom(mvfxGasState* state, const mvfxCompiledGas* gas, int32_t particles,
                      unsigned char* block, const mvfxGasAt* at)
{
    if (gas->def.kind == mvfx_gasSmoke)
    {
        return;
    }
    mvfxLiquidGasRoom* room = &state->room;
    room->cellStart = (int32_t*)(block + at->cellStart);
    room->order = (int32_t*)(block + at->order);
    room->local = (float*)(block + at->local);
    room->capacity = particles;
    uint8_t* masks = block + at->masks;
    for (int32_t l = 0; l < gas->levelCount; ++l)
    {
        room->liquid[l] = masks;
        masks += mvfxGasLevelCells(gas, l);
    }
    room->valid[0] = block + at->valid;
    room->valid[1] = room->valid[0] + (gas->faces[0] + gas->faces[1] + gas->faces[2]);
}

void mvfxStartGas(mvfxGasState* state, const mvfxCompiledGas* gas, int32_t particles,
                  unsigned char* block, const mvfxGasAt* at)
{
    *state = (mvfxGasState){.rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .scale = 1.0f};
    for (int32_t k = 0; k < 3; ++k)
    {
        float* faces = (float*)(block + at->velocity[k]);
        state->velocity[k][0] = faces;
        state->velocity[k][1] = faces + gas->faces[k];
        float* cells = (float*)(block + at->scalars[k]);
        state->scalars[k][0] = cells;
        state->scalars[k][1] = cells + gas->cells;
    }
    for (int32_t l = 0; l < gas->levelCount; ++l)
    {
        float* level = (float*)(block + at->levels[l]);
        int64_t cells = mvfxGasLevelCells(gas, l);
        state->levels[l] = (mvfxGasLevel){level, level + cells, level + 2 * cells};
    }
    state->work = gas->work > 0 ? (float*)(block + at->work) : nullptr;
    for (int32_t l = 0; l < gas->levelCount; ++l)
    {
        float* areas = (float*)(block + at->areas[l]);
        float* coefficients = (float*)(block + at->coefficients[l]);
        for (int32_t k = 0; k < 3; ++k)
        {
            int64_t faces = mvfxGasLevelFaces(gas, l, k + 1) - mvfxGasLevelFaces(gas, l, k);
            state->areas[l][k] = faces > 0 ? areas : nullptr;
            state->coefficients[l][k] = faces > 0 ? coefficients : nullptr;
            areas += faces;
            coefficients += faces;
        }
        state->weights[l] = (float*)(block + at->weights[l]);
    }
    state->solid = gas->hasObstacles ? block + at->solid : nullptr;
    StartRoom(state, gas, particles, block, at);
    mvfxClearGas(state, gas);
}

void mvfxClearGas(mvfxGasState* state, const mvfxCompiledGas* gas)
{
    bool surface = gas->def.kind == mvfx_gasSurface;
    for (int32_t b = 0; b < 2; ++b)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            memset(state->velocity[k][b], 0, (size_t)gas->faces[k] * sizeof(float));
        }
        for (int32_t f = 0; f < (surface ? 1 : 3); ++f)
        {
            memset(state->scalars[f][b], 0, (size_t)gas->cells * sizeof(float));
        }
        // A surface without particles: its reach everywhere.
        for (int64_t i = 0; surface && i < gas->cells; ++i)
        {
            state->scalars[0][b][i] = mvfxSurfaceReach(gas);
        }
    }
    for (int32_t l = 0; l < gas->levelCount; ++l)
    {
        memset(state->levels[l].pressure, 0,
               3u * (size_t)mvfxGasLevelCells(gas, l) * sizeof(float));
    }
    if (state->solid != nullptr)
    {
        memset(state->solid, 0, (size_t)gas->cells);
    }
    state->obstacleCount[0] = 0;
    state->obstacleCount[1] = 0;
    state->obstaclesKnown = false;
    state->obstaclesResolved = false;
    state->current = 0;
    state->holds = false;
}
