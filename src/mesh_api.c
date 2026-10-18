// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating, destroying and moving meshes on a context.

#include "mesh_api.h"

#include "allocator.h"
#include "context.h"
#include "hull.h"
#include "mesh.h"
#include "slots.h"

mvfxMeshDef mvfxDefaultMeshDef(void)
{
    return (mvfxMeshDef){.cookie = MVFX_MESH_DEF_COOKIE};
}

mvfxResult mvfxCreateMesh(mvfxContext* context, const mvfxMeshDef* def, mvfxMeshId* meshOut)
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
        return mvfx_errorState;
    }
    if (!mvfxIsMeshDefValid(def))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    if (context->meshes.firstFree == 0)
    {
        return mvfx_errorLimit;
    }
    mvfxMesh* mesh = mvfxBuildMesh(def, &context->allocator);
    if (mesh == nullptr)
    {
        return mvfx_errorCapacity;
    }
    uint32_t generation = 0;
    uint32_t index1 = mvfxTakeSlot(&context->meshes, mesh, &generation);
    *meshOut = (mvfxMeshId){index1, generation};
    return mvfx_success;
}

mvfxResult mvfxDestroyMesh(mvfxContext* context, mvfxMeshId mesh)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorState;
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

mvfxResult mvfxMesh_SetPositions(mvfxContext* context, mvfxMeshId mesh, const float* positions,
                                 const float* normals, int32_t vertexCount)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorState;
    }
    mvfxMesh* live = mvfxContextMesh(context, mesh);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (positions == nullptr || vertexCount != live->vertexCount ||
        !mvfxArePositionsValid(positions, normals, vertexCount))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxMoveMesh(live, positions, normals);
    return mvfx_success;
}

mvfxResult mvfxMesh_HullCenter(mvfxContext* context, mvfxMeshId mesh, float centerOut[3])
{
    if (context == nullptr || centerOut == nullptr)
    {
        context != nullptr ? mvfxCountMisuse(context) : (void)0;
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorState;
    }
    const mvfxMesh* live = mvfxContextMesh(context, mesh);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    mvfxHull* hull = mvfxAllocate(&context->allocator, sizeof(mvfxHull), alignof(mvfxHull));
    mvfxResult built = hull == nullptr ? mvfx_errorCapacity
                                       : mvfxBuildHull(live->positions, live->vertexCount,
                                                       &context->allocator, hull);
    for (int32_t k = 0; k < 3; ++k)
    {
        centerOut[k] = built == mvfx_success ? hull->centroid[k] : 0.0f;
    }
    built == mvfx_errorInvalid ? mvfxCountMisuse(context) : (void)0;
    mvfxRelease(&context->allocator, hull, sizeof(mvfxHull), alignof(mvfxHull));
    return built;
}

mvfxResult mvfxMesh_HullVertexCount(mvfxContext* context, mvfxMeshId mesh, int32_t* countOut)
{
    if (context == nullptr || countOut == nullptr)
    {
        context != nullptr ? mvfxCountMisuse(context) : (void)0;
        countOut != nullptr ? (void)(*countOut = 0) : (void)0;
        return mvfx_errorInvalid;
    }
    *countOut = 0;
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorState;
    }
    const mvfxMesh* live = mvfxContextMesh(context, mesh);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    mvfxResult counted =
        mvfxCountHullVertices(live->positions, live->vertexCount, &context->allocator, countOut);
    counted == mvfx_errorInvalid ? mvfxCountMisuse(context) : (void)0;
    return counted;
}

mvfxMesh* mvfxContextMesh(const mvfxContext* context, mvfxMeshId mesh)
{
    return mvfxSlotObject(&context->meshes, mesh.index1, mesh.generation);
}
