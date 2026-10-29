// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A sheet's triangle's edges' cross product (mvfxCrossEdges), as written
// below 2^63 and scaled from it, and its share of its relative speed
// (mvfxAirShare) on its own: a product past binary32's largest held at a sixth from its
// factors' exponents; a coefficient's and an area's product past it, and
// a scaled area, though the whole is small, taken from the significands;
// none for a zero coefficient or speed or a NaN speed; and every share
// whose product fits the same bits as the product as written. A step
// reaches the overflowing products only at places and speeds past what a
// short scene makes, so only the share on its own shows them.

#include "aero.h"
#include "test_harness.h"

#include <math.h>

// A word of a simple generator, the draws.
static uint32_t Draw(uint64_t* state)
{
    *state = *state * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t)(*state >> 32u);
}

static void TestShares(void)
{
    // At spacing and scale 1e6, half a triangle's area is about 2.5e23 in
    // the world: a normal drag of 1e6 at a speed of 1e9 takes a product of
    // 2.5e38 before the substep, held at a sixth.
    bool held = mvfxAirShare(1e6f, 2.5e23f, false, 1e9f, 1.0f / 120.0f) == 1.0f / 6.0f &&
                mvfxAirShare(1e6f, 1e33f, false, 1e30f, 1.0f) == 1.0f / 6.0f &&
                mvfxAirShare(1.0f, 0.5f, true, 5.0f, 1.0f / 60.0f) == 1.0f / 6.0f;
    // A coefficient's and an area's product of 1e39 at a speed of 1e-35
    // over a substep of 2^-25 s: about 1e-4, not a sixth.
    double truth = (double)1e6f * (double)1e33f * (double)1e-35f * 0x1p-25 / 3.0;
    float small = mvfxAirShare(1e6f, 1e33f, false, 1e-35f, 0x1p-25f);
    // An area of 1e-10 2^128 at a speed of 1e-30: about 3e-42 2^128 k h.
    double scaledTruth = (double)1e-6f * (double)1e-10f * 0x1p128 * (double)1e-30f * 0x1p-25 / 3.0;
    float scaled = mvfxAirShare(1e-6f, 1e-10f, true, 1e-30f, 0x1p-25f);
    bool exact = fabs((double)small / truth - 1.0) <= 1e-6 &&
                 fabs((double)scaled / scaledTruth - 1.0) <= 1e-6;
    bool none = mvfxAirShare(0.0f, 1.0f, false, 5.0f, 0.01f) == 0.0f &&
                mvfxAirShare(1.0f, 1.0f, false, 0.0f, 0.01f) == 0.0f &&
                mvfxAirShare(1.0f, 1.0f, false, NAN, 0.01f) == 0.0f;
    // Drawn factors over their ranges: where the product as written stays
    // finite, the share is its bits.
    uint64_t state = 5;
    int32_t same = 0;
    int32_t drawn = 0;
    for (int32_t i = 0; i < 100000; ++i)
    {
        float f[4];
        const int32_t low[4] = {-40, -60, -60, -25};
        const int32_t span[4] = {60, 160, 160, 26};
        for (int32_t k = 0; k < 4; ++k)
        {
            f[k] = ldexpf(0.5f + (float)(Draw(&state) >> 9u) * 0x1p-24f,
                          low[k] + (int32_t)(Draw(&state) % (uint32_t)span[k]));
        }
        float product = f[0] * f[1] * f[2] * f[3] / 3.0f;
        if (!isfinite(product))
        {
            continue;
        }
        float written = product < 1.0f / 6.0f ? product : 1.0f / 6.0f;
        drawn += 1;
        same += mvfxAirShare(f[0], f[1], false, f[2], f[3]) == written;
    }
    CHECK(held && exact && none && same == drawn && drawn > 50000, "air shares");
}

static void TestCross(void)
{
    // Edges below 2^63 crossed as written; one 1e20 long and one 0.5
    // across it crossed scaled by 2^-64 each, its length then 1e20 0.5
    // 2^-128; 2^63 itself scaled.
    uint64_t state = 9;
    bool written = true;
    for (int32_t i = 0; i < 10000; ++i)
    {
        float e[6];
        for (int32_t k = 0; k < 6; ++k)
        {
            e[k] = ldexpf((float)Draw(&state) / 4294967296.0f - 0.5f,
                          -30 + (int32_t)(Draw(&state) % 93u));
        }
        float n[3];
        bool scaled = mvfxCrossEdges(e, e + 3, n);
        written = written && !scaled && n[0] == e[1] * e[5] - e[2] * e[4] &&
                  n[1] == e[2] * e[3] - e[0] * e[5] && n[2] == e[0] * e[4] - e[1] * e[3];
    }
    static const float along[3] = {1e20f, 0.0f, 0.0f};
    static const float across[3] = {0.0f, 0.0f, 0.5f};
    float n[3];
    bool scaled = mvfxCrossEdges(along, across, n);
    bool far = scaled && n[0] == 0.0f && n[2] == 0.0f && n[1] == -(1e20f * 0x1p-64f) * 0x1p-65f;
    static const float edge[3] = {0x1p63f, 0.0f, 0.0f};
    bool at = mvfxCrossEdges(edge, across, n);
    static const float below[3] = {0x1.fffffep62f, 0.0f, 0.0f};
    bool under = !mvfxCrossEdges(below, across, n);
    CHECK(written && far && at && under, "cross edges");
}

int main(void)
{
    TestShares();
    TestCross();
    return s_failures == 0 ? 0 : 1;
}
