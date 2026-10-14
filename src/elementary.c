// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sine and cosine by Cody and Waite's reduction to [-pi/4, pi/4] and
// Taylor series; the exponential by reduction to |r| <= ln 2 / 2 and its
// series; the power as 2 to the y log2 x, the logarithm by the atanh
// series. The constants come from tools/math_constants.py.

#include "elementary.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// pi/2 in three parts, the first two of 29 bits so that their products
// with a reduction multiple below 2^24 are exact.
#define HALF_PI_1   0x1.921fb54000000p+0
#define HALF_PI_2   0x1.10b4611000000p-30
#define HALF_PI_3   0x1.4c4c6628b80dcp-59
#define TWO_OVER_PI 0x1.45f306dc9c883p-1
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

void mvfxSinCos(float x, float* sineOut, float* cosineOut)
{
    double value = (double)x;
    // The nearest multiple of pi/2; a tie may go either way, as both
    // leave the remainder within pi/4 and a hair.
    double k = floor(value * TWO_OVER_PI + 0.5);
    double r = ((value - k * HALF_PI_1) - k * HALF_PI_2) - k * HALF_PI_3;
    double sine = SinReduced(r);
    double cosine = CosReduced(r);
    // k is below 2^25 in size, so it converts exactly; its low two bits
    // name the quadrant.
    int64_t quadrant = (int64_t)k & 3;
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
// below it the result rounds to 0 as a float, above it to infinity.
static double ExpDouble(double x)
{
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
