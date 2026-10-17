// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "grids.h"

#include <string.h>

// A grid's words: binary32 values one a word, binary16 two.
static uint32_t WordsOf(const mvfxGridView* view)
{
    return (uint32_t)((view->bytes + 3) / 4);
}

static bool SameId(mvfxGridId a, mvfxGridId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// The grid among first to count with an id, or -1.
static int32_t Find(const mvfxGpuGrids* grids, int32_t first, int32_t count, mvfxGridId id)
{
    for (int32_t g = first; g < count; ++g)
    {
        if (SameId(grids->grids[g].id, id))
        {
            return g;
        }
    }
    return -1;
}

// Adds a grid at word at; false when it is gone.
static bool Add(const mvfxContext* context, mvfxGridId id, uint32_t at, mvfxGpuGrids* grids)
{
    mvfxGridView view;
    if (mvfxGrid_GetView(context, id, &view) != mvfx_success)
    {
        return false;
    }
    mvfxGpuGrid* grid = &grids->grids[grids->count++];
    *grid = (mvfxGpuGrid){.id = id, .first = at, .format = view.format};
    memcpy(grid->size, view.size, sizeof view.size);
    return true;
}

// The grid a fixed reader reads: found or added after the others; -1 for
// a null id; false through ok when it is gone.
static int32_t Fixed(const mvfxContext* context, mvfxGridId id, mvfxGpuGrids* grids, bool* ok)
{
    if (id.index1 == 0)
    {
        return -1;
    }
    int32_t g = Find(grids, 0, grids->count, id);
    if (g >= 0)
    {
        return g;
    }
    mvfxGridView view;
    if (mvfxGrid_GetView(context, id, &view) != mvfx_success ||
        !Add(context, id, grids->words, grids))
    {
        *ok = false;
        return -1;
    }
    grids->words += WordsOf(&view);
    return grids->count - 1;
}

bool mvfxLayGrids(const mvfxContext* context, const mvfxEmitterView* view, uint32_t hostWords,
                  mvfxGpuGrids* grids)
{
    *grids = (mvfxGpuGrids){0};
    bool ok = true;
    for (int32_t f = 0; f < MVFX_MAX_FORCES; ++f)
    {
        grids->forceGrid[f] = Fixed(context, view->forceGrids[f], grids, &ok);
    }
    for (int32_t c = 0; c < MVFX_MAX_COLLIDERS; ++c)
    {
        grids->colliderGrid[c] = Fixed(context, view->colliderGrids[c], grids, &ok);
    }
    grids->fixedCount = grids->count;
    grids->hostFirst = grids->words;
    grids->hostWords = hostWords;
    grids->words += hostWords;
    return ok;
}

bool mvfxPlaceHostGrids(const mvfxContext* context, const mvfxEmitterPlan* plan,
                        int32_t ownColliders, mvfxGpuGrids* grids)
{
    // The region as the last step left it, kept where a grid stays.
    mvfxGpuGrid was[MVFX_MAX_COLLIDERS];
    int32_t wasCount = grids->count - grids->fixedCount;
    memcpy(was, &grids->grids[grids->fixedCount], (size_t)wasCount * sizeof was[0]);
    grids->count = grids->fixedCount;
    uint32_t used = 0;
    for (int32_t c = ownColliders; c < plan->colliderCount; ++c)
    {
        mvfxGridId id = plan->colliders[c].grid;
        if (id.index1 == 0 || Find(grids, grids->fixedCount, grids->count, id) >= 0)
        {
            continue;
        }
        mvfxGridView view;
        if (mvfxGrid_GetView(context, id, &view) != mvfx_success ||
            used + WordsOf(&view) > grids->hostWords ||
            !Add(context, id, grids->hostFirst + used, grids))
        {
            grids->count = grids->fixedCount;
            return false;
        }
        mvfxGpuGrid* grid = &grids->grids[grids->count - 1];
        // The copy stands where the same grid stood last step.
        for (int32_t w = 0; w < wasCount; ++w)
        {
            grid->serial =
                SameId(was[w].id, id) && was[w].first == grid->first ? was[w].serial : grid->serial;
        }
        used += WordsOf(&view);
    }
    return true;
}

bool mvfxHostGridBytes(const mvfxContext* context, const mvfxCollider* colliders, int32_t count,
                       int64_t* bytes)
{
    *bytes = 0;
    for (int32_t c = 0; c < count; ++c)
    {
        bool grid = colliders[c].kind == mvfx_colliderHeightfield ||
                    colliders[c].kind == mvfx_colliderDistance;
        bool before = false;
        for (int32_t b = 0; grid && b < c; ++b)
        {
            before = before || (colliders[b].kind >= mvfx_colliderHeightfield &&
                                SameId(colliders[b].grid, colliders[c].grid));
        }
        mvfxGridView view;
        if (grid && !before)
        {
            if (mvfxGrid_GetView(context, colliders[c].grid, &view) != mvfx_success)
            {
                return false;
            }
            *bytes += 4 * (int64_t)WordsOf(&view);
        }
    }
    return true;
}

// A grid's words, or zeros for -1.
static void Words(const mvfxGpuGrids* grids, int32_t g, uint32_t out[5])
{
    memset(out, 0, 5 * sizeof(uint32_t));
    if (g < 0)
    {
        return;
    }
    const mvfxGpuGrid* grid = &grids->grids[g];
    out[0] = grid->first;
    for (int32_t k = 0; k < 3; ++k)
    {
        out[1 + k] = (uint32_t)grid->size[k];
    }
    out[4] = grid->format;
}

void mvfxForceGridWords(const mvfxGpuGrids* grids, int32_t f, uint32_t out[5])
{
    Words(grids, grids->forceGrid[f], out);
}

void mvfxColliderGridWords(const mvfxGpuGrids* grids, const mvfxEmitterPlan* plan,
                           int32_t ownColliders, int32_t c, uint32_t out[5])
{
    mvfxGridId id = plan->colliders[c].grid;
    int32_t g = c < ownColliders ? grids->colliderGrid[c]
                : id.index1 != 0 ? Find(grids, grids->fixedCount, grids->count, id)
                                 : -1;
    Words(grids, g, out);
}

// Uploads a grid's values from its first word: the whole words, then a
// last half alone in a word of its own.
static bool Upload(mrhiDevice* device, mrhiPassId pass, mrhiResourceId buffer, uint32_t first,
                   const mvfxGridView* view)
{
    uint64_t whole = (uint64_t)view->bytes & ~(uint64_t)3;
    bool written = whole == 0 || mrhiWriteBuffer(device, pass, buffer, (uint64_t)first * 4u,
                                                 view->values, whole) == mrhi_success;
    if (written && whole < (uint64_t)view->bytes)
    {
        uint32_t last = 0;
        memcpy(&last, (const unsigned char*)view->values + whole, (size_t)view->bytes - whole);
        written = mrhiWriteBuffer(device, pass, buffer, (uint64_t)first * 4u + whole, &last,
                                  sizeof last) == mrhi_success;
    }
    return written;
}

bool mvfxWriteGrids(mrhiDevice* device, mrhiPassId pass, mrhiResourceId buffer,
                    const mvfxContext* context, mvfxGpuGrids* grids)
{
    for (int32_t g = 0; g < grids->count; ++g)
    {
        mvfxGpuGrid* grid = &grids->grids[g];
        mvfxGridView view;
        if (mvfxGrid_GetView(context, grid->id, &view) != mvfx_success)
        {
            return false;
        }
        if (view.serial == grid->serial)
        {
            continue;
        }
        if (!Upload(device, pass, buffer, grid->first, &view))
        {
            return false;
        }
        grid->serial = view.serial;
    }
    return true;
}
