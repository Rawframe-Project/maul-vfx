// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The oracle of the library's sine, cosine, exponential and power: the C
// library's binary64 functions, whose results are within a unit of
// binary64's last place, rounded to binary32. That is the correctly
// rounded float except where the true value lies within about 2^-29 of a
// float's halfway point; the checks allow one unit in the last place and
// count how often the results differ at all.

#ifndef MAUL_VFX_TEST_ELEMENTARY_REFERENCE_H
#define MAUL_VFX_TEST_ELEMENTARY_REFERENCE_H

#include "elementary.h"
#include "test_harness.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// A float's place on the line of all floats, so that neighbours differ by
// one: negative floats below zero, and both zeros at 0.
static inline int64_t FloatOrder(float x)
{
    uint32_t bits = 0;
    memcpy(&bits, &x, sizeof bits);
    int64_t magnitude = (int64_t)(bits & 0x7FFFFFFFu);
    return (bits & 0x80000000u) != 0 ? -magnitude : magnitude;
}

// The distance between two floats in units in the last place.
static inline int64_t UlpDistance(float a, float b)
{
    int64_t d = FloatOrder(a) - FloatOrder(b);
    return d < 0 ? -d : d;
}

// What a sweep found: values checked, values that differ from the
// reference at all, and the largest difference.
typedef struct Tally
{
    uint64_t checked;
    uint64_t differing;
    int64_t worst;
} Tally;

static inline void Count(Tally* tally, float got, float expected)
{
    tally->checked += 1;
    int64_t d = UlpDistance(got, expected);
    if (d != 0)
    {
        tally->differing += 1;
    }
    if (d > tally->worst)
    {
        tally->worst = d;
    }
}

static inline void CheckSinCos(Tally* sines, Tally* cosines, float x)
{
    float s = 0;
    float c = 0;
    mvfxSinCos(x, &s, &c);
    Count(sines, s, (float)sin((double)x));
    Count(cosines, c, (float)cos((double)x));
}

static inline void CheckExp(Tally* tally, float x)
{
    Count(tally, mvfxExp(x), (float)exp((double)x));
}

static inline void CheckPow(Tally* tally, float x, float y)
{
    Count(tally, mvfxPow(x, y), (float)pow((double)x, (double)y));
}

static inline void Report(const char* name, const Tally* tally)
{
    printf("%s: %llu checked, %llu differ from the reference, worst %lld ulp\n", name,
           (unsigned long long)tally->checked, (unsigned long long)tally->differing,
           (long long)tally->worst);
    CHECK(tally->checked > 0, "values checked");
    CHECK(tally->worst <= 1, "within one ulp");
}

#endif
