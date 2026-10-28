// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sine and cosine by Cody and Waite's reduction to [-pi/4, pi/4] (Payne
// and Hanek's past 2^24) and Taylor series; the exponential by reduction
// to |r| <= ln 2 / 2 and its series; the power as 2 to the y log2 x, the
// logarithm by the atanh series. The constants come from
// tools/math_constants.py.

#include "elementary.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

// pi/2 in three parts, the first two of 29 bits so that their products
// with a reduction multiple below 2^24 are exact.
#define HALF_PI_1   0x1.921fb54000000p+0
#define HALF_PI_2   0x1.10b4611000000p-30
#define HALF_PI_3   0x1.4c4c6628b80dcp-59
#define TWO_OVER_PI 0x1.45f306dc9c883p-1
#define HALF_PI     0x1.921fb54442d18p+0
static const uint32_t TWO_OVER_PI_WORDS[8] = {0xA2F9836Eu, 0x4E441529u, 0xFC2757D1u, 0xF534DDC0u,
                                              0xDB629599u, 0x3C439041u, 0xFE5163ABu, 0xDEBBC561u};
// ln 2 in two parts, the first of 42 bits for multiples below 2^11.
#define LN2_1  0x1.62e42fefa3800p-1
#define LN2_2  0x1.ef35793c76730p-45
#define LOG2_E 0x1.71547652b82fep+0
#define LN2    0x1.62e42fefa39efp-1

// The reciprocal factorials, 1/n!.
#define INVERSE_FACTORIAL_2  0x1.0000000000000p-1
#define INVERSE_FACTORIAL_3  0x1.5555555555555p-3
#define INVERSE_FACTORIAL_4  0x1.5555555555555p-5
#define INVERSE_FACTORIAL_5  0x1.1111111111111p-7
#define INVERSE_FACTORIAL_6  0x1.6c16c16c16c17p-10
#define INVERSE_FACTORIAL_7  0x1.a01a01a01a01ap-13
#define INVERSE_FACTORIAL_8  0x1.a01a01a01a01ap-16
#define INVERSE_FACTORIAL_9  0x1.71de3a556c734p-19
#define INVERSE_FACTORIAL_10 0x1.27e4fb7789f5cp-22
#define INVERSE_FACTORIAL_11 0x1.ae64567f544e4p-26
#define INVERSE_FACTORIAL_12 0x1.1eed8eff8d898p-29
#define INVERSE_FACTORIAL_13 0x1.6124613a86d09p-33
#define INVERSE_FACTORIAL_14 0x1.93974a8c07c9dp-37
#define INVERSE_FACTORIAL_15 0x1.ae7f3e733b81fp-41
#define INVERSE_FACTORIAL_16 0x1.ae7f3e733b81fp-45

// Whether x is a NaN, from its bits: an all-ones exponent and a mantissa
// that is not zero.
static bool IsNan(float x)
{
    uint32_t bits = 0;
    memcpy(&bits, &x, sizeof bits);
    return (bits & 0x7FFFFFFFu) > 0x7F800000u;
}

// Whether x is a NaN, from its bits, in binary64.
static bool IsNanDouble(double x)
{
    uint64_t bits = 0;
    memcpy(&bits, &x, sizeof bits);
    return (bits & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull;
}

// The sine of r in [-pi/4, pi/4], to r^15: the first term left out,
// r^17/17!, is below 2^-48 of the result.
static double SinReduced(double r)
{
    double r2 = r * r;
    double p = INVERSE_FACTORIAL_15;
    p = INVERSE_FACTORIAL_13 - r2 * p;
    p = INVERSE_FACTORIAL_11 - r2 * p;
    p = INVERSE_FACTORIAL_9 - r2 * p;
    p = INVERSE_FACTORIAL_7 - r2 * p;
    p = INVERSE_FACTORIAL_5 - r2 * p;
    p = INVERSE_FACTORIAL_3 - r2 * p;
    return r - r * r2 * p;
}

// The cosine of r in [-pi/4, pi/4], to r^16.
static double CosReduced(double r)
{
    double r2 = r * r;
    double p = INVERSE_FACTORIAL_16;
    p = INVERSE_FACTORIAL_14 - r2 * p;
    p = INVERSE_FACTORIAL_12 - r2 * p;
    p = INVERSE_FACTORIAL_10 - r2 * p;
    p = INVERSE_FACTORIAL_8 - r2 * p;
    p = INVERSE_FACTORIAL_6 - r2 * p;
    p = INVERSE_FACTORIAL_4 - r2 * p;
    p = INVERSE_FACTORIAL_2 - r2 * p;
    return 1.0 - r2 * p;
}

// 32 bits of 2/pi's fraction from bit `first` on (bit 1 weighing 1/2;
// bit 0, its integer part, is 0).
static uint32_t TwoOverPiBits(int32_t first)
{
    // Bit i at place i + 31 of the words after a word of zeros.
    int32_t place = first + 31;
    int32_t word = place / 32;
    int32_t shift = place % 32;
    uint32_t high = word == 0 ? 0u : TWO_OVER_PI_WORDS[word - 1];
    uint32_t low = TWO_OVER_PI_WORDS[word];
    return shift == 0 ? high : (high << shift) | (low >> (32 - shift));
}

// x past 2^24, a finite float and so an integer m 2^e (m below 2^24, e
// from 1 to 104), less its nearest multiple of pi/2, and that multiple's
// low two bits in *quadrant: Payne and Hanek's reduction. 2/pi's bits
// from bit e - 1 on (those above give multiples of 4) times m in 32-bit
// limbs, 128 of them, put x 2/pi within 2^-102 quarter turns. The
// remainder's leading 64 bits keep at least 34 significant: no float's
// is below 2^-29.9 of a quarter turn (0x1.f37c8ap+95's).
static double ReducedPast(float x, int64_t* quadrant)
{
    uint32_t bits = 0;
    memcpy(&bits, &x, sizeof bits);
    uint64_t m = (uint64_t)((bits & 0x7FFFFFu) | 0x800000u);
    int32_t e = (int32_t)((bits >> 23) & 0xFFu) - 150;
    // The product's limbs, most significant first: 2^-126 of x 2/pi less
    // a multiple of 4.
    uint32_t limbs[5];
    uint64_t carry = 0;
    for (int32_t k = 3; k >= 0; --k)
    {
        uint64_t product = m * TwoOverPiBits(e - 1 + 32 * k) + carry;
        limbs[k + 1] = (uint32_t)product;
        carry = product >> 32;
    }
    limbs[0] = (uint32_t)carry;
    // Bits 126 and 127 count quarter turns; below them the fraction's
    // leading 64 bits.
    uint64_t turns = limbs[1] >> 30;
    uint64_t fraction =
        ((uint64_t)(limbs[1] & 0x3FFFFFFFu) << 34) | ((uint64_t)limbs[2] << 2) | (limbs[3] >> 30);
    // Past half a quarter turn, the next multiple is nearer.
    bool next = fraction >= 0x8000000000000000ull;
    double part = next ? -(double)(~fraction + 1u) : (double)fraction;
    *quadrant = (int64_t)((turns + (next ? 1u : 0u)) & 3u);
    return part * 0x1p-64 * HALF_PI;
}

void mvfxSinCos(float x, float* sineOut, float* cosineOut)
{
    double r = 0.0;
    int64_t quadrant = 0;
    if (fabs((double)x) <= 0x1p24)
    {
        // The nearest multiple of pi/2; a tie may go either way, as both
        // leave the remainder within pi/4 and a hair.
        double k = floor((double)x * TWO_OVER_PI + 0.5);
        r = (((double)x - k * HALF_PI_1) - k * HALF_PI_2) - k * HALF_PI_3;
        // k is below 2^25 in size, so it converts exactly; its low two
        // bits name the quadrant.
        quadrant = (int64_t)k & 3;
    }
    else if (!IsNan(x) && fabs((double)x) <= (double)FLT_MAX)
    {
        // Past 2^24 by its exact value: -x turns back as x does.
        r = ReducedPast(fabsf(x), &quadrant);
        r = x < 0.0f ? -r : r;
        quadrant = x < 0.0f ? (4 - quadrant) & 3 : quadrant;
    }
    double sine = SinReduced(r);
    double cosine = CosReduced(r);
    double s = quadrant == 0 ? sine : quadrant == 1 ? cosine : quadrant == 2 ? -sine : -cosine;
    double c = quadrant == 0 ? cosine : quadrant == 1 ? -sine : quadrant == 2 ? -cosine : sine;
    *sineOut = (float)s;
    *cosineOut = (float)c;
}

// e^r for |r| <= ln 2 / 2 and a hair, to r^14: the first term left out is
// below 2^-60 of the result.
static double ExpReduced(double r)
{
    double p = INVERSE_FACTORIAL_14;
    p = INVERSE_FACTORIAL_13 + r * p;
    p = INVERSE_FACTORIAL_12 + r * p;
    p = INVERSE_FACTORIAL_11 + r * p;
    p = INVERSE_FACTORIAL_10 + r * p;
    p = INVERSE_FACTORIAL_9 + r * p;
    p = INVERSE_FACTORIAL_8 + r * p;
    p = INVERSE_FACTORIAL_7 + r * p;
    p = INVERSE_FACTORIAL_6 + r * p;
    p = INVERSE_FACTORIAL_5 + r * p;
    p = INVERSE_FACTORIAL_4 + r * p;
    p = INVERSE_FACTORIAL_3 + r * p;
    p = INVERSE_FACTORIAL_2 + r * p;
    p = 1.0 + r * p;
    return 1.0 + r * p;
}

// e^x in binary64 for x in [-120, 100], the range a float result needs:
// below it the result rounds to 0 as a float, above it to infinity; a NaN
// is returned before it reaches the conversion of its power of two.
static double ExpDouble(double x)
{
    if (IsNanDouble(x))
    {
        return x;
    }
    if (x > 100.0)
    {
        return (double)INFINITY;
    }
    if (x < -120.0)
    {
        return 0.0;
    }
    double k = floor(x * LOG2_E + 0.5);
    double r = (x - k * LN2_1) - k * LN2_2;
    return ldexp(ExpReduced(r), (int)k);
}

double mvfxExpDouble(double x)
{
    return ExpDouble(x);
}

float mvfxExp(float x)
{
    if (IsNan(x))
    {
        return x;
    }
    return (float)ExpDouble((double)x);
}

// The natural logarithm of x above 0 and finite, in binary64: x is
// m 2^e with m in [sqrt(1/2), sqrt(2)), and ln m is 2 atanh(s) for
// s = (m - 1)/(m + 1), |s| <= 0.1716, summed to s^23.
static double LogDouble(double x)
{
    int exponent = 0;
    double m = frexp(x, &exponent);
    if (m < 0x1.6a09e667f3bcdp-1)
    {
        m *= 2.0;
        exponent -= 1;
    }
    double s = (m - 1.0) / (m + 1.0);
    double s2 = s * s;
    double p = 1.0 / 23.0;
    for (int n = 21; n >= 1; n -= 2)
    {
        p = 1.0 / (double)n + s2 * p;
    }
    return (double)exponent * LN2 + 2.0 * s * p;
}

float mvfxPow(float x, float y)
{
    if (y == 0.0f)
    {
        return 1.0f;
    }
    if (IsNan(x) || IsNan(y) || x < 0.0f)
    {
        return NAN;
    }
    if (x == 0.0f)
    {
        return y > 0.0f ? 0.0f : INFINITY;
    }
    if (x == INFINITY)
    {
        return y > 0.0f ? INFINITY : 0.0f;
    }
    double t = (double)y * LogDouble((double)x);
    return (float)ExpDouble(t);
}
