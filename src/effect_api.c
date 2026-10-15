// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating, destroying and naming effects on a context.

#include "effect_api.h"

#include "allocator.h"
#include "context.h"
#include "effect.h"
#include "effect_parts.h"
#include "float_mode.h"
#include "grid_api.h"
#include "hull.h"
#include "mesh.h"
#include "mesh_api.h"
#include "slots.h"

// Finds the grid of each vector field of a valid def: stale for an id
// that names no live grid, invalid (misuse) for one not of three
// components.
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
                mvfxCountMisuse(context);
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
            mvfxCountMisuse(context);
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
// or whose hull has too many vertices (misuse).
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
            built == mvfx_errorInvalid ? mvfxCountMisuse(context) : (void)0;
            FreeHulls(context, hulls);
            return built;
        }
    }
    return mvfx_success;
}

mvfxResult mvfxCreateEffect(mvfxContext* context, const mvfxEffectDef* def, mvfxEffectId* effectOut)
{
    if (effectOut == nullptr || context == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    *effectOut = (mvfxEffectId){0};
    if (!mvfxIsEffectDefValid(def) || !mvfxAreEffectPartsValid(def))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxMesh* meshes[MVFX_MAX_EMITTERS] = {0};
    mvfxResult meshed = FindMeshes(context, def, meshes);
    if (meshed != mvfx_success)
    {
        return meshed;
    }
    mvfxGrid* grids[MVFX_MAX_EMITTERS][MVFX_MAX_FORCES] = {{0}};
    mvfxGrid* colliderGrids[MVFX_MAX_EMITTERS][MVFX_MAX_COLLIDERS] = {{0}};
    mvfxResult found = FindGrids(context, def, grids);
    found = found == mvfx_success ? FindColliderGrids(context, def, colliderGrids) : found;
    if (found != mvfx_success)
    {
        return found;
    }
    if (context->effects.firstFree == 0)
    {
        return mvfx_errorLimit;
    }
    mvfxHull* hulls[MVFX_MAX_EMITTERS] = {0};
    mvfxResult built = BuildHulls(context, def, hulls);
    if (built != mvfx_success)
    {
        return built;
    }
    mvfxFloatMode mode = mvfxEnterFloatMode();
    mvfxEffect* effect = mvfxCompileEffect(def, meshes, &context->allocator);
    if (effect != nullptr)
    {
        mvfxCompileEffectParts(def, effect);
    }
    mvfxLeaveFloatMode(mode);
    if (effect == nullptr)
    {
        FreeHulls(context, hulls);
        return mvfx_errorCapacity;
    }
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        effect->emitters[i].hull = hulls[i];
        meshes[i] != nullptr ? (void)(meshes[i]->users += 1) : (void)0;
        for (int32_t f = 0; f < def->emitters[i].forceCount; ++f)
        {
            effect->emitters[i].forceGrids[f] = grids[i][f];
        }
        for (int32_t c = 0; c < def->emitters[i].colliderCount; ++c)
        {
            effect->emitters[i].colliderGrids[c] = colliderGrids[i][c];
        }
    }
    HoldGrids(effect, 1);
    uint32_t generation = 0;
    uint32_t index1 = mvfxTakeSlot(&context->effects, effect, &generation);
    *effectOut = (mvfxEffectId){index1, generation};
    return mvfx_success;
}

mvfxResult mvfxDestroyEffect(mvfxContext* context, mvfxEffectId effect)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
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
