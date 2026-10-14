// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating, destroying and naming effects on a context.

#include "effect_api.h"

#include "context.h"
#include "effect.h"
#include "float_mode.h"
#include "mesh_api.h"
#include "slots.h"

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
    if (!mvfxIsEffectDefValid(def))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxMesh* meshes[MVFX_MAX_EMITTERS] = {0};
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
    if (context->effects.firstFree == 0)
    {
        return mvfx_errorLimit;
    }
    mvfxFloatMode mode = mvfxEnterFloatMode();
    mvfxEffect* effect = mvfxCompileEffect(def, meshes, &context->allocator);
    mvfxLeaveFloatMode(mode);
    if (effect == nullptr)
    {
        return mvfx_errorCapacity;
    }
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        meshes[i] != nullptr ? (void)(meshes[i]->users += 1) : (void)0;
    }
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
