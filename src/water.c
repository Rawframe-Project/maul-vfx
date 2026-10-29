// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "water.h"

#include "collide.h"
#include "refusal.h"

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
        .gravity = 9.81f,
        .terrain = {.kind = mvfx_colliderHeightfield, .rotation = {0.0f, 0.0f, 0.0f, 1.0f}},
    };
}

// Whether a shallow surface's source is in range.
static mvfxResult CheckSource(const mvfxWaterSource* source)
{
    if (source->shape != mvfx_gasSphere && source->shape != mvfx_gasBox)
    {
        return mvfx_errorInvalid;
    }
    mvfxResult valid = mvfxFirst(mvfxPositiveUpTo(source->rate, MVFX_MAX_EXTENT),
                                 mvfxBoundedVector(source->center, MVFX_MAX_EXTENT));
    if (valid != mvfx_success)
    {
        return valid;
    }
    if (source->shape == mvfx_gasSphere)
    {
        return mvfxPositiveUpTo(source->radius, MVFX_MAX_EXTENT);
    }
    return mvfxFirst(mvfxPositiveUpTo(source->halfExtents[0], MVFX_MAX_EXTENT),
                     mvfxPositiveUpTo(source->halfExtents[2], MVFX_MAX_EXTENT));
}

// Whether a def's shallow water fields are in range.
static mvfxResult CheckShallow(const mvfxWaterDef* def)
{
    if (def->model > mvfx_waterShallow || def->sourceCount < 0 ||
        (def->sourceCount > 0 && def->sources == nullptr) ||
        (def->hasTerrain &&
         (def->terrain.kind != mvfx_colliderHeightfield || def->terrain.space > 1)))
    {
        return mvfx_errorInvalid;
    }
    // The count bounds the sources read below.
    mvfxResult valid =
        mvfxFirst(mvfxFirst(mvfxPositiveUpTo(def->gravity, MVFX_MAX_EXTENT),
                            mvfxUpToBound(def->friction, 0.0f, MVFX_MAX_DRAG)),
                  mvfxFirst(mvfxBounded(def->fillLevel, -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT),
                            mvfxWithinBound(def->sourceCount <= MVFX_MAX_WATER_SOURCES)));
    if (valid == mvfx_success && def->hasTerrain)
    {
        valid = mvfxCheckCollider(&def->terrain);
    }
    for (int32_t s = 0; valid == mvfx_success && s < def->sourceCount; ++s)
    {
        valid = CheckSource(&def->sources[s]);
    }
    return valid;
}

mvfxResult mvfxCheckWaterDef(const mvfxWaterDef* def)
{
    if (def->cookie != MVFX_WATER_DEF_COOKIE || def->size[0] < 2 || def->size[1] < 2 ||
        (def->space != mvfx_gasLocal && def->space != mvfx_gasWorld) || def->closedWalls > 15 ||
        !(def->waveSpeed > 0.0f))
    {
        return mvfx_errorInvalid;
    }
    const mvfxResult fields[] = {
        mvfxWithinBound(def->size[0] <= MVFX_MAX_WATER_SIZE && def->size[1] <= MVFX_MAX_WATER_SIZE),
        mvfxBounded(def->cellSize, MVFX_MIN_WATER_CELL, MVFX_MAX_EXTENT),
        mvfxBounded(def->waveSpeed, 0.0f, MVFX_MAX_EXTENT),
        mvfxUpToBound(def->damping, 0.0f, MVFX_MAX_DRAG),
        mvfxBoundedVector(def->center, MVFX_MAX_EXTENT),
    };
    for (size_t k = 0; k < sizeof(fields) / sizeof(fields[0]); ++k)
    {
        if (fields[k] != mvfx_success)
        {
            return fields[k];
        }
    }
    return CheckShallow(def);
}

mvfxResult mvfxCheckWaterCoupling(const mvfxWaterCoupling* coupling, const mvfxWaterDef* waters,
                                  int32_t count)
{
    if (!coupling->enabled)
    {
        return mvfx_success;
    }
    if (coupling->water < 0 || coupling->water >= count)
    {
        return mvfx_errorInvalid;
    }
    bool shallow = waters[coupling->water].model == mvfx_waterShallow;
    if (coupling->pour && !shallow)
    {
        return mvfx_errorInvalid;
    }
    return mvfxFirst(mvfxFirst(mvfxPositiveUpTo(coupling->radius, MVFX_MAX_EXTENT),
                               mvfxUpToBound(coupling->push, 0.0f, MVFX_MAX_EXTENT)),
                     mvfxFirst(mvfxUpToBound(coupling->buoyancy, 0.0f, MVFX_MAX_BUOYANCY),
                               mvfxUpToBound(coupling->drag, 0.0f, MVFX_MAX_DRAG)));
}

mvfxCompiledWater mvfxCompileWater(const mvfxWaterDef* def)
{
    mvfxCompiledWater water = {.def = *def, .cells = (int64_t)def->size[0] * def->size[1]};
    for (int32_t s = 0; s < def->sourceCount; ++s)
    {
        water.sources[s] = def->sources[s];
    }
    water.def.sources = nullptr;
    water.terrain = def->hasTerrain ? mvfxCompileCollider(&def->terrain) : def->terrain;
    return water;
}

void mvfxAddWaterLayout(mvfxLayout* layout, const mvfxCompiledWater* water, int64_t crossings,
                        mvfxWaterAt* at)
{
    at->heights = mvfxLayoutAdd(layout, 2u * (size_t)water->cells, sizeof(float), alignof(float));
    at->rates = mvfxLayoutAdd(layout, (size_t)water->cells, sizeof(float), alignof(float));
    at->pushes =
        mvfxLayoutAdd(layout, MVFX_MAX_WATER_PUSHES, sizeof(mvfxWaterPush), alignof(mvfxWaterPush));
    // Shallow water's faces (two buffers of each axis's), scale, ground and
    // rows' held faces.
    bool shallow = water->def.model == mvfx_waterShallow;
    size_t cells = shallow ? (size_t)water->cells : 0;
    size_t rows = shallow ? (size_t)water->def.size[1] : 0;
    size_t faces =
        shallow ? (size_t)(mvfxWaterFaces(water->def.size, 0) + mvfxWaterFaces(water->def.size, 1))
                : 0;
    at->faces = mvfxLayoutAdd(layout, 2u * faces, sizeof(float), alignof(float));
    at->giving = mvfxLayoutAdd(layout, cells, sizeof(float), alignof(float));
    at->ground = mvfxLayoutAdd(layout, cells, sizeof(float), alignof(float));
    at->heldRows = mvfxLayoutAdd(layout, rows, sizeof(int32_t), alignof(int32_t));
    at->crossings = mvfxLayoutAdd(layout, (size_t)crossings, sizeof(mvfxWaterCrossing),
                                  alignof(mvfxWaterCrossing));
}

void mvfxStartWater(mvfxWaterState* state, const mvfxCompiledWater* water, int64_t crossings,
                    unsigned char* block, const mvfxWaterAt* at)
{
    float* heights = (float*)(block + at->heights);
    bool shallow = water->def.model == mvfx_waterShallow;
    *state = (mvfxWaterState){.heights = {heights, heights + water->cells},
                              .rates = (float*)(block + at->rates),
                              .giving = shallow ? (float*)(block + at->giving) : nullptr,
                              .ground = shallow ? (float*)(block + at->ground) : nullptr,
                              .heldRows = shallow ? (int32_t*)(block + at->heldRows) : nullptr,
                              .pushes = (mvfxWaterPush*)(block + at->pushes),
                              .crossings = (mvfxWaterCrossing*)(block + at->crossings),
                              .crossingCapacity = crossings,
                              .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
                              .scale = 1.0f};
    // Each buffer holds both axes' faces, X's first.
    float* faces = shallow ? (float*)(block + at->faces) : nullptr;
    int64_t along = mvfxWaterFaces(water->def.size, 0);
    int64_t both = along + mvfxWaterFaces(water->def.size, 1);
    for (int32_t b = 0; shallow && b < 2; ++b)
    {
        state->faces[0][b] = faces + b * both;
        state->faces[1][b] = faces + b * both + along;
    }
    mvfxClearWater(state, water);
}

void mvfxWaterSpan(float center, float reach, int32_t n, int32_t span[2])
{
    span[0] = (int32_t)fminf(fmaxf(ceilf(center - reach), 0.0f), (float)n);
    span[1] = (int32_t)fminf(fmaxf(floorf(center + reach), -1.0f), (float)(n - 1));
}

void mvfxClearWater(mvfxWaterState* state, const mvfxCompiledWater* water)
{
    memset(state->heights[0], 0, 2u * (size_t)water->cells * sizeof(float));
    memset(state->rates, 0, (size_t)water->cells * sizeof(float));
    if (state->ground != nullptr)
    {
        int64_t faces = mvfxWaterFaces(water->def.size, 0) + mvfxWaterFaces(water->def.size, 1);
        memset(state->faces[0][0], 0, 2u * (size_t)faces * sizeof(float));
        memset(state->giving, 0, (size_t)water->cells * sizeof(float));
        memset(state->ground, 0, (size_t)water->cells * sizeof(float));
    }
    state->faceCurrent = 0;
    state->filled = false;
    state->current = 0;
    state->pushCount = 0;
    state->crossingCount = 0;
    state->holds = false;
    state->reach = 0.0f;
    state->stepSpeed = 0.0f;
    state->reportedSpeed = 0.0f;
    state->reportedTick = -1;
    state->countedTick = -1;
    state->planPushCount = 0;
    state->stepsSlowed = 0;
    state->facesHeld = 0;
    state->volumePoured = 0.0;
}
