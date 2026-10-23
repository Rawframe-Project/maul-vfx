// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "sort.h"

#include "invariant.h"

#include <string.h>

// One pass on the byte at shift, from source to target, stable, by its
// byte's counts.
static void Pass(const uint32_t* keys, const uint32_t* indices, uint32_t* targetKeys,
                 uint32_t* targetIndices, int32_t count, uint32_t shift, const int32_t counts[256])
{
    int32_t starts[256];
    int32_t sum = 0;
    for (int32_t b = 0; b < 256; ++b)
    {
        starts[b] = sum;
        sum += counts[b];
    }
    MVFX_ASSERT(sum == count);
    for (int32_t i = 0; i < count; ++i)
    {
        int32_t at = starts[(keys[i] >> shift) & 0xFFu]++;
        targetKeys[at] = keys[i];
        targetIndices[at] = indices[i];
    }
}

void mvfxRadixSort(uint32_t* keys, uint32_t* indices, uint32_t* temporaryKeys,
                   uint32_t* temporaryIndices, int32_t count)
{
    if (count < 2)
    {
        return;
    }
    // Every byte's counts in one read; a pass whose byte is the same in
    // every key moves nothing and is left out.
    int32_t counts[4][256] = {0};
    for (int32_t i = 0; i < count; ++i)
    {
        uint32_t key = keys[i];
        counts[0][key & 0xFFu] += 1;
        counts[1][(key >> 8u) & 0xFFu] += 1;
        counts[2][(key >> 16u) & 0xFFu] += 1;
        counts[3][key >> 24u] += 1;
    }
    uint32_t* fromKeys = keys;
    uint32_t* fromIndices = indices;
    uint32_t* toKeys = temporaryKeys;
    uint32_t* toIndices = temporaryIndices;
    for (uint32_t pass = 0; pass < 4u; ++pass)
    {
        uint32_t shift = 8u * pass;
        if (counts[pass][(keys[0] >> shift) & 0xFFu] != count)
        {
            Pass(fromKeys, fromIndices, toKeys, toIndices, count, shift, counts[pass]);
            uint32_t* swapKeys = fromKeys;
            uint32_t* swapIndices = fromIndices;
            fromKeys = toKeys;
            fromIndices = toIndices;
            toKeys = swapKeys;
            toIndices = swapIndices;
        }
    }
    if (fromKeys != keys)
    {
        memcpy(keys, fromKeys, (size_t)count * sizeof *keys);
        memcpy(indices, fromIndices, (size_t)count * sizeof *indices);
    }
}
