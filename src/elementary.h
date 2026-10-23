// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The transcendental functions the library needs, written here so that
// they give the same bits on every platform
// (docs/internals/determinism.md): only IEEE arithmetic and the exactly
// rounded floor and frexp and ldexp of <math.h>. Each reduces its
// argument and sums its series in binary64 and rounds once to binary32,
// so its result is within one unit in the last place of the true value,
// and the correctly rounded float in all but a handful of cases the tests
// count. They are defined in round to nearest, the mode every library
// entry point runs in.

#ifndef MAUL_VFX_SRC_ELEMENTARY_H
#define MAUL_VFX_SRC_ELEMENTARY_H

// The sine and cosine of x, for |x| up to 2^24 (beyond it a float has no
// fraction left to turn); larger arguments and NaN give unspecified
// values, never undefined behavior.
void mvfxSinCos(float x, float* sineOut, float* cosineOut);

// e to the x: infinity above about 88.72, 0 below about -103.97, NaN for
// NaN.
float mvfxExp(float x);

// e^x in binary64 for x of at most 100; 0 below -120.
double mvfxExpDouble(double x);

// x to the y for x at least 0: 1 when y is 0 (0 to the 0 included), 0 for
// x 0 and y above 0, infinity for x 0 and y below 0; NaN for x below 0.
float mvfxPow(float x, float y);

#endif
