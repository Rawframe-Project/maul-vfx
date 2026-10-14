// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// pcg4d against an independent implementation, and the float the library
// makes of a word.

#include "random.h"
#include "test_harness.h"

#include <math.h>

// Inputs and outputs printed by tools/pcg4d_vectors.py.
static const struct
{
    mvfxWords4 input;
    mvfxWords4 output;
} s_vectors[] = {
    {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
     {0x0F02F829u, 0x2D568769u, 0x32B0C43Bu, 0xD32548EAu}},
    {{0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u},
     {0x3622CD16u, 0xF11471D8u, 0xE1109B3Fu, 0x02B94C2Fu}},
    {{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu},
     {0x974ED892u, 0xC015DC67u, 0x9F955760u, 0xA1BBA208u}},
    {{0x12345678u, 0x9ABCDEF0u, 0x0F1E2D3Cu, 0x4B5A6978u},
     {0xE6333C67u, 0x765CF160u, 0x941B5A18u, 0xC1785759u}},
    {{0x0000002Au, 0x00000007u, 0x000F4240u, 0x00000003u},
     {0x169B6C0Bu, 0xBB313A1Au, 0x8CB5317Cu, 0x81B62DCBu}},
    {{0x80000000u, 0x00000001u, 0x7FFFFFFFu, 0xDEADBEEFu},
     {0xAF8ED48Fu, 0x32614288u, 0x065A27FDu, 0x47B54EF9u}},
};

static void TestPcg4dMatchesTheListing(void)
{
    for (size_t i = 0; i < sizeof s_vectors / sizeof s_vectors[0]; ++i)
    {
        mvfxWords4 out = mvfxPcg4d(s_vectors[i].input);
        CHECK(out.x == s_vectors[i].output.x && out.y == s_vectors[i].output.y &&
                  out.z == s_vectors[i].output.z && out.w == s_vectors[i].output.w,
              "pcg4d output");
    }
}

static void TestEveryCoordinateMovesEveryOutput(void)
{
    // One bit of one input changes each output word: all four inputs are
    // coordinates, none is ignored.
    mvfxWords4 base = {7, 11, 13, 17};
    mvfxWords4 reference = mvfxPcg4d(base);
    for (int coordinate = 0; coordinate < 4; ++coordinate)
    {
        mvfxWords4 moved = base;
        uint32_t* word = coordinate == 0   ? &moved.x
                         : coordinate == 1 ? &moved.y
                         : coordinate == 2 ? &moved.z
                                           : &moved.w;
        *word ^= 1u;
        mvfxWords4 out = mvfxPcg4d(moved);
        CHECK(out.x != reference.x && out.y != reference.y && out.z != reference.z &&
                  out.w != reference.w,
              "a coordinate reaches every output");
    }
}

static void TestUnitFloatRangeAndSpacing(void)
{
    CHECK(mvfxUnitFloat(0) == 0.0f, "zero maps to 0");
    CHECK(mvfxUnitFloat(0xFFFFFFFFu) == 1.0f - 0x1p-24f, "the largest word stays below 1");
    CHECK(mvfxUnitFloat(0x00000100u) == 0x1p-24f, "one step is 2^-24");
    CHECK(mvfxUnitFloat(0x000000FFu) == 0.0f, "the low 8 bits are dropped");
    CHECK(mvfxUnitFloat(0x80000000u) == 0.5f, "the top bit is one half");
}

static void TestSerialStreamIsUniform(void)
{
    // A particle stream: seed, emitter, serials 0..n-1, counter 0. Its
    // mean and variance and its counts in 16 bins match the uniform
    // distribution within six standard deviations of the estimate.
    enum
    {
        COUNT = 1 << 18,
        BINS = 16
    };
    double sum = 0;
    double squares = 0;
    int32_t bins[BINS] = {0};
    for (uint32_t serial = 0; serial < COUNT; ++serial)
    {
        mvfxWords4 out = mvfxPcg4d((mvfxWords4){0xC0FFEEu, 3, serial, 0});
        float value = mvfxUnitFloat(out.x);
        sum += (double)value;
        squares += (double)value * (double)value;
        bins[(int32_t)(value * BINS)] += 1;
    }
    double mean = sum / COUNT;
    double variance = squares / COUNT - mean * mean;
    CHECK(fabs(mean - 0.5) < 6.0 * sqrt(1.0 / 12.0 / COUNT), "mean of a uniform value");
    CHECK(fabs(variance - 1.0 / 12.0) < 6.0 * sqrt(1.0 / 180.0 / COUNT), "its variance");
    double expected = (double)COUNT / BINS;
    double chiSquare = 0;
    for (int32_t i = 0; i < BINS; ++i)
    {
        chiSquare += (bins[i] - expected) * (bins[i] - expected) / expected;
    }
    // 15 degrees of freedom: 37.7 is past the 0.999 quantile.
    CHECK(chiSquare < 37.7, "bins are even");
}

int main(void)
{
    TestPcg4dMatchesTheListing();
    TestEveryCoordinateMovesEveryOutput();
    TestUnitFloatRangeAndSpacing();
    TestSerialStreamIsUniform();
    return s_failures == 0 ? 0 : 1;
}
