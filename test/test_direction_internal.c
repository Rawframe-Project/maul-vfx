// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A vector's length (mvfxLengthOf) on its own: a vector scaled by a power
// of two measures its length scaled by it, bit for bit, while its
// components stay normal, from past 2^63 down to binary32's least normal;
// and a length measured under flush-to-zero, as a device that flushes
// subnormals measures it, has the same bits. A vector below 2^-63 has
// subnormal squares, which a device may flush; only the length on its own
// reaches them all.

#include "direction.h"
#include "test_fenv.h"
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

#if TEST_HAS_FLUSH
// A vector's length, read through volatile, so it is measured again under
// each setting.
static float Measured(const volatile float* v)
{
    return mvfxLengthOf(v[0], v[1], v[2]);
}
#endif

// Lengths under flush-to-zero against lengths as they are: two vectors
// just above 2^-40 whose subnormal square tipped the sum's rounding while
// the bound was there, a long hanging sheet's scaled cross product, and
// drawn ones whose middle partial sum lies near half a unit of the largest
// square's last place, with a subnormal smallest square.
static void TestFlushed(void)
{
    int32_t apart = 0;
    int32_t measured = 0;
#if TEST_HAS_FLUSH
    enum
    {
        SPECIAL = 4,
        DRAWN = 100000
    };
    static const float special[SPECIAL][3] = {{0x1.fffffep-53f, 0x1.6a09e8p-64f, 0x1.0016ap-40f},
                                              {0x1.93ca06p-64f, 0x1p-52f, 0x1.00d88p-40f},
                                              {0x1.692fb2p-64f, 0x1.fffffep-53f, 0x1.55349ap-40f},
                                              {0.0f, 0.0f, 0x1.2p-66f}};
    TestFloatSetting kept = TestFloatGet();
    TestFloatSetting flushed = kept;
    flushed.control = TEST_FLUSH_BITS;
    uint64_t state = 13;
    for (int32_t i = 0; i < SPECIAL + DRAWN; ++i)
    {
        int32_t top = -50 + (int32_t)(Draw(&state) % 113u);
        uint32_t largest = Draw(&state);
        uint32_t middle = Draw(&state);
        uint32_t smallest = Draw(&state);
        uint32_t below = Draw(&state);
        uint32_t order = Draw(&state);
        // The middle square near half a unit of the largest square's last
        // place, 2^(2 top - 24); the smallest square subnormal.
        float l = ldexpf(1.0f + (float)(largest >> 9u) * 0x1p-23f, top);
        float m = ldexpf(1.0f + ((float)(middle >> 9u) * 0x1p-23f - 0.5f) * 0x1p-20f, top - 12);
        float s = ldexpf(1.0f + (float)(smallest >> 9u) * 0x1p-23f, -64 - (int32_t)(below % 10u));
        volatile float v[3] = {m, s, l};
        if ((order & 1u) != 0u)
        {
            v[0] = s;
            v[1] = m;
        }
        for (int32_t k = 0; i < SPECIAL && k < 3; ++k)
        {
            v[k] = special[i][k];
        }
        // Each result stored through volatile under its own setting, so no
        // compiler moves the arithmetic past the setting's change.
        volatile float as = Measured(v);
        TestFloatSet(flushed);
        volatile float under = Measured(v);
        TestFloatSet(kept);
        apart += Bits(as) != Bits(under);
        ++measured;
    }
#else
    // No flush-to-zero setting to take (WebAssembly): the check does not run.
#endif
    CHECK(apart == 0 && (measured > 100000 || !TEST_HAS_FLUSH),
          "lengths under flush-to-zero have the same bits");
}

int main(void)
{
    TestScaled();
    TestFlushed();
    return s_failures == 0 ? 0 : 1;
}
