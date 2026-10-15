// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "gas.h"

#include "maul-vfx/effect.h"

#include <math.h>
#include <string.h>

mvfxGasDef mvfxDefaultGasDef(void)
{
    return (mvfxGasDef){
        .cookie = MVFX_GAS_DEF_COOKIE,
        .size = {64, 64, 1},
        .cellSize = 0.1f,
        .space = mvfx_gasLocal,
        .substeps = 1,
        .pressureCycles = 4,
        .gravity = {0.0f, -9.81f, 0.0f},
        .gravitySpace = mvfx_gasWorld,
    };
}

// Whether a value is finite and at most limit in size.
static bool IsWithin(float value, float limit)
{
    return isfinite(value) && fabsf(value) <= limit;
}

static bool IsSourceValid(const mvfxGasSource* source)
{
    bool valid = (source->shape == mvfx_gasSphere || source->shape == mvfx_gasBox) &&
                 IsWithin(source->density, MVFX_MAX_EXTENT) &&
                 IsWithin(source->temperature, MVFX_MAX_EXTENT) &&
                 IsWithin(source->fuel, MVFX_MAX_EXTENT);
    for (int32_t k = 0; valid && k < 3; ++k)
    {
        valid = IsWithin(source->center[k], MVFX_MAX_EXTENT) &&
                IsWithin(source->velocity[k], MVFX_MAX_EXTENT);
    }
    if (source->shape == mvfx_gasSphere)
    {
        return valid && source->radius > 0.0f && source->radius <= MVFX_MAX_EXTENT;
    }
    for (int32_t k = 0; valid && k < 3; ++k)
    {
        valid = source->halfExtents[k] > 0.0f && source->halfExtents[k] <= MVFX_MAX_EXTENT;
    }
    return valid;
}

// Whether a gas's size is in range: each axis from 1 (2 along X and Y)
// to the most, at most the most cells in all.
static bool IsSizeValid(const int32_t size[3])
{
    int64_t cells = 1;
    for (int32_t k = 0; k < 3; ++k)
    {
        if (size[k] < (k < 2 ? 2 : 1) || size[k] > MVFX_MAX_GAS_SIZE)
        {
            return false;
        }
        cells *= size[k];
    }
    return cells <= MVFX_MAX_GAS_CELLS;
}

// Whether a gas's place, advection, buoyancy, confinement and combustion
// are in range.
static bool AreForcesValid(const mvfxGasDef* def)
{
    const float weights[7] = {def->densityWeight, def->heatLift,  def->vorticity,    def->burnRate,
                              def->burnHeat,      def->burnSmoke, def->burnExpansion};
    bool valid =
        (def->advection == mvfx_gasSemiLagrangian || def->advection == mvfx_gasMacCormack) &&
        (def->gravitySpace == mvfx_gasLocal || def->gravitySpace == mvfx_gasWorld) &&
        IsWithin(def->ignitionTemperature, MVFX_MAX_EXTENT);
    for (int32_t i = 0; valid && i < 7; ++i)
    {
        valid = weights[i] >= 0.0f && weights[i] <= MVFX_MAX_EXTENT;
    }
    for (int32_t k = 0; valid && k < 3; ++k)
    {
        valid =
            IsWithin(def->center[k], MVFX_MAX_EXTENT) && IsWithin(def->gravity[k], MVFX_MAX_EXTENT);
    }
    return valid;
}

bool mvfxIsGasDefValid(const mvfxGasDef* def)
{
    if (def->cookie != MVFX_GAS_DEF_COOKIE || !IsSizeValid(def->size) ||
        !(def->cellSize > 0.0f && def->cellSize <= MVFX_MAX_EXTENT) ||
        (def->space != mvfx_gasLocal && def->space != mvfx_gasWorld) || def->substeps < 1 ||
        def->substeps > MVFX_MAX_GAS_SUBSTEPS || def->sourceCount < 0 ||
        def->sourceCount > MVFX_MAX_GAS_SOURCES ||
        (def->sourceCount > 0 && def->sources == nullptr) || def->closedWalls > 63 ||
        def->pressureCycles < 0 || def->pressureCycles > MVFX_MAX_GAS_CYCLES)
    {
        return false;
    }
    const float decays[4] = {def->densityDecay, def->temperatureDecay, def->fuelDecay,
                             def->velocityDecay};
    for (int32_t i = 0; i < 4; ++i)
    {
        if (!(decays[i] >= 0.0f && decays[i] <= MVFX_MAX_DRAG))
        {
            return false;
        }
    }
    if (!AreForcesValid(def))
    {
        return false;
    }
    for (int32_t s = 0; s < def->sourceCount; ++s)
    {
        if (!IsSourceValid(&def->sources[s]))
        {
            return false;
        }
    }
    return true;
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

mvfxCompiledGas mvfxCompileGas(const mvfxGasDef* def)
{
    mvfxCompiledGas gas = {.def = *def};
    gas.def.sources = nullptr;
    for (int32_t s = 0; s < def->sourceCount; ++s)
    {
        gas.sources[s] = def->sources[s];
    }
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
    return gas;
}

void mvfxAddGasLayout(mvfxLayout* layout, const mvfxCompiledGas* gas, mvfxGasAt* at)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        at->velocity[k] =
            mvfxLayoutAdd(layout, 2u * (size_t)gas->faces[k], sizeof(float), alignof(float));
        at->scalars[k] =
            mvfxLayoutAdd(layout, 2u * (size_t)gas->cells, sizeof(float), alignof(float));
    }
    for (int32_t l = 0; l < gas->levelCount; ++l)
    {
        at->levels[l] = mvfxLayoutAdd(layout, 3u * (size_t)mvfxGasLevelCells(gas, l), sizeof(float),
                                      alignof(float));
    }
    at->work = mvfxLayoutAdd(layout, (size_t)gas->work, sizeof(float), alignof(float));
}

void mvfxStartGas(mvfxGasState* state, const mvfxCompiledGas* gas, unsigned char* block,
                  const mvfxGasAt* at)
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
    mvfxClearGas(state, gas);
}

void mvfxClearGas(mvfxGasState* state, const mvfxCompiledGas* gas)
{
    for (int32_t b = 0; b < 2; ++b)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            memset(state->velocity[k][b], 0, (size_t)gas->faces[k] * sizeof(float));
        }
        for (int32_t f = 0; f < 3; ++f)
        {
            memset(state->scalars[f][b], 0, (size_t)gas->cells * sizeof(float));
        }
    }
    for (int32_t l = 0; l < gas->levelCount; ++l)
    {
        memset(state->levels[l].pressure, 0,
               3u * (size_t)mvfxGasLevelCells(gas, l) * sizeof(float));
    }
    state->current = 0;
    state->holds = false;
}
