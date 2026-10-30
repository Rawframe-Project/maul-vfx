// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating, destroying and naming effects on a context.

#include "effect_api.h"

#include "allocator.h"
#include "context.h"
#include "effect.h"
#include "effect_check.h"
#include "effect_parts.h"
#include "fpenv.h"
#include "grid_api.h"
#include "hull.h"
#include "mesh.h"
#include "mesh_api.h"
#include "slots.h"

// Finds the grid of each vector field of a valid def: stale for an id
// that names no live grid, invalid for one not of three components.
static mvfxResult FindGrids(mvfxContext* context, const mvfxEffectDef* def,
                            mvfxGrid* grids[MVFX_MAX_EMITTERS][MVFX_MAX_FORCES])
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        for (int32_t f = 0; f < def->emitters[i].forceCount; ++f)
        {
            const mvfxForce* force = &def->emitters[i].forces[f];
            if (force->kind != mvfx_forceVectorField)
            {
                continue;
            }
            grids[i][f] = mvfxContextGrid(context, force->grid);
            if (grids[i][f] == nullptr)
            {
                return mvfx_errorStale;
            }
            if (grids[i][f]->components != 3)
            {
                return mvfx_errorInvalid;
            }
        }
    }
    return mvfx_success;
}

// Finds the grid of each grid collider of a valid def, as
// mvfxFindColliderGrid.
static mvfxResult FindColliderGrids(mvfxContext* context, const mvfxEffectDef* def,
                                    mvfxGrid* grids[MVFX_MAX_EMITTERS][MVFX_MAX_COLLIDERS])
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        for (int32_t c = 0; c < def->emitters[i].colliderCount; ++c)
        {
            mvfxResult found =
                mvfxFindColliderGrid(context, &def->emitters[i].colliders[c], &grids[i][c]);
            if (found != mvfx_success)
            {
                return found;
            }
        }
    }
    return mvfx_success;
}

// Finds the grid of each grid obstacle of a valid def's gases, as
// mvfxFindColliderGrid.
static mvfxResult FindObstacleGrids(mvfxContext* context, const mvfxEffectDef* def,
                                    mvfxGrid* grids[MVFX_MAX_GASES][MVFX_MAX_COLLIDERS])
{
    for (int32_t g = 0; g < def->gasCount; ++g)
    {
        for (int32_t c = 0; c < def->gases[g].obstacleCount; ++c)
        {
            mvfxResult found =
                mvfxFindColliderGrid(context, &def->gases[g].obstacles[c], &grids[g][c]);
            if (found != mvfx_success)
            {
                return found;
            }
        }
    }
    return mvfx_success;
}

// Finds the grid of each vector field on a valid def's gases: stale for
// an id that names no live grid, invalid for one not of three
// components.
static mvfxResult FindGasForceGrids(mvfxContext* context, const mvfxEffectDef* def,
                                    mvfxGrid* grids[MVFX_MAX_GASES][MVFX_MAX_FORCES])
{
    for (int32_t g = 0; g < def->gasCount; ++g)
    {
        for (int32_t f = 0; f < def->gases[g].forceCount; ++f)
        {
            const mvfxForce* force = &def->gases[g].forces[f];
            if (force->kind != mvfx_forceVectorField)
            {
                continue;
            }
            grids[g][f] = mvfxContextGrid(context, force->grid);
            if (grids[g][f] == nullptr)
            {
                return mvfx_errorStale;
            }
            if (grids[g][f]->components != 3)
            {
                return mvfx_errorInvalid;
            }
        }
    }
    return mvfx_success;
}

// Holds an effect's grids, or lets them go (by -1).
static void HoldGrids(mvfxEffect* effect, int32_t by)
{
    for (int32_t i = 0; i < effect->emitterCount; ++i)
    {
        mvfxCompiledEmitter* emitter = &effect->emitters[i];
        for (int32_t f = 0; f < emitter->forceCount; ++f)
        {
            mvfxGrid* grid = emitter->forceGrids[f];
            grid != nullptr ? (void)(grid->users += by) : (void)0;
        }
        for (int32_t c = 0; c < emitter->colliderCount; ++c)
        {
            mvfxGrid* grid = emitter->colliderGrids[c];
            grid != nullptr ? (void)(grid->users += by) : (void)0;
        }
    }
    for (int32_t g = 0; g < effect->gasCount; ++g)
    {
        for (int32_t c = 0; c < effect->gases[g].def.obstacleCount; ++c)
        {
            mvfxGrid* grid = effect->gases[g].obstacleGrids[c];
            grid != nullptr ? (void)(grid->users += by) : (void)0;
        }
        for (int32_t f = 0; f < effect->gases[g].def.forceCount; ++f)
        {
            mvfxGrid* grid = effect->gases[g].forceGrids[f];
            grid != nullptr ? (void)(grid->users += by) : (void)0;
        }
    }
    for (int32_t w = 0; w < effect->waterCount; ++w)
    {
        mvfxGrid* grid = effect->waters[w].terrainGrid;
        grid != nullptr ? (void)(grid->users += by) : (void)0;
    }
}

// The mesh each mesh shape spawns from; mvfx_errorStale for one that is
// gone, mvfx_errorInvalid for one without the elements its mode needs
// (misuse).
static mvfxResult FindMeshes(mvfxContext* context, const mvfxEffectDef* def,
                             mvfxMesh* meshes[MVFX_MAX_EMITTERS])
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        const mvfxShape* shape = &def->emitters[i].shape;
        if (shape->kind != mvfx_shapeMesh)
        {
            continue;
        }
        meshes[i] = mvfxContextMesh(context, shape->mesh);
        if (meshes[i] == nullptr)
        {
            return mvfx_errorStale;
        }
        if (!mvfxMeshHasMode(meshes[i], shape->meshMode))
        {
            return mvfx_errorInvalid;
        }
    }
    return mvfx_success;
}

// Returns the hulls built for an effect's emitters.
static void FreeHulls(mvfxContext* context, mvfxHull* hulls[MVFX_MAX_EMITTERS])
{
    for (int32_t i = 0; i < MVFX_MAX_EMITTERS; ++i)
    {
        mvfxRelease(&context->allocator, hulls[i], sizeof(mvfxHull), alignof(mvfxHull));
        hulls[i] = nullptr;
    }
}

// Builds the hull of each hull emitter's mesh; mvfx_errorStale for a mesh
// that is gone, mvfx_errorInvalid for one whose vertices span no volume
// or whose hull has too many vertices. The mesh's vertices are data a
// correct program meets (a model file's), so that is never misuse.
static mvfxResult BuildHulls(mvfxContext* context, const mvfxEffectDef* def,
                             mvfxHull* hulls[MVFX_MAX_EMITTERS])
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        const mvfxRigidDef* rigid = &def->emitters[i].rigid;
        if (!rigid->enabled || rigid->shape != mvfx_rigidHull)
        {
            continue;
        }
        const mvfxMesh* mesh = mvfxContextMesh(context, rigid->hull);
        hulls[i] = mesh != nullptr
                       ? mvfxAllocate(&context->allocator, sizeof(mvfxHull), alignof(mvfxHull))
                       : nullptr;
        mvfxResult built = mesh == nullptr       ? mvfx_errorStale
                           : hulls[i] == nullptr ? mvfx_errorCapacity
                                                 : mvfxBuildHull(mesh->positions, mesh->vertexCount,
                                                                 &context->allocator, hulls[i]);
        if (built != mvfx_success)
        {
            FreeHulls(context, hulls);
            return built;
        }
    }
    return mvfx_success;
}

// The grids an effect's def names, found.
typedef struct EffectGrids
{
    mvfxGrid* forces[MVFX_MAX_EMITTERS][MVFX_MAX_FORCES];
    mvfxGrid* colliders[MVFX_MAX_EMITTERS][MVFX_MAX_COLLIDERS];
    mvfxGrid* obstacles[MVFX_MAX_GASES][MVFX_MAX_COLLIDERS];
    mvfxGrid* gasForces[MVFX_MAX_GASES][MVFX_MAX_FORCES];
    mvfxGrid* terrains[MVFX_MAX_WATERS];
} EffectGrids;

// Finds the grid of each water surface's terrain, as
// mvfxFindColliderGrid.
static mvfxResult FindTerrainGrids(mvfxContext* context, const mvfxEffectDef* def,
                                   mvfxGrid* grids[MVFX_MAX_WATERS])
{
    for (int32_t w = 0; w < def->waterCount; ++w)
    {
        mvfxResult found = def->waters[w].hasTerrain
                               ? mvfxFindColliderGrid(context, &def->waters[w].terrain, &grids[w])
                               : mvfx_success;
        if (found != mvfx_success)
        {
            return found;
        }
    }
    return mvfx_success;
}

// Finds every grid of a valid def, as the finders above.
static mvfxResult FindEffectGrids(mvfxContext* context, const mvfxEffectDef* def,
                                  EffectGrids* grids)
{
    mvfxResult found = FindGrids(context, def, grids->forces);
    found = found == mvfx_success ? FindColliderGrids(context, def, grids->colliders) : found;
    found = found == mvfx_success ? FindObstacleGrids(context, def, grids->obstacles) : found;
    found = found == mvfx_success ? FindGasForceGrids(context, def, grids->gasForces) : found;
    return found == mvfx_success ? FindTerrainGrids(context, def, grids->terrains) : found;
}

// Gives a compiled effect the grids found for its def.
static void AttachGrids(mvfxEffect* effect, const mvfxEffectDef* def, const EffectGrids* grids)
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        for (int32_t f = 0; f < def->emitters[i].forceCount; ++f)
        {
            effect->emitters[i].forceGrids[f] = grids->forces[i][f];
        }
        for (int32_t c = 0; c < def->emitters[i].colliderCount; ++c)
        {
            effect->emitters[i].colliderGrids[c] = grids->colliders[i][c];
        }
    }
    for (int32_t g = 0; g < def->gasCount; ++g)
    {
        for (int32_t c = 0; c < def->gases[g].obstacleCount; ++c)
        {
            effect->gases[g].obstacleGrids[c] = grids->obstacles[g][c];
        }
        for (int32_t f = 0; f < def->gases[g].forceCount; ++f)
        {
            effect->gases[g].forceGrids[f] = grids->gasForces[g][f];
        }
    }
    for (int32_t w = 0; w < def->waterCount; ++w)
    {
        effect->waters[w].terrainGrid = grids->terrains[w];
    }
}

mvfxResult mvfxCreateEffectOf(mvfxContext* context, const mvfxEffectDef* def,
                              mvfxEffectId* effectOut, bool* misuseOut)
{
    *misuseOut = false;
    *effectOut = (mvfxEffectId){0};
    mvfxResult valid = mvfxCheckEffectDef(def);
    valid = valid != mvfx_success ? valid : mvfxCheckEffectParts(def);
    if (valid != mvfx_success)
    {
        // A def past a bound is data a correct program may pass: no misuse.
        *misuseOut = valid == mvfx_errorInvalid;
        return valid;
    }
    mvfxMesh* meshes[MVFX_MAX_EMITTERS] = {0};
    mvfxResult meshed = FindMeshes(context, def, meshes);
    if (meshed != mvfx_success)
    {
        *misuseOut = meshed == mvfx_errorInvalid;
        return meshed;
    }
    EffectGrids grids = {0};
    mvfxResult found = FindEffectGrids(context, def, &grids);
    if (found != mvfx_success)
    {
        *misuseOut = found == mvfx_errorInvalid;
        return found;
    }
    if (context->effects.firstFree == 0)
    {
        return mvfx_errorCapacity;
    }
    mvfxHull* hulls[MVFX_MAX_EMITTERS] = {0};
    mvfxResult built = BuildHulls(context, def, hulls);
    if (built != mvfx_success)
    {
        return built;
    }
    mvfxEffect* effect = mvfxCompileEffect(def, meshes, &context->allocator);
    if (effect != nullptr)
    {
        mvfxCompileEffectParts(def, effect);
    }
    if (effect == nullptr)
    {
        FreeHulls(context, hulls);
        return mvfx_errorCapacity;
    }
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        effect->emitters[i].hull = hulls[i];
        meshes[i] != nullptr ? (void)(meshes[i]->users += 1) : (void)0;
    }
    AttachGrids(effect, def, &grids);
    HoldGrids(effect, 1);
    uint32_t generation = 0;
    uint32_t index1 = mvfxTakeSlot(&context->effects, effect, &generation);
    *effectOut = (mvfxEffectId){index1, generation};
    return mvfx_success;
}

static MVFX_NOINLINE mvfxResult mvfxCreateEffectWork(mvfxContext* context, const mvfxEffectDef* def,
                                                     mvfxEffectId* effectOut)
{
    if (effectOut != nullptr)
    {
        *effectOut = (mvfxEffectId){0};
    }
    if (effectOut == nullptr || context == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    bool misuse = false;
    mvfxResult created = mvfxCreateEffectOf(context, def, effectOut, &misuse);
    misuse ? mvfxCountMisuse(context) : (void)0;
    return created;
}

mvfxResult mvfxCreateEffect(mvfxContext* context, const mvfxEffectDef* def, mvfxEffectId* effectOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxCreateEffectWork(context, def, effectOut);
    mvfxFloatEnvLeave(env);
    return result;
}

mvfxResult mvfxDestroyEffect(mvfxContext* context, mvfxEffectId effect)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    mvfxEffect* compiled = mvfxSlotObject(&context->effects, effect.index1, effect.generation);
    if (compiled == nullptr)
    {
        return mvfx_errorStale;
    }
    if (compiled->users > 0)
    {
        return mvfx_errorBusy;
    }
    for (int32_t i = 0; i < compiled->emitterCount; ++i)
    {
        mvfxMesh* mesh = compiled->emitters[i].shape.mesh;
        if (mesh != nullptr)
        {
            mesh->users -= 1;
        }
    }
    HoldGrids(compiled, -1);
    mvfxReleaseEffect(compiled, &context->allocator);
    mvfxFreeSlot(&context->effects, effect.index1);
    return mvfx_success;
}

bool mvfxEffect_IsValid(const mvfxContext* context, mvfxEffectId effect)
{
    return context != nullptr &&
           mvfxSlotObject(&context->effects, effect.index1, effect.generation) != nullptr;
}

mvfxEffect* mvfxContextEffect(const mvfxContext* context, mvfxEffectId effect)
{
    return mvfxSlotObject(&context->effects, effect.index1, effect.generation);
}
