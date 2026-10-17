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

bool mvfxLayGrids(const mvfxContext* context, const mvfxEmitterView* view, mvfxGpuGrids* grids)
{
    *grids = (mvfxGpuGrids){0};
    for (int32_t f = 0; f < MVFX_MAX_FORCES; ++f)
    {
        grids->forceGrid[f] = -1;
        mvfxGridId id = view->forceGrids[f];
        if (id.index1 == 0)
        {
            continue;
        }
        int32_t g = 0;
        while (g < grids->count && !SameId(grids->ids[g], id))
        {
            ++g;
        }
        if (g == grids->count)
        {
            mvfxGridView grid;
            if (mvfxGrid_GetView(context, id, &grid) != mvfx_success)
            {
                return false;
            }
            grids->ids[g] = id;
            grids->first[g] = grids->words;
            memcpy(grids->size[g], grid.size, sizeof grid.size);
            grids->format[g] = grid.format;
            grids->words += WordsOf(&grid);
            grids->count = g + 1;
        }
        grids->forceGrid[f] = g;
    }
    return true;
}

void mvfxGridWords(const mvfxGpuGrids* grids, int32_t f, uint32_t out[5])
{
    memset(out, 0, 5 * sizeof(uint32_t));
    int32_t g = grids->forceGrid[f];
    if (g < 0)
    {
        return;
    }
    out[0] = grids->first[g];
    for (int32_t k = 0; k < 3; ++k)
    {
        out[1 + k] = (uint32_t)grids->size[g][k];
    }
    out[4] = grids->format[g];
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
        mvfxGridView view;
        if (mvfxGrid_GetView(context, grids->ids[g], &view) != mvfx_success)
        {
            return false;
        }
        if (view.serial == grids->serials[g])
        {
            continue;
        }
        if (!Upload(device, pass, buffer, grids->first[g], &view))
        {
            return false;
        }
        grids->serials[g] = view.serial;
    }
    return true;
}
