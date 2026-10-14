// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The integral of a baked curve against closed forms, and its inverse.

#include "curve_integral.h"
#include "test_harness.h"

#include <math.h>

static void Bake(const mvfxCurveKey* keys, int32_t count, float start, float end,
                 mvfxCurveTable* table, mvfxCurveIntegral* integral)
{
    mvfxCurve curve = {keys, count, mvfx_tangentsGiven};
    mvfxBakeCurve(&curve, start, end, table);
    mvfxIntegrateCurve(table, (double)end, integral);
}

static void TestConstantRate(void)
{
    mvfxCurveKey keys[2] = {{0.0f, 30.0f, 0, 0, mvfx_interpolationLinear},
                            {4.0f, 30.0f, 0, 0, mvfx_interpolationLinear}};
    mvfxCurveTable table;
    mvfxCurveIntegral integral;
    Bake(keys, 2, 0.0f, 4.0f, &table, &integral);
    for (int32_t s = -40; s <= 120; ++s)
    {
        double t = (double)s * 0.0625;
        CHECK(fabs(mvfxCurveIntegralAt(&table, &integral, t) - 30.0 * t) < 1e-9,
              "a constant rate's integral, held past the ends");
    }
}

static void TestRampIsQuadratic(void)
{
    // The rate rises from 0 to 100 over two seconds: the integral is 25 t^2.
    mvfxCurveKey keys[2] = {{0.0f, 0.0f, 0, 0, mvfx_interpolationLinear},
                            {2.0f, 100.0f, 0, 0, mvfx_interpolationLinear}};
    mvfxCurveTable table;
    mvfxCurveIntegral integral;
    Bake(keys, 2, 0.0f, 2.0f, &table, &integral);
    for (int32_t s = 0; s <= 200; ++s)
    {
        double t = (double)s * 0.01;
        CHECK(fabs(mvfxCurveIntegralAt(&table, &integral, t) - 25.0 * t * t) < 1e-4,
              "a ramp's integral");
    }
}

static void TestInverseRoundTrips(void)
{
    mvfxCurveKey keys[4] = {{0.0f, 5.0f, 0, 0, mvfx_interpolationLinear},
                            {1.0f, 200.0f, 0, 0, mvfx_interpolationCubic},
                            {2.5f, 0.0f, 0, 0, mvfx_interpolationLinear},
                            {3.0f, 40.0f, 0, 0, mvfx_interpolationLinear}};
    mvfxCurveTable table;
    mvfxCurveIntegral integral;
    Bake(keys, 4, 0.0f, 3.0f, &table, &integral);
    CHECK(mvfxIsCurveTableNonNegative(&table), "the test's rate is non-negative");
    double previous = -(double)INFINITY;
    for (int32_t k = 0; k < 600; ++k)
    {
        double target = (double)k * 0.75;
        double t = mvfxCurveIntegralTime(&table, &integral, target);
        CHECK(fabs(mvfxCurveIntegralAt(&table, &integral, t) - target) <= 1e-9 * (1.0 + target),
              "the integral at the solved time is the target");
        CHECK(t >= previous, "later targets are reached later");
        previous = t;
    }
}

static void TestPlateausAndEnds(void)
{
    // Rate 10, then 0 from 1 to 2, then 10 again.
    mvfxCurveKey keys[4] = {{0.0f, 10.0f, 0, 0, mvfx_interpolationConstant},
                            {1.0f, 0.0f, 0, 0, mvfx_interpolationConstant},
                            {2.0f, 10.0f, 0, 0, mvfx_interpolationConstant},
                            {3.0f, 10.0f, 0, 0, mvfx_interpolationConstant}};
    mvfxCurveTable table;
    mvfxCurveIntegral integral;
    Bake(keys, 4, 0.0f, 3.0f, &table, &integral);
    double atPlateau = mvfxCurveIntegralAt(&table, &integral, 1.5);
    double first = mvfxCurveIntegralTime(&table, &integral, atPlateau);
    CHECK(first < 1.01, "a plateau's value is first reached where it begins");
    CHECK(mvfxCurveIntegralTime(&table, &integral, 0.0) == 0.0, "zero at the start");
    CHECK(fabs(mvfxCurveIntegralTime(&table, &integral, -5.0) + 0.5) < 1e-12,
          "before the start, along the first rate");
    double total = mvfxCurveIntegralAt(&table, &integral, 3.0);
    CHECK(fabs(mvfxCurveIntegralTime(&table, &integral, total + 20.0) - 5.0) < 1e-9,
          "past the end, along the last rate");

    mvfxCurveKey ending[2] = {{0.0f, 10.0f, 0, 0, mvfx_interpolationLinear},
                              {1.0f, 0.0f, 0, 0, mvfx_interpolationLinear}};
    Bake(ending, 2, 0.0f, 1.0f, &table, &integral);
    CHECK(isinf(mvfxCurveIntegralTime(&table, &integral, 1000.0)),
          "a rate that ends at zero never reaches more");
}

int main(void)
{
    TestConstantRate();
    TestRampIsQuadratic();
    TestInverseRoundTrips();
    TestPlateausAndEnds();
    return s_failures == 0 ? 0 : 1;
}
