// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "water.h"

#include "collide.h"
#include "defaults.h"
#include "refusal.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/shape.h"

#include <math.h>
#include <string.h>

mvfxWaterDef mvfxDefaultWaterDef(void)
{
    return (mvfxWaterDef)MVFX_DEFAULT_WATER_DEF;
}

// Whether a shallow surface's source is in range.
static mvfxResult CheckSource(const mvfxFieldPath* at, const mvfxWaterSource* source)
{
    if (source->shape != mvfx_gasSphere && source->shape != mvfx_gasBox)
    {
        return mvfxRefuseField(at, MVFX_TAG_WATER_SOURCE_SHAPE, mvfx_errorInvalid);
    }
    mvfxResult valid = mvfxRefuseField(at, MVFX_TAG_WATER_SOURCE_RATE,
                                       mvfxPositiveUpTo(source->rate, MVFX_MAX_EXTENT));
    valid = valid != mvfx_success ? valid
                                  : mvfxRefuseVector(at, MVFX_TAG_WATER_SOURCE_CENTER,
                                                     source->center, MVFX_MAX_EXTENT);
    if (valid != mvfx_success)
    {
        return valid;
    }
    if (source->shape == mvfx_gasSphere)
    {
        return mvfxRefuseField(at, MVFX_TAG_WATER_SOURCE_RADIUS,
                               mvfxPositiveUpTo(source->radius, MVFX_MAX_EXTENT));
    }
    // A box's height is the column's: no half extent along Y.
    valid = mvfxRefuseItem(at, MVFX_TAG_WATER_SOURCE_HALF_EXTENTS, 0,
                           mvfxPositiveUpTo(source->halfExtents[0], MVFX_MAX_EXTENT));
    return valid != mvfx_success
               ? valid
               : mvfxRefuseItem(at, MVFX_TAG_WATER_SOURCE_HALF_EXTENTS, 2,
                                mvfxPositiveUpTo(source->halfExtents[2], MVFX_MAX_EXTENT));
}

// Whether a def's shallow water fields are in range.
static mvfxResult CheckShallow(const mvfxFieldPath* at, const mvfxWaterDef* def)
{
    mvfxFieldPath terrain = mvfxFieldIn(at, MVFX_TAG_WATER_DEF_TERRAIN, -1);
    if (!mvfxIsKind(def->model, mvfx_waterShallow))
    {
        return mvfxRefuseField(at, MVFX_TAG_WATER_DEF_MODEL, mvfx_errorInvalid);
    }
    if (def->sourceCount < 0 || (def->sourceCount > 0 && def->sources == nullptr))
    {
        return mvfxRefuseField(at, MVFX_TAG_WATER_DEF_SOURCES, mvfx_errorInvalid);
    }
    if (def->enableTerrain && def->terrain.kind != mvfx_colliderHeightfield)
    {
        return mvfxRefuseField(&terrain, MVFX_TAG_COLLIDER_KIND, mvfx_errorInvalid);
    }
    if (def->enableTerrain && !mvfxIsKind(def->terrain.space, mvfx_spaceLocal))
    {
        return mvfxRefuseField(&terrain, MVFX_TAG_COLLIDER_SPACE, mvfx_errorInvalid);
    }
    // The count bounds the sources read below.
    mvfxResult valid = mvfxRefuseField(at, MVFX_TAG_WATER_DEF_GRAVITY,
                                       mvfxPositiveUpTo(def->gravity, MVFX_MAX_EXTENT));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_WATER_DEF_FRICTION,
                                  mvfxUpToBound(def->friction, 0.0f, MVFX_MAX_DRAG));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_WATER_DEF_FILL_LEVEL,
                                  mvfxBounded(def->fillLevel, -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_WATER_DEF_SOURCES,
                                  mvfxWithinBound(def->sourceCount <= MVFX_MAX_WATER_SOURCES));
    if (valid == mvfx_success && def->enableTerrain)
    {
        valid = mvfxCheckCollider(&terrain, &def->terrain);
    }
    for (int32_t s = 0; valid == mvfx_success && s < def->sourceCount; ++s)
    {
        mvfxFieldPath source = mvfxFieldIn(at, MVFX_TAG_WATER_DEF_SOURCES, s);
        valid = CheckSource(&source, &def->sources[s]);
    }
    return valid;
}

mvfxResult mvfxCheckWaterDef(const mvfxFieldPath* at, const mvfxWaterDef* def)
{
    if (def->cookie != MVFX_WATER_DEF_COOKIE)
    {
        return mvfxRefuseHere(at, mvfx_errorInvalid);
    }
    for (int32_t k = 0; k < 2; ++k)
    {
        if (def->size[k] < 2)
        {
            return mvfxRefuseItem(at, MVFX_TAG_WATER_DEF_SIZE, k, mvfx_errorInvalid);
        }
    }
    uint32_t choice = def->space != mvfx_gasLocal && def->space != mvfx_gasWorld
                          ? MVFX_TAG_WATER_DEF_SPACE
                      : def->closedWalls > 15    ? MVFX_TAG_WATER_DEF_CLOSED_WALLS
                      : !(def->waveSpeed > 0.0f) ? MVFX_TAG_WATER_DEF_WAVE_SPEED
                                                 : 0;
    if (choice != 0)
    {
        return mvfxRefuseField(at, choice, mvfx_errorInvalid);
    }
    for (int32_t k = 0; k < 2; ++k)
    {
        if (def->size[k] > MVFX_MAX_WATER_SIZE)
        {
            return mvfxRefuseItem(at, MVFX_TAG_WATER_DEF_SIZE, k, mvfx_errorLimit);
        }
    }
    mvfxResult valid =
        mvfxRefuseField(at, MVFX_TAG_WATER_DEF_CELL_SIZE,
                        mvfxBounded(def->cellSize, MVFX_MIN_WATER_CELL, MVFX_MAX_EXTENT));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_WATER_DEF_WAVE_SPEED,
                                  mvfxBounded(def->waveSpeed, 0.0f, MVFX_MAX_EXTENT));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_WATER_DEF_DAMPING,
                                  mvfxUpToBound(def->damping, 0.0f, MVFX_MAX_DRAG));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseVector(at, MVFX_TAG_WATER_DEF_CENTER, def->center, MVFX_MAX_EXTENT);
    return valid != mvfx_success ? valid : CheckShallow(at, def);
}

mvfxResult mvfxCheckWaterCoupling(const mvfxFieldPath* at, const mvfxWaterCoupling* coupling,
                                  const mvfxWaterDef* waters, int32_t count)
{
    if (!coupling->enable)
    {
        return mvfx_success;
    }
    if (coupling->water < 0 || coupling->water >= count)
    {
        return mvfxRefuseField(at, MVFX_TAG_WATER_COUPLING_WATER, mvfx_errorInvalid);
    }
    bool shallow = waters[coupling->water].model == mvfx_waterShallow;
    if (coupling->pour && !shallow)
    {
        return mvfxRefuseField(at, MVFX_TAG_WATER_COUPLING_POUR, mvfx_errorInvalid);
    }
    mvfxResult valid = mvfxRefuseField(at, MVFX_TAG_WATER_COUPLING_RADIUS,
                                       mvfxPositiveUpTo(coupling->radius, MVFX_MAX_EXTENT));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_WATER_COUPLING_PUSH,
                                  mvfxUpToBound(coupling->push, 0.0f, MVFX_MAX_EXTENT));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_WATER_COUPLING_BUOYANCY,
                                  mvfxUpToBound(coupling->buoyancy, 0.0f, MVFX_MAX_BUOYANCY));
    return valid != mvfx_success
               ? valid
               : mvfxRefuseField(at, MVFX_TAG_WATER_COUPLING_DRAG,
                                 mvfxUpToBound(coupling->drag, 0.0f, MVFX_MAX_DRAG));
}

mvfxCompiledWater mvfxCompileWater(const mvfxWaterDef* def)
{
    mvfxCompiledWater water = {.def = *def, .cells = (int64_t)def->size[0] * def->size[1]};
    for (int32_t s = 0; s < def->sourceCount; ++s)
    {
        water.sources[s] = def->sources[s];
    }
    water.def.sources = nullptr;
    water.terrain = def->enableTerrain ? mvfxCompileCollider(&def->terrain) : def->terrain;
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
