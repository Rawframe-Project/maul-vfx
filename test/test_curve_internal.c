// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The baked curve's read against its own function: a NaN time reads the
// first sample, as no public path passes one (#46).

#include "curve.h"
#include "test_harness.h"

#include <math.h>

static void TestNotANumber(void)
{
    static const mvfxCurveKey keys[2] = {{0.0f, 2.0f, 0, 0, mvfx_interpolationLinear},
                                         {1.0f, 5.0f, 0, 0, mvfx_interpolationLinear}};
    const mvfxCurve curve = {keys, 2, mvfx_tangentsGiven};
    static mvfxCurveTable table;
    mvfxBakeCurve(&curve, 0.0f, 1.0f, &table);
    CHECK(mvfxCurveTableAt(&table, NAN) == 2.0f && mvfxCurveTableAt(&table, -NAN) == 2.0f,
          "a NaN time reads the first sample");
    CHECK(mvfxCurveTableAt(&table, -INFINITY) == 2.0f && mvfxCurveTableAt(&table, INFINITY) == 5.0f,
          "infinities read the ends");
    mvfxBakeCurve(&curve, 0.5f, 0.5f, &table);
    CHECK(mvfxCurveTableAt(&table, INFINITY) == 3.5f, "a domain of one point, read at infinity");
}

int main(void)
{
    TestNotANumber();
    return s_failures == 0 ? 0 : 1;
}
