// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating, destroying and changing grids on a context.

#include "grid_api.h"

#include "context.h"
#include "grid.h"
#include "slots.h"

mvfxGridDef mvfxDefaultGridDef(void)
{
    return (mvfxGridDef){.cookie = MVFX_GRID_DEF_COOKIE, .size = {1, 1, 1}, .components = 1};
}

mvfxResult mvfxCreateGrid(mvfxContext* context, const mvfxGridDef* def, mvfxGridId* gridOut)
{
    if (context == nullptr || gridOut == nullptr)
    {
        if (gridOut != nullptr)
        {
            *gridOut = (mvfxGridId){0};
        }
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    *gridOut = (mvfxGridId){0};
    if (!mvfxIsGridDefValid(def))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    if (context->grids.firstFree == 0)
    {
        return mvfx_errorLimit;
    }
    mvfxGrid* grid = mvfxBuildGrid(def, &context->allocator);
    if (grid == nullptr)
    {
        return mvfx_errorCapacity;
    }
    grid->serial = ++context->gridValues;
    uint32_t generation = 0;
    uint32_t index1 = mvfxTakeSlot(&context->grids, grid, &generation);
    *gridOut = (mvfxGridId){index1, generation};
    return mvfx_success;
}

mvfxResult mvfxDestroyGrid(mvfxContext* context, mvfxGridId grid)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxGrid* live = mvfxContextGrid(context, grid);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (live->users > 0)
    {
        return mvfx_errorBusy;
    }
    mvfxReleaseGrid(live, &context->allocator);
    mvfxFreeSlot(&context->grids, grid.index1);
    return mvfx_success;
}

bool mvfxGrid_IsValid(const mvfxContext* context, mvfxGridId grid)
{
    return context != nullptr && mvfxContextGrid(context, grid) != nullptr;
}

mvfxResult mvfxGrid_SetValues(mvfxContext* context, mvfxGridId grid, const float* values,
                              int64_t count)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxGrid* live = mvfxContextGrid(context, grid);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (values == nullptr || count != live->count ||
        !mvfxAreGridValuesValid(live->format, values, count))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxStoreGridValues(live, values);
    live->serial = ++context->gridValues;
    return mvfx_success;
}

mvfxGrid* mvfxContextGrid(const mvfxContext* context, mvfxGridId grid)
{
    return mvfxSlotObject(&context->grids, grid.index1, grid.generation);
}

mvfxResult mvfxFindColliderGrid(mvfxContext* context, const mvfxCollider* collider,
                                mvfxGrid** gridOut)
{
    *gridOut = nullptr;
    if (collider->kind != mvfx_colliderHeightfield && collider->kind != mvfx_colliderDistance)
    {
        return mvfx_success;
    }
    mvfxGrid* grid = mvfxContextGrid(context, collider->grid);
    if (grid == nullptr)
    {
        return mvfx_errorStale;
    }
    if (grid->components != 1 || (collider->kind == mvfx_colliderHeightfield && grid->size[1] != 1))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    *gridOut = grid;
    return mvfx_success;
}
