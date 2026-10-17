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
    grid->step = context->steps;
    grid->since = grid->serial;
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

mvfxResult mvfxGrid_GetView(const mvfxContext* context, mvfxGridId grid, mvfxGridView* viewOut)
{
    if (viewOut != nullptr)
    {
        *viewOut = (mvfxGridView){0};
    }
    if (context == nullptr || viewOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    const mvfxGrid* live = mvfxContextGrid(context, grid);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    size_t item = live->format == mvfx_gridFloat16 ? sizeof(uint16_t) : sizeof(float);
    *viewOut = (mvfxGridView){
        .size = {live->size[0], live->size[1], live->size[2]},
        .components = live->components,
        .format = live->format,
        .values = live->values,
        .bytes = live->count * (int64_t)item,
        .serial = live->serial,
    };
    return mvfx_success;
}

// A grid's values changed at the points from first, size along each axis:
// a new serial, and the points among those changed since the context's
// last step (the first change after a step starts them anew, from the
// serial before it).
static void Changed(mvfxContext* context, mvfxGrid* grid, const int32_t first[3],
                    const int32_t size[3])
{
    if (grid->step != context->steps)
    {
        grid->step = context->steps;
        grid->since = grid->serial;
        for (int32_t k = 0; k < 3; ++k)
        {
            grid->first[k] = 0;
            grid->end[k] = 0;
        }
    }
    bool empty = !(grid->first[0] < grid->end[0]);
    for (int32_t k = 0; k < 3; ++k)
    {
        int32_t end = first[k] + size[k];
        grid->first[k] = empty || first[k] < grid->first[k] ? first[k] : grid->first[k];
        grid->end[k] = empty || end > grid->end[k] ? end : grid->end[k];
    }
    grid->serial = ++context->gridValues;
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
    static const int32_t origin[3] = {0, 0, 0};
    Changed(context, live, origin, live->size);
    return mvfx_success;
}

mvfxResult mvfxGrid_SetValuesIn(mvfxContext* context, mvfxGridId grid, const int32_t first[3],
                                const int32_t size[3], const float* values, int64_t count)
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
    bool fits = first != nullptr && size != nullptr;
    int64_t points = 1;
    for (int32_t k = 0; fits && k < 3; ++k)
    {
        fits = first[k] >= 0 && size[k] >= 1 && size[k] <= live->size[k] - first[k];
        points *= fits ? size[k] : 1;
    }
    if (!fits || values == nullptr || count != points * live->components ||
        !mvfxAreGridValuesValid(live->format, values, count))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxStoreGridBox(live, first, size, values);
    Changed(context, live, first, size);
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
