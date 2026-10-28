// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The library's sine, cosine, exponential and power against the oracle of
// elementary_reference.h, on strides of their domains and at their edges;
// test_elementary_sweep checks every float of each domain.

#include "elementary_reference.h"

#include <float.h>

// Every float from low to high in steps of `stride` floats.
#define FOR_FLOATS(x, low, high, stride)                                                           \
    for (int64_t order_ = FloatOrder(low); order_ <= FloatOrder(high); order_ += (stride))         \
        for (int once_ = 1; once_; once_ = 0)                                                      \
            for (float x = FloatAt(order_); once_; once_ = 0)

static float FloatAt(int64_t order)
{
    uint32_t bits = order < 0 ? (uint32_t)(-order) | 0x80000000u : (uint32_t)order;
    float x = 0;
    memcpy(&x, &bits, sizeof x);
    return x;
}

static void TestSinCosOnAStride(void)
{
    Tally sines = {0};
    Tally cosines = {0};
    FOR_FLOATS(x, -0x1p24f, 0x1p24f, 9973)
    {
        CheckSinCos(&sines, &cosines, x);
    }
    Report("sine", &sines);
    Report("cosine", &cosines);
    // Past 2^24, where a float is an integer reduced exactly, to the
    // largest.
    Tally largeSines = {0};
    Tally largeCosines = {0};
    FOR_FLOATS(x, -FLT_MAX, FLT_MAX, 99991)
    {
        CheckSinCos(&largeSines, &largeCosines, x);
    }
    Report("sine past 2^24", &largeSines);
    Report("cosine past 2^24", &largeCosines);
}

static void TestSinCosEdges(void)
{
    float s = 1;
    float c = 0;
    mvfxSinCos(0.0f, &s, &c);
    CHECK(s == 0.0f && c == 1.0f, "sin 0 and cos 0");
    mvfxSinCos(-0.0f, &s, &c);
    CHECK(s == 0.0f && c == 1.0f, "negative zero");
    // Infinities and NaN give those of 0, never an overflowing
    // conversion.
    static const float wild[3] = {INFINITY, -INFINITY, NAN};
    for (int32_t i = 0; i < 3; ++i)
    {
        mvfxSinCos(wild[i], &s, &c);
        CHECK(s == 0.0f && c == 1.0f, "an argument not finite");
    }
    // Either side of 2^24, where the reduction changes, and at the ends.
    Tally farSines = {0};
    Tally farCosines = {0};
    // The floats past 2^24 nearest a multiple of pi/2: 0x1.f37c8ap+95,
    // 2^-29.9 of a quarter turn past one, and 0x1.628d4cp+40, 2^-27.8
    // short of one.
    static const float far[14] = {
        0x1p24f,         0x1.000002p24f,   -0x1.000002p24f, 0x1p25f,         -6.3e27f,
        3.0e38f,         FLT_MAX,          -FLT_MAX,        1e10f,           0x1.fffffep23f,
        0x1.f37c8ap+95f, -0x1.f37c8ap+95f, 0x1.628d4cp+40f, -0x1.628d4cp+40f};
    for (int32_t i = 0; i < 14; ++i)
    {
        CheckSinCos(&farSines, &farCosines, far[i]);
    }
    Report("sine at 2^24 and the ends", &farSines);
    Report("cosine at 2^24 and the ends", &farCosines);
    Tally sines = {0};
    Tally cosines = {0};
    // The floats nearest each multiple of pi/4 up to 2^20, where the
    // reduction's remainder is smallest or its quadrant changes.
    for (int64_t k = -(1 << 20); k <= (1 << 20); k += 997)
    {
        float x = (float)((double)k * 0x1.921fb54442d18p-1);
        for (int32_t step = -2; step <= 2; ++step)
        {
            CheckSinCos(&sines, &cosines, FloatAt(FloatOrder(x) + step));
        }
    }
    Report("sine near multiples of pi/4", &sines);
    Report("cosine near multiples of pi/4", &cosines);
}

static void TestExpOnAStride(void)
{
    Tally tally = {0};
    FOR_FLOATS(x, -104.0f, 89.0f, 2003)
    {
        CheckExp(&tally, x);
    }
    Report("exponential", &tally);
}

static void TestExpEdges(void)
{
    CHECK(mvfxExp(0.0f) == 1.0f, "e^0 is 1");
    CHECK(mvfxExp(-0.0f) == 1.0f, "e^-0 is 1");
    CHECK(mvfxExp(88.72284f) == INFINITY, "overflows above 88.7228");
    CHECK(mvfxExp(88.72283f) < INFINITY, "the largest finite result");
    CHECK(mvfxExp(-103.98f) == 0.0f, "underflows below -103.97");
    CHECK(mvfxExp(-103.27f) > 0.0f, "the smallest subnormal results");
    CHECK(mvfxExp(INFINITY) == INFINITY, "e^inf");
    CHECK(mvfxExp(-INFINITY) == 0.0f, "e^-inf");
    CHECK(isnan(mvfxExp(NAN)), "NaN stays NaN");
    CHECK(isnan(mvfxExpDouble((double)NAN)), "NaN stays NaN in binary64");
    Tally tally = {0};
    // The subnormal results, every float.
    FOR_FLOATS(x, -103.98f, -87.33f, 1)
    {
        CheckExp(&tally, x);
    }
    Report("exponential into subnormals", &tally);
}

static void TestPowOnAStride(void)
{
    static const float exponents[] = {2.0f, 0.5f,   3.0f,  1.0f / 2.4f, 2.4f,  -1.0f,  -0.5f, 0.25f,
                                      7.5f, -3.25f, 1e-3f, 1.0f / 2.2f, 12.0f, -20.0f, 0.1f};
    Tally tally = {0};
    for (size_t i = 0; i < sizeof exponents / sizeof exponents[0]; ++i)
    {
        FOR_FLOATS(x, 0x1p-126f, 0x1p100f, 4001)
        {
            CheckPow(&tally, x, exponents[i]);
        }
    }
    Report("power", &tally);
}

static void TestPowEdges(void)
{
    CHECK(mvfxPow(0.0f, 0.0f) == 1.0f, "0^0 is 1");
    CHECK(mvfxPow(5.0f, 0.0f) == 1.0f, "x^0 is 1");
    CHECK(mvfxPow(0.0f, 2.0f) == 0.0f, "0^y is 0 for y above 0");
    CHECK(mvfxPow(0.0f, -2.0f) == INFINITY, "0^y is infinity for y below 0");
    CHECK(mvfxPow(INFINITY, 0.5f) == INFINITY, "inf^y for y above 0");
    CHECK(mvfxPow(INFINITY, -0.5f) == 0.0f, "inf^y for y below 0");
    CHECK(mvfxPow(1.0f, 123.0f) == 1.0f, "1^y is 1");
    CHECK(isnan(mvfxPow(-1.0f, 2.0f)), "a negative base is NaN");
    CHECK(isnan(mvfxPow(NAN, 2.0f)) && isnan(mvfxPow(2.0f, NAN)), "NaN in, NaN out");
    CHECK(mvfxPow(2.0f, 10.0f) == 1024.0f, "an exact power of two");
    CHECK(mvfxPow(10.0f, 40.0f) == INFINITY, "overflow");
    CHECK(mvfxPow(10.0f, -50.0f) == 0.0f, "underflow");
    // x^1 gives x back for every float on a stride.
    bool identity = true;
    FOR_FLOATS(x, 0x1p-149f, 0x1.fffffep127f, 1009)
    {
        identity = identity && mvfxPow(x, 1.0f) == x;
    }
    CHECK(identity, "x^1 is x");
}

int main(void)
{
    TestSinCosOnAStride();
    TestSinCosEdges();
    TestExpOnAStride();
    TestExpEdges();
    TestPowOnAStride();
    TestPowEdges();
    return s_failures == 0 ? 0 : 1;
}
