// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gases as the host reads them.

#include "gas_api.h"

#include "box.h"
#include "context.h"
#include "direction.h"
#include "fpenv.h"
#include "gas.h"
#include "instance.h"
#include "instance_api.h"

#include "maul-vfx/instance.h"

#include <math.h>

// The live instance and its gas's index, or NULL with the reason: a NULL
// context or a gas out of range (misuse), or a stale id.
const mvfxInstance* mvfxFindGas(const mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                                mvfxResult* result)
{
    *result = mvfx_errorInvalid;
    if (context == nullptr)
    {
        return nullptr;
    }
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        *result = mvfx_errorStale;
        return nullptr;
    }
    if (gas < 0 || gas >= live->effect->gasCount)
    {
        mvfxCountMisuse((mvfxContext*)context);
        return nullptr;
    }
    *result = mvfx_success;
    return live;
}

// Where a gas's box is now.
// Where gas gas's box is with the instance at a transform.
static mvfxBoxPlace PlaceAt(const mvfxInstance* live, int32_t gas, const mvfxTransform* at)
{
    const mvfxCompiledGas* compiled = &live->effect->gases[gas];
    const mvfxGasState* state = &live->gases[gas];
    return mvfxPlaceBox(at, compiled->def.space == mvfx_gasLocal, compiled->def.center,
                        state->origin, state->rotation, state->scale);
}

static mvfxBoxPlace Place(const mvfxInstance* live, int32_t gas)
{
    return PlaceAt(live, gas, &live->to);
}

mvfxBounds mvfxGasBounds(const mvfxInstance* instance, int32_t gas)
{
    mvfxBoxPlace place = Place(instance, gas);
    float cell = place.scale * instance->effect->gases[gas].def.cellSize;
    float half[3];
    for (int32_t a = 0; a < 3; ++a)
    {
        half[a] = 0.5f * cell * (float)instance->effect->gases[gas].def.size[a];
    }
    return mvfxBoxBounds(&place, half);
}

void mvfxGasGravity(const mvfxInstance* instance, int32_t gas, float out[3])
{
    const mvfxGasDef* def = &instance->effect->gases[gas].def;
    if (def->gravitySpace == mvfx_gasLocal)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            out[k] = def->gravity[k];
        }
        return;
    }
    mvfxBoxPlace place = Place(instance, gas);
    const float back[4] = {-place.rotation[0], -place.rotation[1], -place.rotation[2],
                           place.rotation[3]};
    float turned[3];
    mvfxRotate(back, def->gravity, turned);
    // The box's lengths are the world's over its scale.
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = turned[k] / place.scale;
    }
}

mvfxSpaceMap mvfxGasParticleMap(const mvfxInstance* instance, int32_t gas, int32_t emitter)
{
    bool world = instance->effect->emitters[emitter].space == mvfx_spaceWorld;
    mvfxSpaceMap map = mvfxGasMap(instance, gas, world ? mvfx_spaceWorld : mvfx_spaceLocal);
    for (int32_t k = 0; k < 3; ++k)
    {
        map.before[k] -= world ? instance->emitters[emitter].anchor[k] : 0.0;
    }
    return map;
}

mvfxSpaceMap mvfxGasMap(const mvfxInstance* instance, int32_t gas, mvfxSimulationSpace space)
{
    mvfxBoxPlace place = Place(instance, gas);
    return mvfxBoxMap(instance, &place, space);
}

mvfxSpaceMap mvfxGasToSpace(const mvfxInstance* instance, int32_t gas,
                            const mvfxSpaceMap* fromWorld)
{
    // Where the gas last stepped: with the instance where the step starts.
    mvfxBoxPlace place = PlaceAt(instance, gas, &instance->from);
    return mvfxBoxToSpace(&place, fromWorld);
}

void mvfxGasFollow(const mvfxInstance* instance, int32_t gas, mvfxResolvedForce* force)
{
    const mvfxCompiledGas* compiled = &instance->effect->gases[gas];
    const mvfxGasState* state = &instance->gases[gas];
    force->gasLattice = (mvfxGasLattice){.cell = compiled->def.cellSize};
    for (int32_t k = 0; k < 3; ++k)
    {
        force->gasLattice.n[k] = compiled->def.size[k];
        force->gasVelocity[k] = state->velocity[k][state->current];
    }
}

void mvfxResolveGasObstacles(mvfxInstance* instance, int32_t gas)
{
    const mvfxCompiledGas* compiled = &instance->effect->gases[gas];
    mvfxGasState* state = &instance->gases[gas];
    state->obstaclesKnown = state->obstaclesResolved;
    state->obstaclesResolved = true;
    state->obstaclesNow = 1 - state->obstaclesNow;
    mvfxResolvedCollider* out = state->obstacles[state->obstaclesNow];
    int32_t own = compiled->def.obstacleCount;
    int32_t hosts = compiled->def.hostObstacles ? instance->hostColliderCount : 0;
    for (int32_t c = 0; c < own + hosts; ++c)
    {
        const mvfxCollider* collider =
            c < own ? &compiled->obstacles[c] : &instance->hostColliders[c - own];
        const struct mvfxGrid* grid =
            c < own ? compiled->obstacleGrids[c] : instance->hostColliderGrids[c - own];
        mvfxSpaceMap map = mvfxGasMap(instance, gas, (mvfxSimulationSpace)collider->space);
        out[c] = mvfxResolveCollider(collider, grid, &map, 0.0f);
    }
    state->obstacleCount[state->obstaclesNow] = own + hosts;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_GetGasInfoWork(const mvfxContext* context,
                                                            mvfxInstanceId instance, int32_t gas,
                                                            mvfxGasInfo* infoOut)
{
    if (infoOut == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse((mvfxContext*)context);
        }
        return mvfx_errorInvalid;
    }
    *infoOut = (mvfxGasInfo){0};
    mvfxResult result = mvfx_success;
    const mvfxInstance* live = mvfxFindGas(context, instance, gas, &result);
    if (live == nullptr)
    {
        return result;
    }
    mvfxBoxPlace place = Place(live, gas);
    for (int32_t k = 0; k < 3; ++k)
    {
        infoOut->center[k] = place.center[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        infoOut->rotation[k] = place.rotation[k];
    }
    infoOut->cellSize = place.scale * live->effect->gases[gas].def.cellSize;
    infoOut->scale = place.scale;
    for (int32_t k = 0; k < 3; ++k)
    {
        infoOut->size[k] = live->effect->gases[gas].def.size[k];
    }
    return mvfx_success;
}

mvfxResult mvfxInstance_GetGasInfo(const mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                                   mvfxGasInfo* infoOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_GetGasInfoWork(context, instance, gas, infoOut);
    mvfxFloatEnvLeave(env);
    return result;
}

// Copies a velocity's component k at the cells' centers: the mean of the
// faces across k about each.
static void CopyVelocity(const mvfxCompiledGas* gas, const float* faces, int32_t k, float* out)
{
    const int32_t* n = gas->def.size;
    // The faces' points along each axis, and from a face to the next
    // across k.
    const int64_t m[3] = {n[0] + (k == 0 ? 1 : 0), n[1] + (k == 1 ? 1 : 0),
                          n[2] + (k == 2 ? 1 : 0)};
    int64_t next = k == 0 ? 1 : (k == 1 ? m[0] : m[0] * m[1]);
    int64_t i = 0;
    for (int64_t z = 0; z < n[2]; ++z)
    {
        for (int64_t y = 0; y < n[1]; ++y)
        {
            for (int64_t x = 0; x < n[0]; ++x, ++i)
            {
                int64_t at = (z * m[1] + y) * m[0] + x;
                out[i] = 0.5f * (faces[at] + faces[at + next]);
            }
        }
    }
}

static MVFX_NOINLINE mvfxResult mvfxInstance_ReadGasWork(const mvfxContext* context,
                                                         mvfxInstanceId instance, int32_t gas,
                                                         mvfxGasField field, float* out,
                                                         int64_t count)
{
    mvfxResult result = mvfx_success;
    const mvfxInstance* live = mvfxFindGas(context, instance, gas, &result);
    if (live == nullptr)
    {
        return result;
    }
    const mvfxCompiledGas* compiled = &live->effect->gases[gas];
    if (out == nullptr || !mvfxIsGasFieldOf(compiled, field) || count != compiled->cells)
    {
        mvfxCountMisuse((mvfxContext*)context);
        return mvfx_errorInvalid;
    }
    if (live->planned)
    {
        // Its fields are the simulation's.
        return mvfx_errorState;
    }
    const mvfxGasState* state = &live->gases[gas];
    if (field >= mvfx_gasVelocityX && field <= mvfx_gasVelocityZ)
    {
        int32_t k = field - mvfx_gasVelocityX;
        CopyVelocity(compiled, state->velocity[k][state->current], k, out);
        return mvfx_success;
    }
    // A surface's distance is held as its first scalar.
    const float* values = state->scalars[field == mvfx_gasDistance ? 0 : field][state->current];
    for (int64_t i = 0; i < count; ++i)
    {
        out[i] = values[i];
    }
    return mvfx_success;
}

mvfxResult mvfxInstance_ReadGas(const mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                                mvfxGasField field, float* out, int64_t count)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_ReadGasWork(context, instance, gas, field, out, count);
    mvfxFloatEnvLeave(env);
    return result;
}
