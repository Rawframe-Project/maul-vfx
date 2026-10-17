// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "grid.h"

#include "half.h"

#include <math.h>
#include <stdalign.h>

// The largest binary16.
#define HALF_MAX 65504.0f

bool mvfxIsGridDefValid(const mvfxGridDef* def)
{
    if (def == nullptr || def->cookie != MVFX_GRID_DEF_COOKIE || def->values == nullptr ||
        def->components < 1 || def->components > 4 || def->format > mvfx_gridFloat16)
    {
        return false;
    }
    int64_t points = 1;
    for (int32_t k = 0; k < 3; ++k)
    {
        if (def->size[k] < 1 || def->size[k] > MVFX_MAX_GRID_SIZE)
        {
            return false;
        }
        points *= def->size[k];
    }
    return points <= MVFX_MAX_GRID_POINTS &&
           mvfxAreGridValuesValid(def->format, def->values, points * def->components);
}

bool mvfxAreGridValuesValid(mvfxGridFormat format, const float* values, int64_t count)
{
    float bound = format == mvfx_gridFloat16 ? HALF_MAX : MVFX_MAX_VALUE;
    for (int64_t i = 0; i < count; ++i)
    {
        if (!(fabsf(values[i]) <= bound))
        {
            return false;
        }
    }
    return true;
}

mvfxGrid* mvfxBuildGrid(const mvfxGridDef* def, const mvfxAllocator* allocator)
{
    int64_t count = (int64_t)def->size[0] * def->size[1] * def->size[2] * def->components;
    size_t item = def->format == mvfx_gridFloat16 ? sizeof(uint16_t) : sizeof(float);
    mvfxLayout layout = {0};
    (void)mvfxLayoutAdd(&layout, 1, sizeof(mvfxGrid), alignof(mvfxGrid));
    size_t valuesAt = mvfxLayoutAdd(&layout, (size_t)count, item, alignof(float));
    unsigned char* block = mvfxAllocate(allocator, layout.size, alignof(mvfxGrid));
    if (block == nullptr)
    {
        return nullptr;
    }
    mvfxGrid* grid = (mvfxGrid*)block;
    *grid = (mvfxGrid){
        .size = {def->size[0], def->size[1], def->size[2]},
        .components = def->components,
        .format = def->format,
        .count = count,
        .values = block + valuesAt,
        .blockBytes = layout.size,
    };
    mvfxStoreGridValues(grid, def->values);
    return grid;
}

void mvfxReleaseGrid(mvfxGrid* grid, const mvfxAllocator* allocator)
{
    mvfxRelease(allocator, grid, grid->blockBytes, alignof(mvfxGrid));
}

void mvfxStoreGridValues(mvfxGrid* grid, const float* values)
{
    if (grid->format == mvfx_gridFloat16)
    {
        uint16_t* halves = grid->values;
        for (int64_t i = 0; i < grid->count; ++i)
        {
            halves[i] = mvfxHalfFromFloat(values[i]);
        }
        return;
    }
    float* floats = grid->values;
    for (int64_t i = 0; i < grid->count; ++i)
    {
        floats[i] = values[i];
    }
}

void mvfxStoreGridBox(mvfxGrid* grid, const int32_t first[3], const int32_t size[3],
                      const float* values)
{
    int64_t c = grid->components;
    int64_t row = c * size[0];
    for (int64_t z = 0; z < size[2]; ++z)
    {
        for (int64_t y = 0; y < size[1]; ++y)
        {
            int64_t at =
                (((first[2] + z) * grid->size[1] + first[1] + y) * grid->size[0] + first[0]) * c;
            const float* from = values + (z * size[1] + y) * row;
            for (int64_t i = 0; i < row; ++i)
            {
                if (grid->format == mvfx_gridFloat16)
                {
                    ((uint16_t*)grid->values)[at + i] = mvfxHalfFromFloat(from[i]);
                }
                else
                {
                    ((float*)grid->values)[at + i] = from[i];
                }
            }
        }
    }
}

// Along one axis of n points: the two points a place lies between and its
// share toward the second.
static void Locate(float u, int32_t n, mvfxGridBorder border, int32_t* low, int32_t* high,
                   float* share)
{
    if (border == mvfx_borderRepeat)
    {
        // n points a period, the last joining the first. A place that is not
        // finite (a particle flown past the float range) reads the first
        // cell. For a finite one u times n is exact in binary64, so the wrap
        // is exact and lies in [0, n); the cell is held there all the same.
        double g = isfinite(u) ? (double)u * (double)n : 0.0;
        if (fabs(g) < 0x1p52)
        {
            // In integers, the same: the floor of a binary64 below 2^52 is
            // a whole int64_t, and its wrap the remainder put in [0, n).
            int64_t whole = (int64_t)g;
            whole -= (double)whole > g ? 1 : 0;
            *share = (float)(g - (double)whole);
            int64_t wrapped = whole % n;
            wrapped += wrapped < 0 ? n : 0;
            *low = (int32_t)wrapped;
            *high = *low + 1 == n ? 0 : *low + 1;
            return;
        }
        double cell = floor(g);
        *share = (float)(g - cell);
        double wrapped = cell - (double)n * floor(cell / (double)n);
        int32_t index = wrapped >= 0.0 && wrapped < (double)n ? (int32_t)wrapped : 0;
        *low = index;
        *high = index + 1 == n ? 0 : index + 1;
        return;
    }
    // fmaxf takes a NaN to 0.
    float clamped = fminf(fmaxf(u, 0.0f), 1.0f);
    float g = clamped * (float)(n - 1);
    float cell = floorf(g);
    *low = (int32_t)cell;
    *high = *low + 1 < n ? *low + 1 : *low;
    *share = g - cell;
}

// Adds a point's values, from at, times weight to out: the format chosen
// once a point rather than once a value.
static void AddPoint(const mvfxGrid* grid, int64_t at, float weight, float out[4])
{
    if (grid->format == mvfx_gridFloat16)
    {
        const uint16_t* halves = (const uint16_t*)grid->values + at;
        for (int32_t c = 0; c < grid->components; ++c)
        {
            out[c] += weight * mvfxFloatFromHalf(halves[c]);
        }
        return;
    }
    const float* floats = (const float*)grid->values + at;
    for (int32_t c = 0; c < grid->components; ++c)
    {
        out[c] += weight * floats[c];
    }
}

bool mvfxSampleGrid(const mvfxGrid* grid, const float u[3], mvfxGridBorder border, float out[4])
{
    for (int32_t c = 0; c < 4; ++c)
    {
        out[c] = 0.0f;
    }
    if (border == mvfx_borderNone)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            if (!(u[k] >= 0.0f && u[k] <= 1.0f))
            {
                return false;
            }
        }
    }
    int32_t low[3];
    int32_t high[3];
    float share[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        Locate(u[k], grid->size[k], border, &low[k], &high[k], &share[k]);
    }
    int64_t sx = grid->components;
    int64_t sy = sx * grid->size[0];
    int64_t sz = sy * grid->size[1];
    for (uint32_t corner = 0; corner < 8; ++corner)
    {
        bool i = (corner & 1u) != 0;
        bool j = (corner & 2u) != 0;
        bool k = (corner & 4u) != 0;
        float weight = (i ? share[0] : 1.0f - share[0]) * (j ? share[1] : 1.0f - share[1]) *
                       (k ? share[2] : 1.0f - share[2]);
        int64_t at = (int64_t)(i ? high[0] : low[0]) * sx + (int64_t)(j ? high[1] : low[1]) * sy +
                     (int64_t)(k ? high[2] : low[2]) * sz;
        AddPoint(grid, at, weight, out);
    }
    return true;
}
