// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// binary32 to binary16 against the oracles of half_reference.h, on a
// prime stride of floats and every float near a half's value or halfway
// between two; test_half_sweep checks every float.

#include "half_reference.h"

#include <math.h>

static void TestEdgeCases(void)
{
    static const struct
    {
        float value;
        uint16_t half;
    } cases[] = {
        {0.0f, 0x0000u},       {-0.0f, 0x8000u},        {1.0f, 0x3C00u},
        {-2.0f, 0xC000u},      {65504.0f, 0x7BFFu},     {65519.99f, 0x7BFFu},
        {65520.0f, 0x7C00u},   {INFINITY, 0x7C00u},     {-INFINITY, 0xFC00u},
        {0x1p-14f, 0x0400u},   {0x1p-24f, 0x0001u},     {0x1p-25f, 0x0000u},
        {0x1.8p-25f, 0x0001u}, {0x1.8p-24f, 0x0002u},   {0x1.002p0f, 0x3C00u},
        {0x1.006p0f, 0x3C02u}, {0x1.ffep-15f, 0x0400u}, {1e-30f, 0x0000u},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        CHECK(mvfxHalfFromFloat(cases[i].value) == cases[i].half, "edge case");
    }
    CHECK(mvfxHalfFromFloat(NAN) == 0x7E00u, "NaN is the quiet NaN");
    CHECK(mvfxHalfFromFloat(-NAN) == 0xFE00u, "keeping its sign");
}

static void TestFloats(void)
{
    for (uint64_t bits = 0; bits <= 0xFFFFFFFFu; bits += 65521u)
    {
        CheckBits((uint32_t)bits);
    }
    // Every float within 16 ulps of a half's value or of a point halfway
    // between two halves, of both signs.
    for (uint32_t half = 0; half < 0x7C00u; ++half)
    {
        for (int32_t sign = 0; sign < 2; ++sign)
        {
            float lower = 0;
            uint32_t low = (half << 13u) + 0x38000000u;
            if (half < 0x0400u)
            {
                lower = (float)half * 0x1p-24f;
                memcpy(&low, &lower, sizeof low);
            }
            for (int32_t offset = -16; offset <= 16; ++offset)
            {
                uint32_t near = (uint32_t)((int64_t)low + offset) & 0x7FFFFFFFu;
                CheckBits(near | (sign != 0 ? 0x80000000u : 0u));
                uint32_t middle = low + (half < 0x0400u ? 0u : 0x1000u);
                CheckBits(((uint32_t)((int64_t)middle + offset) & 0x7FFFFFFFu) |
                          (sign != 0 ? 0x80000000u : 0u));
            }
        }
    }
    CHECK(s_checked > 0, "floats checked");
}

int main(void)
{
    TestEdgeCases();
    TestFloats();
    return s_failures == 0 ? 0 : 1;
}
