// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "sort.h"

#include <stdbool.h>
#include <string.h>

uint32_t mvfxOrderedKey(float value)
{
    float canonical = value + 0.0f;
    uint32_t bits = 0;
    memcpy(&bits, &canonical, sizeof bits);
    // A negative value: every bit flipped, so larger magnitudes come
    // first; a positive one: the sign bit set, above every negative.
    uint32_t mask = (bits >> 31u) != 0 ? 0xFFFFFFFFu : 0x80000000u;
    return bits ^ mask;
}

// One pass on the byte at shift, from source to target, stable; false
// when every key has the same byte, so nothing moves.
static bool Pass(const uint32_t* keys, const uint32_t* indices, uint32_t* targetKeys,
                 uint32_t* targetIndices, int32_t count, uint32_t shift)
{
    int32_t starts[256] = {0};
    for (int32_t i = 0; i < count; ++i)
    {
        starts[(keys[i] >> shift) & 0xFFu] += 1;
    }
    if (starts[(keys[0] >> shift) & 0xFFu] == count)
    {
        return false;
    }
    int32_t sum = 0;
    for (int32_t b = 0; b < 256; ++b)
    {
        int32_t here = starts[b];
        starts[b] = sum;
        sum += here;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        int32_t at = starts[(keys[i] >> shift) & 0xFFu]++;
        targetKeys[at] = keys[i];
        targetIndices[at] = indices[i];
    }
    return true;
}

void mvfxRadixSort(uint32_t* keys, uint32_t* indices, uint32_t* temporaryKeys,
                   uint32_t* temporaryIndices, int32_t count)
{
    if (count < 2)
    {
        return;
    }
    uint32_t* fromKeys = keys;
    uint32_t* fromIndices = indices;
    uint32_t* toKeys = temporaryKeys;
    uint32_t* toIndices = temporaryIndices;
    for (uint32_t shift = 0; shift < 32u; shift += 8u)
    {
        if (Pass(fromKeys, fromIndices, toKeys, toIndices, count, shift))
        {
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
