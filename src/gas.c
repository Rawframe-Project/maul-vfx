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

bool mvfxIsGasDefValid(const mvfxGasDef* def)
{
    if (def->cookie != MVFX_GAS_DEF_COOKIE || !IsSizeValid(def->size) ||
        !(def->cellSize > 0.0f && def->cellSize <= MVFX_MAX_EXTENT) ||
        (def->space != mvfx_gasLocal && def->space != mvfx_gasWorld) || def->substeps < 1 ||
        def->substeps > MVFX_MAX_GAS_SUBSTEPS || def->sourceCount < 0 ||
        def->sourceCount > MVFX_MAX_GAS_SOURCES ||
        (def->sourceCount > 0 && def->sources == nullptr))
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
    for (int32_t k = 0; k < 3; ++k)
    {
        if (!IsWithin(def->center[k], MVFX_MAX_EXTENT))
        {
            return false;
        }
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
    }
    return gas;
}

void mvfxAddGasLayout(mvfxLayout* layout, const mvfxCompiledGas* gas, size_t at[6])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        at[k] = mvfxLayoutAdd(layout, 2u * (size_t)gas->faces[k], sizeof(float), alignof(float));
    }
    for (int32_t f = 0; f < 3; ++f)
    {
        at[3 + f] = mvfxLayoutAdd(layout, 2u * (size_t)gas->cells, sizeof(float), alignof(float));
    }
}

void mvfxStartGas(mvfxGasState* state, const mvfxCompiledGas* gas, unsigned char* block,
                  const size_t at[6])
{
    *state = (mvfxGasState){.rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .scale = 1.0f};
    for (int32_t k = 0; k < 3; ++k)
    {
        float* faces = (float*)(block + at[k]);
        state->velocity[k][0] = faces;
        state->velocity[k][1] = faces + gas->faces[k];
    }
    for (int32_t f = 0; f < 3; ++f)
    {
        float* cells = (float*)(block + at[3 + f]);
        state->scalars[f][0] = cells;
        state->scalars[f][1] = cells + gas->cells;
    }
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
    state->current = 0;
    state->holds = false;
}
