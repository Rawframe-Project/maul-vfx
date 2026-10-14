// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Curves: validation, interpolation, the tangent rules, baking within the
// bound of linear interpolation, and the integral and its inverse.

#include "curve.h"
#include "curve_integral.h"
#include "test_harness.h"

#include <math.h>
#include <stdlib.h>

static mvfxCurve CurveOf(const mvfxCurveKey* keys, int32_t count, mvfxTangents tangents)
{
    mvfxCurve curve = {keys, count, tangents};
    return curve;
}

static void TestValidation(void)
{
    mvfxCurveKey keys[3] = {{0.0f, 1.0f, 0, 0, mvfx_interpolationLinear},
                            {0.5f, 2.0f, 0, 0, mvfx_interpolationCubic},
                            {1.0f, 0.0f, 0, 0, mvfx_interpolationConstant}};
    mvfxCurve curve = CurveOf(keys, 3, mvfx_tangentsGiven);
    CHECK(mvfxIsCurveValid(&curve, true), "a valid curve");
    CHECK(!mvfxIsCurveValid(NULL, false), "no curve");
    mvfxCurve empty = CurveOf(keys, 0, mvfx_tangentsGiven);
    CHECK(!mvfxIsCurveValid(&empty, false), "no keys");
    mvfxCurve noKeys = CurveOf(NULL, 3, mvfx_tangentsGiven);
    CHECK(!mvfxIsCurveValid(&noKeys, false), "a null key array");
    mvfxCurve tooMany = CurveOf(keys, MVFX_CURVE_MAX_KEYS + 1, mvfx_tangentsGiven);
    CHECK(!mvfxIsCurveValid(&tooMany, false), "too many keys");
    mvfxCurve badRule = CurveOf(keys, 3, (mvfxTangents)3);
    CHECK(!mvfxIsCurveValid(&badRule, false), "an unknown tangent rule");

    mvfxCurveKey bad[3];
    for (int32_t field = 0; field < 6; ++field)
    {
        for (int32_t i = 0; i < 3; ++i)
        {
            bad[i] = keys[i];
        }
        switch (field)
        {
        case 0:
            bad[1].time = NAN;
            break;
        case 1:
            bad[1].value = INFINITY;
            break;
        case 2:
            bad[1].inTangent = -INFINITY;
            break;
        case 3:
            bad[1].outTangent = 2e18f;
            break;
        case 4:
            bad[1].interpolation = 3;
            break;
        default:
            bad[2].time = 0.25f;
            break;
        }
        mvfxCurve broken = CurveOf(bad, 3, mvfx_tangentsGiven);
        CHECK(!mvfxIsCurveValid(&broken, false), "a broken key is refused");
    }
    mvfxCurveKey outside[2] = {{-0.5f, 0, 0, 0, 0}, {2.0f, 0, 0, 0, 0}};
    mvfxCurve wide = CurveOf(outside, 2, mvfx_tangentsGiven);
    CHECK(mvfxIsCurveValid(&wide, false), "any domain without the unit rule");
    CHECK(!mvfxIsCurveValid(&wide, true), "outside [0, 1] with it");
}

static void TestInterpolations(void)
{
    mvfxCurveKey keys[4] = {{0.0f, 1.0f, 0, 0, mvfx_interpolationConstant},
                            {1.0f, 3.0f, 0, 0, mvfx_interpolationLinear},
                            {2.0f, 5.0f, 4.0f, 4.0f, mvfx_interpolationCubic},
                            {3.0f, 0.0f, -1.0f, -1.0f, mvfx_interpolationLinear}};
    mvfxCurve curve = CurveOf(keys, 4, mvfx_tangentsGiven);
    CHECK(mvfxCurveValueAt(&curve, -1.0) == 1.0, "held before the first key");
    CHECK(mvfxCurveValueAt(&curve, 0.5) == 1.0, "constant segment");
    CHECK(mvfxCurveValueAt(&curve, 1.0) == 3.0, "at a key");
    CHECK(mvfxCurveValueAt(&curve, 1.25) == 3.5, "linear segment");
    // Hermite from 5 (slope 4) to 0 (slope -1) over one unit, at a half.
    double expected = 0.5 * 5.0 + 0.125 * 4.0 + 0.5 * 0.0 - 0.125 * -1.0;
    CHECK(fabs(mvfxCurveValueAt(&curve, 2.5) - expected) < 1e-12, "cubic segment");
    CHECK(mvfxCurveValueAt(&curve, 9.0) == 0.0, "held after the last key");

    mvfxCurveKey step[3] = {{0.0f, 1.0f, 0, 0, mvfx_interpolationLinear},
                            {0.5f, 1.0f, 0, 0, mvfx_interpolationLinear},
                            {0.5f, 7.0f, 0, 0, mvfx_interpolationLinear}};
    mvfxCurve stepped = CurveOf(step, 3, mvfx_tangentsGiven);
    CHECK(mvfxCurveValueAt(&stepped, 0.4999) == 1.0, "before a step");
    CHECK(mvfxCurveValueAt(&stepped, 0.5) == 7.0, "at a step, the later key");
}

static void TestSmoothKeepsALineStraight(void)
{
    mvfxCurveKey keys[5];
    for (int32_t i = 0; i < 5; ++i)
    {
        keys[i] = (mvfxCurveKey){(float)i * 0.25f, (float)i * 2.0f - 1.0f, 99.0f, -99.0f,
                                 mvfx_interpolationCubic};
    }
    mvfxCurve curve = CurveOf(keys, 5, mvfx_tangentsSmooth);
    for (int32_t s = 0; s <= 400; ++s)
    {
        double t = (double)s / 400.0;
        CHECK(fabs(mvfxCurveValueAt(&curve, t) - (8.0 * t - 1.0)) < 1e-9,
              "the smooth rule ignores given tangents and keeps a line");
    }
}

static void TestMonotoneNeverOvershoots(void)
{
    srand(7);
    for (int32_t trial = 0; trial < 200; ++trial)
    {
        mvfxCurveKey keys[8];
        float time = 0.0f;
        float value = 0.0f;
        for (int32_t i = 0; i < 8; ++i)
        {
            keys[i] = (mvfxCurveKey){time, value, 50.0f, 50.0f, mvfx_interpolationCubic};
            time += 0.01f + (float)(rand() % 100) / 100.0f;
            // Increasing with flats and jumps, where tangents from neighbors
            // would overshoot.
            int32_t kind = rand() % 3;
            value += kind == 0 ? 0.0f : (kind == 1 ? 0.1f : 10.0f);
        }
        mvfxCurve curve = CurveOf(keys, 8, mvfx_tangentsMonotone);
        double previous = -(double)INFINITY;
        bool ok = true;
        for (int32_t s = 0; s <= 2000; ++s)
        {
            double t = (double)keys[7].time * (double)s / 2000.0;
            double v = mvfxCurveValueAt(&curve, t);
            ok = ok && v >= previous - 1e-12 && v >= -1e-12 && v <= (double)keys[7].value + 1e-12;
            previous = v;
        }
        CHECK(ok, "monotone keys give a monotone curve within their range");
    }
    // The given tangents do overshoot, so the test above can fail.
    mvfxCurveKey keys[3] = {{0.0f, 0.0f, 0, 50.0f, mvfx_interpolationCubic},
                            {1.0f, 1.0f, 50.0f, 50.0f, mvfx_interpolationCubic},
                            {2.0f, 1.0f, 0, 0, mvfx_interpolationCubic}};
    mvfxCurve given = CurveOf(keys, 3, mvfx_tangentsGiven);
    CHECK(mvfxCurveValueAt(&given, 1.5) > 1.0, "given tangents overshoot");
    mvfxCurve monotone = CurveOf(keys, 3, mvfx_tangentsMonotone);
    CHECK(mvfxCurveValueAt(&monotone, 1.5) <= 1.0, "the monotone rule does not");
}

static void TestBakingIsWithinItsBound(void)
{
    // A smooth cubic over [0, 1]: the baked table differs from it by at most
    // max|f''| h^2 / 8, h = 1/255, plus a float's rounding of the values.
    mvfxCurveKey keys[3] = {{0.0f, 0.0f, 0, 6.0f, mvfx_interpolationCubic},
                            {0.4f, 2.0f, -3.0f, -3.0f, mvfx_interpolationCubic},
                            {1.0f, -1.0f, 2.0f, 0, mvfx_interpolationCubic}};
    mvfxCurve curve = CurveOf(keys, 3, mvfx_tangentsGiven);
    mvfxCurveTable table;
    mvfxBakeCurve(&curve, 0.0f, 1.0f, &table);
    double h = 1.0 / 255.0;
    double curvature = 0.0;
    double fine = 1e-4;
    for (double t = fine; t < 1.0 - fine; t += fine)
    {
        double second = (mvfxCurveValueAt(&curve, t + fine) - 2.0 * mvfxCurveValueAt(&curve, t) +
                         mvfxCurveValueAt(&curve, t - fine)) /
                        (fine * fine);
        curvature = fabs(second) > curvature ? fabs(second) : curvature;
    }
    double bound = curvature * h * h / 8.0 + 4e-6;
    double worst = 0.0;
    for (int32_t s = 0; s <= 10000; ++s)
    {
        float t = (float)s / 10000.0f;
        double error =
            fabs((double)mvfxCurveTableAt(&table, t) - mvfxCurveValueAt(&curve, (double)t));
        worst = error > worst ? error : worst;
    }
    CHECK(worst <= bound, "baked within the interpolation bound");
    CHECK(mvfxCurveTableAt(&table, -5.0f) == table.samples[0], "clamped below");
    CHECK(mvfxCurveTableAt(&table, 5.0f) == table.samples[MVFX_CURVE_SAMPLES - 1], "clamped above");
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        float t = (float)i / (float)(MVFX_CURVE_SAMPLES - 1);
        CHECK(fabsf(mvfxCurveTableAt(&table, t) - table.samples[i]) < 1e-5f, "sample points");
    }
    mvfxCurveKey one = {0.3f, 4.0f, 0, 0, mvfx_interpolationLinear};
    mvfxCurve single = CurveOf(&one, 1, mvfx_tangentsGiven);
    mvfxCurveTable flat;
    mvfxBakeCurve(&single, 0.3f, 0.3f, &flat);
    CHECK(flat.scale == 0.0f && mvfxCurveTableAt(&flat, 100.0f) == 4.0f, "a one-point domain");
    CHECK(mvfxIsCurveTableNonNegative(&flat), "a positive curve is non-negative");
    CHECK(!mvfxIsCurveTableNonNegative(&table), "a curve below zero is not");
}

int main(void)
{
    TestValidation();
    TestInterpolations();
    TestSmoothKeepsALineStraight();
    TestMonotoneNeverOvershoots();
    TestBakingIsWithinItsBound();
    return s_failures == 0 ? 0 : 1;
}
