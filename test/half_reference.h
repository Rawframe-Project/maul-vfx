// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The oracles binary32 to binary16 conversion is checked against: rounding
// done in double, which holds every float exactly, and the compiler's
// _Float16 where it has one.

#ifndef MAUL_VFX_TEST_HALF_REFERENCE_H
#define MAUL_VFX_TEST_HALF_REFERENCE_H

#include "half.h"
#include "test_harness.h"

#include <math.h>
#include <string.h>

// The half nearest to value, by arithmetic on doubles: the value's
// quantum among halves (2^-24 below 2^-14, else 2^(e-10)), rint (ties to
// even in the default mode) of the value in quanta, then the encoding.
static inline uint16_t ReferenceHalf(float value)
{
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof bits);
    uint16_t sign = (uint16_t)((bits >> 16u) & 0x8000u);
    if (isnan(value))
    {
        return (uint16_t)(sign | 0x7E00u);
    }
    double magnitude = fabs((double)value);
    if (magnitude >= 65520.0)
    {
        return (uint16_t)(sign | 0x7C00u);
    }
    int exponent = 0;
    (void)frexp(magnitude, &exponent);
    // frexp gives magnitude = m * 2^exponent with m in [0.5, 1).
    int unbiased = exponent - 1;
    double quantum = unbiased < -14 ? ldexp(1.0, -24) : ldexp(1.0, unbiased - 10);
    double units = rint(magnitude / quantum);
    double rounded = units * quantum;
    if (rounded >= 65520.0)
    {
        return (uint16_t)(sign | 0x7C00u);
    }
    if (rounded < ldexp(1.0, -14))
    {
        return (uint16_t)(sign | (uint16_t)rint(rounded / ldexp(1.0, -24)));
    }
    int roundedExponent = 0;
    double fraction = frexp(rounded, &roundedExponent);
    uint16_t field = (uint16_t)(roundedExponent - 1 + 15);
    uint16_t mantissa = (uint16_t)rint((fraction * 2.0 - 1.0) * 1024.0);
    return (uint16_t)(sign | (uint16_t)(field << 10u) | mantissa);
}

static uint64_t s_checked = 0;

static inline void CheckBits(uint32_t bits)
{
    float value = 0;
    memcpy(&value, &bits, sizeof value);
    uint16_t got = mvfxHalfFromFloat(value);
    s_checked += 1;
    if (got != ReferenceHalf(value))
    {
        CHECK(false, "matches the double reference");
        return;
    }
#ifdef __FLT16_MAX__
    if (!isnan(value))
    {
        _Float16 half = (_Float16)value;
        uint16_t compiler = 0;
        memcpy(&compiler, &half, sizeof compiler);
        CHECK(got == compiler, "matches the compiler's _Float16");
    }
#endif
}

#endif
