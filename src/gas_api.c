// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gases as the host reads them.

#include "gas_api.h"

#include "context.h"
#include "direction.h"
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

// Where a gas's box is now: its center and orientation in the world,
// and its scale (the world's lengths in its).
static void Place(const mvfxInstance* live, int32_t gas, double center[3], float rotation[4],
                  float* scaleOut)
{
    const mvfxCompiledGas* compiled = &live->effect->gases[gas];
    const mvfxGasState* state = &live->gases[gas];
    // A local gas's box is where the instance is now.
    bool local = compiled->def.space == mvfx_gasLocal;
    const double* origin = local ? live->to.origin : state->origin;
    const float* turn = local ? live->to.rotation : state->rotation;
    float scale = local ? live->to.scale : state->scale;
    const float offset[3] = {scale * compiled->def.center[0], scale * compiled->def.center[1],
                             scale * compiled->def.center[2]};
    float turned[3];
    mvfxRotate(turn, offset, turned);
    for (int32_t k = 0; k < 3; ++k)
    {
        center[k] = origin[k] + (double)turned[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        rotation[k] = turn[k];
    }
    *scaleOut = scale;
}

mvfxBounds mvfxGasBounds(const mvfxInstance* instance, int32_t gas)
{
    double center[3];
    float rotation[4];
    float scale = 0.0f;
    Place(instance, gas, center, rotation, &scale);
    float cell = scale * instance->effect->gases[gas].def.cellSize;
    mvfxBounds out = {.empty = false};
    // Each world axis's reach of the box's turned half sizes.
    float reach[3] = {0.0f, 0.0f, 0.0f};
    for (int32_t a = 0; a < 3; ++a)
    {
        float half[3] = {0.0f, 0.0f, 0.0f};
        half[a] = 0.5f * cell * (float)instance->effect->gases[gas].def.size[a];
        float turned[3];
        mvfxRotate(rotation, half, turned);
        for (int32_t k = 0; k < 3; ++k)
        {
            reach[k] += fabsf(turned[k]);
        }
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        out.min[k] = center[k] - (double)reach[k];
        out.max[k] = center[k] + (double)reach[k];
    }
    return out;
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
    double center[3];
    float rotation[4];
    float scale = 0.0f;
    Place(instance, gas, center, rotation, &scale);
    const float back[4] = {-rotation[0], -rotation[1], -rotation[2], rotation[3]};
    float turned[3];
    mvfxRotate(back, def->gravity, turned);
    // The box's lengths are the world's over its scale.
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = turned[k] / scale;
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
    double center[3];
    float rotation[4];
    float scale = 0.0f;
    Place(instance, gas, center, rotation, &scale);
    const float back[4] = {-rotation[0], -rotation[1], -rotation[2], rotation[3]};
    mvfxSpaceMap map = {.sigma = 1.0f / scale};
    if (space == mvfx_spaceWorld)
    {
        // Moved to the box's center, turned back, over its scale.
        for (int32_t k = 0; k < 4; ++k)
        {
            map.rotation[k] = back[k];
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            map.before[k] = center[k];
        }
        return map;
    }
    // From the instance's frame: scaled and turned into the world, then
    // turned back into the box's, the instance's origin's offset from the
    // box's center added in binary64.
    const mvfxTransform* now = &instance->to;
    mvfxMultiplyRotations(back, now->rotation, map.rotation);
    map.sigma = now->scale / scale;
    double offset[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        offset[k] = (now->origin[k] - center[k]) / (double)scale;
    }
    mvfxRotateDouble(back, offset, map.after);
    return map;
}

mvfxSpaceMap mvfxGasToSpace(const mvfxInstance* instance, int32_t gas,
                            const mvfxSpaceMap* fromWorld)
{
    double center[3];
    float rotation[4];
    float scale = 0.0f;
    Place(instance, gas, center, rotation, &scale);
    // A box point x is the world's C + R s x, which fromWorld takes to
    // turn(q, sigma (C + R s x - before)) + after: turn(q R, sigma s (x -
    // before')) + after, before' = -R^-1 (C - before) / s.
    mvfxSpaceMap map = {.sigma = fromWorld->sigma * scale};
    mvfxMultiplyRotations(fromWorld->rotation, rotation, map.rotation);
    const float back[4] = {-rotation[0], -rotation[1], -rotation[2], rotation[3]};
    double d[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        d[k] = (center[k] - fromWorld->before[k]) / (double)scale;
        map.after[k] = fromWorld->after[k];
    }
    double turned[3];
    mvfxRotateDouble(back, d, turned);
    for (int32_t k = 0; k < 3; ++k)
    {
        map.before[k] = -turned[k];
    }
    return map;
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

mvfxResult mvfxInstance_GetGasInfo(const mvfxContext* context, mvfxInstanceId instance, int32_t gas,
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
    float scale = 0.0f;
    Place(live, gas, infoOut->center, infoOut->rotation, &scale);
    infoOut->cellSize = scale * live->effect->gases[gas].def.cellSize;
    for (int32_t k = 0; k < 3; ++k)
    {
        infoOut->size[k] = live->effect->gases[gas].def.size[k];
    }
    return mvfx_success;
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

mvfxResult mvfxInstance_ReadGas(const mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                                mvfxGasField field, float* out, int64_t count)
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
