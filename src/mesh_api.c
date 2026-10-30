// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating, destroying and moving meshes on a context.

#include "mesh_api.h"

#include "context.h"
#include "fpenv.h"
#include "hull.h"
#include "mesh.h"
#include "slots.h"

mvfxMeshDef mvfxDefaultMeshDef(void)
{
    return (mvfxMeshDef){.cookie = MVFX_MESH_DEF_COOKIE};
}

static MVFX_NOINLINE mvfxResult mvfxCreateMeshWork(mvfxContext* context, const mvfxMeshDef* def,
                                                   mvfxMeshId* meshOut)
{
    if (context == nullptr || meshOut == nullptr)
    {
        if (meshOut != nullptr)
        {
            *meshOut = (mvfxMeshId){0};
        }
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    *meshOut = (mvfxMeshId){0};
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    // A def past a bound is data a correct program may pass: no misuse.
    mvfxResult valid = mvfxCheckMeshDef(def);
    if (valid != mvfx_success)
    {
        valid == mvfx_errorInvalid ? mvfxCountMisuse(context) : (void)0;
        return valid;
    }
    if (context->meshes.firstFree == 0)
    {
        return mvfx_errorCapacity;
    }
    mvfxMesh* mesh = mvfxBuildMesh(def, &context->allocator);
    if (mesh == nullptr)
    {
        return mvfx_errorCapacity;
    }
    mesh->serial = ++context->meshValues;
    uint32_t generation = 0;
    uint32_t index1 = mvfxTakeSlot(&context->meshes, mesh, &generation);
    *meshOut = (mvfxMeshId){index1, generation};
    return mvfx_success;
}

mvfxResult mvfxCreateMesh(mvfxContext* context, const mvfxMeshDef* def, mvfxMeshId* meshOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxCreateMeshWork(context, def, meshOut);
    mvfxFloatEnvLeave(env);
    return result;
}

mvfxResult mvfxDestroyMesh(mvfxContext* context, mvfxMeshId mesh)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    mvfxMesh* live = mvfxContextMesh(context, mesh);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (live->users > 0)
    {
        return mvfx_errorBusy;
    }
    mvfxReleaseMesh(live, &context->allocator);
    mvfxFreeSlot(&context->meshes, mesh.index1);
    return mvfx_success;
}

bool mvfxMesh_IsValid(const mvfxContext* context, mvfxMeshId mesh)
{
    return context != nullptr && mvfxContextMesh(context, mesh) != nullptr;
}

static MVFX_NOINLINE mvfxResult mvfxMesh_SetPositionsWork(mvfxContext* context, mvfxMeshId mesh,
                                                          const float* positions,
                                                          const float* normals, int32_t vertexCount)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    mvfxMesh* live = mvfxContextMesh(context, mesh);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    mvfxResult valid = positions == nullptr || vertexCount != live->vertexCount
                           ? mvfx_errorInvalid
                           : mvfxCheckPositions(positions, normals, vertexCount);
    if (valid != mvfx_success)
    {
        mvfxCountMisuse(context);
        return valid;
    }
    mvfxMoveMesh(live, positions, normals);
    live->serial = ++context->meshValues;
    return mvfx_success;
}

mvfxResult mvfxMesh_SetPositions(mvfxContext* context, mvfxMeshId mesh, const float* positions,
                                 const float* normals, int32_t vertexCount)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxMesh_SetPositionsWork(context, mesh, positions, normals, vertexCount);
    mvfxFloatEnvLeave(env);
    return result;
}

mvfxResult mvfxMesh_GetView(const mvfxContext* context, mvfxMeshId mesh, mvfxMeshView* viewOut)
{
    if (viewOut != nullptr)
    {
        *viewOut = (mvfxMeshView){0};
    }
    if (context == nullptr || viewOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    const mvfxMesh* live = mvfxContextMesh(context, mesh);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    *viewOut = mvfxViewOfMesh(live);
    return mvfx_success;
}

mvfxResult mvfxMesh_GetHullCenter(const mvfxContext* context, mvfxMeshId mesh, bool* hullOut,
                                  float centerOut[3])
{
    hullOut != nullptr ? (void)(*hullOut = false) : (void)0;
    for (int32_t k = 0; centerOut != nullptr && k < 3; ++k)
    {
        centerOut[k] = 0.0f;
    }
    if (context == nullptr || hullOut == nullptr || centerOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    const mvfxMesh* live = mvfxContextMesh(context, mesh);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    // Vertices that span no volume, or whose hull is past what a rigid
    // particle takes, are a model's data: no hull, not an error.
    *hullOut = live->hull != nullptr;
    for (int32_t k = 0; live->hull != nullptr && k < 3; ++k)
    {
        centerOut[k] = live->hull->centroid[k];
    }
    return mvfx_success;
}

mvfxResult mvfxMesh_GetHullVertexCount(const mvfxContext* context, mvfxMeshId mesh,
                                       int32_t* countOut)
{
    countOut != nullptr ? (void)(*countOut = 0) : (void)0;
    if (context == nullptr || countOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    const mvfxMesh* live = mvfxContextMesh(context, mesh);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    *countOut = live->hullMeasure.count;
    return live->hullMeasure.counted;
}

mvfxMesh* mvfxContextMesh(const mvfxContext* context, mvfxMeshId mesh)
{
    return mvfxSlotObject(&context->meshes, mesh.index1, mesh.generation);
}
