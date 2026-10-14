// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating, destroying and naming effects on a context.

#include "effect_api.h"

#include "context.h"
#include "effect.h"
#include "float_mode.h"
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
    if (context->effects.firstFree == 0)
    {
        return mvfx_errorLimit;
    }
    mvfxFloatMode mode = mvfxEnterFloatMode();
    mvfxEffect* effect = mvfxCompileEffect(def, &context->allocator);
    mvfxLeaveFloatMode(mode);
    if (effect == nullptr)
    {
        return mvfx_errorCapacity;
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
    mvfxReleaseEffect(compiled, &context->allocator);
    mvfxFreeSlot(&context->effects, effect.index1);
    return mvfx_success;
}

bool mvfxEffect_IsValid(const mvfxContext* context, mvfxEffectId effect)
{
    return context != nullptr &&
           mvfxSlotObject(&context->effects, effect.index1, effect.generation) != nullptr;
}

const mvfxEffect* mvfxContextEffect(const mvfxContext* context, mvfxEffectId effect)
{
    return mvfxSlotObject(&context->effects, effect.index1, effect.generation);
}
