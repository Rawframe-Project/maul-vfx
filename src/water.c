// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "water.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/shape.h"

#include <math.h>
#include <string.h>

mvfxWaterDef mvfxDefaultWaterDef(void)
{
    return (mvfxWaterDef){
        .cookie = MVFX_WATER_DEF_COOKIE,
        .size = {64, 64},
        .cellSize = 0.1f,
        .space = mvfx_gasLocal,
        .closedWalls = 15,
        .waveSpeed = 1.0f,
        .damping = 0.5f,
    };
}

bool mvfxIsWaterDefValid(const mvfxWaterDef* def)
{
    bool valid = def->cookie == MVFX_WATER_DEF_COOKIE && def->size[0] >= 2 && def->size[1] >= 2 &&
                 def->size[0] <= MVFX_MAX_WATER_SIZE && def->size[1] <= MVFX_MAX_WATER_SIZE &&
                 def->cellSize >= MVFX_MIN_WATER_CELL && def->cellSize <= MVFX_MAX_EXTENT &&
                 (def->space == mvfx_gasLocal || def->space == mvfx_gasWorld) &&
                 def->closedWalls <= 15 && def->waveSpeed > 0.0f &&
                 def->waveSpeed <= MVFX_MAX_EXTENT && def->damping >= 0.0f &&
                 def->damping <= MVFX_MAX_DRAG;
    for (int32_t k = 0; valid && k < 3; ++k)
    {
        valid = isfinite(def->center[k]) && fabsf(def->center[k]) <= MVFX_MAX_EXTENT;
    }
    return valid;
}

mvfxCompiledWater mvfxCompileWater(const mvfxWaterDef* def)
{
    return (mvfxCompiledWater){.def = *def, .cells = (int64_t)def->size[0] * def->size[1]};
}

void mvfxAddWaterLayout(mvfxLayout* layout, const mvfxCompiledWater* water, mvfxWaterAt* at)
{
    at->heights = mvfxLayoutAdd(layout, 2u * (size_t)water->cells, sizeof(float), alignof(float));
    at->rates = mvfxLayoutAdd(layout, (size_t)water->cells, sizeof(float), alignof(float));
    at->pushes =
        mvfxLayoutAdd(layout, MVFX_MAX_WATER_PUSHES, sizeof(mvfxWaterPush), alignof(mvfxWaterPush));
}

void mvfxStartWater(mvfxWaterState* state, const mvfxCompiledWater* water, unsigned char* block,
                    const mvfxWaterAt* at)
{
    float* heights = (float*)(block + at->heights);
    *state = (mvfxWaterState){.heights = {heights, heights + water->cells},
                              .rates = (float*)(block + at->rates),
                              .pushes = (mvfxWaterPush*)(block + at->pushes),
                              .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
                              .scale = 1.0f};
    mvfxClearWater(state, water);
}

void mvfxClearWater(mvfxWaterState* state, const mvfxCompiledWater* water)
{
    memset(state->heights[0], 0, 2u * (size_t)water->cells * sizeof(float));
    memset(state->rates, 0, (size_t)water->cells * sizeof(float));
    state->current = 0;
    state->pushCount = 0;
    state->holds = false;
    state->reach = 0.0f;
}
