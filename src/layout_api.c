// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating and destroying layouts on a context.

#include "layout_api.h"

#include "context.h"
#include "slots.h"

mvfxLayoutDef mvfxDefaultLayoutDef(void)
{
    return (mvfxLayoutDef){.cookie = MVFX_LAYOUT_DEF_COOKIE};
}

mvfxResult mvfxCreateLayout(mvfxContext* context, const mvfxLayoutDef* def, mvfxLayoutId* layoutOut)
{
    if (context == nullptr || layoutOut == nullptr)
    {
        if (layoutOut != nullptr)
        {
            *layoutOut = (mvfxLayoutId){0};
        }
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    *layoutOut = (mvfxLayoutId){0};
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    // A def past a bound is data a correct program may pass: no misuse.
    mvfxResult valid = mvfxCheckLayoutDef(def);
    if (valid != mvfx_success)
    {
        valid == mvfx_errorInvalid ? mvfxCountMisuse(context) : (void)0;
        return valid;
    }
    if (context->layouts.firstFree == 0)
    {
        return mvfx_errorCapacity;
    }
    mvfxOutputLayout* layout = mvfxBuildLayout(def, &context->allocator);
    if (layout == nullptr)
    {
        return mvfx_errorCapacity;
    }
    uint32_t generation = 0;
    uint32_t index1 = mvfxTakeSlot(&context->layouts, layout, &generation);
    *layoutOut = (mvfxLayoutId){index1, generation};
    return mvfx_success;
}

mvfxOutputLayout* mvfxContextLayout(const mvfxContext* context, mvfxLayoutId layout)
{
    return mvfxSlotObject(&context->layouts, layout.index1, layout.generation);
}

mvfxResult mvfxDestroyLayout(mvfxContext* context, mvfxLayoutId layout)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    mvfxOutputLayout* live = mvfxContextLayout(context, layout);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    mvfxReleaseLayout(live, &context->allocator);
    mvfxFreeSlot(&context->layouts, layout.index1);
    return mvfx_success;
}

mvfxResult mvfxLayout_GetView(const mvfxContext* context, mvfxLayoutId layout,
                              mvfxLayoutView* viewOut)
{
    if (viewOut != nullptr)
    {
        *viewOut = (mvfxLayoutView){0};
    }
    if (context == nullptr || viewOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    const mvfxOutputLayout* live = mvfxContextLayout(context, layout);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    *viewOut = (mvfxLayoutView){
        .fields = live->fields, .fieldCount = live->fieldCount, .stride = live->stride};
    return mvfx_success;
}

bool mvfxLayout_IsValid(const mvfxContext* context, mvfxLayoutId layout)
{
    return context != nullptr && mvfxContextLayout(context, layout) != nullptr;
}
