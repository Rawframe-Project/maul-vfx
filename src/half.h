// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// binary32 to binary16 by integer operations alone, rounding to nearest
// with ties to even, so an output record's half floats have the same bits
// on every platform whatever its own conversion instructions do.

#ifndef MAUL_VFX_SRC_HALF_H
#define MAUL_VFX_SRC_HALF_H

#include <stdint.h>
#include <string.h>

// The binary16 nearest to value: overflow to infinity, subnormal halves
// where the value is that small, signed zero kept, and NaN as a quiet NaN
// of the same sign.
static inline uint16_t mvfxHalfFromFloat(float value)
{
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof bits);
    uint32_t sign = (bits >> 16u) & 0x8000u;
    uint32_t magnitude = bits & 0x7FFFFFFFu;
    if (magnitude >= 0x7F800000u)
    {
        // Infinity stays infinity; NaN becomes the quiet NaN.
        return (uint16_t)(sign | (magnitude > 0x7F800000u ? 0x7E00u : 0x7C00u));
    }
    if (magnitude >= 0x477FF000u)
    {
        // 65520 and above round past the largest half, 65504.
        return (uint16_t)(sign | 0x7C00u);
    }
    if (magnitude >= 0x38800000u)
    {
        // A normal half: rebias the exponent (127 to 15) and round the 13
        // mantissa bits that go, ties to even. A carry out of the mantissa
        // moves into the exponent, which is the right result.
        uint32_t rebased = magnitude - 0x38000000u;
        uint32_t odd = (rebased >> 13u) & 1u;
        return (uint16_t)(sign | ((rebased + 0x0FFFu + odd) >> 13u));
    }
    if (magnitude < 0x33000000u)
    {
        // Below half of the smallest subnormal half, 2^-25: zero.
        return (uint16_t)sign;
    }
    // A subnormal half: the mantissa with its implicit bit, shifted to
    // units of 2^-24, rounded to nearest with ties to even.
    uint32_t exponent = magnitude >> 23u;
    uint32_t mantissa = (magnitude & 0x007FFFFFu) | 0x00800000u;
    uint32_t shift = 126u - exponent;
    uint32_t kept = mantissa >> shift;
    uint32_t rest = mantissa & ((1u << shift) - 1u);
    uint32_t halfway = 1u << (shift - 1u);
    if (rest > halfway || (rest == halfway && (kept & 1u) != 0))
    {
        kept += 1u;
    }
    return (uint16_t)(sign | kept);
}

// A half's value as a float, exactly: every half is a float.
static inline float mvfxFloatFromHalf(uint16_t half)
{
    uint32_t sign = ((uint32_t)half & 0x8000u) << 16u;
    uint32_t exponent = ((uint32_t)half >> 10u) & 0x1Fu;
    uint32_t mantissa = (uint32_t)half & 0x3FFu;
    if (exponent == 0)
    {
        // Zero or a subnormal: the mantissa times 2^-24, exactly.
        float magnitude = (float)mantissa * 0x1p-24f;
        return sign != 0 ? -magnitude : magnitude;
    }
    uint32_t bits = exponent == 31 ? sign | 0x7F800000u | (mantissa << 13u)
                                   : sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof value);
    return value;
}

#endif
