// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A vector's length (mvfxLengthOf) on its own: a vector scaled by a power
// of two measures its length scaled by it, bit for bit, while its
// components stay normal, from past 2^63 down to binary32's least normal.
// A vector below 2^-63 has subnormal squares, which a device may flush;
// only the length on its own reaches them all.

#include "direction.h"
#include "test_harness.h"

#include <float.h>
#include <math.h>
#include <string.h>

// A word of a simple generator, the draws.
static uint32_t Draw(uint64_t* state)
{
    *state = *state * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t)(*state >> 32u);
}

static uint32_t Bits(float f)
{
    uint32_t u = 0;
    memcpy(&u, &f, sizeof u);
    return u;
}

static void TestScaled(void)
{
    // Vectors whose largest component is in [1/2, 1), the others down to
    // 2^-30 of it, scaled by 2^s for s from -125 to 127 where every
    // component stays normal.
    uint64_t state = 7;
    int32_t apart = 0;
    int32_t measured = 0;
    for (int32_t i = 0; i < 2000; ++i)
    {
        float v[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            uint32_t size = Draw(&state);
            uint32_t below = k == i % 3 ? 0u : Draw(&state) % 31u;
            v[k] = ldexpf(0.5f + (float)(size >> 9u) * 0x1p-24f, -(int32_t)below);
        }
        float length = mvfxLengthOf(v[0], v[1], v[2]);
        for (int32_t s = -125; s <= 127; ++s)
        {
            float x = ldexpf(v[0], s);
            float y = ldexpf(v[1], s);
            float z = ldexpf(v[2], s);
            if (!(fminf(x, fminf(y, z)) >= FLT_MIN))
            {
                continue;
            }
            apart += Bits(mvfxLengthOf(x, y, z)) != Bits(ldexpf(length, s));
            ++measured;
        }
    }
    // A long hanging sheet's scaled cross product, about 2^-66.
    bool sheet = mvfxLengthOf(0.0f, 0.0f, 0x1.2p-66f) == 0x1.2p-66f &&
                 mvfxLengthOf(0x1p-126f, 0.0f, 0.0f) == 0x1p-126f &&
                 mvfxLengthOf(0.0f, 0.0f, 0.0f) == 0.0f;
    CHECK(apart == 0 && measured > 100000 && sheet,
          "lengths scale with their vectors, bit for bit, while their components are normal");
}

int main(void)
{
    TestScaled();
    return s_failures == 0 ? 0 : 1;
}
