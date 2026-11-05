// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The tests' switches (test_hooks.h).

#include "test_hooks.h"

#include "convex.h"
#include "effect.h"
#include "gas.h"
#include "gas_liquid.h"
#include "instance.h"
#include "instance_api.h"
#include "plexus.h"
#include "water.h"

#include <string.h>

bool mvfxTestSetOwed(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, uint64_t owed)
{
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr || emitter < 0 || emitter >= live->effect->emitterCount)
    {
        return false;
    }
    live->emitters[emitter].owed = owed;
    return true;
}

bool mvfxTestTearEdges(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                       int32_t particle, uint32_t bits)
{
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr || emitter < 0 || emitter >= live->effect->emitterCount ||
        !mvfxHasAttribute(&live->effect->emitters[emitter].plan, mvfx_attributeTorn) ||
        particle < 0 || particle >= live->emitters[emitter].count)
    {
        return false;
    }
    uint32_t* torn = mvfxAttributeArray(live, emitter, mvfx_attributeTorn);
    torn[particle] = bits;
    return true;
}

int32_t mvfxTestRigidPairs(const mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                           int32_t* a, int32_t* b, int32_t capacity)
{
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr || emitter < 0 || emitter >= live->effect->emitterCount)
    {
        return -1;
    }
    const mvfxEmitterState* state = &live->emitters[emitter];
    for (int32_t c = 0; c < state->rigidPairCount && c < capacity; ++c)
    {
        a[c] = state->rigidPairs[c].a;
        b[c] = state->rigidPairs[c].b;
    }
    return state->rigidPairCount;
}

// A live instance's gas state, or NULL.
static mvfxGasState* GasOf(const mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                           const mvfxCompiledGas** compiledOut)
{
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr || gas < 0 || gas >= live->effect->gasCount)
    {
        return nullptr;
    }
    *compiledOut = &live->effect->gases[gas];
    return &live->gases[gas];
}

int64_t mvfxTestReadGasFaces(const mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                             int32_t axis, float* out, int64_t capacity)
{
    const mvfxCompiledGas* compiled = nullptr;
    const mvfxGasState* state = GasOf(context, instance, gas, &compiled);
    if (state == nullptr || axis < 0 || axis > 2 ||
        state->velocity[axis][state->current] == nullptr)
    {
        return -1;
    }
    const float* faces = state->velocity[axis][state->current];
    for (int64_t f = 0; f < compiled->faces[axis] && f < capacity; ++f)
    {
        out[f] = faces[f];
    }
    return compiled->faces[axis];
}

bool mvfxTestWriteGasFaces(mvfxContext* context, mvfxInstanceId instance, int32_t gas, int32_t axis,
                           const float* values, int64_t count)
{
    const mvfxCompiledGas* compiled = nullptr;
    mvfxGasState* state = GasOf(context, instance, gas, &compiled);
    if (state == nullptr || axis < 0 || axis > 2 ||
        state->velocity[axis][state->current] == nullptr || count != compiled->faces[axis])
    {
        return false;
    }
    float* faces = state->velocity[axis][state->current];
    for (int64_t f = 0; f < count; ++f)
    {
        faces[f] = values[f];
    }
    return true;
}

bool mvfxTestWriteGasScalar(mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                            mvfxGasField field, const float* values, int64_t count)
{
    const mvfxCompiledGas* compiled = nullptr;
    mvfxGasState* state = GasOf(context, instance, gas, &compiled);
    if (state == nullptr || field < 0 || (field > mvfx_gasFuel && field != mvfx_gasDistance) ||
        !mvfxIsGasFieldOf(compiled, field) || count != compiled->cells)
    {
        return false;
    }
    // A surface's distance is held as its first scalar.
    float* cells = state->scalars[field == mvfx_gasDistance ? 0 : field][state->current];
    if (cells == nullptr)
    {
        return false;
    }
    for (int64_t i = 0; i < count; ++i)
    {
        cells[i] = values[i];
    }
    return true;
}

int64_t mvfxTestReadGasSolid(const mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                             uint8_t* out, int64_t capacity)
{
    const mvfxCompiledGas* compiled = nullptr;
    const mvfxGasState* state = GasOf(context, instance, gas, &compiled);
    if (state == nullptr || state->solid == nullptr)
    {
        return -1;
    }
    for (int64_t i = 0; i < compiled->cells && i < capacity; ++i)
    {
        out[i] = state->solid[i];
    }
    return compiled->cells;
}

bool mvfxTestWriteWaterDepth(mvfxContext* context, mvfxInstanceId instance, int32_t water,
                             const float* values, int64_t count)
{
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr || water < 0 || water >= live->effect->waterCount)
    {
        return false;
    }
    const mvfxCompiledWater* compiled = &live->effect->waters[water];
    mvfxWaterState* state = &live->waters[water];
    if (compiled->def.model != mvfx_waterShallow || count != compiled->cells)
    {
        return false;
    }
    for (int64_t i = 0; i < count; ++i)
    {
        state->heights[state->current][i] = values[i];
    }
    return true;
}

bool mvfxTestWriteWaterFaces(mvfxContext* context, mvfxInstanceId instance, int32_t water,
                             int32_t axis, const float* values, int64_t count)
{
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr || water < 0 || water >= live->effect->waterCount || axis < 0 || axis > 1)
    {
        return false;
    }
    const mvfxCompiledWater* compiled = &live->effect->waters[water];
    mvfxWaterState* state = &live->waters[water];
    if (compiled->def.model != mvfx_waterShallow ||
        count != mvfxWaterFaces(compiled->def.size, axis))
    {
        return false;
    }
    for (int64_t i = 0; i < count; ++i)
    {
        state->faces[axis][state->faceCurrent][i] = values[i];
    }
    return true;
}

bool mvfxTestHashInstance(const mvfxContext* context, mvfxInstanceId instance, uint64_t* hash)
{
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return false;
    }
    *hash = mvfxHashInstance(live, *hash);
    return true;
}

// A live instance's emitter's state and its array, or NULL.
static uint32_t* ArrayAt(const mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                         int32_t array, mvfxEmitterState** stateOut)
{
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr || emitter < 0 || emitter >= live->effect->emitterCount || array < 0 ||
        array >= live->effect->emitters[emitter].plan.arrayCount)
    {
        return nullptr;
    }
    mvfxEmitterState* state = &live->emitters[emitter];
    *stateOut = state;
    return (uint32_t*)(state->particles + (size_t)array * state->arrayBytes);
}

int32_t mvfxTestReadParticles(const mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                              int32_t array, uint32_t* out, int32_t capacity)
{
    mvfxEmitterState* state = nullptr;
    const uint32_t* words = ArrayAt(context, instance, emitter, array, &state);
    if (words == nullptr)
    {
        return -1;
    }
    for (int32_t i = 0; i < state->count && i < capacity; ++i)
    {
        out[i] = words[i];
    }
    return state->count;
}

bool mvfxTestAdoptParticles(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                            int32_t array, const uint32_t* words, int32_t count)
{
    mvfxEmitterState* state = nullptr;
    uint32_t* to = ArrayAt(context, instance, emitter, array, &state);
    if (to == nullptr || count != state->count)
    {
        return false;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        to[i] = words[i];
    }
    return true;
}

int64_t mvfxTestRibbonCounter(const mvfxContext* context, mvfxInstanceId instance, int32_t emitter)
{
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr || emitter < 0 || emitter >= live->effect->emitterCount)
    {
        return -1;
    }
    return live->emitters[emitter].ribbonCounter;
}

int32_t mvfxTestFindLinks(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                          int32_t* links, int32_t capacity, uint64_t* dropped)
{
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr || emitter < 0 || emitter >= live->effect->emitterCount ||
        !live->effect->emitters[emitter].plexus.enable)
    {
        return -1;
    }
    mvfxEmitterState* state = &live->emitters[emitter];
    uint64_t kept = state->linksDropped;
    state->linksDropped = 0;
    mvfxFindLinks(live, emitter);
    *dropped = state->linksDropped;
    state->linksDropped = kept;
    for (int32_t l = 0; l < 2 * state->plexus.linkCount && l < 2 * capacity; ++l)
    {
        links[l] = state->plexus.links[l];
    }
    return state->plexus.linkCount;
}

bool mvfxTestWaterReach(const mvfxContext* context, mvfxInstanceId instance, int32_t water,
                        float* reach)
{
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr || water < 0 || water >= live->effect->waterCount)
    {
        return false;
    }
    *reach = live->waters[water].reach;
    return true;
}

int32_t mvfxTestLiquidSubsteps(const mvfxContext* context, mvfxInstanceId instance, int64_t ticks)
{
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    return live != nullptr ? mvfxLiquidSubsteps(live, ticks) : -1;
}

static_assert(MVFX_TEST_EPA_POINTS == MVFX_EPA_POINTS && MVFX_TEST_EPA_FACES == MVFX_EPA_FACES,
              "the hooks' EPA sizes are convex.h's");

int32_t mvfxTestEpaPolytope(const float* a, int32_t countA, const float* b, int32_t countB,
                            int32_t most, double points[MVFX_TEST_EPA_POINTS][3],
                            int32_t* pointCount, uint8_t faces[MVFX_TEST_EPA_FACES][3])
{
    *pointCount = 0;
    if (countA < 1 || countA > MVFX_MAX_HULL_VERTICES || countB < 1 ||
        countB > MVFX_MAX_HULL_VERTICES)
    {
        return 0;
    }
    mvfxPolytope pa = {.vertexCount = countA};
    mvfxPolytope pb = {.vertexCount = countB};
    memcpy(pa.vertices, a, (size_t)countA * 3 * sizeof(float));
    memcpy(pb.vertices, b, (size_t)countB * 3 * sizeof(float));
    return mvfxEpaPolytope(&pa, &pb, most, points, pointCount, faces);
}

bool mvfxTestConvexMeet(const float* a, int32_t countA, const float* b, int32_t countB,
                        float margin, bool* stopped, int32_t* epaSteps, float* distance,
                        float normal[3])
{
    *stopped = false;
    *epaSteps = 0;
    *distance = 0.0f;
    memset(normal, 0, 3 * sizeof(float));
    if (countA < 1 || countA > MVFX_MAX_HULL_VERTICES || countB < 1 ||
        countB > MVFX_MAX_HULL_VERTICES)
    {
        return false;
    }
    mvfxPolytope pa = {.vertexCount = countA};
    mvfxPolytope pb = {.vertexCount = countB};
    memcpy(pa.vertices, a, (size_t)countA * 3 * sizeof(float));
    memcpy(pb.vertices, b, (size_t)countB * 3 * sizeof(float));
    mvfxConvexMeet meet;
    mvfxConvexSteps steps;
    if (!mvfxConvexMeetsWithin(&pa, &pb, margin, MVFX_CONVEX_STEPS, &meet, &steps))
    {
        return false;
    }
    *stopped = meet.capped;
    *epaSteps = steps.epa;
    *distance = meet.distance;
    memcpy(normal, meet.normal, 3 * sizeof(float));
    return true;
}
