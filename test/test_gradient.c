// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gradients: validation, keys, linear blends and clamping.

#include "gradient.h"
#include "test_harness.h"

#include <math.h>

static void TestValidation(void)
{
    mvfxColorKey colors[2] = {{0.0f, 1, 0, 0}, {1.0f, 0, 0, 1}};
    mvfxAlphaKey alphas[1] = {{0.5f, 0.25f}};
    mvfxGradient gradient = {colors, 2, alphas, 1};
    CHECK(mvfxIsGradientValid(&gradient), "a valid gradient");
    CHECK(!mvfxIsGradientValid(NULL), "no gradient");
    mvfxGradient noAlpha = {colors, 2, alphas, 0};
    CHECK(!mvfxIsGradientValid(&noAlpha), "no alpha keys");
    mvfxGradient nullColors = {NULL, 2, alphas, 1};
    CHECK(!mvfxIsGradientValid(&nullColors), "a null color array");
    mvfxGradient tooMany = {colors, MVFX_CURVE_MAX_KEYS + 1, alphas, 1};
    CHECK(!mvfxIsGradientValid(&tooMany), "too many keys");
    mvfxColorKey backward[2] = {{0.6f, 1, 0, 0}, {0.5f, 0, 0, 1}};
    mvfxGradient reversed = {backward, 2, alphas, 1};
    CHECK(!mvfxIsGradientValid(&reversed), "decreasing times");
    mvfxColorKey past[1] = {{1.5f, 1, 0, 0}};
    mvfxGradient beyond = {past, 1, alphas, 1};
    CHECK(!mvfxIsGradientValid(&beyond), "a time past 1");
    mvfxColorKey nan[1] = {{0.5f, NAN, 0, 0}};
    mvfxGradient notANumber = {nan, 1, alphas, 1};
    CHECK(!mvfxIsGradientValid(&notANumber), "a NaN channel");
    mvfxAlphaKey badAlpha[1] = {{-0.1f, 1.0f}};
    mvfxGradient negative = {colors, 2, badAlpha, 1};
    CHECK(!mvfxIsGradientValid(&negative), "a time below 0");
}

static void TestBlends(void)
{
    mvfxColorKey colors[3] = {{0.25f, 1, 0, 0}, {0.5f, 0, 1, 0}, {0.75f, 0, 0, 1}};
    mvfxAlphaKey alphas[2] = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    mvfxGradient gradient = {colors, 3, alphas, 2};
    mvfxGradientTable table;
    mvfxBakeGradient(&gradient, &table);
    float rgba[4];
    mvfxGradientTableAt(&table, 0.0f, rgba);
    CHECK(rgba[0] == 1.0f && rgba[1] == 0.0f && rgba[3] == 0.0f, "held before the first key");
    mvfxGradientTableAt(&table, 1.0f, rgba);
    CHECK(rgba[2] == 1.0f && rgba[3] == 1.0f, "held after the last key");
    for (int32_t s = 0; s <= 1000; ++s)
    {
        float t = (float)s / 1000.0f;
        mvfxGradientTableAt(&table, t, rgba);
        double red = t <= 0.25f ? 1.0 : (t >= 0.5f ? 0.0 : (0.5 - (double)t) / 0.25);
        CHECK(fabs((double)rgba[0] - red) < 1.0 / 255.0 + 1e-6, "red blends linearly");
        CHECK(fabs((double)rgba[3] - (double)t) < 1e-5, "alpha apart from color");
    }
    mvfxGradientTableAt(&table, -3.0f, rgba);
    CHECK(rgba[0] == 1.0f, "clamped below 0");
}

int main(void)
{
    TestValidation();
    TestBlends();
    return s_failures == 0 ? 0 : 1;
}
