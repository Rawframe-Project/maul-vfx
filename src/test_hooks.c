// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The tests' switches (test_hooks.h).

#include "test_hooks.h"

#include "effect.h"
#include "gas.h"
#include "instance.h"
#include "instance_api.h"

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
    if (state == nullptr || (field > mvfx_gasFuel && field != mvfx_gasDistance) ||
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
